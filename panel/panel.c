/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This should be compiled into libgraphene for GIntrospection, and NOT compiled into the panel application binary.
 */

#include "config.h"
#include "panel.h"
#include <gdk/gdkscreen.h>
#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include "launcher/launcher-applet.h"
#include "tasklist/tasklist-applet.h"
#include "clock/clock-applet.h"
#include "settings/settings-applet.h"

// GraphenePanel class (private)
struct _GraphenePanel {
  GtkWindow parent;
  
  GtkBox *AppletLayout;
  GtkBox *LauncherBox;
  GtkBox *SystemTray;
  
  GtkPositionType Location;
  gint Height;
  gint MonitorID;
  GdkRectangle PanelRect;
  
  GtkMenu *ContextMenu;
  
  GtkWindow *CaptureWindow;
  int Captures; // Each time capture is called, this ++es, and when someone ends the capture this --es. When it hits 0, the capture actually ends.

  gboolean Rebooting;
  
  guint NotificationServerBusNameID;
  GHashTable *Notifications;
  guint32 NextNotificationID;
};

// Make sure only one panel exists at a time
static gboolean PanelExists = FALSE;

// Create the GraphenePanel class
// No properties or anything special, the GraphenePanel is only a class because
// it creates the GraphenePanel struct for private data
G_DEFINE_TYPE(GraphenePanel, graphene_panel, GTK_TYPE_WINDOW)
GraphenePanel* graphene_panel_new(void) { if(PanelExists) return NULL; else return GRAPHENE_PANEL(g_object_new(GRAPHENE_TYPE_PANEL, NULL)); }
static void graphene_panel_finalize(GraphenePanel *self);
static void graphene_panel_class_init(GraphenePanelClass *klass) { GObjectClass *object_class = G_OBJECT_CLASS (klass); object_class->finalize = graphene_panel_finalize; }

// Private event declarations
static void init_layout(GraphenePanel *self);
static void init_capture(GraphenePanel *self);
static void init_notifications(GraphenePanel *self);
static void update_position(GraphenePanel *self);
static void on_monitors_changed(GdkScreen *screen, GraphenePanel *self);
static gboolean on_panel_clicked(GraphenePanel *self, GdkEventButton *event);
static void on_context_menu_item_activate(GraphenePanel *self, GtkMenuItem *menuitem);
static void update_notification_windows(GraphenePanel *self);

// Initializes the panel (declared by G_DEFINE_TYPE; called through graphene_panel_new())
static void graphene_panel_init(GraphenePanel *self)
{
  PanelExists = TRUE;
  self->Rebooting = FALSE;
  
  // Set properties
  gtk_window_set_type_hint(GTK_WINDOW(self), GDK_WINDOW_TYPE_HINT_DOCK);
  gtk_window_set_position(GTK_WINDOW(self), GTK_WIN_POS_NONE);
  gtk_window_set_decorated(GTK_WINDOW(self), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(self), TRUE);
  gtk_window_set_role(GTK_WINDOW(self), "GrapheneDock"); // Tells graphene-wm this is the panel

  // Set the application theme
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_path(provider, GRAPHENE_DATA_DIR "/panel.css", NULL); // TODO: Check for errors
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  // Update the position now and when the size or monitors change
  g_signal_connect(gtk_window_get_screen(GTK_WINDOW(self)), "monitors-changed", G_CALLBACK(on_monitors_changed), self);
  g_signal_connect(self, "map", G_CALLBACK(update_position), NULL);
  g_signal_connect(self, "button_press_event", G_CALLBACK(on_panel_clicked), NULL);
  // g_signal_connect(self, "size-allocate", G_CALLBACK(on_size_allocate), NULL); // TODO: Size allocate necessary? Seems to just create lots of unnecessary position updates

  // Load things
  init_layout(self);
  init_capture(self);
  init_notifications(self);
}

static void graphene_panel_finalize(GraphenePanel *self)
{
  if(self->NotificationServerBusNameID)
    g_bus_unown_name(self->NotificationServerBusNameID);
    
  PanelExists = FALSE;
  G_OBJECT_CLASS(graphene_panel_parent_class)->finalize(G_OBJECT(self));
}

static void init_layout(GraphenePanel *self)
{
  self->Location = GTK_POS_BOTTOM;
  self->Height = 32;
  
  // Main layout
  self->AppletLayout = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(self->AppletLayout));
  
  GtkStyleContext *layoutStyle = gtk_widget_get_style_context(GTK_WIDGET(self));
  gtk_style_context_add_class(layoutStyle, "panel");
  gtk_widget_set_name(GTK_WIDGET(self), "panel-bar");

  GrapheneLauncherApplet *launcher = graphene_launcher_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(launcher)), "graphene-applet");
  graphene_launcher_applet_set_panel(launcher, self);
  gtk_box_pack_start(self->AppletLayout, GTK_WIDGET(launcher), FALSE, FALSE, 0);
  
  GrapheneTasklistApplet *tasklist = graphene_tasklist_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(tasklist)), "graphene-applet");
  gtk_box_pack_start(self->AppletLayout, GTK_WIDGET(tasklist), TRUE, TRUE, 0);
  
  GrapheneClockApplet *clock = graphene_clock_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(clock)), "graphene-applet");
  gtk_box_pack_end(self->AppletLayout, GTK_WIDGET(clock), FALSE, FALSE, 0);
  
  GrapheneSettingsApplet *settings = graphene_settings_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(settings)), "graphene-applet");
  graphene_settings_applet_set_panel(settings, self);
  gtk_box_pack_end(self->AppletLayout, GTK_WIDGET(settings), FALSE, FALSE, 0);
  
  // Context menu
  self->ContextMenu = GTK_MENU(gtk_menu_new());
  GtkWidget *reloadapplets = gtk_menu_item_new_with_label("Reload Applets");
  g_signal_connect_swapped(reloadapplets, "activate", G_CALLBACK(on_context_menu_item_activate), self);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->ContextMenu), reloadapplets);
  gtk_widget_show_all(GTK_WIDGET(self->ContextMenu));
  
  // Show
  gtk_widget_show_all(GTK_WIDGET(self->AppletLayout));
}

// Positions/sizes the self at the proper location on the window
static void update_position(GraphenePanel *self)
{
  GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(self));
  
  // Get the monitor for this panel
  // TODO: Allow user-controlled monitor settings
  // Currently just default to the primary monitor
  self->MonitorID = gdk_screen_get_primary_monitor(screen);
  
  // Get the size of the monitor the panel is on
  GdkRectangle monitorRect;
  gdk_screen_get_monitor_geometry(gtk_window_get_screen(GTK_WINDOW(self)), self->MonitorID, &monitorRect);
  
  // Set the panel rect
  GdkRectangle panelRect;
  GdkRectangle captureRect;
  long struts[12];
  switch(self->Location)
  {
    case GTK_POS_TOP:
    case GTK_POS_LEFT:
    case GTK_POS_RIGHT:
      g_warning("Specified panel location (%i) not implemented", self->Location);
      // fallthrough
      
    case GTK_POS_BOTTOM:
      gtk_orientable_set_orientation(GTK_ORIENTABLE(self->AppletLayout), GTK_ORIENTATION_HORIZONTAL);

      // if(self->Captures > 0)
      // {
        captureRect.x = monitorRect.x;
        captureRect.y = monitorRect.y;
        captureRect.width = monitorRect.width;
        captureRect.height = monitorRect.height - self->Height;
      // }
      // else
      // {
      //   // Positioning the capture window in a small rect at the top allows it to resize to fill the screen
      //   // very fast without taking any time loading the window or showing it (gtk_widget_show is kinda laggy)
      //   captureRect.x = monitorRect.x; 
      //   captureRect.y = monitorRect.y;
      //   captureRect.width = 1;
      //   captureRect.height = 1;
      // }
      
      panelRect.x = monitorRect.x;
      panelRect.y = monitorRect.y + monitorRect.height - self->Height;
      panelRect.width = monitorRect.width;
      panelRect.height = self->Height;
      
      struts[0] = 0; struts[1] = 0; struts[2] = 0;
      struts[3] = (gdk_screen_get_height(screen) - monitorRect.height - monitorRect.y) + self->Height;
      struts[4] = 0; struts[5] = 0; struts[6] = 0; struts[7] = 0; struts[8] = 0; struts[9] = 0;
      struts[10] = monitorRect.x;
      struts[11] = monitorRect.x + monitorRect.width;
      break;
    // case GTK_POS_TOP:
    //   newRect.x = monitorRect.x;
    //   newRect.y = monitorRect.y;
    //   newRect.width = monitorRect.width;
    //   newRect.height = self->Height;
    //   gtk_orientable_set_orientation(GTK_ORIENTABLE(self->Layout), GTK_ORIENTATION_HORIZONTAL);
    //   break;
  }
  
  // Check for changes
  int wx, wy, wwidth, wheight;
  gtk_window_get_position(GTK_WINDOW(self), &wx, &wy);
  gtk_window_get_size(GTK_WINDOW(self), &wwidth, &wheight);
  
  if(wx != panelRect.x || wy != panelRect.y || wwidth != panelRect.width || wheight != panelRect.height)
  {
    g_message("Updating position: [%i, %i, %i, %i, %i]\n",
      self->Location,
      panelRect.x, panelRect.y,
      panelRect.width, panelRect.height);

    // Position window
    self->PanelRect = panelRect;
    gtk_window_resize(GTK_WINDOW(self), panelRect.width, panelRect.height);
    gtk_window_move(GTK_WINDOW(self), panelRect.x, panelRect.y);
    
    GdkWindow *panelWindow = gtk_widget_get_window(GTK_WIDGET(self));
    if(panelWindow)
    {
      // Set struts. This makes the available screen space not include the panel, so that fullscreen windows don't go under it
      gdk_property_change(panelWindow,
        gdk_atom_intern("_NET_WM_STRUT_PARTIAL", FALSE),
        gdk_atom_intern("CARDINAL", FALSE),
        32, GDK_PROP_MODE_REPLACE,
        (const guchar *)struts, 12);
    }
  }

  // Position capture window
  int cx, cy, cwidth, cheight;
  gtk_window_get_position(GTK_WINDOW(self->CaptureWindow), &cx, &cy);
  gtk_window_get_size(GTK_WINDOW(self->CaptureWindow), &cwidth, &cheight);
  
  if(self->CaptureWindow && (cx != captureRect.x || cy != captureRect.y || cwidth != captureRect.width || cheight != captureRect.height))
  {
    g_message("Updating capture position: [%i, %i, %i, %i, %i]\n",
      self->Location,
      captureRect.x, captureRect.y,
      captureRect.width, captureRect.height);
    
    gtk_window_resize(GTK_WINDOW(self->CaptureWindow), captureRect.width, captureRect.height);
    gtk_window_move(GTK_WINDOW(self->CaptureWindow), captureRect.x, captureRect.y);
  }
}

static void on_monitors_changed(GdkScreen *screen, GraphenePanel *self)
{
  update_position(self);
  update_notification_windows(self);
}

static gboolean on_panel_clicked(GraphenePanel *self, GdkEventButton *event)
{
  if(event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY)
  {
    gtk_menu_popup(self->ContextMenu, NULL, NULL, NULL, NULL, event->button, event->time);
    return GDK_EVENT_STOP;
  }
  return GDK_EVENT_PROPAGATE;
}

static void on_context_menu_item_activate(GraphenePanel *self, GtkMenuItem *menuitem)
{
  const gchar *name = gtk_menu_item_get_label(menuitem);
  
  if(g_strcmp0(name, "Reload Applets") == 0)
  {
    // Reboot the panel.
    self->Rebooting = TRUE;
    g_application_quit(g_application_get_default());
  }
}

gboolean graphene_panel_is_rebooting(GraphenePanel *self)
{
  return self->Rebooting;
}


/**
 * graphene_panel_get_monitor:
 * @self: The #GraphenePanel affected.
 *
 * Returns: (transfer none): The monitor ID that the panel is docked on (for the panel's current screen; see gtk_widget_get_screen).
 */
gint graphene_panel_get_monitor(GraphenePanel *self)
{
  return self->MonitorID;
}

/**
 * graphene_panel_get_height:
 * @self: The #GraphenePanel affected.
 *
 * Returns: (transfer none): The height of the panel relative to the docking side of the screen.
 */
gint graphene_panel_get_height(GraphenePanel *self)
{
  return self->Height;
}



/*
 *
 * CAPTURE
 *
 */
 
static void capture_on_map(GtkWindow *capture, GraphenePanel *self);
 
static void init_capture(GraphenePanel *self)
{
  self->Captures = 0;
  self->CaptureWindow = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_type_hint(self->CaptureWindow, GDK_WINDOW_TYPE_HINT_DOCK);
  gtk_widget_set_app_paintable(GTK_WIDGET(self->CaptureWindow), TRUE);
  g_signal_connect(self->CaptureWindow, "map", G_CALLBACK(capture_on_map), self);

  GtkStyleContext *style = gtk_widget_get_style_context(GTK_WIDGET(self->CaptureWindow));
  gtk_style_context_remove_class(style, "background");

  GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(GTK_WIDGET(self->CaptureWindow)));
  if(visual)
    gtk_widget_set_visual(GTK_WIDGET(self->CaptureWindow), visual);
  else
    g_critical("No compositing! Stuff's not gonna look top.");
}

static void capture_on_map(GtkWindow *capture, GraphenePanel *self)
{
  update_position(self);
}

/**
 * graphene_panel_capture_screen:
 * @self: The #GraphenePanel affected.
 *
 * Creates a window which fills the specified amount of the screen.
 * Applets can draw to this window however they please.
 *
 * Returns: (transfer none): The capture count. If this is one, the capture has just been created.
 */
int graphene_panel_capture_screen(GraphenePanel *self)
{
  g_return_val_if_fail(GRAPHENE_IS_PANEL(self), 0);

  self->Captures++;
  if(self->Captures > 0)
    gtk_widget_show(GTK_WIDGET(self->CaptureWindow));
  return self->Captures;
}

/**
 * graphene_panel_end_capture:
 * @self: The #GraphenePanel affected.
 *
 * Decreases the capture count by one. If it reaches zero, the capture is removed.
 *
 * Returns: (transfer none): The capture count. If this is zero, the capture has ended.
 */
int graphene_panel_end_capture(GraphenePanel *self)
{
  g_return_val_if_fail(GRAPHENE_IS_PANEL(self), 0);
  
  self->Captures--;
  if(self->Captures <= 0)
  {
    self->Captures = 0;
    gtk_widget_hide(GTK_WIDGET(self->CaptureWindow));
  }
  return self->Captures;
}

/**
 * graphene_panel_clear_capture:
 * @self: The #GraphenePanel affected.
 *
 * Sets the capture count to 0 (removing the capture)
 */
void graphene_panel_clear_capture(GraphenePanel *self)
{
  g_return_if_fail(GRAPHENE_IS_PANEL(self));
  self->Captures = 0;
  update_position(self);
}

/**
 * graphene_panel_logout:
 * @self: The #GraphenePanel affected.
 *
 * Asks the session manager for a logout dialog. Does not guarantee a logout.
 */
void graphene_panel_logout(GraphenePanel *self)
{
  GDBusConnection *connection = g_application_get_dbus_connection(g_application_get_default());
  
  GDBusProxy *smProxy = g_dbus_proxy_new_sync(connection,
                  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                  NULL,
                  "org.gnome.SessionManager",
                  "/org/gnome/SessionManager",
                  "org.gnome.SessionManager",
                  NULL,
                  NULL);
                  
  g_dbus_proxy_call_sync(smProxy, "Logout", g_variant_new("(u)", 0), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, NULL);
  
  g_object_unref(smProxy);
}

/**
 * graphene_panel_shutdown:
 * @self: The #GraphenePanel affected.
 * @reboot: Whether or not to reboot.
 * 
 * Asks the session manager for a shutdown dialog. Does not guarantee a shutdown.
 */
void graphene_panel_shutdown(GraphenePanel *self, gboolean reboot)
{
  GDBusConnection *connection = g_application_get_dbus_connection(g_application_get_default());
  
  GDBusProxy *smProxy = g_dbus_proxy_new_sync(connection,
                  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                  NULL,
                  "org.gnome.SessionManager",
                  "/org/gnome/SessionManager",
                  "org.gnome.SessionManager",
                  NULL,
                  NULL);
                  
  g_dbus_proxy_call_sync(smProxy, reboot ? "Reboot" : "Shutdown", g_variant_new("(u)", 0), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, NULL);
  
  g_object_unref(smProxy);
}





/*
 *
 * NOTIFICATIONS
 *
 */

#define NOTIFICATION_DEFAULT_SHOW_TIME 5000 // ms
#define NOTIFICATION_URGENCY_LOW 0
#define NOTIFICATION_URGENCY_NORMAL 1
#define NOTIFICATION_URGENCY_CRITICAL 2
#define NOTIFICATION_SPACING 20 // pixels
#define NOTIFICATION_WIDTH 320
#define NOTIFICATION_HEIGHT 60

static const gchar *NotificationServerInterfaceXML =
"<node>"
"  <interface name='org.freedesktop.Notifications'>"
"    <method name='GetCapabilities'>"
"      <arg type='as' direction='out' name='capabilities'/>"
"    </method>"
"    <method name='Notify'>"
"      <arg type='s' direction='in' name='app_name'/>"
"      <arg type='u' direction='in' name='replaces_id'/>"
"      <arg type='s' direction='in' name='app_icon'/>"
"      <arg type='s' direction='in' name='summary'/>"
"      <arg type='s' direction='in' name='body'/>"
"      <arg type='as' direction='in' name='actions'/>"
"      <arg type='a{sv}' direction='in' name='hints'/>"
"      <arg type='i' direction='in' name='expire_timeout'/>"
"      <arg type='u' direction='out' name='id'/>"
"    </method>"
"    <method name='CloseNotification'>"
"      <arg type='u' direction='in' name='id'/>"
"    </method>"
"    <method name='GetServerInformation'>"
"      <arg type='s' direction='out' name='name'/>"
"      <arg type='s' direction='out' name='vendor'/>"
"      <arg type='s' direction='out' name='version'/>"
"      <arg type='s' direction='out' name='spec_version'/>"
"    </method>"
"    <signal name='ActionInvoked'>"
"      <arg type='u' name='id'/>"
"      <arg type='s' name='action_key'/>"
"    </signal>"
"    <signal name='NotificationClosed'>"
"      <arg type='u' name='id'/>"
"      <arg type='u' name='reason'/>"
"    </signal>"
"  </interface>"
"</node>";

typedef struct {
  guint32 id;
  const gchar *appName;
  const gchar *icon;
  const gchar *summary;
  const gchar *body;
  const gchar *category;
  gint timeout;
  gint urgency;
  // Do not set these when creating a notification
  guint timeoutSourceTag;
  GtkWindow *window; // do not set before calling show_notification
  GraphenePanel *panel; // owner; used for signal handling
} NotificationInfo;

static void notification_info_free(NotificationInfo *info)
{
  gtk_widget_destroy(GTK_WIDGET(info->window));
  g_free(info->appName);
  g_free(info->icon);
  g_free(info->summary);
  g_free(info->body);
  g_free(info->category);
  g_free(info);
}

static void notification_server_name_acquired(GDBusConnection *connection, const gchar *name, GraphenePanel *self);
static void notification_server_name_lost(GDBusConnection *connection, const gchar *name, GraphenePanel *self);
static void on_notification_server_method_called(GDBusConnection *connection, const gchar* sender,
              const gchar *objectPath, const gchar *interfaceName, const gchar *methodName, GVariant *parameters,
              GDBusMethodInvocation *invocation, gpointer *user_data);
static void show_notification(GraphenePanel *self, NotificationInfo *info);
static void remove_notification(GraphenePanel *self, guint32 id);
static gboolean on_notification_clicked(GtkWindow *window, GdkEventButton *event, NotificationInfo *notificationInfo);

static void init_notifications(GraphenePanel *self)
{
  self->Notifications = g_hash_table_new_full(NULL, NULL, NULL, notification_info_free);
  
  // TODO: Notify user on failure?
  self->NotificationServerBusNameID = g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_REPLACE,
    NULL, notification_server_name_acquired, notification_server_name_lost,
    self, NULL);
}

static void notification_server_name_acquired(GDBusConnection *connection, const gchar *name, GraphenePanel *self)
{
  self->NextNotificationID = 1;
  
  // TODO: Notify user on failure?
  const GDBusNodeInfo *interfaceInfo = g_dbus_node_info_new_for_xml(NotificationServerInterfaceXML, NULL);
  static const GDBusInterfaceVTable interfaceCallbacks = { on_notification_server_method_called, NULL, NULL };
  g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications", interfaceInfo->interfaces[0], &interfaceCallbacks, self, NULL, NULL);
}

static void notification_server_name_lost(GDBusConnection *connection, const gchar *name, GraphenePanel *self)
{
  NotificationInfo *info = g_new0(NotificationInfo, 1);
  info->icon = g_strdup("dialog-error");
  info->summary = g_strdup("System Notification Server Failed");
  info->body = g_strdup("You may not receive any notifications until you relog.");
  info->urgency = NOTIFICATION_URGENCY_CRITICAL;
  show_notification(self, info);
}

static void on_notification_server_method_called(GDBusConnection *connection, const gchar* sender,
              const gchar           *objectPath,
              const gchar           *interfaceName,
              const gchar           *methodName,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation,
              gpointer              *user_data)
{
  GraphenePanel *self = GRAPHENE_PANEL(user_data);
  
  if(g_strcmp0(methodName, "GetCapabilities") == 0)
  {
    const gchar *capabilities[] = {"body"};
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", capabilities));
  }
  else if(g_strcmp0(methodName, "Notify") == 0)
  {
    NotificationInfo *info = g_new0(NotificationInfo, 1);
    info->category = NULL; // TODO: Get from hints
    info->urgency = NOTIFICATION_URGENCY_NORMAL; // TODO: Get from hints
    
    // Note: g_varient_get allocates new memory for getting string parameters
    g_variant_get(parameters, "(susssasa{sv}i)", &info->appName, &info->id, &info->icon, &info->summary, &info->body, NULL, NULL, &info->timeout);
    show_notification(self, info);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", info->id));
  }
  else if(g_strcmp0(methodName, "CloseNotification") == 0)
  {
    guint32 id;
    
    g_variant_get(parameters, "(u)", &id);
    remove_notification(self, id);
    g_dbus_method_invocation_return_value(invocation, NULL);
  }
  else if(g_strcmp0(methodName, "GetServerInformation") == 0)
  {
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(ssss)", "Graphene Notifications", "Velt", "0.2", "1.2"));
  }
}

static gboolean show_notification_remove_cb(NotificationInfo *info)
{
  remove_notification(info->panel, info->id);
  return FALSE; // Don't call again
}

/*
 * Shows a notification on the screen. Takes a NotificationInfo *info, which must be heap-allocated.
 * The memory for info will be automatically freed when the notification is removed. All strings in
 * info must also be heap-allocated, and will also be freed when the notification is removed.
 *
 * The id value in NotificationInfo should be 0 for a new ID, or an existing ID to replace a
 * notification.
 *
 * Values in info will be changed to their default values if an 'unspecified' value is passed.
 * For example, 0 for id goes to the new ID, and -1 for timeout goes to default number of seconds.
 * NULLs for strings are also allowed. Defaults will be used in their place.
 *
 * See https://developer.gnome.org/notification-spec/ for more info.
 */
static void show_notification(GraphenePanel *self, NotificationInfo *info)
{
  if(info->id == 0)
  {
    if(self->NextNotificationID == 0)
      self->NextNotificationID = 1;

    info->id = self->NextNotificationID++;
  }
  
  if(info->timeout < 0)
    info->timeout = NOTIFICATION_DEFAULT_SHOW_TIME; // milliseconds
  
  info->panel = self;
  
  // Create popup window
  {
    info->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_POPUP));
    GtkStyleContext *style = gtk_widget_get_style_context(GTK_WIDGET(info->window));
    gtk_style_context_add_class(style, "notification");
    
    g_signal_connect(info->window, "button_press_event", on_notification_clicked, info);
    
    GtkBox *hBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_box_set_homogeneous(hBox, FALSE);

    GtkImage *icon = GTK_IMAGE(gtk_image_new_from_icon_name(info->icon, GTK_ICON_SIZE_DIALOG));
    gtk_box_pack_start(hBox, GTK_WIDGET(icon), FALSE, FALSE, 5);
    
    GtkBox *vBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_box_set_homogeneous(vBox, FALSE);
    gtk_box_pack_start(hBox, GTK_WIDGET(vBox), TRUE, TRUE, 5);
    
    GtkLabel *summaryLabel = GTK_LABEL(gtk_label_new(NULL));
    gtk_label_set_markup(summaryLabel, info->summary);
    gtk_widget_set_halign(GTK_WIDGET(summaryLabel), GTK_ALIGN_START);
    gtk_label_set_ellipsize(summaryLabel, PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(vBox, GTK_WIDGET(summaryLabel), TRUE, TRUE, 0);
    
    GtkLabel *bodyLabel = GTK_LABEL(gtk_label_new(NULL));
    const gchar *bodyMarkup = g_strdup_printf("<span size='smaller'>%s</span>", info->body);
    gtk_label_set_markup(bodyLabel, bodyMarkup);
    g_free(bodyMarkup);
    gtk_widget_set_halign(GTK_WIDGET(bodyLabel), GTK_ALIGN_START);
    gtk_label_set_ellipsize(bodyLabel, PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(vBox, GTK_WIDGET(bodyLabel), TRUE, TRUE, 0);
    
    gtk_window_resize(info->window, NOTIFICATION_WIDTH, NOTIFICATION_HEIGHT);

    gtk_container_add(GTK_CONTAINER(info->window), GTK_WIDGET(hBox));
    gtk_widget_show_all(GTK_WIDGET(info->window));
  }

  // Add to table
  g_hash_table_insert(self->Notifications, GUINT_TO_POINTER(info->id), info);
  
  info->timeoutSourceTag = 0;
  if(info->timeout > 0 && info->urgency != NOTIFICATION_URGENCY_CRITICAL)
    info->timeoutSourceTag = g_timeout_add(info->timeout, show_notification_remove_cb, info);
      
  update_notification_windows(self);
}

static void remove_notification(GraphenePanel *self, guint32 id)
{
  NotificationInfo *info = g_hash_table_lookup(self->Notifications, GUINT_TO_POINTER(id));
  if(info && info->timeoutSourceTag > 0)
    g_source_remove(info->timeoutSourceTag);
  
  g_hash_table_remove(self->Notifications, GUINT_TO_POINTER(id)); // Destroys the window
  update_notification_windows(self);
}

static gint notification_compare_func(const NotificationInfo *a, const NotificationInfo *b)
{
  // TODO: Sort "critical" notifications to the top
  return (a->id < b->id)?1:-1; // Sort newest to the top
}

static void update_notification_windows(GraphenePanel *self)
{
  GList *notificationList = g_hash_table_get_values(self->Notifications); // NotificationInfo* list
  notificationList = g_list_sort(notificationList, notification_compare_func);
  
  guint i=0;
  for(GList *notification=notificationList; notification; notification=notification->next)
  {
    NotificationInfo *n = notification->data;
    // TODO: Make sure it shows on the monitor the Panel is on
    gtk_window_move(n->window, NOTIFICATION_SPACING, NOTIFICATION_SPACING + i*(NOTIFICATION_HEIGHT + NOTIFICATION_SPACING));
    ++i;
  }
}

static gboolean on_notification_clicked(GtkWindow *window, GdkEventButton *event, NotificationInfo *notificationInfo)
{
  if(event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY)
  {
    remove_notification(notificationInfo->panel, notificationInfo->id);
    return GDK_EVENT_STOP;
  }
  return GDK_EVENT_PROPAGATE;
}
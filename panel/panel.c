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
 */

#include "config.h"
#include "panel.h"
#include <gdk/gdkscreen.h>
#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include "launcher/launcher-applet.h"
#include "tasklist/tasklist-applet.h"
#include "settings/settings-applet.h"
#include "clock/clock-applet.h"
#include "notifications.h"
#include "status-notifier-dbus-ifaces.h"

struct _GraphenePanel {
  GtkWindow parent;
  
  GDBusProxy *SMProxy;
  GDBusProxy *ClientProxy;

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

  GrapheneNotificationManager *notificationManager;
  
  // System Tray
  guint statusNotifierWatchId;
  DBusFreedesktopStatusNotifierWatcher *statusNotifierWatcherProxy;
  gchar *statusNotifierHostDBusName;
  guint statusNotifierHostDBusNameId;
  GHashTable *statusNotifierApplets;
};


G_DEFINE_TYPE(GraphenePanel, graphene_panel, GTK_TYPE_WINDOW)


static void app_activate(GtkApplication *app, gpointer userdata);
static void on_exit_signal(gpointer userdata);
static void graphene_panel_dispose(GObject *self_);
static void init_layout(GraphenePanel *self);
static void init_systray(GraphenePanel *self);
static void init_capture(GraphenePanel *self);
static GtkWidget * add_applet(GraphenePanel *self, GtkWidget *applet);
static void update_position(GraphenePanel *self);
static void on_monitors_changed(GdkScreen *screen, GraphenePanel *self);
static gboolean on_panel_clicked(GraphenePanel *self, GdkEventButton *event);
static void on_context_menu_item_activate(GraphenePanel *self, GtkMenuItem *menuitem);


int main(int argc, char **argv)
{
  GtkApplication *app = gtk_application_new("io.velt.graphene-panel", G_APPLICATION_FLAGS_NONE);
  g_object_set(G_OBJECT(app), "register-session", TRUE, NULL);
  g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
  
  g_unix_signal_add(SIGTERM, (GSourceFunc)on_exit_signal, NULL);
  g_unix_signal_add(SIGINT, (GSourceFunc)on_exit_signal, NULL);
  g_unix_signal_add(SIGHUP, (GSourceFunc)on_exit_signal, NULL);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  gtk_widget_destroy(GTK_WIDGET(graphene_panel_get_default()));
  g_object_unref(app);
  return status;
}

static void app_activate(GtkApplication *app, gpointer userdata)
{
  GraphenePanel *panel = graphene_panel_get_default(); // First call will create it
  gtk_application_add_window(app, GTK_WINDOW(panel));
  gtk_widget_show(GTK_WIDGET(panel));  
}

static void on_exit_signal(gpointer userdata)
{
  g_application_quit(g_application_get_default());
}

/*
 * Panel class
 */

GraphenePanel* graphene_panel_new(void)
{
  return GRAPHENE_PANEL(g_object_new(GRAPHENE_TYPE_PANEL, NULL));
}

GraphenePanel * graphene_panel_get_default(void)
{
  static GraphenePanel *panel = NULL;
  if(!GRAPHENE_IS_PANEL(panel))
    panel = graphene_panel_new();
  return panel;
}

static void graphene_panel_class_init(GraphenePanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = graphene_panel_dispose;
}

static void graphene_panel_init(GraphenePanel *self)
{
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

  // Get SM proxy
  GDBusConnection *connection = g_application_get_dbus_connection(g_application_get_default());
  self->SMProxy = g_dbus_proxy_new_sync(connection, 0, NULL, "org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager", NULL, NULL);
  if(self->SMProxy)
  {
    GVariant *clientObjectPathVariant = g_dbus_proxy_call_sync(self->SMProxy, "GetCurrentClient", NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
    if(clientObjectPathVariant)
    {
      gchar *clientObjectPath = NULL;
      g_variant_get(clientObjectPathVariant, "(o)", &clientObjectPath);
      g_variant_unref(clientObjectPathVariant);
      if(clientObjectPath)
      {
        self->ClientProxy = g_dbus_proxy_new_sync(connection, 0, NULL, "org.gnome.SessionManager", clientObjectPath, "org.gnome.SessionManager.Client", NULL, NULL);
        g_free(clientObjectPath);
      }
    }
  }
  
  // Update the position now and when the size or monitors change
  g_signal_connect(gtk_window_get_screen(GTK_WINDOW(self)), "monitors-changed", G_CALLBACK(on_monitors_changed), self);
  g_signal_connect(self, "map", G_CALLBACK(update_position), NULL);
  // g_signal_connect(self, "size-allocate", G_CALLBACK(on_size_allocate), NULL); // TODO: Size allocate necessary? Seems to just create lots of unnecessary position updates

  // Load things
  init_layout(self);
  init_capture(self);
  init_systray(self);
  
  self->notificationManager = graphene_notification_manager_get_default();
}

static void graphene_panel_dispose(GObject *self_)
{
  GraphenePanel *self = GRAPHENE_PANEL(self_);
  
  g_clear_object(&self->ClientProxy);
  g_clear_object(&self->SMProxy);
  g_clear_object(&self->notificationManager);
  
  if(self->statusNotifierApplets)
    g_hash_table_destroy(self->statusNotifierApplets);
  if(self->statusNotifierHostDBusNameId)
    g_bus_unown_name(self->statusNotifierHostDBusNameId);
  if(self->statusNotifierWatchId)
    g_bus_unwatch_name(self->statusNotifierWatchId);
  self->statusNotifierApplets = NULL;
  self->statusNotifierHostDBusNameId = 0;
  self->statusNotifierWatchId = 0;
  g_free(self->statusNotifierHostDBusName);
  g_clear_object(&self->statusNotifierWatcherProxy);

  G_OBJECT_CLASS(graphene_panel_parent_class)->dispose(self_);
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

  // Base applets
  GtkWidget *launcher = add_applet(self, GTK_WIDGET(graphene_launcher_applet_new()));
  gtk_box_set_child_packing(self->AppletLayout, launcher, FALSE, FALSE, 0, GTK_PACK_START);
  GtkWidget *tasklist = add_applet(self, GTK_WIDGET(graphene_tasklist_applet_new()));
  gtk_box_set_child_packing(self->AppletLayout, tasklist, TRUE, TRUE, 0, GTK_PACK_START);
  add_applet(self, GTK_WIDGET(graphene_clock_applet_new()));
  add_applet(self, GTK_WIDGET(graphene_settings_applet_new()));
  
  // Context menu
  self->ContextMenu = GTK_MENU(gtk_menu_new());
  GtkWidget *reloadapplets = gtk_menu_item_new_with_label("Reload Applets");
  g_signal_connect_swapped(reloadapplets, "activate", G_CALLBACK(on_context_menu_item_activate), self);
  gtk_menu_shell_append(GTK_MENU_SHELL(self->ContextMenu), reloadapplets);
  gtk_widget_show_all(GTK_WIDGET(self->ContextMenu));
  g_signal_connect_swapped(launcher, "button_press_event", G_CALLBACK(on_panel_clicked), self);

  // Show
  gtk_widget_show_all(GTK_WIDGET(self->AppletLayout));
}

/*
 * Adds an applet (GtkWidget) to the right side of the panel. Uses g_object_ref_sink() on parameter 'applet' to obtain a reference.
 * Returns 'applet' for convenience.
 */
static GtkWidget * add_applet(GraphenePanel *self, GtkWidget *applet)
{
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(applet)), "graphene-applet");
  gtk_box_pack_end(self->AppletLayout, applet, FALSE, FALSE, 0);
  return applet;
}

// Positions/sizes the self at the proper location on the window
static void update_position(GraphenePanel *self)
{
  GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(self));
  
  // Get the monitor for this panel
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
    g_debug("Updating position: [%i, %i, %i, %i, %i]\n",
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
  // TODO: Fix capture window only convers one monitor
  int cx, cy, cwidth, cheight;
  gtk_window_get_position(GTK_WINDOW(self->CaptureWindow), &cx, &cy);
  gtk_window_get_size(GTK_WINDOW(self->CaptureWindow), &cwidth, &cheight);
  
  if(self->CaptureWindow && (cx != captureRect.x || cy != captureRect.y || cwidth != captureRect.width || cheight != captureRect.height))
  {
    g_debug("Updating capture position: [%i, %i, %i, %i, %i]\n",
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
    if(self->ClientProxy)
      g_dbus_proxy_call_sync(self->ClientProxy, "Restart", NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
  }
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

/**
 * graphene_panel_logout:
 * @self: The #GraphenePanel affected.
 *
 * Asks the session manager for a logout dialog. Does not guarantee a logout.
 */
void graphene_panel_logout(GraphenePanel *self)
{
  if(self->SMProxy)
    g_dbus_proxy_call_sync(self->SMProxy, "Logout", g_variant_new("(u)", 0), G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
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
  if(self->SMProxy)      
    g_dbus_proxy_call_sync(self->SMProxy, reboot ? "Reboot" : "Shutdown", g_variant_new("(u)", 0), G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
}




/*
 * System Tray
 *
 * See status-notifier-watcher.c for a summary of the strangeness that is the status notificer spec(s).
 * TODO: Support more features than just the app's icon
 */

#define STATUSNOTIFIER_WATCHER_DBUS_NAME "org.freedesktop.StatusNotifierWatcher"
#define STATUSNOTIFIER_WATCHER_DBUS_PATH "/StatusNotifierWatcher"

#define STATUSNOTIFIER_ITEM_DBUS_IFACE "org.freedesktop.StatusNotifierItem"
#define STATUSNOTIFIER_ITEM_KDE_DBUS_IFACE "org.kde.StatusNotifierItem"
#define STATUSNOTIFIER_ITEM_DBUS_PATH "/StatusNotifierItem"

#define STATUSNOTIFIER_HOST_DBUS_NAME_BASE "org.freedesktop.StatusNotifierHost"

typedef struct {
  GtkWidget *applet;
  GtkImage *icon; // This is expected to be a child of 'applet,' and only its own variable for ease of access
  gchar *dbusName;
  GCancellable *cancellable;
  GList *signalIds;
  GDBusConnection *connection;
  GraphenePanel *panel;
  gint refs;
} StatusNotifierApplet;

static void statusnotifier_on_watcher_appeared(GDBusConnection *connection, const gchar *name, const gchar *nameOwner, GraphenePanel *self);
static void statusnotifier_on_watcher_vanished(GDBusConnection *connection, const gchar *name, GraphenePanel *self);
static void statusnotifier_on_watcher_proxy_ready(GObject *sourceObject, GAsyncResult *res, GraphenePanel *self);
static void statusnotifier_on_item_registered(GraphenePanel *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy);
static void statusnotifier_on_item_unregistered(GraphenePanel *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy);
static void remove_status_notifier_applet(StatusNotifierApplet *snApplet);
static void unref_status_notifier_applet(StatusNotifierApplet *snApplet);
static void statusnotifier_update_icon(GDBusConnection *connection, const gchar *senderName, const gchar *objectPath,
  const gchar *interfaceName, const gchar *signalName, GVariant *parameters, StatusNotifierApplet *snApplet);
static GdkPixbufAnimation * icon_variant_array_to_best_icon(GVariant *variant, gboolean bestForHeight, guint sizeRequest,
  gfloat maxWideMult, gfloat autoRotateLimit, gfloat autoScaleLimit);
static gboolean statusnotifier_button_press_event(GtkButton *button, GdkEventButton *event, StatusNotifierApplet *snApplet);
static gboolean statusnotifier_button_scroll_event(GtkButton *button, GdkEventScroll *event, StatusNotifierApplet *snApplet);
static gboolean statusnotifier_button_mapped(GtkButton *button, GdkEvent *event);

static void init_systray(GraphenePanel *self)
{
  GDBusConnection *connection = g_application_get_dbus_connection(g_application_get_default());
  if(!connection)
    return;
  
  self->statusNotifierApplets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)remove_status_notifier_applet);
  
  // Own the name of a host, it will be registered in the Watcher proxy ready callback
  self->statusNotifierHostDBusName = g_strdup_printf("%s-%i-%i", STATUSNOTIFIER_HOST_DBUS_NAME_BASE, getpid(), g_random_int());
  self->statusNotifierHostDBusNameId = g_bus_own_name_on_connection(connection, self->statusNotifierHostDBusName, G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, NULL, self, NULL);
  
  if(!self->statusNotifierHostDBusNameId)
    return;
  
  // Watch for the Watcher to be available (it probably already is)  
  self->statusNotifierWatchId = g_bus_watch_name_on_connection(connection, STATUSNOTIFIER_WATCHER_DBUS_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
    (GBusNameAppearedCallback)statusnotifier_on_watcher_appeared, (GBusNameVanishedCallback)statusnotifier_on_watcher_vanished, self, NULL);
}

static void statusnotifier_on_watcher_appeared(GDBusConnection *connection, const gchar *name, const gchar *nameOwner, GraphenePanel *self)
{
  // Get a proxy for the Watcher
  dbus_freedesktop_status_notifier_watcher_proxy_new(connection, G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
    name, STATUSNOTIFIER_WATCHER_DBUS_PATH, NULL, (GAsyncReadyCallback)statusnotifier_on_watcher_proxy_ready, self);
}

static void statusnotifier_on_watcher_vanished(GDBusConnection *connection, const gchar *name, GraphenePanel *self)
{
  g_clear_object(&self->statusNotifierWatcherProxy);
}

static void statusnotifier_on_watcher_proxy_ready(GObject *sourceObject, GAsyncResult *res, GraphenePanel *self)
{
  g_clear_object(&self->statusNotifierWatcherProxy);
  self->statusNotifierWatcherProxy = dbus_freedesktop_status_notifier_watcher_proxy_new_finish(res, NULL);
  if(!self->statusNotifierWatcherProxy)
    return;
  
  // Listen for items to be added or removed
  g_signal_connect_swapped(self->statusNotifierWatcherProxy, "status-notifier-item-registered", G_CALLBACK(statusnotifier_on_item_registered), self);
  g_signal_connect_swapped(self->statusNotifierWatcherProxy, "status-notifier-item-unregistered", G_CALLBACK(statusnotifier_on_item_unregistered), self);
  
  // Add any items that already exist
  const gchar * const *items = dbus_freedesktop_status_notifier_watcher_get_registered_status_notifier_items(self->statusNotifierWatcherProxy);
  for(guint i=0; items[i] != NULL; ++i)
    statusnotifier_on_item_registered(self, items[i], self->statusNotifierWatcherProxy);
  
  // Register as a host
  dbus_freedesktop_status_notifier_watcher_call_register_status_notifier_host(self->statusNotifierWatcherProxy, self->statusNotifierHostDBusName, NULL, NULL, NULL);
}

static void statusnotifier_on_item_registered(GraphenePanel *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy)
{
  if(g_hash_table_contains(self->statusNotifierApplets, service))
    return;
  
  StatusNotifierApplet *snApplet = g_new0(StatusNotifierApplet, 1);
  snApplet->dbusName = g_strdup(service);
  snApplet->panel = self;
  snApplet->cancellable = g_cancellable_new();
  snApplet->connection = g_application_get_dbus_connection(g_application_get_default());
  
  // Init the button
  // The widget is shown only once an icon is set
  GtkButton *button = GTK_BUTTON(gtk_button_new_from_icon_name("default", GTK_ICON_SIZE_MENU));
  gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
  g_signal_connect(button, "button-press-event", G_CALLBACK(statusnotifier_button_press_event), snApplet);
  g_signal_connect(button, "scroll-event", G_CALLBACK(statusnotifier_button_scroll_event), snApplet);
  g_signal_connect(button, "map", G_CALLBACK(statusnotifier_button_mapped), NULL);
  add_applet(snApplet->panel, GTK_WIDGET(button));
  snApplet->applet = GTK_WIDGET(button);
  snApplet->icon = GTK_IMAGE(gtk_button_get_image(button));

  // Add the applet
  snApplet->refs = 1;
  g_hash_table_insert(self->statusNotifierApplets, g_strdup(service), snApplet);
  
  // Some clients don't send a org.freedesktop.DBus.Properties.PropertiesChanged signal when
  // their properties change, leaving a GDBusProxy with an out-of-date cache. Also, multiple
  // interfaces (freedesktop and KDE) need to be monitored. For these reasons, it's easier to
  // not use a GDBusProxy and instead watch for signals ourselves.
  
  guint newIconSubId = g_dbus_connection_signal_subscribe(snApplet->connection, service, NULL, // Passing NULL for interface allows signals from both freedesktop and KDE interfaces
    "NewIcon", STATUSNOTIFIER_ITEM_DBUS_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE, (GDBusSignalCallback)statusnotifier_update_icon, snApplet, NULL);
  snApplet->signalIds = g_list_prepend(snApplet->signalIds, GUINT_TO_POINTER(newIconSubId));
  
  statusnotifier_update_icon(snApplet->connection, NULL, NULL, STATUSNOTIFIER_ITEM_DBUS_IFACE, NULL, NULL, snApplet);
  statusnotifier_update_icon(snApplet->connection, NULL, NULL, STATUSNOTIFIER_ITEM_KDE_DBUS_IFACE, NULL, NULL, snApplet);
}

static void statusnotifier_on_item_unregistered(GraphenePanel *self, const gchar *service, DBusFreedesktopStatusNotifierWatcher *proxy)
{
  // When the snApplet is removed (remove_status_notifier_applet), it cancels the 'proxy_new' operation if it is in process
  g_hash_table_remove(self->statusNotifierApplets, service);
}

static void remove_status_notifier_applet(StatusNotifierApplet *snApplet)
{
  if(!snApplet)
    return;
  
  for(GList *id=snApplet->signalIds;id!=NULL;id=id->next)
    g_dbus_connection_signal_unsubscribe(snApplet->connection, GPOINTER_TO_UINT(id->data));
  g_clear_pointer(&snApplet->signalIds, g_list_free);
  
  gtk_widget_hide(snApplet->applet);
  g_cancellable_cancel(snApplet->cancellable);
  
  unref_status_notifier_applet(snApplet);
}

static void unref_status_notifier_applet(StatusNotifierApplet *snApplet)
{
  if(!snApplet)
    return;
  
  snApplet->refs--;
  if(snApplet->refs <= 0)
  {
    for(GList *id=snApplet->signalIds;id!=NULL;id=id->next)
      g_dbus_connection_signal_unsubscribe(snApplet->connection, GPOINTER_TO_UINT(id->data));
    g_clear_pointer(&snApplet->signalIds, g_list_free);
    
    g_clear_pointer(&snApplet->dbusName, g_free);
    g_clear_pointer(&snApplet->applet, gtk_widget_destroy);
    g_cancellable_cancel(snApplet->cancellable);
    g_clear_object(&snApplet->cancellable);
    g_free(snApplet);
  }
}

typedef struct {
  StatusNotifierApplet *snApplet;
  gchar *interface;
} sn_applet_callback_data;

static gboolean str_null_or_whitespace(const gchar *str)
{
  if(!str)
    return TRUE;
  
  gchar *dup = g_strdup(str);
  dup = g_strstrip(dup); // Modifies string in-place
  
  gboolean ret = (g_strcmp0(dup, "") == 0);
  g_free(dup);
  return ret;
}

/*
 * Will show the applet (snApplet->applet) for the first time once the icon has been set.
 * statusnotifier_update_icon calls statusnotifier_update_icon_test_named once the IconName dbus
 * property has been retrieved. It then checks the icon name, and if it is valid, sets the icon
 * and stops. If it is not, it calls statusnotifier_update_icon_test_pixmap once the IconPixmap
 * property has been retrieved, which then attempts to set the icon from a pixmap.
 */
static void statusnotifier_update_icon_test_pixmap(GDBusConnection *connection, GAsyncResult *res, StatusNotifierApplet *snApplet)
{
  // Check return value for a valid pixmap
  GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);
  if(ret && g_variant_is_of_type(ret, G_VARIANT_TYPE("(v)")))
  {
    GVariant *iconPixmapVariantBoxed = g_variant_get_child_value(ret, 0);
    GVariant *iconPixmapVariant = g_variant_get_variant(iconPixmapVariantBoxed);
    if(iconPixmapVariant && g_variant_is_of_type(iconPixmapVariant, G_VARIANT_TYPE("a(iiay)")))
    {
      GdkPixbufAnimation *anim = icon_variant_array_to_best_icon(iconPixmapVariant, TRUE, 24, 3, 30.0/24.0, 36.0/24.0); // TODO: These should probably depend on the size of the panel
      if(anim)
      {
        gtk_image_set_from_animation(snApplet->icon, anim);
        gtk_widget_show(snApplet->applet);
        g_object_unref(anim);
        g_variant_unref(iconPixmapVariant);
        g_variant_unref(iconPixmapVariantBoxed);
        g_variant_unref(ret);
        unref_status_notifier_applet(snApplet);
        return;
      }
    }
    g_clear_pointer(&iconPixmapVariant, g_variant_unref);
    g_clear_pointer(&iconPixmapVariantBoxed, g_variant_unref);
  }
  g_clear_pointer(&ret, g_variant_unref);
  unref_status_notifier_applet(snApplet);
}
static void statusnotifier_update_icon_test_named(GDBusConnection *connection, GAsyncResult *res, sn_applet_callback_data *data)
{
  StatusNotifierApplet *snApplet = data->snApplet;
  gchar *interfaceName = data->interface;
  g_free(data);
  
  // Check return value for a valid icon name
  GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);
  if(ret && g_variant_is_of_type(ret, G_VARIANT_TYPE("(v)")))
  {
    GVariant *iconNameVariantBoxed = g_variant_get_child_value(ret, 0);
    GVariant *iconNameVariant = g_variant_get_variant(iconNameVariantBoxed);
    if(g_variant_is_of_type(iconNameVariant, G_VARIANT_TYPE("s")))
    {
      const gchar *iconName = g_variant_get_string(iconNameVariant, NULL);
      if(!str_null_or_whitespace(iconName))
      {
        // Set the icon
        gtk_image_set_from_icon_name(snApplet->icon, iconName, GTK_ICON_SIZE_MENU);
        gtk_widget_show(snApplet->applet);
        g_variant_unref(iconNameVariant);
        g_variant_unref(iconNameVariantBoxed);
        g_variant_unref(ret);
        g_free(interfaceName);
        unref_status_notifier_applet(snApplet);
        return;
      }
    }
    g_clear_pointer(&iconNameVariant, g_variant_unref);
    g_clear_pointer(&iconNameVariantBoxed, g_variant_unref);
  }
  g_clear_pointer(&ret, g_variant_unref);
  
  // If the icon wasn't valid, try a pixmap instead
  g_dbus_connection_call(connection,
    snApplet->dbusName,
    STATUSNOTIFIER_ITEM_DBUS_PATH,
    "org.freedesktop.DBus.Properties",
    "Get",
    g_variant_new("(ss)", interfaceName, "IconPixmap"),
    G_VARIANT_TYPE("(v)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    snApplet->cancellable,
    (GAsyncReadyCallback)statusnotifier_update_icon_test_pixmap,
    snApplet);
    
  g_free(interfaceName);
}
static void statusnotifier_update_icon(GDBusConnection *connection, const gchar *senderName, const gchar *objectPath,
  const gchar *interfaceName, const gchar *signalName, GVariant *parameters, StatusNotifierApplet *snApplet)
{
  if(!snApplet)
    return;
  
  snApplet->refs ++;
  
  sn_applet_callback_data *data = g_new0(sn_applet_callback_data, 1);
  data->snApplet = snApplet;
  data->interface = g_strdup(interfaceName);

  g_dbus_connection_call(connection,
    snApplet->dbusName,
    STATUSNOTIFIER_ITEM_DBUS_PATH,
    "org.freedesktop.DBus.Properties",
    "Get",
    g_variant_new("(ss)", interfaceName, "IconName"),
    G_VARIANT_TYPE("(v)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    snApplet->cancellable,
    (GAsyncReadyCallback)statusnotifier_update_icon_test_named,
    data);
}

/*
 * Converts an in icon array variant of type a(iiay) to a GdkPixbufAnimation.
 * This type of variant is documented here: https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/Icons/
 * To summarize, each (iiay) is an icon with width, height, and byte data (ARGB32 format, network byte order).
 * There are multiple icons (the outer a) in order to specify multiple icon sizes or an animation.
 * Returns NULL if there is an error, or if there is no valid icon. Otherwise, a GdkPixbufAnimation pointer is returned
 * that displays an animation or static (one frame) image [transfer full].
 * @bestForHeight: if TRUE, side A is vertical and side B is horizontal, and the opposite if FALSE
 * @sizeRequest: attempt to find an icon that best matches side A
 * @maxWideMult: icons that have side B more than @maxWideMult times larger than side A are ignored (<= 0 to disable)
 * @autoRotateLimit: if (best-fit) side A is @autoRotateLimit times longer/shorter than than side B, rotate 90 degrees to fit (< 1 to disable)
 * @autoScaleLimit: if (best-fit) side A is @autoScaleLimit longer/shorter than @sizeRequest, scale it to match (< 1 to disable, scaled after rotation)
 */
static GdkPixbufAnimation * icon_variant_array_to_best_icon(GVariant *variant, gboolean bestForHeight, guint sizeRequest,
  gfloat maxWideMult, gfloat autoRotateLimit, gfloat autoScaleLimit)
{
  if(!g_variant_check_format_string(variant, "a(iiay)", FALSE))
    return NULL;
  
  if(g_variant_n_children(variant) == 0)
    return NULL;

  // Find the best-matched icon depending on size
  // Only the first of multiple same-best-matching icons will be picked
  gint bestMatchIndex = -1;
  {
    guint bestMatchDist = G_MAXUINT, i=0;
    gint width=0, height=0;
    GVariantIter iter;
    g_variant_iter_init(&iter, variant);
    while(g_variant_iter_loop(&iter, "(iiay)", &width, &height, NULL))
    {
      ++i;
      
      if(width == 0 || height == 0)
        continue;
      
      // Ignore if too wide/tall
      if(maxWideMult > 0 && ((bestForHeight && width > height * maxWideMult) || (!bestForHeight && height > width * maxWideMult)))
        continue;
      
      // Check best width or height
      guint dist = ABS((bestForHeight ? height : width) - (gint)sizeRequest);
      if(dist < bestMatchDist)
      {
        bestMatchDist = dist;
        bestMatchIndex = i-1;
      }
    }
  }
  
  if(bestMatchIndex < 0 || bestMatchIndex >= g_variant_n_children(variant))
    return NULL;
    
  gint bestWidth=0, bestHeight=0;
  GVariant *bestMatchVariant = g_variant_get_child_value(variant, bestMatchIndex);
  g_variant_get(bestMatchVariant, "(iiay)", &bestWidth, &bestHeight, NULL);
  
  gint originalWidth = bestWidth;
  gint originalHeight = bestHeight;
  
  gint sideALength = bestForHeight ? bestHeight : bestWidth;
  gint sideBLength = bestForHeight ? bestWidth : bestHeight;
  
  // Rotate (for wide/tall icons)
  gboolean rotate = FALSE;
  if(autoRotateLimit >= 1 && (sideALength > (gfloat)sideBLength * autoRotateLimit || sideALength < (gfloat)sideBLength / autoRotateLimit))
  {
    gint temp = sideALength;
    sideALength = sideBLength;
    sideBLength = temp;
    bestWidth = originalHeight;
    bestHeight = originalWidth;
    rotate = TRUE;
  }
  
  // Scale
  if(autoScaleLimit >= 1 && (sideALength > (gfloat)sizeRequest * autoScaleLimit || sideALength < (gfloat)sizeRequest / autoScaleLimit))
  {
    sideBLength *= (gfloat)sizeRequest / (gfloat)sideALength;
    sideALength = sizeRequest;
    bestWidth = bestForHeight ? sideBLength : sideALength;
    bestHeight = bestForHeight ? sideALength : sideBLength;
  }
  
  // Now get all the icons that match the best size (will be an animation if more than 1)
  GList *pixbufs = NULL;
  {
    gint width=0, height=0;
    GVariantIter *byteIter;
    GVariantIter iter;
    g_variant_iter_init(&iter, variant);
    while(g_variant_iter_loop(&iter, "(iiay)", &width, &height, &byteIter))
    {
      if(width != originalWidth || height != originalHeight)
        continue;
      
      gsize numBytes = g_variant_iter_n_children(byteIter);
      guchar *data = g_new(guchar, numBytes);
      
      guchar byte=0;
      guint i=0;
      while(g_variant_iter_loop(byteIter, "y", &byte))
        data[i++] = byte;
      
      // The incoming bytes are a series of ARGB8888-formatted pixels.
      // Not dealing with ints here to make endian conversion easier.
      
      guchar a,r,g,b;
      for(guint i=0;i<numBytes;i+=4)
      {
        // Re-order the bytes from ARGB to RGBA
        a = data[i+0], r = data[i+1], g = data[i+2], b = data[i+3];
        data[i+0] = r, data[i+1] = g, data[i+2] = b, data[i+3] = a;
      }
      
      // The pixbuf will automatically free 'data' when it is finalized
      GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, TRUE, 8, width, height, width * 4, (GdkPixbufDestroyNotify)g_free, NULL);
      
      // Rotate
      if(rotate)
      {
        GdkPixbuf *rotated = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
        g_object_unref(pixbuf);
        pixbuf = rotated;
      }
      
      // Scale
      if(width != bestWidth || height != bestHeight)
      {
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, bestWidth, bestHeight, GDK_INTERP_HYPER);
        g_object_unref(pixbuf);
        pixbuf = scaled;
      }
      
      pixbufs = g_list_prepend(pixbufs, pixbuf);
    }
  }
  
  g_variant_unref(bestMatchVariant);
  
  pixbufs = g_list_reverse(pixbufs); // Since they were added in reverse order for speed
  
  guint numPixbufs = g_list_length(pixbufs);
  
  gfloat fps = numPixbufs; // Unsure of what the FPS should be, so just make it one loop per second
  GdkPixbufSimpleAnim *anim = gdk_pixbuf_simple_anim_new(bestWidth, bestHeight, fps);
  gdk_pixbuf_simple_anim_set_loop(anim, TRUE);
  
  for(GList *x=pixbufs; x!=NULL; x=x->next)
    gdk_pixbuf_simple_anim_add_frame(anim, GDK_PIXBUF(x->data));
  
  return GDK_PIXBUF_ANIMATION(anim);
}

static gboolean statusnotifier_button_press_event(GtkButton *button, GdkEventButton *event, StatusNotifierApplet *snApplet)
{
  const gchar *method = NULL;
  
  if(event->button == GDK_BUTTON_PRIMARY && event->type == GDK_BUTTON_PRESS) // TODO: Make (Secondary)Activate use RELEASE. For some reason, button-press-event isn't sending releases
    method = "Activate";
  else if(event->button == GDK_BUTTON_MIDDLE && event->type == GDK_BUTTON_PRESS)
    method = "SecondaryActivate";
  else if(event->button == GDK_BUTTON_SECONDARY && event->type == GDK_BUTTON_PRESS)
    method = "ContextMenu";

  if(method)
  {
    g_dbus_connection_call(snApplet->connection,
      snApplet->dbusName, STATUSNOTIFIER_ITEM_DBUS_PATH, STATUSNOTIFIER_ITEM_DBUS_IFACE,
      method, g_variant_new("(ii)", event->x_root, event->y_root),
      NULL,  G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
      
    g_dbus_connection_call(snApplet->connection,
      snApplet->dbusName, STATUSNOTIFIER_ITEM_DBUS_PATH, STATUSNOTIFIER_ITEM_KDE_DBUS_IFACE,
      method, g_variant_new("(ii)", event->x_root, event->y_root),
      NULL,  G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    return GDK_EVENT_STOP;
  }
  return GDK_EVENT_PROPAGATE;
}

static gboolean statusnotifier_button_scroll_event(GtkButton *button, GdkEventScroll *event, StatusNotifierApplet *snApplet)
{
  gint delta = 0;
  const gchar *orientation = NULL;
  
  if(event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_DOWN)
  {
    orientation = "vertical";
    delta = event->delta_y;
    if(delta == 0)
      delta = (event->direction == GDK_SCROLL_UP) ? 1 : -1;
  }
  else if(event->direction == GDK_SCROLL_LEFT || event->direction == GDK_SCROLL_RIGHT)
  {
    orientation = "horizontal";
    delta = event->delta_x;
    if(delta == 0)
      delta = (event->direction == GDK_SCROLL_RIGHT) ? 1 : -1;
  }
  else if(event->direction == GDK_SCROLL_SMOOTH)
  {
    gdouble sdx = event->delta_x,  sdy = event->delta_y;
    gdk_event_get_scroll_deltas((GdkEvent *)event, &sdx, &sdy);
    orientation = (sdx > sdy) ? "horizontal" : "vertical";
    delta = (sdx > sdy) ? sdx : sdy;
  }
  
  if(orientation)
  {
    g_dbus_connection_call(snApplet->connection,
      snApplet->dbusName, STATUSNOTIFIER_ITEM_DBUS_PATH, STATUSNOTIFIER_ITEM_DBUS_IFACE,
      "Scroll", g_variant_new("(is)", delta, orientation),
      NULL,  G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
      
    g_dbus_connection_call(snApplet->connection,
      snApplet->dbusName, STATUSNOTIFIER_ITEM_DBUS_PATH, STATUSNOTIFIER_ITEM_KDE_DBUS_IFACE,
      "Scroll", g_variant_new("(is)", delta, orientation),
      NULL,  G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    return GDK_EVENT_STOP;
  }
  return GDK_EVENT_PROPAGATE;
}

static gboolean statusnotifier_button_mapped(GtkButton *button, GdkEvent *event)
{
  GdkWindow *window = gtk_button_get_event_window(button);
  if(window)
    gdk_window_set_events(window, gdk_window_get_events(window) | GDK_SCROLL_MASK); // Allow scroll events
  return GDK_EVENT_PROPAGATE;
}




/*
 * Capture
 *
 * Captures all input when panel applets have a popup by creating a full-monitor invisible window over all other windows.
 * TODO: Currently the capture window only covers the primary monitor. It needs one window per monitor.
 * TODO: This is a mediocre way to handle capturing, because relying on the capture window being z-sorted above other windows
 * but under the panel windows is probably going to fail with some apps. Possibly get the window manager to redirect all input
 * to the panel instead?
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
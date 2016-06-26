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
};


G_DEFINE_TYPE(GraphenePanel, graphene_panel, GTK_TYPE_WINDOW)


static void app_activate(GtkApplication *app, gpointer userdata);
static void on_exit_signal(gpointer userdata);
static void graphene_panel_dispose(GObject *self_);
static void init_layout(GraphenePanel *self);
static void init_capture(GraphenePanel *self);
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
  g_signal_connect(self, "button_press_event", G_CALLBACK(on_panel_clicked), NULL);
  // g_signal_connect(self, "size-allocate", G_CALLBACK(on_size_allocate), NULL); // TODO: Size allocate necessary? Seems to just create lots of unnecessary position updates

  // Load things
  init_layout(self);
  init_capture(self);
  
  self->notificationManager = graphene_notification_manager_get_default();
}

static void graphene_panel_dispose(GObject *self_)
{
  GraphenePanel *self = GRAPHENE_PANEL(self_);
  g_clear_object(&self->ClientProxy);
  g_clear_object(&self->SMProxy);
  g_clear_object(&self->notificationManager);
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
  GrapheneLauncherApplet *launcher = graphene_launcher_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(launcher)), "graphene-applet");
  gtk_box_pack_start(self->AppletLayout, GTK_WIDGET(launcher), FALSE, FALSE, 0);
  
  GrapheneTasklistApplet *tasklist = graphene_tasklist_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(tasklist)), "graphene-applet");
  gtk_box_pack_start(self->AppletLayout, GTK_WIDGET(tasklist), TRUE, TRUE, 0);
  
  GrapheneClockApplet *clock = graphene_clock_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(clock)), "graphene-applet");
  gtk_box_pack_end(self->AppletLayout, GTK_WIDGET(clock), FALSE, FALSE, 0);
  
  GrapheneSettingsApplet *settings = graphene_settings_applet_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(settings)), "graphene-applet");
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
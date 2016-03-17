/*
 * graphene-desktop
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
 * This should be compiled into libvos for GIntrospection, and NOT compiled into the panel application binary.
 */

#include "panel.h"
#include "applet-extension.h"
#include <libpeas/peas.h>
#include <gdk/gdkscreen.h>
#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>

// VosPanel class (private)
struct _VosPanel {
  GtkWindow parent;
  
  GtkBox *AppletLayout;
  GtkBox *LauncherBox;
  GtkBox *SystemTray;
  
  GHashTable *ExtensionWidgetTable;
  PeasEngine *Engine;
  
  GtkPositionType Location;
  gint Height;
  gint MonitorID;
  GdkRectangle PanelRect;
  PeasExtensionSet *ExtensionSet;
  
  GtkMenu *ContextMenu;
  
  GtkWindow *CaptureWindow;
  int Captures; // Each time capture is called, this ++es, and when someone ends the capture this --es. When it hits 0, the capture actually ends.

  gboolean Rebooting;
};

// Make sure only one panel exists at a time
static gboolean PanelExists = FALSE;

// Create the VosPanel class
// No properties or anything special, the VosPanel is only a class because
// it creates the VosPanel struct for private data
G_DEFINE_TYPE(VosPanel, vos_panel, GTK_TYPE_WINDOW)
VosPanel* vos_panel_new(void) { if(PanelExists) return NULL; else return VOS_PANEL(g_object_new(VOS_TYPE_PANEL, NULL)); }
static void vos_panel_finalize(GObject *gobject) { PanelExists = FALSE; G_OBJECT_CLASS(vos_panel_parent_class)->finalize(gobject); }
static void vos_panel_class_init(VosPanelClass *klass) { GObjectClass *object_class = G_OBJECT_CLASS (klass); object_class->finalize = vos_panel_finalize; }

// Private event declarations
static void init_layout(VosPanel *self);
static void init_capture(VosPanel *self);
static void init_plugins(VosPanel *self);
static void update_position(VosPanel *self);
static void on_monitors_changed(GdkScreen *screen, VosPanel *self);
static gboolean on_panel_clicked(VosPanel *self, GdkEventButton *event);
static void on_context_menu_item_activate(VosPanel *self, GtkMenuItem *menuitem);

// Initializes the panel (declared by G_DEFINE_TYPE; called through vos_panel_new())
static void vos_panel_init(VosPanel *self)
{
  PanelExists = TRUE;
  self->Rebooting = FALSE;
  
  // Set properties
  gtk_window_set_type_hint(GTK_WINDOW(self), GDK_WINDOW_TYPE_HINT_DOCK);
  gtk_window_set_position(GTK_WINDOW(self), GTK_WIN_POS_NONE);
  gtk_window_set_decorated(GTK_WINDOW(self), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(self), TRUE);

  // Set the application theme
  // GtkCssProvider *fallbackProvider = gtk_css_provider_new();
  // gtk_css_provider_load_from_path(fallbackProvider, VDE_DATA_DIR "/panel-fallback.css", NULL); // TODO: Check for errors
  // gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(fallbackProvider), GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_path(provider, VDE_DATA_DIR "/panel.css", NULL); // TODO: Check for errors
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  // Update the position now and when the size or monitors change
  g_signal_connect(gtk_window_get_screen(GTK_WINDOW(self)), "monitors-changed", G_CALLBACK(on_monitors_changed), self);
  g_signal_connect(self, "map", G_CALLBACK(update_position), NULL);
  g_signal_connect(self, "button_press_event", G_CALLBACK(on_panel_clicked), NULL);
  // g_signal_connect(self, "size-allocate", G_CALLBACK(on_size_allocate), NULL); // TODO: Size allocate necessary? Seems to just create lots of unnecessary position updates

  // Load things
  init_layout(self);
  init_capture(self);
  init_plugins(self);
}

static void init_layout(VosPanel *self)
{
  self->Location = GTK_POS_BOTTOM;
  self->Height = 32;
  
  // Main layout
  self->AppletLayout = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(self->AppletLayout));

  GtkStyleContext *layoutStyle = gtk_widget_get_style_context(GTK_WIDGET(self->AppletLayout));
  gtk_style_context_add_class(layoutStyle, "panel");
  gtk_widget_set_name(GTK_WIDGET(self->AppletLayout), "panel-bar");
  
  // A box for the left-side applets
  self->LauncherBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_box_pack_start(self->AppletLayout, GTK_WIDGET(self->LauncherBox), FALSE, FALSE, 0);
  gtk_box_set_homogeneous(self->LauncherBox, FALSE);
  
  // A box for the right-side applets
  self->SystemTray = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_box_pack_end(self->AppletLayout, GTK_WIDGET(self->SystemTray), FALSE, FALSE, 0);
  gtk_box_set_homogeneous(self->SystemTray, FALSE);
  
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
static void update_position(VosPanel *self)
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

static void on_monitors_changed(GdkScreen *screen, VosPanel *self)
{
  update_position(self);
}

static gboolean on_panel_clicked(VosPanel *self, GdkEventButton *event)
{
  if(event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY)
  {
    gtk_menu_popup(self->ContextMenu, NULL, NULL, NULL, NULL, event->button, event->time);
    return GDK_EVENT_STOP;
  }
  return GDK_EVENT_PROPAGATE;
}

static void on_context_menu_item_activate(VosPanel *self, GtkMenuItem *menuitem)
{
  const gchar *name = gtk_menu_item_get_label(menuitem);
  
  if(g_strcmp0(name, "Reload Applets") == 0)
  {
    // Reboot the panel. There is apparently no way to reload a plugin using Peas without completely exiting the process.
    self->Rebooting = TRUE;
    g_application_quit(g_application_get_default());
  }
}

gboolean vos_panel_is_rebooting(VosPanel *self)
{
  return self->Rebooting;
}


/**
 * vos_panel_get_monitor:
 * @self: The #VosPanel affected.
 *
 * Returns: (transfer none): The monitor ID that the panel is docked on (for the panel's current screen; see gtk_widget_get_screen).
 */
gint vos_panel_get_monitor(VosPanel *self)
{
  return self->MonitorID;
}

/**
 * vos_panel_get_height:
 * @self: The #VosPanel affected.
 *
 * Returns: (transfer none): The height of the panel relative to the docking side of the screen.
 */
gint vos_panel_get_height(VosPanel *self)
{
  return self->Height;
}



/*
 *
 * CAPTURE
 *
 */
 
static void capture_on_map(GtkWindow *capture, VosPanel *self);
 
static void init_capture(VosPanel *self)
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

static void capture_on_map(GtkWindow *capture, VosPanel *self)
{
  update_position(self);
}

/**
 * vos_panel_capture_screen:
 * @self: The #VosPanel affected.
 *
 * Creates a window which fills the specified amount of the screen.
 * Applets can draw to this window however they please.
 *
 * Returns: (transfer none): The capture count. If this is one, the capture has just been created.
 */
int vos_panel_capture_screen(VosPanel *self)
{
  g_return_val_if_fail(VOS_IS_PANEL(self), 0);

  self->Captures++;
  if(self->Captures > 0)
    gtk_widget_show(GTK_WIDGET(self->CaptureWindow));
  return self->Captures;
}

/**
 * vos_panel_end_capture:
 * @self: The #VosPanel affected.
 *
 * Decreases the capture count by one. If it reaches zero, the capture is removed.
 *
 * Returns: (transfer none): The capture count. If this is zero, the capture has ended.
 */
int vos_panel_end_capture(VosPanel *self)
{
  g_return_val_if_fail(VOS_IS_PANEL(self), 0);
  
  self->Captures--;
  if(self->Captures <= 0)
  {
    self->Captures = 0;
    gtk_widget_hide(GTK_WIDGET(self->CaptureWindow));
  }
  return self->Captures;
}

/**
 * vos_panel_clear_capture:
 * @self: The #VosPanel affected.
 *
 * Sets the capture count to 0 (removing the capture)
 */
void vos_panel_clear_capture(VosPanel *self)
{
  g_return_if_fail(VOS_IS_PANEL(self));
  self->Captures = 0;
  update_position(self);
}

/**
 * vos_panel_logout:
 * @self: The #VosPanel affected.
 *
 * Closes all applications and logs out of the Velt Desktop session.
 * Internally, this literally just quits the panel, which tells the WM/session manager to exit.
 *
 * This function returns; logout occurs once the panel becomes idle.
 */
void vos_panel_logout(VosPanel *self)
{
  // TODO: Close applications!
  // TODO: Upon relogging in, the previous state of the screen shows for a second, including the open settings panel.
  //       Maybe force-close all open panel windows before quitting?
  g_application_quit(g_application_get_default());
}

/**
 * vos_panel_shutdown:
 * @self: The #VosPanel affected.
 * @reboot: Whether or not to reboot.
 * 
 * Closes all applications and shuts down or reboots the computer.
 */
void vos_panel_shutdown(VosPanel *self, gboolean reboot)
{
  g_message("SHUTDOWN/REBOOT FROM VDE NOT IMPLEMENTED YET SORRY");
}


/*
 *
 * PLUGINS
 *
 */

static void load_girepository(char *name, char *version);
static void on_extension_added(PeasExtensionSet *set, PeasPluginInfo *info, VosAppletExtension *exten, VosPanel *self);
static void on_extension_removed(PeasExtensionSet *set, PeasPluginInfo *info, VosAppletExtension *exten, VosPanel *self);

static void init_plugins(VosPanel *self)
{
  // Init peas
  PeasEngine *engine = peas_engine_get_default();
  peas_engine_add_search_path(engine, VDE_DATA_DIR "/applets", VDE_DATA_DIR "/applets");
  peas_engine_enable_loader(engine, "python3");
  peas_engine_enable_loader(engine, "lua5.1");
  load_girepository("Vos", "1.0");

  // Create a hash table between each VosAppletExtension* and its corresponding GtkWidget*
  self->ExtensionWidgetTable = g_hash_table_new(g_direct_hash, g_direct_equal);

  // Create extension set
  self->ExtensionSet = peas_extension_set_new(engine, VOS_TYPE_APPLET_EXTENSION, NULL);
  peas_extension_set_foreach(self->ExtensionSet, (PeasExtensionSetForeachFunc)on_extension_added, self);
  g_signal_connect(self->ExtensionSet, "extension-added", G_CALLBACK(on_extension_added), self);
  g_signal_connect(self->ExtensionSet, "extension-removed", G_CALLBACK(on_extension_removed), self);
  
  peas_engine_rescan_plugins(engine);
  const GList *plugins = peas_engine_get_plugin_list(engine);
  for (; plugins != NULL; plugins = plugins->next)
  {
    PeasPluginInfo *info = (PeasPluginInfo *)plugins->data;
    if(peas_plugin_info_is_builtin(info))
      peas_engine_load_plugin(engine, info);
  }
}

static void load_girepository(char *name, char *version)
{
  GError *error = NULL;
  g_irepository_require(g_irepository_get_default(), name, version, (GIRepositoryLoadFlags)0, &error);
  if(error)
  {
    g_critical("Failed to load girepository '%s' version %s: %s\n", name, version, error->message);
    g_error_free(error);
  }
}

static void insert_extension(VosPanel *self, const char *name, GtkWidget *applet)
{
  if(g_strcmp0(name, "launcher") == 0) // Special built-in extension
  {
    gtk_box_pack_start(GTK_BOX(self->LauncherBox), applet, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(self->LauncherBox), applet, 0);
  }
  else if(g_strcmp0(name, "tasklist") == 0) // Special built-in extension
  {
    gtk_box_pack_start(GTK_BOX(self->LauncherBox), applet, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(self->LauncherBox), applet, 1);
  }
  else if(g_strcmp0(name, "clock") == 0)
  {
    gtk_box_pack_end(GTK_BOX(self->SystemTray), applet, FALSE, FALSE, 0);
 		gtk_box_reorder_child(GTK_BOX(self->SystemTray), applet, 0);
  }
  else
  {
    gtk_box_pack_end(GTK_BOX(self->SystemTray), applet, FALSE, FALSE, 0);
  }
}

static void on_extension_added(PeasExtensionSet *set, PeasPluginInfo *info, VosAppletExtension *exten, VosPanel *self)
{
  const char *pluginmodule = peas_plugin_info_get_module_name(info);

  GtkWidget *applet = vos_applet_extension_get_widget(VOS_APPLET_EXTENSION(exten), self);
  if(!applet)
  {
    g_warning("Failed to initialize plugin '%s'", pluginmodule);
    return;
  }
  
  g_hash_table_insert(self->ExtensionWidgetTable, exten, applet);
	insert_extension(self, pluginmodule, applet);
}

static void on_extension_removed(PeasExtensionSet *set, PeasPluginInfo *info, VosAppletExtension *exten, VosPanel *self)
{
  GtkWidget *applet = GTK_WIDGET(g_hash_table_lookup(self->ExtensionWidgetTable, exten));
  g_hash_table_remove(self->ExtensionWidgetTable, exten);
  if(applet == NULL)
    return;
  
  gtk_widget_destroy(applet);
}
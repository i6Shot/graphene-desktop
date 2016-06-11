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

#include "settings-applet.h"
#include "materialbox.h"
#include "battery.h"
#include "volume.h"
#include <glib.h>
#include <gdk/gdkx.h>

#define GRAPHENE_TYPE_SETTINGS_POPUP  graphene_settings_popup_get_type()
G_DECLARE_FINAL_TYPE(GrapheneSettingsPopup, graphene_settings_popup, GRAPHENE, SETTINGS_POPUP, GtkWindow)
GrapheneSettingsPopup * graphene_settings_popup_new();


struct _GrapheneSettingsApplet
{
  GtkButton parent;
  
  GtkStyleContext *style;
  GrapheneSettingsPopup *popup;
};


static void graphene_settings_applet_finalize(GrapheneSettingsApplet *self);
static gboolean applet_on_click(GrapheneSettingsApplet *self, GdkEvent *event);
static void applet_on_popup_hide(GrapheneSettingsApplet *self, GrapheneSettingsPopup *popup);


G_DEFINE_TYPE(GrapheneSettingsApplet, graphene_settings_applet, GTK_TYPE_BUTTON)


GrapheneSettingsApplet* graphene_settings_applet_new(void)
{
  return GRAPHENE_SETTINGS_APPLET(g_object_new(GRAPHENE_TYPE_SETTINGS_APPLET, NULL));
}

static void graphene_settings_applet_class_init(GrapheneSettingsAppletClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = G_CALLBACK(graphene_settings_applet_finalize);
}

static void graphene_settings_applet_init(GrapheneSettingsApplet *self)
{
  self->style = gtk_widget_get_style_context(GTK_WIDGET(self));
  gtk_style_context_add_class(self->style, "graphene-settings-applet");

  self->popup = graphene_settings_popup_new();
  g_signal_connect_swapped(self->popup, "hide", G_CALLBACK(applet_on_popup_hide), self);

  // Init applet buttons
  GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_box_set_homogeneous(box, TRUE);
  gtk_box_pack_end(box, GTK_WIDGET(graphene_battery_icon_new()), FALSE, FALSE, 0);
  // gtk_box_pack_end(box, GTK_WIDGET(graphene_network_icon_new()), FALSE, FALSE, 0);
  gtk_box_pack_end(box, GTK_WIDGET(graphene_volume_icon_new()), FALSE, FALSE, 0);
  gtk_box_pack_end(box, GTK_WIDGET(gtk_image_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_MENU)), FALSE, FALSE, 0);
  
  gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(box));
  gtk_widget_show_all(GTK_WIDGET(self));
  
  g_signal_connect(self, "button_press_event", G_CALLBACK(applet_on_click), NULL);
}

static void graphene_settings_applet_finalize(GrapheneSettingsApplet *self)
{
  g_clear_object(&self->popup);
}

static gboolean applet_on_click(GrapheneSettingsApplet *self, GdkEvent *event)
{
  gtk_style_context_add_class(self->style, "clicked");
  gtk_widget_show(GTK_WIDGET(self->popup));
  return GDK_EVENT_STOP; // Required to keep the button from staying highlighted permanently 
}

static void applet_on_popup_hide(GrapheneSettingsApplet *self, GrapheneSettingsPopup *popup)
{
  gtk_style_context_remove_class(self->style, "clicked");
}



/*
 ******** Popup ********
 */



struct _GrapheneSettingsPopup
{
  GtkWindow parent;
  
  GtkBox *sessionBox;
  GrapheneMaterialBox *settingsView;
  GtkBox *settingWidgetBox;
};

typedef struct {
  GrapheneSettingsPopup *popup;
  GtkButton *button;
  const gchar *panel;
  
} SettingsWidgetData;

static void graphene_settings_popup_finalize(GrapheneSettingsPopup *self);
static void popup_on_show(GrapheneSettingsPopup *self);
static void popup_on_hide(GrapheneSettingsPopup *self);
static void popup_on_mapped(GrapheneSettingsPopup *self);
static void popup_on_monitors_changed(GrapheneSettingsPopup *self, GdkScreen *screen);
static gboolean popup_on_mouse_event(GrapheneSettingsPopup *self, GdkEventButton *event);
static void popup_update_size(GrapheneSettingsPopup *self);
static void popup_on_logout_button_clicked(GrapheneSettingsPopup *self, GtkButton *button);
static void popup_on_vertical_scrolled(GrapheneSettingsPopup *self, GtkAdjustment *vadj);
static void enum_settings_widgets(GrapheneSettingsPopup *self);
static void add_settings_category_label(GrapheneSettingsPopup *self, const gchar *title);
static void add_setting_widget(GrapheneSettingsPopup *self, const gchar *title, const gchar *iconName, gboolean toggleable, const gchar *panel, gboolean bottomSeparator);
static void popup_on_settings_widget_clicked(GtkButton *button, SettingsWidgetData* data);


G_DEFINE_TYPE(GrapheneSettingsPopup, graphene_settings_popup, GTK_TYPE_WINDOW)


GrapheneSettingsPopup* graphene_settings_popup_new(void)
{
  return GRAPHENE_SETTINGS_POPUP(g_object_new(GRAPHENE_TYPE_SETTINGS_POPUP, NULL));
}

static void graphene_settings_popup_class_init(GrapheneSettingsPopupClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = G_CALLBACK(graphene_settings_popup_finalize);
}

static void graphene_settings_popup_init(GrapheneSettingsPopup *self)
{
  gtk_window_set_type_hint(GTK_WINDOW(self), GDK_WINDOW_TYPE_HINT_POPUP_MENU); // Must be POPUP_MENU or else z-sorting conflicts with the dock
  g_signal_connect(self, "show", G_CALLBACK(popup_on_show), NULL);
  g_signal_connect(self, "hide", G_CALLBACK(popup_on_hide), NULL);
  g_signal_connect(self, "map", G_CALLBACK(popup_on_mapped), NULL);
  g_signal_connect(self, "button_press_event", G_CALLBACK(popup_on_mouse_event), NULL);
  g_signal_connect(gtk_widget_get_screen(GTK_WIDGET(self)), "monitors-changed", G_CALLBACK(popup_on_monitors_changed), NULL);
  gtk_window_set_role(GTK_WINDOW(self), "GraphenePopup"); // Tells graphene-wm this is a popup
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "graphene-settings-popup");
  
  // Layout
  GtkBox *layout = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(layout)), "panel");
  gtk_widget_set_halign(GTK_WIDGET(layout), GTK_ALIGN_FILL);
  gtk_widget_set_valign(GTK_WIDGET(layout), GTK_ALIGN_FILL);
  
  // Current session info (profile name, profile icon)
  GtkLabel *profileNameLabel = GTK_LABEL(gtk_label_new("[name]"));
  gtk_widget_set_name(GTK_WIDGET(profileNameLabel), "profile-name-label");
  gtk_widget_set_halign(GTK_WIDGET(profileNameLabel), GTK_ALIGN_CENTER);
  gtk_widget_set_valign(GTK_WIDGET(profileNameLabel), GTK_ALIGN_CENTER);
  
  // Create box for session control button
  GtkBox *sessionControlBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5));
  gtk_widget_set_halign(GTK_WIDGET(sessionControlBox), GTK_ALIGN_CENTER);
  gtk_widget_set_name(GTK_WIDGET(sessionControlBox), "session-control-box");

  GtkButton *logoutButton = gtk_button_new_from_icon_name("system-shutdown-symbolic", GTK_ICON_SIZE_DND);
  g_signal_connect_swapped(logoutButton, "clicked", G_CALLBACK(popup_on_logout_button_clicked), self);
  gtk_box_pack_start(sessionControlBox, GTK_WIDGET(logoutButton), FALSE, FALSE, 0);

  // Create top box for session info and control
  self->sessionBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 5));
  gtk_box_pack_start(self->sessionBox, GTK_WIDGET(profileNameLabel), FALSE, FALSE, 0);
  gtk_box_pack_start(self->sessionBox, GTK_WIDGET(sessionControlBox), FALSE, FALSE, 0);
  gtk_widget_set_name(GTK_WIDGET(self->sessionBox), "session-box");
  gtk_box_pack_start(layout, GTK_WIDGET(self->sessionBox), FALSE, FALSE, 0);

  // Box for all the system settings (below the session info)
  self->settingsView = graphene_material_box_new();
  gtk_box_pack_start(layout, GTK_WIDGET(self->settingsView), TRUE, TRUE, 0);
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self->settingsView)), "graphene-settings-view");

  GtkScrolledWindow *settingWidgetScrolled = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
  gtk_scrolled_window_set_policy(settingWidgetScrolled, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  g_signal_connect_swapped(gtk_scrolled_window_get_vadjustment(settingWidgetScrolled), "value-changed", G_CALLBACK(popup_on_vertical_scrolled), self);
  graphene_material_box_add_sheet(self->settingsView, GRAPHENE_MATERIAL_SHEET(settingWidgetScrolled), GRAPHENE_MATERIAL_BOX_LOCATION_CENTER);

  self->settingWidgetBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  gtk_container_add(GTK_CONTAINER(settingWidgetScrolled), self->settingWidgetBox);

  enum_settings_widgets(self);

  // Add layout to window
  gtk_widget_show_all(GTK_WIDGET(self->sessionBox));
  gtk_widget_show_all(GTK_WIDGET(settingWidgetScrolled));
  gtk_widget_show(GTK_WIDGET(self->settingsView));
  gtk_widget_show(GTK_WIDGET(layout));
  gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(layout));
}

static void graphene_settings_popup_finalize(GrapheneSettingsPopup *self)
{
}

static void popup_on_show(GrapheneSettingsPopup *self)
{
  graphene_panel_capture_screen(graphene_panel_get_default());
  gtk_grab_add(GTK_WIDGET(self));
}

static void popup_on_hide(GrapheneSettingsPopup *self)
{
  gtk_grab_remove(GTK_WIDGET(self));
  graphene_panel_end_capture(graphene_panel_get_default());
}

static void popup_on_mapped(GrapheneSettingsPopup *self)
{
  GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(self));
  gdk_window_focus(window, gdk_x11_get_server_time(window));
  popup_update_size(self);
}

static void popup_on_monitors_changed(GrapheneSettingsPopup *self, GdkScreen *screen)
{
  popup_update_size(self);
}

static gboolean popup_on_mouse_event(GrapheneSettingsPopup *self, GdkEventButton *event)
{
  if(gdk_window_get_toplevel(event->window) != gtk_widget_get_window(GTK_WIDGET(self)))
    gtk_widget_hide(GTK_WIDGET(self));
  return GDK_EVENT_PROPAGATE;
}

static void popup_update_size(GrapheneSettingsPopup *self)
{
  GdkRectangle rect;
  gdk_screen_get_monitor_geometry(gtk_widget_get_screen(GTK_WIDGET(self)), graphene_panel_get_monitor(graphene_panel_get_default()), &rect);
  GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(self));
  if(window)
    gdk_window_move_resize(window, rect.x + rect.width - (rect.width/6), rect.y, rect.width/6, rect.height-graphene_panel_get_height(graphene_panel_get_default()));
}

static void popup_on_logout_button_clicked(GrapheneSettingsPopup *self, GtkButton *button)
{
  gtk_widget_hide(GTK_WIDGET(self));
  graphene_panel_logout(graphene_panel_get_default());
}

static void popup_on_vertical_scrolled(GrapheneSettingsPopup *self, GtkAdjustment *vadj)
{
  if(gtk_adjustment_get_value(vadj) > 5)
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self->sessionBox)), "shadow");
  else
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(self->sessionBox)), "shadow");
}

static void enum_settings_widgets(GrapheneSettingsPopup *self)
{
  add_settings_category_label(self, "Personal");
  add_setting_widget(self, "Background",       "preferences-desktop-wallpaper",    TRUE,  "background",     FALSE);
  add_setting_widget(self, "Notifications",    "preferences-system-notifications", TRUE,  "notifications",  FALSE);
  add_setting_widget(self, "Privacy",          "preferences-system-privacy",       FALSE, "privacy",        FALSE);
  add_setting_widget(self, "Region & Language","preferences-desktop-locale",       FALSE, "region",         FALSE);
  add_setting_widget(self, "Search",           "preferences-system-search",        FALSE, "search",         TRUE);
  add_settings_category_label(self, "Hardware");
  add_setting_widget(self, "Bluetooth",        "bluetooth",                        TRUE,  "bluetooth",      FALSE);
  add_setting_widget(self, "Color",            "preferences-color",                FALSE, "color",          FALSE);
  add_setting_widget(self, "Displays",         "preferences-desktop-display",      FALSE, "display",        FALSE);
  add_setting_widget(self, "Keyboard",         "input-keyboard",                   FALSE, "keyboard",       FALSE);
  add_setting_widget(self, "Mouse & Touchpad", "input-mouse",                      FALSE, "mouse",          FALSE);
  add_setting_widget(self, "Network",          "network-workgroup",                TRUE,  "network",        FALSE);
  add_setting_widget(self, "Power",            "gnome-power-manager",              FALSE, "power",          FALSE);
  add_setting_widget(self, "Printers",         "printer",                          FALSE, "printers",       FALSE);
  add_setting_widget(self, "Sound",            "sound",                            TRUE,  "sound",          FALSE);
  add_setting_widget(self, "Wacom Tablet",     "input-tablet",                     FALSE, "wacom",          TRUE);
  add_settings_category_label(self, "System");
  add_setting_widget(self, "Date & Time",      "preferences-system-time",          FALSE, "datetime",       FALSE);
  add_setting_widget(self, "Details",          "applications-system",              FALSE, "info",           FALSE);
  add_setting_widget(self, "Sharing",          "preferences-system-sharing",       FALSE, "sharing",        FALSE);
  add_setting_widget(self, "Universal",        "preferences-desktop-accessibility",FALSE, "universal-access",FALSE);
  add_setting_widget(self, "Users",            "system-users",                     FALSE, "user-accounts",  TRUE);
}

static void add_settings_category_label(GrapheneSettingsPopup *self, const gchar *title)
{
  GtkLabel *label = GTK_LABEL(gtk_label_new(title));
  gtk_widget_set_halign(GTK_WIDGET(label), GTK_ALIGN_START);
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(label)), "group-label");
  gtk_box_pack_start(self->settingWidgetBox, GTK_WIDGET(label), FALSE, FALSE, 0);
}

static void free_settings_widget_data_closure_notify(gpointer data, GClosure *closure)
{
  // ApplistButtonData *applistData = (ApplistButtonData *)data;
  g_free(data);
}

static void add_setting_widget(GrapheneSettingsPopup *self, const gchar *title, const gchar *iconName, gboolean toggleable, const gchar *panel, gboolean bottomSeparator)
{
  GrapheneMaterialBox *box = graphene_material_box_new();
  
  GtkButton *button = GTK_BUTTON(gtk_button_new());
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(button)), "settings-widget-button");

  SettingsWidgetData *data = g_new0(SettingsWidgetData, 1);
  data->popup = self;
  data->button = button;
  data->panel = panel;
  g_signal_connect_data(button, "clicked", G_CALLBACK(popup_on_settings_widget_clicked), data, free_settings_widget_data_closure_notify, 0);
  
  GtkBox *buttonBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7));
  gtk_box_pack_start(buttonBox, GTK_WIDGET(gtk_image_new_from_icon_name(iconName, GTK_ICON_SIZE_DND)), TRUE, TRUE, 7);
  GtkLabel *label = GTK_LABEL(gtk_label_new(title));
  gtk_label_set_yalign(label, 0.5);
  gtk_box_pack_start(buttonBox, GTK_WIDGET(label), TRUE, TRUE, 0);
  gtk_widget_set_halign(GTK_WIDGET(buttonBox), GTK_ALIGN_START);
  gtk_container_add(GTK_CONTAINER(button), GTK_WIDGET(buttonBox));
  graphene_material_box_add_sheet(box, GRAPHENE_MATERIAL_SHEET(button), GRAPHENE_MATERIAL_BOX_LOCATION_CENTER);

  if(toggleable)
  {
    GtkSwitch *toggle = gtk_switch_new();
    gtk_widget_set_valign(GTK_WIDGET(toggle), GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(toggle)), "settings-widget-switch");
    graphene_material_box_add_sheet(box, GRAPHENE_MATERIAL_SHEET(toggle), GRAPHENE_MATERIAL_BOX_LOCATION_RIGHT);
    gtk_widget_show_all(GTK_WIDGET(toggle));
  }
  
  GtkSeparator *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(sep)), "list-item-separator");
  gtk_box_pack_start(self->settingWidgetBox, GTK_WIDGET(sep), FALSE, FALSE, 0);
  
  gtk_box_pack_start(self->settingWidgetBox, GTK_WIDGET(box), FALSE, FALSE, 0);
  
  if(bottomSeparator)
  {
    GtkSeparator *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(sep)), "list-item-separator");
    gtk_box_pack_start(self->settingWidgetBox, GTK_WIDGET(sep), FALSE, FALSE, 0);
  }
}

static void popup_on_settings_widget_clicked(GtkButton *button, SettingsWidgetData* data)
{
  gtk_widget_hide(GTK_WIDGET(data->popup));
  
  const gchar *argsSplit[3] = {"gnome-control-center", data->panel, NULL};
  g_spawn_async(NULL, argsSplit, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}
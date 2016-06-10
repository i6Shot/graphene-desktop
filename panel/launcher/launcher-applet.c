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

#define GMENU_I_KNOW_THIS_IS_UNSTABLE // TODO: Apparently libgnome-menu is unstable (public API frequently changed). Maybe find an alternative? 

#include "launcher-applet.h"
#include <gdk/gdkx.h>
#include <gmenu-tree.h>
#include <gio/gdesktopappinfo.h>
#include <glib.h>


struct _GrapheneLauncherApplet
{
  GtkButton parent;
  
  GraphenePanel *panel;
  GtkStyleContext *style;
  GtkImage *image;
  GrapheneLauncherPopup *popup;
};


static void graphene_launcher_applet_finalize(GrapheneLauncherApplet *self);
static gboolean applet_on_click(GrapheneLauncherApplet *self, GdkEvent *event);
static void applet_on_popup_show(GrapheneLauncherApplet *self, GrapheneLauncherPopup *popup);
static void applet_on_popup_hide(GrapheneLauncherApplet *self, GrapheneLauncherPopup *popup);


G_DEFINE_TYPE(GrapheneLauncherApplet, graphene_launcher_applet, GTK_TYPE_BUTTON)


GrapheneLauncherApplet* graphene_launcher_applet_new(void)
{
  return GRAPHENE_LAUNCHER_APPLET(g_object_new(GRAPHENE_TYPE_LAUNCHER_APPLET, NULL));
}

static void graphene_launcher_applet_class_init(GrapheneLauncherAppletClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = G_CALLBACK(graphene_launcher_applet_finalize);
}

static void graphene_launcher_applet_init(GrapheneLauncherApplet *self)
{
  self->style = gtk_widget_get_style_context(GTK_WIDGET(self));
  gtk_style_context_add_class(self->style, "graphene-launcher-applet");
  
  // Init button
  gtk_button_set_label(GTK_BUTTON(self), "");
  g_signal_connect(self, "button_press_event", G_CALLBACK(applet_on_click), NULL);
  
  self->image = GTK_IMAGE(gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_INVALID));
  gtk_image_set_pixel_size(self->image, 32);
  gtk_button_set_image(GTK_BUTTON(self), GTK_WIDGET(self->image));
  gtk_button_set_always_show_image(GTK_BUTTON(self), TRUE);
  gtk_widget_show_all(GTK_WIDGET(self));
  
  // Create popup
  self->popup = graphene_launcher_popup_new();
  graphene_launcher_popup_set_panel(self->popup, self->panel);
}

static void graphene_launcher_applet_finalize(GrapheneLauncherApplet *self)
{
  g_clear_object(&self->popup);
  gtk_button_set_image(GTK_BUTTON(self), NULL);
  g_clear_object(&self->image);
}

void graphene_launcher_applet_set_panel(GrapheneLauncherApplet *self, GraphenePanel *panel)
{
  self->panel = panel;
  graphene_launcher_popup_set_panel(self->popup, self->panel);
}

static gboolean applet_on_click(GrapheneLauncherApplet *self, GdkEvent *event)
{
  g_return_val_if_fail(GRAPHENE_IS_PANEL(self->panel), GDK_EVENT_STOP);
  gtk_style_context_add_class(self->style, "clicked");
  gtk_widget_show(GTK_WIDGET(self->popup));
  return GDK_EVENT_STOP; // Required to keep the button from staying highlighted permanently 
}


/*
 ******** Popup ********
 */



struct _GrapheneLauncherPopup
{
  GtkWindow parent;
  
  GraphenePanel *panel;
  GtkBox *popupLayout;
  GtkBox *searchBarContainer;
  GtkSearchEntry *searchBar;
  gchar *filter;
  
  GtkScrolledWindow *appListView;
  GtkBox *appListBox;
  
  GMenuTree *appTree;
};


G_DEFINE_TYPE(GrapheneLauncherPopup, graphene_launcher_popup, GTK_TYPE_WINDOW)


typedef struct {
  GrapheneLauncherPopup *popup;
  GDesktopAppInfo *appInfo;
  GtkButton *button;
  
} ApplistButtonData;

static void graphene_launcher_popup_finalize(GrapheneLauncherPopup *self);
static void popup_on_show(GrapheneLauncherPopup *self);
static void popup_on_hide(GrapheneLauncherPopup *self);
static void popup_on_mapped(GrapheneLauncherPopup *self);
static void popup_on_monitors_changed(GrapheneLauncherPopup *self, GdkScreen *screen);
static gboolean popup_on_mouse_event(GrapheneLauncherPopup *self, GdkEventButton *event);
static void popup_update_size(GrapheneLauncherPopup *self);
static void popup_on_search_changed(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar);
static void popup_on_search_enter(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar);
static gboolean popup_on_key_event(GrapheneLauncherPopup *self, GdkEvent *event);
static void popup_on_vertical_scrolled(GrapheneLauncherPopup *self, GtkAdjustment *vadj);
static void popup_applist_refresh(GrapheneLauncherPopup *self);
static void popup_applist_populate(GrapheneLauncherPopup *self);
static guint popup_applist_populate_directory(GrapheneLauncherPopup *self, GMenuTreeDirectory *directory);
static void applist_on_item_clicked(GtkButton *button, ApplistButtonData *data);
static void applist_launch_first(GrapheneLauncherPopup *self);

GrapheneLauncherPopup* graphene_launcher_popup_new(void)
{
  return GRAPHENE_LAUNCHER_POPUP(g_object_new(GRAPHENE_TYPE_LAUNCHER_POPUP, NULL));
}

static void graphene_launcher_popup_class_init(GrapheneLauncherPopupClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = graphene_launcher_popup_finalize;
}

static void graphene_launcher_popup_init(GrapheneLauncherPopup *self)
{
  gtk_window_set_type_hint(GTK_WINDOW(self), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
  g_signal_connect(self, "show", G_CALLBACK(popup_on_show), NULL);
  g_signal_connect(self, "hide", G_CALLBACK(popup_on_hide), NULL);
  g_signal_connect(self, "map", G_CALLBACK(popup_on_mapped), NULL);
  g_signal_connect(self, "button_press_event", G_CALLBACK(popup_on_mouse_event), NULL);
  g_signal_connect(self, "key_press_event", G_CALLBACK(popup_on_key_event), NULL);
  g_signal_connect(self, "key_release_event", G_CALLBACK(popup_on_key_event), NULL);
  g_signal_connect(gtk_widget_get_screen(GTK_WIDGET(self)), "monitors-changed", G_CALLBACK(popup_on_monitors_changed), NULL);
  gtk_window_set_role(GTK_WINDOW(self), "GraphenePopup");
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "graphene-launcher-popup");
  
  // Layout
  self->popupLayout = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self->popupLayout)), "panel");
  gtk_widget_set_halign(GTK_WIDGET(self->popupLayout), GTK_ALIGN_FILL);
  gtk_widget_set_valign(GTK_WIDGET(self->popupLayout), GTK_ALIGN_FILL);
  gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(self->popupLayout));
  
  // Search bar
  self->searchBar = GTK_SEARCH_ENTRY(gtk_search_entry_new());
  self->filter = NULL;
  g_signal_connect_swapped(self->searchBar, "changed", G_CALLBACK(popup_on_search_changed), self);
  g_signal_connect_swapped(self->searchBar, "activate", G_CALLBACK(popup_on_search_enter), self);
  gtk_widget_set_name(GTK_WIDGET(self->searchBar), "graphene-launcher-searchbar");
  self->searchBarContainer = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)); // It seems the shadow-box property can't be animated on a searchentry, so wrap it in a container
  gtk_box_pack_start(self->searchBarContainer, GTK_WIDGET(self->searchBar), FALSE, FALSE, 0);
  gtk_widget_set_name(GTK_WIDGET(self->searchBarContainer), "graphene-launcher-searchbar-container");
  gtk_box_pack_start(self->popupLayout, GTK_WIDGET(self->searchBarContainer), FALSE, FALSE, 0);
  
  // App list
  self->appListView = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(NULL, NULL));
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self->appListView)), "graphene-applist-view");
  gtk_scrolled_window_set_policy(self->appListView, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  g_signal_connect_swapped(gtk_scrolled_window_get_vadjustment(self->appListView), "value-changed", G_CALLBACK(popup_on_vertical_scrolled), self);
  self->appListBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  gtk_container_add(GTK_CONTAINER(self->appListView), GTK_WIDGET(self->appListBox));
  gtk_box_pack_start(self->popupLayout, GTK_WIDGET(self->appListView), TRUE, TRUE, 0);
  
  // Load applications
  self->appTree = gmenu_tree_new("gnome-applications.menu", GMENU_TREE_FLAGS_SORT_DISPLAY_NAME);
  popup_applist_refresh(self);
  
  gtk_widget_show_all(GTK_WIDGET(self->popupLayout));
}

static void graphene_launcher_popup_finalize(GrapheneLauncherPopup *self)
{
  g_clear_object(&self->appTree);
  g_clear_pointer(&self->filter, g_free);
  g_clear_object(&self->popupLayout);
}

void graphene_launcher_popup_set_panel(GrapheneLauncherPopup *self, GraphenePanel *panel)
{
  self->panel = panel;
}

static void popup_on_show(GrapheneLauncherPopup *self)
{
  popup_applist_refresh(self);
  graphene_panel_capture_screen(self->panel);
  gtk_grab_add(GTK_WIDGET(self));
}

static void popup_on_hide(GrapheneLauncherPopup *self)
{
  gtk_grab_remove(GTK_WIDGET(self));
  graphene_panel_end_capture(self->panel);
}

static void popup_on_mapped(GrapheneLauncherPopup *self)
{
  GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(self));
  gdk_window_focus(window, gdk_x11_get_server_time(window));
  popup_update_size(self);
}

static void popup_on_monitors_changed(GrapheneLauncherPopup *self, GdkScreen *screen)
{
  popup_update_size(self);
}

static gboolean popup_on_mouse_event(GrapheneLauncherPopup *self, GdkEventButton *event)
{
  if(gdk_window_get_toplevel(event->window) != gtk_widget_get_window(GTK_WIDGET(self)))
    gtk_widget_hide(GTK_WIDGET(self));
  return GDK_EVENT_PROPAGATE;
}

static void popup_update_size(GrapheneLauncherPopup *self)
{
  GdkRectangle rect;
  gdk_screen_get_monitor_geometry(gtk_widget_get_screen(GTK_WIDGET(self)), graphene_panel_get_monitor(self->panel), &rect);
  GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(self));
  if(window)
    gdk_window_move_resize(window, rect.x, rect.y, rect.width/6, rect.height-graphene_panel_get_height(self->panel));
}

static void popup_on_search_changed(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar)
{
  g_clear_pointer(&self->filter, g_free);
  self->filter = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(searchBar)), -1);
  popup_applist_populate(self);
}

static void popup_on_search_enter(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar)
{
  if(g_utf8_strlen(self->filter, -1) > 0 && gtk_widget_is_focus(GTK_WIDGET(searchBar)))
    applist_launch_first(self);
}

static gboolean popup_on_key_event(GrapheneLauncherPopup *self, GdkEvent *event)
{
  if(!gtk_widget_is_focus(GTK_WIDGET(self->searchBar)))
    return gtk_search_entry_handle_event(self->searchBar, event);
  return GDK_EVENT_PROPAGATE;
}

static void popup_on_vertical_scrolled(GrapheneLauncherPopup *self, GtkAdjustment *vadj)
{
  if(gtk_adjustment_get_value(vadj) > 5)
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self->searchBarContainer)), "shadow");
  else
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(self->searchBarContainer)), "shadow");
}

static void popup_applist_refresh(GrapheneLauncherPopup *self)
{
  gmenu_tree_load_sync(self->appTree, NULL);
  popup_applist_populate(self);
}

static void popup_applist_populate(GrapheneLauncherPopup *self)
{
  GList *widgets = gtk_container_get_children(GTK_CONTAINER(self->appListBox));
  g_list_free_full(widgets, (GDestroyNotify)gtk_widget_destroy); // Conveniently able to destroy all the child widgets using this single function
  GMenuTreeDirectory *directory = gmenu_tree_get_root_directory(self->appTree);
  popup_applist_populate_directory(self, directory);
  gmenu_tree_item_unref(directory);
}

static void free_applist_button_data_closure_notify(gpointer data, GClosure *closure)
{
  ApplistButtonData *applistData = (ApplistButtonData *)data;
  g_object_unref(applistData->appInfo);
  g_free(data);
}

static guint popup_applist_populate_directory(GrapheneLauncherPopup *self, GMenuTreeDirectory *directory)
{
  guint count = 0;
  GMenuTreeIter *it = gmenu_tree_directory_iter(directory);
  
  while(TRUE)
  {
    GMenuTreeItemType type = gmenu_tree_iter_next(it);
    if(type == GMENU_TREE_ITEM_INVALID)
      break;
      
    if(type == GMENU_TREE_ITEM_ENTRY)
    {
      GMenuTreeEntry *entry = gmenu_tree_iter_get_entry(it);
      GDesktopAppInfo *appInfo = gmenu_tree_entry_get_app_info(entry);
      g_object_ref(appInfo);
      gmenu_tree_item_unref(entry);
      
      if(g_desktop_app_info_get_nodisplay(appInfo))
        continue;
        
      gchar *displayNameDown = g_utf8_strdown(g_app_info_get_display_name(G_APP_INFO(appInfo)), -1);
      gboolean passedFilter = self->filter && g_strstr_len(displayNameDown, -1, self->filter) != NULL;
      g_free(displayNameDown);
      if(self->filter && !passedFilter)
        continue;
      
      GtkBox *buttonBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7));
      gtk_box_pack_start(buttonBox, GTK_WIDGET(gtk_image_new_from_gicon(g_app_info_get_icon(G_APP_INFO(appInfo)), GTK_ICON_SIZE_DND)), TRUE, TRUE, 7);
      GtkLabel *label = GTK_LABEL(gtk_label_new(g_app_info_get_display_name(G_APP_INFO(appInfo))));
      gtk_label_set_yalign(label, 0.5);
      gtk_box_pack_start(buttonBox, GTK_WIDGET(label), TRUE, TRUE, 0);
      gtk_widget_set_halign(GTK_WIDGET(buttonBox), GTK_ALIGN_START);

      GtkButton *button = GTK_BUTTON(gtk_button_new());
      gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(button)), "launcher-app-button");
      gtk_container_add(GTK_CONTAINER(button), GTK_WIDGET(buttonBox));
      gtk_widget_show_all(GTK_WIDGET(button));
      gtk_box_pack_start(self->appListBox, GTK_WIDGET(button), FALSE, FALSE, 0);
      
      GtkSeparator *sep = GTK_SEPARATOR(gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
      gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(sep)), "list-item-separator");
      gtk_widget_show(GTK_WIDGET(sep));
      gtk_box_pack_start(self->appListBox, GTK_WIDGET(sep), FALSE, FALSE, 0);

      ApplistButtonData *data = g_new0(ApplistButtonData, 1);
      data->appInfo = appInfo;
      data->button = button;
      data->popup = self;
      g_signal_connect_data(button, "clicked", applist_on_item_clicked, data, free_applist_button_data_closure_notify, 0);

      count += 1;
    }
    else if(type == GMENU_TREE_ITEM_DIRECTORY)
    {
      GMenuTreeDirectory *directory = gmenu_tree_iter_get_directory(it);
      
      GtkLabel *label = GTK_LABEL(gtk_label_new(gmenu_tree_directory_get_name(directory)));
      gtk_widget_set_halign(GTK_WIDGET(label), GTK_ALIGN_START);
      gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(label)), "group-label");
      gtk_box_pack_start(self->appListBox, GTK_WIDGET(label), FALSE, FALSE, 0);

      GtkSeparator *sep = GTK_SEPARATOR(gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
      gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(sep)), "list-item-separator");
      gtk_box_pack_start(self->appListBox, GTK_WIDGET(sep), FALSE, FALSE, 0);
      
      guint subcount = popup_applist_populate_directory(self, directory);
      gmenu_tree_item_unref(directory);
      
      if(subcount > 0)
      {
        gtk_widget_show(GTK_WIDGET(label));
        gtk_widget_show(GTK_WIDGET(sep));
        // Dropping 'count += subcount' here was not a mistake during c port
      }
      else
      {
        gtk_widget_destroy(GTK_WIDGET(label));
        gtk_widget_destroy(GTK_WIDGET(sep));
      }
    }
  }
  
  gmenu_tree_iter_unref(it);
  return count;
}

static void applist_on_item_clicked(GtkButton *button, ApplistButtonData *data)
{
  gtk_entry_set_text(GTK_ENTRY(data->popup->searchBar), "");
  gtk_widget_hide(GTK_WIDGET(data->popup));
  
  gchar **argsSplit = g_strsplit(g_app_info_get_executable(data->appInfo), " ", -1);
  g_spawn_async(NULL, argsSplit, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
  g_strfreev(argsSplit);
}

static void applist_launch_first(GrapheneLauncherPopup *self)
{
  GList *widgets = gtk_container_get_children(GTK_CONTAINER(self->appListBox));
  for(GList *widget = widgets; widget != NULL; widget = widget->next)
  {
    if(GTK_IS_BUTTON(widget->data))
    {
      gtk_button_clicked(GTK_BUTTON(widget->data)); // Click the first button
      break;
    }
  }
  g_list_free(widgets);
}
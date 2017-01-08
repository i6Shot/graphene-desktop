/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#define GMENU_I_KNOW_THIS_IS_UNSTABLE // TODO: Apparently libgnome-menu is unstable (public API frequently changed). Maybe find an alternative? 

#include "launcher.h"
#include "../cmk/cmk-icon.h"
#include <gdk/gdkx.h>
#include <gmenu-tree.h>
#include <gio/gdesktopappinfo.h>
#include <glib.h>

struct _GrapheneLauncherApplet
{
	CmkButton parent;
	
	//GrapheneLauncherPopup *popup;
};

static void graphene_launcher_applet_dispose(GObject *self_);

G_DEFINE_TYPE(GrapheneLauncherApplet, graphene_launcher_applet, CMK_TYPE_BUTTON)



GrapheneLauncherApplet* graphene_launcher_applet_new(void)
{
	return GRAPHENE_LAUNCHER_APPLET(g_object_new(GRAPHENE_TYPE_LAUNCHER_APPLET, NULL));
}

static void graphene_launcher_applet_class_init(GrapheneLauncherAppletClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_launcher_applet_dispose;
}

static void graphene_launcher_applet_init(GrapheneLauncherApplet *self)
{
	//cmk_button_set_text(CMK_BUTTON(self), "Launcher");
	
	CmkIcon *icon = cmk_icon_new_from_name("open-menu-symbolic");
	cmk_icon_set_icon_theme(icon, "Adwaita");
	cmk_icon_set_size(icon, 64);
	//clutter_actor_set_height(CLUTTER_ACTOR(icon), 64);
	cmk_button_set_content(CMK_BUTTON(self), CMK_WIDGET(icon));
	//g_signal_connect(self, "clicked", G_CALLBACK(applet_on_click), NULL);
	
	//self->popup = graphene_launcher_popup_new();
	//g_signal_connect_swapped(self->popup, "hide", G_CALLBACK(applet_on_popup_hide), self);
}

static void graphene_launcher_applet_dispose(GObject *self_)
{
	GrapheneLauncherApplet *self = GRAPHENE_LAUNCHER_APPLET(self_);
	//g_clear_pointer(&self->popup, gtk_widget_destroy);
	G_OBJECT_CLASS(graphene_launcher_applet_parent_class)->dispose(self_);
}


/*
 ******** Popup ********
 */

/*
struct _GrapheneLauncherPopup
{
	GtkWindow parent;
	
	GtkBox *popupLayout;
	GtkBox *searchBarContainer;
	GtkSearchEntry *searchBar;
	gchar *filter;
	
	GtkScrolledWindow *appListView;
	GtkBox *appListBox;
	
	GMenuTree *appTree;
};


G_DEFINE_TYPE(GrapheneLauncherPopup, graphene_launcher_popup, GTK_TYPE_WINDOW)


static void graphene_launcher_popup_dispose(GObject *self_);
static void popup_on_show(GrapheneLauncherPopup *self);
static void popup_on_hide(GrapheneLauncherPopup *self);
static void popup_on_mapped(GrapheneLauncherPopup *self);
static void popup_on_monitors_changed(GrapheneLauncherPopup *self, GdkScreen *screen);
static void popup_on_size_allocate(GrapheneLauncherPopup *self, GtkAllocation *alloc);
static void popup_update_size(GrapheneLauncherPopup *self);
static gboolean popup_on_mouse_event(GrapheneLauncherPopup *self, GdkEventButton *event);
static void popup_on_search_changed(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar);
static void popup_on_search_enter(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar);
static gboolean popup_on_key_event(GrapheneLauncherPopup *self, GdkEvent *event);
static void popup_on_vertical_scrolled(GrapheneLauncherPopup *self, GtkAdjustment *vadj);
static void popup_applist_refresh(GrapheneLauncherPopup *self);
static void popup_applist_populate(GrapheneLauncherPopup *self);
static guint popup_applist_populate_directory(GrapheneLauncherPopup *self, GMenuTreeDirectory *directory);
static void applist_on_item_clicked(GrapheneLauncherPopup *self, GtkButton *button);
static void applist_launch_first(GrapheneLauncherPopup *self);

GrapheneLauncherPopup* graphene_launcher_popup_new(void)
{
	return GRAPHENE_LAUNCHER_POPUP(g_object_new(GRAPHENE_TYPE_LAUNCHER_POPUP, NULL));
}

static void graphene_launcher_popup_class_init(GrapheneLauncherPopupClass *klass)
{
	GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
	gobjectClass->dispose = graphene_launcher_popup_dispose;
}

static void graphene_launcher_popup_init(GrapheneLauncherPopup *self)
{
	gtk_window_set_type_hint(GTK_WINDOW(self), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
	g_signal_connect(self, "show", G_CALLBACK(popup_on_show), NULL);
	g_signal_connect(self, "hide", G_CALLBACK(popup_on_hide), NULL);
	g_signal_connect(self, "map", G_CALLBACK(popup_on_mapped), NULL);
	g_signal_connect(self, "size-allocate", G_CALLBACK(popup_on_size_allocate), NULL);
	g_signal_connect(self, "button_press_event", G_CALLBACK(popup_on_mouse_event), NULL);
	g_signal_connect(self, "key_press_event", G_CALLBACK(popup_on_key_event), NULL);
	g_signal_connect(self, "key_release_event", G_CALLBACK(popup_on_key_event), NULL);
	g_signal_connect_swapped(gtk_widget_get_screen(GTK_WIDGET(self)), "monitors-changed", G_CALLBACK(popup_on_monitors_changed), self);
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

static void graphene_launcher_popup_dispose(GObject *self_)
{
	GrapheneLauncherPopup *self = GRAPHENE_LAUNCHER_POPUP(self_);
	g_clear_object(&self->appTree);
	g_clear_pointer(&self->filter, g_free);
	G_OBJECT_CLASS(graphene_launcher_popup_parent_class)->dispose(self_);
}

static void popup_on_show(GrapheneLauncherPopup *self)
{
	popup_applist_refresh(self); // TODO: Don't recreate everything every time the popup is shown. Only when things change.
	graphene_panel_capture_screen(graphene_panel_get_default());
	gtk_grab_add(GTK_WIDGET(self));
}

static void popup_on_hide(GrapheneLauncherPopup *self)
{
	gtk_grab_remove(GTK_WIDGET(self));
	graphene_panel_end_capture(graphene_panel_get_default());
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

static void popup_on_size_allocate(GrapheneLauncherPopup *self, GtkAllocation *alloc)
{
	GdkRectangle rect;
	gdk_screen_get_monitor_workarea(gtk_widget_get_screen(GTK_WIDGET(self)), graphene_panel_get_monitor(graphene_panel_get_default()), &rect);
	gtk_window_move(GTK_WINDOW(self), rect.x, rect.y);
}

static void popup_update_size(GrapheneLauncherPopup *self)
{
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(self));
	if(window)
	{
		GdkRectangle rect;
		GtkAllocation alloc;
		gdk_screen_get_monitor_workarea(gtk_widget_get_screen(GTK_WIDGET(self)), graphene_panel_get_monitor(graphene_panel_get_default()), &rect);
		gtk_widget_get_allocation(GTK_WIDGET(self), &alloc);
		gdk_window_move_resize(window, rect.x, rect.y, alloc.width, rect.height);
	}
}

static gboolean popup_on_mouse_event(GrapheneLauncherPopup *self, GdkEventButton *event)
{
	if(gdk_window_get_toplevel(event->window) != gtk_widget_get_window(GTK_WIDGET(self)))
		gtk_widget_hide(GTK_WIDGET(self));
	return GDK_EVENT_PROPAGATE;
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
			
				// Get icon at correct size. TODO: Update when the icon theme updates
			GIcon *icon = g_app_info_get_icon(G_APP_INFO(appInfo));
			GtkIconInfo *iconInfo = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_default(), icon, 32, GTK_ICON_LOOKUP_FORCE_SIZE);
			GdkPixbuf *pixbuf = gtk_icon_info_load_icon(iconInfo, NULL);
			GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
			g_object_unref(pixbuf);
			g_object_unref(iconInfo);
			
			GtkBox *buttonBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7));
			gtk_box_pack_start(buttonBox, image, TRUE, TRUE, 7);
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

			g_object_set_data_full(G_OBJECT(button), "app-info", appInfo, g_object_unref);
			g_signal_connect_swapped(button, "clicked", G_CALLBACK(applist_on_item_clicked), self);

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

static void applist_on_item_clicked(GrapheneLauncherPopup *self, GtkButton *button)
{
	gtk_entry_set_text(GTK_ENTRY(self->searchBar), "");
	gtk_widget_hide(GTK_WIDGET(self));
	
	GDesktopAppInfo *appInfo = G_DESKTOP_APP_INFO(g_object_get_data(G_OBJECT(button), "app-info"));
	if(appInfo)
	{
		gchar **argsSplit = g_strsplit(g_app_info_get_executable(G_APP_INFO(appInfo)), " ", -1);
		g_spawn_async(NULL, argsSplit, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
		g_strfreev(argsSplit);
	}
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
}*/

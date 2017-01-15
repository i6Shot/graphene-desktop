/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#define GMENU_I_KNOW_THIS_IS_UNSTABLE // TODO: Maybe find an alternative? 

#include "panel-internal.h"
#include "cmk/cmk-label.h"
#include "cmk/button.h"
#include "cmk/cmk-icon.h"
#include "cmk/shadow.h"
#include <gdk/gdkx.h>
#include <gmenu-tree.h>
#include <gio/gdesktopappinfo.h>

struct _GrapheneLauncherPopup
{
	CmkWidget parent;
	
	CmkShadowContainer *sdc;
	CmkWidget *window;
	ClutterScrollActor *scroll;
	
	gdouble scrollAmount;
	gchar *filter;
	
	GMenuTree *appTree;
};


G_DEFINE_TYPE(GrapheneLauncherPopup, graphene_launcher_popup, CMK_TYPE_WIDGET)


static void graphene_launcher_popup_dispose(GObject *self_);
static void graphene_launcher_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static gboolean on_scroll(ClutterScrollActor *scroll, ClutterScrollEvent *event, GrapheneLauncherPopup *self);
//static void popup_on_search_changed(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar);
//static void popup_on_search_enter(GrapheneLauncherPopup *self, GtkSearchEntry *searchBar);
//static gboolean popup_on_key_event(GrapheneLauncherPopup *self, GdkEvent *event);
//static void popup_on_vertical_scrolled(GrapheneLauncherPopup *self, GtkAdjustment *vadj);
static void popup_applist_refresh(GrapheneLauncherPopup *self);
static void popup_applist_populate(GrapheneLauncherPopup *self);
static guint popup_applist_populate_directory(GrapheneLauncherPopup *self, GMenuTreeDirectory *directory);
static void applist_on_item_clicked(GrapheneLauncherPopup *self, CmkButton *button);
//static void applist_launch_first(GrapheneLauncherPopup *self);

GrapheneLauncherPopup* graphene_launcher_popup_new(void)
{
	return GRAPHENE_LAUNCHER_POPUP(g_object_new(GRAPHENE_TYPE_LAUNCHER_POPUP, NULL));
}

static void graphene_launcher_popup_class_init(GrapheneLauncherPopupClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_launcher_popup_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_launcher_popup_allocate;
}

static void graphene_launcher_popup_init(GrapheneLauncherPopup *self)
{
	self->sdc = cmk_shadow_container_new();
	cmk_shadow_container_set_blur(self->sdc, 40);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->sdc));

	self->window = cmk_widget_new();
	cmk_widget_set_draw_background_color(self->window, TRUE);
	cmk_widget_set_background_color_name(self->window, "background");
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->window));

	// Despite the scroll box looking like its inside the popup window, it
	// isn't actually a child of the window actor; it is a child of self.
	// This makes allocation/sizing easer, and helps keep the scroll window
	// from expanding too far.
	self->scroll = CLUTTER_SCROLL_ACTOR(clutter_scroll_actor_new());
	clutter_scroll_actor_set_scroll_mode(self->scroll, CLUTTER_SCROLL_VERTICALLY);
	ClutterBoxLayout *listLayout = CLUTTER_BOX_LAYOUT(clutter_box_layout_new());
	clutter_box_layout_set_orientation(listLayout, CLUTTER_ORIENTATION_VERTICAL); 
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->scroll), CLUTTER_LAYOUT_MANAGER(listLayout));
	clutter_actor_set_reactive(CLUTTER_ACTOR(self->scroll), TRUE);
	g_signal_connect(self->scroll, "scroll-event", G_CALLBACK(on_scroll), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->scroll));

	// Load applications
	self->appTree = gmenu_tree_new("gnome-applications.menu", GMENU_TREE_FLAGS_SORT_DISPLAY_NAME);
	popup_applist_refresh(self);
}

static void graphene_launcher_popup_dispose(GObject *self_)
{
	GrapheneLauncherPopup *self = GRAPHENE_LAUNCHER_POPUP(self_);
	g_clear_object(&self->appTree);
	g_clear_pointer(&self->filter, g_free);
	G_OBJECT_CLASS(graphene_launcher_popup_parent_class)->dispose(self_);
}

static void graphene_launcher_popup_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GrapheneLauncherPopup *self = GRAPHENE_LAUNCHER_POPUP(self_);
	
	ClutterActorBox windowBox = {box->x1, box->y1, MIN(box->x1 + 600, box->x2/2), box->y2};
	ClutterActorBox sdcBox = {box->x1-40, box->y1-40, windowBox.x2 + 40, box->y2 + 40};
	ClutterActorBox scrollBox = {windowBox.x1, windowBox.y1, windowBox.x2, windowBox.y2};

	clutter_actor_allocate(CLUTTER_ACTOR(self->window), &windowBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->sdc), &sdcBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->scroll), &scrollBox, flags);

	CLUTTER_ACTOR_CLASS(graphene_launcher_popup_parent_class)->allocate(self_, box, flags);
}

static gboolean on_scroll(ClutterScrollActor *scroll, ClutterScrollEvent *event, GrapheneLauncherPopup *self)
{
	// TODO: Disable button highlight when scrolling, so it feels smoother
	if(event->direction == CLUTTER_SCROLL_SMOOTH)
	{
		gdouble dx, dy;
		clutter_event_get_scroll_delta((ClutterEvent *)event, &dx, &dy);
		self->scrollAmount += dy*50; // TODO: Not magic number for multiplier
		if(self->scrollAmount < 0)
			self->scrollAmount = 0;
	
		gfloat min, nat;
		clutter_layout_manager_get_preferred_height(clutter_actor_get_layout_manager(CLUTTER_ACTOR(scroll)), CLUTTER_CONTAINER(scroll), -1, &min, &nat);

		gfloat height = clutter_actor_get_height(CLUTTER_ACTOR(scroll));
		gfloat maxScroll = nat - height;

		if(self->scrollAmount > maxScroll)
			self->scrollAmount = maxScroll;

		ClutterPoint p = {0, self->scrollAmount};
		clutter_scroll_actor_scroll_to_point(scroll, &p);
	}
	return TRUE;
}

/*
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
*/

static void popup_applist_refresh(GrapheneLauncherPopup *self)
{
	// TODO: This lags the entire WM. Do it not synced, also cache it
	gmenu_tree_load_sync(self->appTree, NULL);

	popup_applist_populate(self);
}

static void popup_applist_populate(GrapheneLauncherPopup *self)
{
	clutter_actor_destroy_all_children(CLUTTER_ACTOR(self->scroll));
	GMenuTreeDirectory *directory = gmenu_tree_get_root_directory(self->appTree);
	popup_applist_populate_directory(self, directory);
	gmenu_tree_item_unref(directory);
}

static ClutterActor * separator_new()
{
	ClutterActor *sep = clutter_actor_new();
	ClutterColor c = {0,0,0,25};
	clutter_actor_set_background_color(sep, &c);
	clutter_actor_set_x_expand(sep, TRUE);
	clutter_actor_set_height(sep, 2);
	return sep;
}

static gboolean add_app(GrapheneLauncherPopup *self, GDesktopAppInfo *appInfo)
{	
	if(g_desktop_app_info_get_nodisplay(appInfo))
		return FALSE;

	if(self->filter)
	{
		gchar *displayNameDown = g_utf8_strdown(g_app_info_get_display_name(G_APP_INFO(appInfo)), -1);
		gboolean passedFilter = g_strstr_len(displayNameDown, -1, self->filter) != NULL;
		g_free(displayNameDown);
		if(!passedFilter)
			return FALSE;
	}
	
	CmkButton *button = cmk_button_new();
	const gchar *iconName;
	GIcon *gicon = g_app_info_get_icon(G_APP_INFO(appInfo));
	if(G_IS_THEMED_ICON(gicon))
	{
		const gchar * const * names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
		iconName = names[0];
	}

	CmkIcon *icon = cmk_icon_new_from_name(iconName ? iconName : "open-menu-symbolic");
	cmk_icon_set_size(icon, 48);
	cmk_button_set_content(button, CMK_WIDGET(icon));
	cmk_button_set_text(button, g_app_info_get_display_name(G_APP_INFO(appInfo)));
	cmk_widget_set_style_parent(CMK_WIDGET(button), self->window);
	clutter_actor_set_x_expand(CLUTTER_ACTOR(button), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(button));
	
	clutter_actor_set_name(CLUTTER_ACTOR(button), g_app_info_get_executable(G_APP_INFO(appInfo)));
	g_signal_connect_swapped(button, "activate", G_CALLBACK(applist_on_item_clicked), self);
	
	clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), separator_new());
	return TRUE;
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
			if(add_app(self, gmenu_tree_entry_get_app_info(entry)))
				count += 1;
			gmenu_tree_item_unref(entry);
		}
		else if(type == GMENU_TREE_ITEM_DIRECTORY)
		{
			GMenuTreeDirectory *directory = gmenu_tree_iter_get_directory(it);
	
			CmkLabel *label = cmk_label_new_with_text(gmenu_tree_directory_get_name(directory));
			cmk_widget_set_style_parent(CMK_WIDGET(label), self->window);
			clutter_actor_set_x_expand(CLUTTER_ACTOR(label), TRUE);
			clutter_actor_set_x_align(CLUTTER_ACTOR(label), CLUTTER_ACTOR_ALIGN_START);
			ClutterMargin margin = {50, 40, 20, 20};
			clutter_actor_set_margin(CLUTTER_ACTOR(label), &margin);
			clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), CLUTTER_ACTOR(label));

			ClutterActor *sep = separator_new();
			clutter_actor_add_child(CLUTTER_ACTOR(self->scroll), sep);
			
			guint subcount = popup_applist_populate_directory(self, directory);
			gmenu_tree_item_unref(directory);

			if(subcount == 0)
			{
				clutter_actor_destroy(CLUTTER_ACTOR(label));
				clutter_actor_destroy(sep);
			}
		}
	}
	
	gmenu_tree_iter_unref(it);
	return count;
}

static void applist_on_item_clicked(GrapheneLauncherPopup *self, CmkButton *button)
{
	clutter_actor_destroy(CLUTTER_ACTOR(self));
	
	const gchar *args = clutter_actor_get_name(CLUTTER_ACTOR(button));
	if(args)
	{
		gchar **argsSplit = g_strsplit(args, " ", -1);
		g_spawn_async(NULL, argsSplit, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
		g_strfreev(argsSplit);
	}
}

/*
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

/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.	If not, see <http://www.gnu.org/licenses/>.
 */

#include "wm.h"
#include "background.h"
#include "dialog.h"
#include "common/sound.h"
#include <meta/meta-shadow-factory.h>
#include <meta/display.h>
#include <meta/keybindings.h>
#include <pulse/glib-mainloop.h>
#include <glib-unix.h>

#define WM_VERSION_STRING "1.0.0"

static void on_monitors_changed(MetaScreen *screen, GrapheneWM *wm);
static void minimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void destroy_done(ClutterActor *actor, MetaPlugin *plugin);
static void map_done(ClutterActor *actor, MetaPlugin *plugin);

const MetaPluginInfo * wm_plugin_info(MetaPlugin *plugin)
{
	static const MetaPluginInfo info = {
		.name = "Graphene WM Manager",
		.version = WM_VERSION_STRING,
		.author = "Velt (Aidan Shafran)",
		.license = "GPLv3",
		.description = "Graphene WM+Window Manager for VeltOS"
	};
	
	return &info;
}

void wm_start(MetaPlugin *plugin)
{
	GrapheneWM *wm = GRAPHENE_WM(plugin);
	
	// Get stage
	MetaScreen *screen = meta_plugin_get_screen(plugin);
	ClutterActor *stage = meta_get_stage_for_screen(screen);
	
	// Create background
	ClutterActor *backgroundGroup = meta_background_group_new();
	wm->backgroundGroup = META_BACKGROUND_GROUP(backgroundGroup);
	clutter_actor_set_reactive(backgroundGroup, TRUE);
	clutter_actor_insert_child_below(stage, backgroundGroup, NULL);
	clutter_actor_show(backgroundGroup);
	g_signal_connect(screen, "monitors_changed", G_CALLBACK(on_monitors_changed), wm);
	on_monitors_changed(screen, wm);

	// Show windows
	clutter_actor_show(meta_get_window_group_for_screen(screen));
	
	// Show stage
	clutter_actor_show(stage);
}

static void on_monitors_changed(MetaScreen *screen, GrapheneWM *wm)
{
	ClutterActor *bgGroup = CLUTTER_ACTOR(wm->backgroundGroup);
	clutter_actor_destroy_all_children(bgGroup);
	
	gint numMonitors = meta_screen_get_n_monitors(screen);
	for(int i=0;i<numMonitors;++i)
		clutter_actor_add_child(bgGroup, CLUTTER_ACTOR(graphene_wm_background_new(screen, i)));
}

void wm_minimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = CLUTTER_ACTOR(windowActor);
	
	// Get the minimized position
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);
	MetaRectangle rect = meta_rect(0,0,0,0);
	meta_window_get_icon_geometry(window, &rect); // This is set by the Launcher applet
	// printf("%i, %i, %i, %i\n", rect.x, rect.y, rect.width, rect.height);
	
	// Ease the window into its minimized position
	clutter_actor_set_pivot_point(actor, 0, 0);
	clutter_actor_save_easing_state(actor);
	clutter_actor_set_easing_mode(actor, CLUTTER_EASE_IN_SINE);
	clutter_actor_set_easing_duration(actor, 200);
	g_signal_connect(actor, "transitions_completed", G_CALLBACK(minimize_done), plugin);
	clutter_actor_set_x(actor, rect.x);
	clutter_actor_set_y(actor, rect.y);
	clutter_actor_set_scale(actor, rect.width/clutter_actor_get_width(actor), rect.height/clutter_actor_get_height(actor));
	clutter_actor_restore_easing_state(actor);
}

static void minimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
	// End transition
	clutter_actor_remove_all_transitions(actor);
	g_signal_handlers_disconnect_by_func(actor, minimize_done, plugin);
	clutter_actor_set_scale(actor, 1, 1);
	clutter_actor_hide(actor); // Actually hide the window
	
	// Must call to complete the minimization
	meta_plugin_minimize_completed(plugin, META_WINDOW_ACTOR(actor));
}

void wm_unminimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = CLUTTER_ACTOR(windowActor);

	// Get the unminimized position
	gint x = clutter_actor_get_x(actor);
	gint y = clutter_actor_get_y(actor);
	
	// Move the window to it's minimized position and scale
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);
	MetaRectangle rect = meta_rect(0,0,0,0);
	meta_window_get_icon_geometry(window, &rect);
	clutter_actor_set_x(actor, rect.x);
	clutter_actor_set_y(actor, rect.y);
	clutter_actor_set_scale(actor, rect.width/clutter_actor_get_width(actor), rect.height/clutter_actor_get_height(actor));
	clutter_actor_show(actor);
	
	// Ease it into its unminimized position
	clutter_actor_set_pivot_point(actor, 0, 0);
	clutter_actor_save_easing_state(actor);
	clutter_actor_set_easing_mode(actor, CLUTTER_EASE_OUT_SINE);
	clutter_actor_set_easing_duration(actor, 200);
	g_signal_connect(actor, "transitions_completed", G_CALLBACK(unminimize_done), plugin);
	clutter_actor_set_x(actor, x);
	clutter_actor_set_y(actor, y);
	clutter_actor_set_scale(actor, 1, 1);
	clutter_actor_restore_easing_state(actor);
}

static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
	clutter_actor_remove_all_transitions(actor);
	g_signal_handlers_disconnect_by_func(actor, unminimize_done, plugin);
	meta_plugin_unminimize_completed(plugin, META_WINDOW_ACTOR(actor));
}

void wm_destroy(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = CLUTTER_ACTOR(windowActor);

	clutter_actor_remove_all_transitions(actor);
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);

	switch(meta_window_get_window_type(window))
	{
	case META_WINDOW_NORMAL:
	case META_WINDOW_NOTIFICATION:
	case META_WINDOW_DIALOG:
	case META_WINDOW_MODAL_DIALOG:
		clutter_actor_set_pivot_point(actor, 0.5, 0.5);
		clutter_actor_save_easing_state(actor);
		clutter_actor_set_easing_mode(actor, CLUTTER_EASE_OUT_QUAD);
		clutter_actor_set_easing_duration(actor, 200);
		g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
		clutter_actor_set_scale(actor, 0, 0);
		clutter_actor_restore_easing_state(actor);
		break;
		
	case META_WINDOW_MENU:
	case META_WINDOW_DOCK:
	default:
		meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
	}
}

static void destroy_done(ClutterActor *actor, MetaPlugin *plugin)
{
	clutter_actor_remove_all_transitions(actor);
	g_signal_handlers_disconnect_by_func(actor, destroy_done, plugin);
	meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
}

void wm_map(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
	ClutterActor *actor = CLUTTER_ACTOR(windowActor);

	clutter_actor_remove_all_transitions(actor);
	MetaWindow *window = meta_window_actor_get_meta_window(windowActor);

	switch(meta_window_get_window_type(window))
	{
	case META_WINDOW_NORMAL:
	case META_WINDOW_NOTIFICATION:
	case META_WINDOW_DIALOG:
	case META_WINDOW_MODAL_DIALOG:
		clutter_actor_set_pivot_point(actor, 0.5, 0.5);
		clutter_actor_set_scale(actor, 0, 0);
		clutter_actor_show(actor);
		clutter_actor_save_easing_state(actor);
		clutter_actor_set_easing_mode(actor, CLUTTER_EASE_IN_QUAD);
		clutter_actor_set_easing_duration(actor, 200);
		g_signal_connect(actor, "transitions_completed", G_CALLBACK(map_done), plugin);
		clutter_actor_set_scale(actor, 1, 1);
		clutter_actor_restore_easing_state(actor);
		break;
		
	case META_WINDOW_MENU:
	case META_WINDOW_DOCK:
	default:
		meta_plugin_map_completed(plugin, META_WINDOW_ACTOR(actor));
	}
	
	if(g_strcmp0(meta_window_get_role(window), "GrapheneDock") == 0 || g_strcmp0(meta_window_get_role(window), "GraphenePopup") == 0)
	{
		g_object_set(windowActor, "shadow-mode", META_SHADOW_MODE_FORCED_ON, "shadow-class", "dock", NULL);
	}
}

static void map_done(ClutterActor *actor, MetaPlugin *plugin)
{
	clutter_actor_remove_all_transitions(actor);
	g_signal_handlers_disconnect_by_func(actor, map_done, plugin);
	meta_plugin_map_completed(plugin, META_WINDOW_ACTOR(actor));
}



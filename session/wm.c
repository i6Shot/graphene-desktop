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
#include "wmwidgets/background.h"
#include "dialog.h"
#include <meta/meta-shadow-factory.h>
#include <meta/display.h>
#include <meta/keybindings.h>
#include <pulse/glib-mainloop.h>
#include <glib-unix.h>

#define WM_VERSION_STRING "1.0.0"
#define WM_PERCENT_BAR_STEPS 15
#define WM_TRANSITION_TIME 200 // Common transition time, ms

/*
 * From what I can tell, the current version of Clutter has a memory leak
 * where the ClutterTransition object isn't freed after a transition,
 * and since it holds a reference to the actor, the actor gets an extra
 * reference.
 * I may be mistaken on this, but it seems to be so. I think the line
 * 19059 in clutter-actor.c, commit 40bbab8, is the issue.
 * This quick-fix just unrefs the ClutterTransition object after the
 * transition completes.
 * This also shouldn't cause crashes if the memleak is fixed, since
 * the signal connects after all the internal signals, and g_object_unref
 * would just throw an error message.
 * Submitted as bug 776471 on GNOME BugZilla.
 */
#define TRANSITION_MEMLEAK_FIX(actor, tname) g_signal_connect_after(clutter_actor_get_transition((actor), (tname)), "stopped", G_CALLBACK(g_object_unref), NULL)


static void on_monitors_changed(MetaScreen *screen, GrapheneWM *wm);
static void center_actor_on_primary(GrapheneWM *self, ClutterActor *actor);

static void minimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void destroy_done(ClutterActor *actor, MetaPlugin *plugin);
static void map_done(ClutterActor *actor, MetaPlugin *plugin);

static void init_keybindings(GrapheneWM *self);

const MetaPluginInfo * graphene_wm_plugin_info(MetaPlugin *plugin)
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

void graphene_wm_start(MetaPlugin *plugin)
{
	GrapheneWM *self = GRAPHENE_WM(plugin);

	// Get stage
	MetaScreen *screen = meta_plugin_get_screen(plugin);
	self->stage = meta_get_stage_for_screen(screen);

	init_keybindings(self);
	self->percentBar = graphene_percent_floater_new();
	graphene_percent_floater_set_divisions(self->percentBar, WM_PERCENT_BAR_STEPS);
	graphene_percent_floater_set_scale(self->percentBar, 2);
	clutter_actor_insert_child_above(self->stage, CLUTTER_ACTOR(self->percentBar), NULL);

	// Create background
	ClutterActor *backgroundGroup = meta_background_group_new();
	self->backgroundGroup = META_BACKGROUND_GROUP(backgroundGroup);
	clutter_actor_set_reactive(backgroundGroup, FALSE);
	clutter_actor_insert_child_below(self->stage, backgroundGroup, NULL);

	self->coverGroup = clutter_actor_new();
	clutter_actor_set_reactive(self->coverGroup, FALSE);
	clutter_actor_insert_child_above(self->stage, self->coverGroup, NULL);

	g_signal_connect(screen, "monitors_changed", G_CALLBACK(on_monitors_changed), self);
	on_monitors_changed(screen, self);

	clutter_actor_show(backgroundGroup);
	
	// Show windows
	ClutterActor *windowGroup = meta_get_window_group_for_screen(screen);
	clutter_actor_show(windowGroup);
	//clutter_actor_set_opacity(windowGroup, 50);

	// Show stage
	clutter_actor_show(self->stage);
	
	// Start the WM modal, and the session manager can end the modal when
	// startup completes with graphene_wm_show_dialog(wm, NULL);
	meta_plugin_begin_modal(META_PLUGIN(self), 0, 0);
	clutter_actor_show(self->coverGroup);
}

static void on_monitors_changed(MetaScreen *screen, GrapheneWM *self)
{
	ClutterActor *bgGroup = CLUTTER_ACTOR(self->backgroundGroup);
	clutter_actor_destroy_all_children(bgGroup);
	clutter_actor_destroy_all_children(self->coverGroup);
	
	ClutterColor *coverColor = clutter_color_new(0,0,0,140);

	gint numMonitors = meta_screen_get_n_monitors(screen);
	for(int i=0;i<numMonitors;++i)
	{
		clutter_actor_add_child(bgGroup, CLUTTER_ACTOR(graphene_wm_background_new(screen, i)));
	
		MetaRectangle rect = meta_rect(0,0,0,0);
		meta_screen_get_monitor_geometry(screen, i, &rect);

		ClutterActor *cover = clutter_actor_new();
		clutter_actor_set_background_color(cover, coverColor);
		clutter_actor_set_position(cover, rect.x, rect.y);
		clutter_actor_set_size(cover, rect.width, rect.height);
		clutter_actor_add_child(self->coverGroup, cover);
	}
	
	clutter_color_free(coverColor);

	int width = 0, height = 0;
	meta_screen_get_size(screen, &width, &height);
	
	clutter_actor_set_y(CLUTTER_ACTOR(self->percentBar), 30);
	clutter_actor_set_x(CLUTTER_ACTOR(self->percentBar), width/2-width/8);
	clutter_actor_set_width(CLUTTER_ACTOR(self->percentBar), width/4);
	clutter_actor_set_height(CLUTTER_ACTOR(self->percentBar), 20);

	if(self->dialog)
		center_actor_on_primary(self, self->dialog);
}

static void close_dialog_complete(GrapheneWM *self, ClutterActor *dialog)
{
	g_signal_handlers_disconnect_by_func(dialog, close_dialog_complete, self);
	clutter_actor_remove_child(self->stage, dialog);
	if(dialog == self->dialog)
		self->dialog = NULL;
}

static void graphene_wm_close_dialog(GrapheneWM *self, gboolean closeCover)
{
	if(self->dialog)
	{
		g_signal_connect_swapped(self->dialog, "transitions_completed", G_CALLBACK(close_dialog_complete), self);
		clutter_actor_save_easing_state(self->dialog);
		clutter_actor_set_easing_mode(self->dialog, CLUTTER_EASE_OUT_SINE);
		clutter_actor_set_easing_duration(self->dialog, WM_TRANSITION_TIME);
		clutter_actor_set_scale(self->dialog, 0, 0);
		clutter_actor_restore_easing_state(self->dialog);
		clutter_actor_set_reactive(self->dialog, FALSE);
		TRANSITION_MEMLEAK_FIX(self->dialog, "scale-x");
		TRANSITION_MEMLEAK_FIX(self->dialog, "scale-y");
	}
	
	meta_plugin_end_modal(META_PLUGIN(self), 0);
	
	if(!closeCover || clutter_actor_get_opacity(self->coverGroup) == 0)
		return;

	clutter_actor_save_easing_state(self->coverGroup);
	clutter_actor_set_easing_mode(self->coverGroup, CLUTTER_EASE_OUT_SINE);
	clutter_actor_set_easing_duration(self->coverGroup, WM_TRANSITION_TIME);
	clutter_actor_set_opacity(self->coverGroup, 0);
	clutter_actor_restore_easing_state(self->coverGroup);
	TRANSITION_MEMLEAK_FIX(self->coverGroup, "opacity");
}

void graphene_wm_show_dialog(GrapheneWM *self, ClutterActor *dialog)
{
	if(!dialog || (dialog && self->dialog))
		graphene_wm_close_dialog(self, !dialog);
	
	if(!dialog)
		return;

	self->dialog = dialog;
	clutter_actor_insert_child_above(self->stage, self->dialog, NULL);
	clutter_actor_show(self->dialog);
	clutter_actor_set_pivot_point(self->dialog, 0.5, 0.5);
	clutter_actor_set_scale(self->dialog, 0, 0);
	center_actor_on_primary(self, self->dialog);

	clutter_actor_save_easing_state(self->dialog);
	clutter_actor_set_easing_mode(self->dialog, CLUTTER_EASE_IN_SINE);
	clutter_actor_set_easing_duration(self->dialog, WM_TRANSITION_TIME);
	clutter_actor_set_scale(self->dialog, 1, 1);
	clutter_actor_restore_easing_state(self->dialog);
	clutter_actor_set_reactive(self->dialog, TRUE);
	TRANSITION_MEMLEAK_FIX(self->dialog, "scale-x");
	TRANSITION_MEMLEAK_FIX(self->dialog, "scale-y");

	clutter_actor_save_easing_state(self->coverGroup);
	clutter_actor_set_easing_mode(self->coverGroup, CLUTTER_EASE_IN_SINE);
	clutter_actor_set_easing_duration(self->coverGroup, WM_TRANSITION_TIME);
	clutter_actor_set_opacity(self->coverGroup, 255);
	clutter_actor_restore_easing_state(self->coverGroup);
	TRANSITION_MEMLEAK_FIX(self->coverGroup, "opacity");
	meta_plugin_begin_modal(META_PLUGIN(self), 0, 0);
}


static void center_actor_on_primary(GrapheneWM *self, ClutterActor *actor)
{
	MetaScreen *screen = meta_plugin_get_screen(META_PLUGIN(self));
	int primaryMonitor = meta_screen_get_primary_monitor(screen);
	MetaRectangle rect = meta_rect(0,0,0,0);
	meta_screen_get_monitor_geometry(screen, primaryMonitor, &rect);
	
	gfloat width, height;
	clutter_actor_get_size(actor, &width, &height);
	
	clutter_actor_set_position(actor,
		rect.x + rect.width/2.0 - width/2.0,
		rect.y + rect.height/2.0 - height/2.0);
}



void graphene_wm_minimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
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
	clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
	g_signal_connect(actor, "transitions_completed", G_CALLBACK(minimize_done), plugin);
	clutter_actor_set_x(actor, rect.x);
	clutter_actor_set_y(actor, rect.y);
	clutter_actor_set_scale(actor, rect.width/clutter_actor_get_width(actor), rect.height/clutter_actor_get_height(actor));
	TRANSITION_MEMLEAK_FIX(actor, "x");
	TRANSITION_MEMLEAK_FIX(actor, "y");
	TRANSITION_MEMLEAK_FIX(actor, "scale-x");
	TRANSITION_MEMLEAK_FIX(actor, "scale-y");
	clutter_actor_restore_easing_state(actor);
}

static void minimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
	// End transition
	g_signal_handlers_disconnect_by_func(actor, minimize_done, plugin);
	clutter_actor_set_scale(actor, 1, 1);
	clutter_actor_hide(actor); // Actually hide the window
	
	// Must call to complete the minimization
	meta_plugin_minimize_completed(plugin, META_WINDOW_ACTOR(actor));
}

void graphene_wm_unminimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
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
	clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
	g_signal_connect(actor, "transitions_completed", G_CALLBACK(unminimize_done), plugin);
	clutter_actor_set_x(actor, x);
	clutter_actor_set_y(actor, y);
	clutter_actor_set_scale(actor, 1, 1);
	clutter_actor_restore_easing_state(actor);
	TRANSITION_MEMLEAK_FIX(actor, "x");
	TRANSITION_MEMLEAK_FIX(actor, "y");
	TRANSITION_MEMLEAK_FIX(actor, "scale-x");
	TRANSITION_MEMLEAK_FIX(actor, "scale-y");
}

static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
	g_signal_handlers_disconnect_by_func(actor, unminimize_done, plugin);
	meta_plugin_unminimize_completed(plugin, META_WINDOW_ACTOR(actor));
}

void graphene_wm_destroy(MetaPlugin *plugin, MetaWindowActor *windowActor)
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
		clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
		g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
		clutter_actor_set_scale(actor, 0, 0);
		clutter_actor_restore_easing_state(actor);
		TRANSITION_MEMLEAK_FIX(actor, "scale-x");
		TRANSITION_MEMLEAK_FIX(actor, "scale-y");
		break;
		
	case META_WINDOW_MENU:
	case META_WINDOW_DOCK:
	default:
		meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
	}
}

static void destroy_done(ClutterActor *actor, MetaPlugin *plugin)
{
	g_signal_handlers_disconnect_by_func(actor, destroy_done, plugin);
	meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
}

void graphene_wm_map(MetaPlugin *plugin, MetaWindowActor *windowActor)
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
		clutter_actor_set_easing_duration(actor, WM_TRANSITION_TIME);
		g_signal_connect(actor, "transitions_completed", G_CALLBACK(map_done), plugin);
		clutter_actor_set_scale(actor, 1, 1);
		clutter_actor_restore_easing_state(actor);
		TRANSITION_MEMLEAK_FIX(actor, "scale-x");
		TRANSITION_MEMLEAK_FIX(actor, "scale-y");
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
	g_signal_handlers_disconnect_by_func(actor, map_done, plugin);
	meta_plugin_map_completed(plugin, META_WINDOW_ACTOR(actor));
}



/*
 * Keybindings
 */

static void on_key_volume_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	SoundDevice *device = sound_settings_get_active_output_device(self->soundSettings);
	sound_device_set_muted(device, FALSE);
	
	float stepSize = 1.0/WM_PERCENT_BAR_STEPS;
	if(clutter_event_has_shift_modifier((ClutterEvent *)event))
		stepSize /= 2;
	
	float vol = sound_device_get_volume(device) + stepSize;
	vol = (vol > 1) ? 1 : vol;
	graphene_percent_floater_set_percent(self->percentBar, vol);
	sound_device_set_volume(device, vol);
}

static void on_key_volume_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	SoundDevice *device = sound_settings_get_active_output_device(self->soundSettings);
	sound_device_set_muted(device, FALSE);
	
	float stepSize = 1.0/WM_PERCENT_BAR_STEPS;
	if(clutter_event_has_shift_modifier((ClutterEvent *)event))
		stepSize /= 2;
	
	float vol = sound_device_get_volume(device) - stepSize;
	vol = (vol < 0) ? 0 : vol;
	graphene_percent_floater_set_percent(self->percentBar, vol);
	sound_device_set_volume(device, vol);
}

static void on_key_volume_mute(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	SoundDevice *device = sound_settings_get_active_output_device(self->soundSettings);
	gboolean newMute = !sound_device_get_muted(device);
	graphene_percent_floater_set_percent(self->percentBar, newMute ? 0 : sound_device_get_volume(device));
	sound_device_set_muted(device, newMute);
}

static void on_key_backlight_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	//if(!self->connection)
	//	return;
	//
	//g_dbus_connection_call(self->connection,
	//	"org.gnome.SettingsDaemon.Power",
	//	"/org/gnome/SettingsDaemon/Power",
	//	"org.gnome.SettingsDaemon.Power.Screen",
	//	"StepUp",
	//	NULL,
	//	NULL,
	//	G_DBUS_CALL_FLAGS_NONE,
	//	-1, NULL, NULL, NULL);
}

static void on_key_backlight_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	//if(!self->connection)
	//	return;
	//
	//g_dbus_connection_call(self->connection,
	//	"org.gnome.SettingsDaemon.Power",
	//	"/org/gnome/SettingsDaemon/Power",
	//	"org.gnome.SettingsDaemon.Power.Screen",
	//	"StepDown",
	//	NULL,
	//	NULL,
	//	G_DBUS_CALL_FLAGS_NONE,
	//	-1, NULL, NULL, NULL);
}

static void on_key_kb_backlight_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	//if(!self->connection)
	//	return;
	//
	//g_dbus_connection_call(self->connection,
	//	"org.gnome.SettingsDaemon.Power",
	//	"/org/gnome/SettingsDaemon/Power",
	//	"org.gnome.SettingsDaemon.Power.Keyboard",
	//	"StepUp",
	//	NULL,
	//	NULL,
	//	G_DBUS_CALL_FLAGS_NONE,
	//	-1, NULL, NULL, NULL);
}

static void on_key_kb_backlight_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	//if(!self->connection)
	//	return;
	//
	//g_dbus_connection_call(self->connection,
	//	"org.gnome.SettingsDaemon.Power",
	//	"/org/gnome/SettingsDaemon/Power",
	//	"org.gnome.SettingsDaemon.Power.Keyboard",
	//	"StepDown",
	//	NULL,
	//	NULL,
	//	G_DBUS_CALL_FLAGS_NONE,
	//	-1, NULL, NULL, NULL);
}

static void init_keybindings(GrapheneWM *self)
{
	pa_proplist *proplist = pa_proplist_new();
	pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "graphene-window-manager");
	// pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, g_application_get_application_id(g_application_get_default()));
	pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "multimedia-volume-control-symbolic");
	pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, WM_VERSION_STRING);
	
	pa_glib_mainloop *mainloop = pa_glib_mainloop_new(g_main_context_default());
	self->soundSettings = sound_settings_init(mainloop, pa_glib_mainloop_get_api(mainloop), proplist, (DestroyPAMainloopNotify)pa_glib_mainloop_free);


	GSettings *keybindings = g_settings_new("io.velt.desktop.keybindings");
	MetaDisplay *display = meta_screen_get_display(meta_plugin_get_screen(META_PLUGIN(self)));
	
	#define bind(key, func) meta_display_add_keybinding(display, key, keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)func, self, NULL);
	bind("volume-up", on_key_volume_up);
	bind("volume-down", on_key_volume_down);
	bind("volume-up-half", on_key_volume_up);
	bind("volume-down-half", on_key_volume_down);
	bind("volume-mute", on_key_volume_mute);
	bind("backlight-up", on_key_backlight_up);
	bind("backlight-down", on_key_backlight_down);
	bind("kb-backlight-up", on_key_kb_backlight_up);
	bind("kb-backlight-down", on_key_kb_backlight_down);
	#undef bind

	g_object_unref(keybindings);

	// meta_keybindings_set_custom_handler("panel-main-menu", (MetaKeyHandlerFunc)on_panel_main_menu, self, NULL);
	// meta_keybindings_set_custom_handler("switch-windows", switch_windows);
	// meta_keybindings_set_custom_handler("switch-applications", switch_windows);
}


/*
 * Dialog
 */

void graphene_wm_show_logout_dialog(GrapheneWM *self, GCallback onCloseCb)
{
	//gchar **buttons = g_strsplit("Logout Sleep Restart Shutdown Cancel", " ", 0);
	//GrapheneWMDialog *dialog = graphene_wm_dialog_new(NULL, buttons);
	//g_strfreev(buttons);

	//g_signal_connect(dialog, "close", onCloseCb, self);
	//graphene_wm_dialog_show(dialog, meta_plugin_get_screen(META_PLUGIN(self)), 0);
	//meta_plugin_begin_modal(META_PLUGIN(self), 0, 0);
}
	

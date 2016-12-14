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
 *
 *
 * This program functions as both the wm manager and window manager.
 * Partially because I see "wm management" and "window management" as
 * very similar jobs, partially because it makes wm-related graphics
 * easier to display (ex. logout dialog).
 *
 * Code dealing with Mutter must be GPL'd, but session management code
 * is under the Apache License and in its own file.
 */

#include <meta/main.h>
#include <meta/meta-plugin.h>
#include <glib-unix.h>
#include "session.h"
#include "wm.h"

G_DEFINE_TYPE(GrapheneWM, graphene_wm, META_TYPE_PLUGIN);
static void graphene_wm_dispose(GObject *gobject);
static gboolean on_exit_signal(gpointer userdata);
static void on_session_quit(gboolean failed, gpointer userdata);
static void on_session_startup_complete(gpointer userdata);

int main(int argc, char **argv)
{
	meta_plugin_manager_set_plugin_type(GRAPHENE_TYPE_WM);
	meta_set_wm_name("GRAPHENE Desktop");
	meta_set_gnome_wm_keybindings("Mutter,GNOME Shell");
	
	GError *error = NULL;
	GOptionContext *opt = meta_get_option_context();
	if(!g_option_context_parse(opt, &argc, &argv, &error))
	{
		g_critical("Bad arguments to graphene-wm: %s", error->message);
		return 1;
	}
	g_option_context_free(opt);
	
	g_setenv("NO_GAIL", "1", TRUE);
	g_setenv("NO_AT_BRIDGE", "1", TRUE);
	meta_init();
	g_unsetenv("NO_AT_BRIDGE");
	g_unsetenv("NO_GAIL");
	
	return meta_run();
}

static void graphene_wm_class_init(GrapheneWMClass *class)
{
	MetaPluginClass *pluginClass = META_PLUGIN_CLASS(class);
	pluginClass->start = wm_start;
	pluginClass->minimize = wm_minimize;
	pluginClass->unminimize = wm_unminimize;
	pluginClass->map = wm_map;
	pluginClass->destroy = wm_destroy;
	pluginClass->plugin_info = wm_plugin_info;
	
	G_OBJECT_CLASS(class)->dispose = graphene_wm_dispose;
}

static void graphene_wm_init(GrapheneWM *wm)
{
	graphene_session_init(on_session_startup_complete, on_session_quit, wm);

	g_unix_signal_add(SIGTERM, (GSourceFunc)on_exit_signal, NULL);
	g_unix_signal_add(SIGINT, (GSourceFunc)on_exit_signal, NULL);
	g_unix_signal_add(SIGHUP, (GSourceFunc)on_exit_signal, NULL);
}

static void graphene_wm_dispose(GObject *gobject)
{
	GrapheneWM *wm = GRAPHENE_WM(gobject);
	graphene_session_exit(); // Has no effect if the session already exited
	// g_signal_handlers_disconnect_by_func(
	// 	meta_plugin_get_screen(META_PLUGIN(gobject)),
	// 	on_monitors_changed,
	// 	gobject);
	// g_clear_object(&GRAPHENE_WM(gobject)->BackgroundGroup);
	G_OBJECT_CLASS(graphene_wm_parent_class)->dispose(gobject);
}

static gboolean on_exit_signal(gpointer userdata)
{
	g_message("Received interrupt. Logging out.");
	graphene_session_logout();
	return G_SOURCE_CONTINUE;
}

static void on_session_quit(gboolean failed, gpointer userdata)
{
	g_message("SM has completed %s. Exiting mutter.", failed ? "with an error" : "successfully");
	meta_quit(failed ? META_EXIT_ERROR : META_EXIT_SUCCESS);
}

static void on_session_startup_complete(gpointer userdata)
{
	g_message("SM startup complete.");
	GrapheneWM *wm = GRAPHENE_WM(userdata);
	// TODO: The WM should show only the background picture up until this point
}

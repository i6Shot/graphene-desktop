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
#include <meta/meta-shadow-factory.h>
#include <meta/display.h>
#include <meta/keybindings.h>
#include <libsettings/sound.h>
#include <pulse/glib-mainloop.h>
#include <glib-unix.h>

#define WM_VERSION_STRING "1.0.0"

// GrapheneWM class (private)
struct _GrapheneWM {
	MetaPlugin parent;
	MetaBackgroundGroup *BackgroundGroup;
	gchar *clientId;
	guint dbusNameId;
	GDBusConnection *connection;
	GDBusProxy *smProxy;
	gchar *clientPath;
	GDBusProxy *clientProxy;
	guint interfaceRegistrationId;
	SoundSettings *soundSettings;
};

// Structs copied from meta-shadow-factory.c (commit a191554 on Jul 6, 2015)
typedef struct
{
	GObject parent_instance;
	GHashTable *shadows;
	GHashTable *shadow_classes;
} _MetaShadowFactory;
typedef struct
{
	const char *name;
	MetaShadowParams focused;
	MetaShadowParams unfocused;
} _MetaShadowClassInfo;


G_DEFINE_TYPE(GrapheneWM, graphene_wm, META_TYPE_PLUGIN);


static void graphene_wm_dispose(GObject *gobject); 
static void start(MetaPlugin *plugin);
static const MetaPluginInfo * plugin_info(MetaPlugin *plugin);
static void quit(GrapheneWM *wm);
static void on_monitors_changed(MetaScreen *screen, MetaPlugin *plugin);
static void register_with_session(GrapheneWM *wm);
static void show_logout_dialog(GrapheneWM *wm);
static void minimize(MetaPlugin *wm, MetaWindowActor *actor);
static void unminimize(MetaPlugin *wm, MetaWindowActor *actor);
static void map(MetaPlugin *plugin, MetaWindowActor *actor);
static void destroy(MetaPlugin *plugin, MetaWindowActor *actor);
static void graphene_wm_init_keybindings(GrapheneWM *self);



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

GrapheneWM* graphene_wm_new(void)
{
	return GRAPHENE_WM(g_object_new(GRAPHENE_TYPE_WM, NULL));
}

static void graphene_wm_class_init(GrapheneWMClass *klass)
{
	MetaPluginClass *pluginClass = META_PLUGIN_CLASS(klass);
	pluginClass->start = start;
	pluginClass->minimize = minimize;
	pluginClass->unminimize = unminimize;
	pluginClass->map = map;
	pluginClass->destroy = destroy;
	pluginClass->plugin_info = plugin_info;
	
	GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
	gobjectClass->dispose = graphene_wm_dispose;
}

static void graphene_wm_init(GrapheneWM *wm)
{
}

static void graphene_wm_dispose(GObject *gobject)
{
	g_signal_handlers_disconnect_by_func(
		meta_plugin_get_screen(META_PLUGIN(gobject)),
		on_monitors_changed,
		gobject);
	g_clear_object(&GRAPHENE_WM(gobject)->BackgroundGroup);
	G_OBJECT_CLASS(graphene_wm_parent_class)->dispose(gobject);
}

static void start(MetaPlugin *plugin)
{
	g_unix_signal_add(SIGTERM, (GSourceFunc)quit, plugin);
	g_unix_signal_add(SIGINT, (GSourceFunc)quit, plugin);
	g_unix_signal_add(SIGHUP, (GSourceFunc)quit, plugin);
	
	/*
	The shadow factory has a bug which causes new shadow classes to
	not only not be created, but also corrupt the "normal" class. The
	only way I was able to fix this is by directly modifying the factory's
	hash table via private member variables. However, the private
	interface has remained the same for six years, so it's probably safe
	for a while...
	
	The bug is (I think):
		class_info->name = g_strdup (class_info->name);
	on line 830 of meta-shadow-factory.c should be
		class_info->name = g_strdup (class_name);
	
	TODO: Maybe submit a bug report? Or something.
	*/
	
	// Add a shadow class for the panel
	MetaShadowParams dockShadow = {3, -1, 0, 0, 200}; // radius, top_fade, x_offset, y_offset, opacity
	_MetaShadowFactory *factory = (_MetaShadowFactory*)meta_shadow_factory_get_default();
	_MetaShadowClassInfo *info = g_slice_new0(_MetaShadowClassInfo);
	info->name = "dock";
	info->focused = dockShadow;
	info->unfocused = dockShadow;
	g_hash_table_insert(factory->shadow_classes, "dock", info);
	
	MetaScreen *screen = meta_plugin_get_screen(plugin);
	ClutterActor *screenGroup = meta_get_window_group_for_screen(screen);
	ClutterActor *stage = meta_get_stage_for_screen(screen);
	
	ClutterActor *backgroundGroup = meta_background_group_new();
	GRAPHENE_WM(plugin)->BackgroundGroup = META_BACKGROUND_GROUP(backgroundGroup);
	clutter_actor_set_reactive(backgroundGroup, TRUE);
	clutter_actor_insert_child_below(screenGroup, backgroundGroup, NULL);
	
	g_signal_connect(screen, "monitors_changed", G_CALLBACK(on_monitors_changed), plugin);
	on_monitors_changed(screen, plugin);
	
	clutter_actor_show(backgroundGroup);
	clutter_actor_show(screenGroup);
	clutter_actor_show(stage);
	
	graphene_wm_init_keybindings(GRAPHENE_WM(plugin));
	
	register_with_session(GRAPHENE_WM(plugin));
}

static const MetaPluginInfo * plugin_info(MetaPlugin *plugin)
{
	static const MetaPluginInfo info = {
		.name = "Graphene Window Manager",
		.version = WM_VERSION_STRING,
		.author = "Velt (Aidan Shafran)",
		.license = "GPLv3",
		.description = "Graphene Window Manager for VeltOS"
	};
	
	return &info;
}

static void quit(GrapheneWM *wm)
{
	if(wm->smProxy && wm->clientPath)
	{
		g_dbus_proxy_call_sync(wm->smProxy,
			"UnregisterClient",
			g_variant_new("(o)", wm->clientPath),
			G_DBUS_CALL_FLAGS_NONE,
			G_MAXINT, NULL, NULL);
	}
	
	g_clear_object(&wm->clientProxy);
	g_clear_object(&wm->smProxy);
	if(wm->dbusNameId)
		g_bus_unown_name(wm->dbusNameId);
	wm->dbusNameId = 0;
	g_clear_pointer(&wm->clientId, g_free);
	
	sound_settings_unref(wm->soundSettings);
	
	meta_quit(META_EXIT_SUCCESS);
}

static void on_monitors_changed(MetaScreen *screen, MetaPlugin *plugin)
{
	ClutterActor *backgroundGroup = CLUTTER_ACTOR(GRAPHENE_WM(plugin)->BackgroundGroup);
	clutter_actor_destroy_all_children(backgroundGroup);
	
	gint numMonitors = meta_screen_get_n_monitors(screen);
	for(int i=0;i<numMonitors;++i)
		clutter_actor_add_child(backgroundGroup, CLUTTER_ACTOR(graphene_wm_background_new(screen, i)));
}



/*
 * DBus registration
 * Mutter's default registration, meta_register_with_session(), uses legacy
 * XSMP. Instead, register with the session using DBus.
 */

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, GrapheneWM *wm);
static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, GrapheneWM *wm);
static void on_dbus_method_call(GDBusConnection *connection, const gchar *sender, const gchar *objectPath, const gchar *interfaceName, const gchar *methodName, GVariant *parameters, GDBusMethodInvocation *invocation, GrapheneWM *wm);
static void on_client_proxy_signal(GDBusProxy *proxy, const gchar *sender, const gchar *signal, GVariant *parameters, GrapheneWM *wm);

static const gchar *WMInterfaceXML =
"<node>"
"	<interface name='io.velt.GrapheneWM'>"
"		<method name='ShowLogoutDialog'>"
// "			<arg type='u' direction='in' name='monitor'/>"
"			<arg type='b' direction='out' name='fail'/>"
"		</method>"
"		<method name='ShowBlockingClientsDialog'> <arg type='ao' direction='in' name='blocking_clients'/> <arg type='b' direction='out' name='fail'/> </method>"
"		<signal name='LogoutDialogResponse'> <arg type='s' name='response'/> </signal>"
"		<signal name='BlockingDialogResponse'> <arg type='s' name='response'/> </signal>"
"	</interface>"
"</node>";

static void register_with_session(GrapheneWM *wm)
{
	wm->clientId = g_strdup(g_getenv("DESKTOP_AUTOSTART_ID"));
	g_unsetenv("DESKTOP_AUTOSTART_ID");
	
	wm->dbusNameId = g_bus_own_name(G_BUS_TYPE_SESSION, "io.velt.GrapheneWM",
		G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL,
		(GBusNameAcquiredCallback)on_dbus_name_acquired,
		(GBusNameLostCallback)on_dbus_name_lost,
		wm, NULL);
}

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, GrapheneWM *wm)
{
	wm->connection = connection;
	
	// Create the WM's DBus interface
	const GDBusNodeInfo *interfaceInfo = g_dbus_node_info_new_for_xml(WMInterfaceXML, NULL);
	static const GDBusInterfaceVTable interfaceCallbacks = {(GDBusInterfaceMethodCallFunc)on_dbus_method_call, NULL, NULL};
	
	wm->interfaceRegistrationId = g_dbus_connection_register_object(
		connection,
		"/io/velt/GrapheneWM",
		interfaceInfo->interfaces[0],
		&interfaceCallbacks,
		wm, NULL, NULL);
		
	if(!wm->interfaceRegistrationId)
	{
		g_critical("Failed to register object at /io/velt/GrapheneWM");
		return;
	}
	
	/* Register with the Session Manager */

	wm->smProxy = g_dbus_proxy_new_sync(connection,
		G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
		NULL,
		"org.gnome.SessionManager",
		"/org/gnome/SessionManager",
		"org.gnome.SessionManager",
		NULL, NULL);
	
	if(!wm->smProxy)
	{
		g_critical("Failed to connect to session manager");
		return;
	}
	
	GVariant *clientPathVariant = g_dbus_proxy_call_sync(wm->smProxy,
		"RegisterClient",
		g_variant_new("(ss)", "io.velt.GrapheneWM", wm->clientId ? wm->clientId : ""),
		G_DBUS_CALL_FLAGS_NONE,
		G_MAXINT, NULL, NULL);
	
	if(!clientPathVariant)
	{
		g_critical("Failed to register client");
		return;
	}
	
	g_variant_get(clientPathVariant, "(o)", &wm->clientPath);
	
	wm->clientProxy = g_dbus_proxy_new_sync(connection,
		0,
		NULL,
		"org.gnome.SessionManager",
		wm->clientPath,
		"org.gnome.SessionManager.ClientPrivate",
		NULL, NULL);
	
	if(!wm->clientProxy)
	{
		g_critical("Failed to get connection to client object");
		return;
	}
	
	g_signal_connect(wm->clientProxy, "g-signal", G_CALLBACK(on_client_proxy_signal), wm);
}

static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, GrapheneWM *wm)
{
	wm->connection = NULL;
	quit(wm);
}

static void on_client_proxy_signal(GDBusProxy *proxy,
	const gchar *sender,
	const gchar *signal,
	GVariant    *parameters,
	GrapheneWM  *wm)
{
	// Make sure only Session Manager signals are listened to
	gchar *smUniqueName = g_dbus_proxy_get_name_owner(proxy);
	gboolean sentFromSM = (g_strcmp0(sender, smUniqueName) == 0);
	g_free(smUniqueName);
	if(!sentFromSM)
		return;
	
	if(g_strcmp0(signal, "QueryEndSession") == 0)
	{
		g_dbus_proxy_call(wm->clientProxy,
			"EndSessionResponse",
			g_variant_new("(bs)", TRUE, ""),
			G_DBUS_CALL_FLAGS_NONE,
			G_MAXINT, NULL, NULL, NULL);
	}
	else if(g_strcmp0(signal, "EndSession") == 0)
	{
		g_dbus_proxy_call(wm->clientProxy,
			"EndSessionResponse",
			g_variant_new("(bs)", TRUE, ""),
			G_DBUS_CALL_FLAGS_NONE,
			G_MAXINT, NULL, NULL, NULL);
		quit(wm);
	}
	else if(g_strcmp0(signal, "Stop") == 0)
	{
		quit(wm);
	}
}

static void on_dbus_method_call(GDBusConnection *connection,
	const gchar *sender,
	const gchar *objectPath,
	const gchar *interfaceName,
	const gchar *methodName,
	GVariant    *parameters,
	GDBusMethodInvocation *invocation,
	GrapheneWM  *wm)
{
	// Make sure that only the Session Manager can call methods
	gchar *smUniqueName = g_dbus_proxy_get_name_owner(wm->smProxy);	
	gboolean sentFromSM = (g_strcmp0(sender, smUniqueName) == 0);
	g_free(smUniqueName);
	
	if(!sentFromSM)
	{
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}
	
	if(g_strcmp0(interfaceName, "io.velt.GrapheneWM") == 0)
	{
		if(g_strcmp0(methodName, "ShowLogoutDialog") == 0)
		{
			show_logout_dialog(wm);
			g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
			return;
		}
		else if(g_strcmp0(methodName, "ShowBlockingClientsDialog") == 0)
		{
			// g_variant_get(parameters, "(ao)", &appId, &xId, &reason, &flags);
		}
	}
	
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void on_logout_dialog_close(GrapheneWMDialog *dialog, const gchar *response, MetaPlugin *plugin)
{
	meta_plugin_end_modal(plugin, 0);

	GrapheneWM *wm = GRAPHENE_WM(plugin);
	
	g_dbus_connection_emit_signal(wm->connection, NULL, "/io/velt/GrapheneWM",
		"io.velt.GrapheneWM", "LogoutDialogResponse", g_variant_new("(s)", response), NULL);
}

static void show_logout_dialog(GrapheneWM *wm)
{
	g_debug("show_logout_dialog");
	gchar **buttons = g_strsplit("Logout Sleep Restart Shutdown Cancel", " ", 0);
	GrapheneWMDialog *dialog = graphene_wm_dialog_new(NULL, buttons);
	g_strfreev(buttons);
	g_signal_connect(dialog, "close", G_CALLBACK(on_logout_dialog_close), wm);
	graphene_wm_dialog_show(dialog, meta_plugin_get_screen(META_PLUGIN(wm)), 0);
	
	meta_plugin_begin_modal(META_PLUGIN(wm), 0, 0);
}





/*
 * Window management
 */

static void minimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin);
static void destroy_done(ClutterActor *actor, MetaPlugin *plugin);
static void map_done(ClutterActor *actor, MetaPlugin *plugin);

static void minimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
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

static void unminimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
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

static void destroy(MetaPlugin *plugin, MetaWindowActor *windowActor)
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

static void map(MetaPlugin *plugin, MetaWindowActor *windowActor)
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



/*
 * *** Keybindings ***
 *
 * A lot of the basic keybindings are already handled by Mutter, and are attached to
 * the GSettings path org.gnome.desktop.wm.keybindings. However, some of the default
 * actions need to be overridden, as well as some new keybindings need to be added,
 * such as media keys (which used to be supported by gnome-settings-daemon, but
 * that seems to be deprecated now).
 */

static void on_panel_main_menu(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self);
static void on_key_volume_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self);
static void on_key_volume_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self);
static void on_key_volume_mute(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self);

// static void on_sound_settings_event(SoundSettings *settings, SoundSettingsEventType type, SoundDevice *device, void *userdata)
// {
//	 // g_message("event type: %i", type);
//	 
//	 
//	 switch(type)
//	 {
//		 case SOUND_SETTINGS_EVENT_TYPE_STATE_CHANGED:
//			 g_message("state: %i", sound_settings_get_state(settings));
//			 break;
//		 case SOUND_SETTINGS_EVENT_TYPE_ACTIVE_DEVICE_CHANGED:
//			 g_message("device '%s' is now active", sound_device_get_description(device));
//			 break;
//		 case SOUND_SETTINGS_EVENT_TYPE_DEVICE_CHANGED:
//			 g_message("device '%s' (%i) volume: %f", sound_device_get_description(device), sound_device_get_type(device), sound_device_get_volume(device));
//			 break;
//		 case SOUND_SETTINGS_EVENT_TYPE_DEVICE_ADDED:
//			 g_message("device '%s' (%i) added", sound_device_get_description(device), sound_device_get_type(device));
//			 break;
//		 case SOUND_SETTINGS_EVENT_TYPE_DEVICE_REMOVED:
//			 g_message("device '%s' (%i) removed", sound_device_get_description(device), sound_device_get_type(device));
//			 sound_device_unref(device);
//			 break;
//	 }
// }

static void graphene_wm_init_keybindings(GrapheneWM *self)
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
	
	meta_display_add_keybinding(display, "volume-up", keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)on_key_volume_up, self, NULL);
	meta_display_add_keybinding(display, "volume-down", keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)on_key_volume_down, self, NULL);
	meta_display_add_keybinding(display, "volume-mute", keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)on_key_volume_mute, self, NULL);
	
	// meta_display_add_keybinding(display, "backlight-up", keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)on_key_volume_up, self, NULL);
	// meta_display_add_keybinding(display, "backlight-down", keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)on_key_volume_up, self, NULL);
	// 
	// meta_display_add_keybinding(display, "kb-backlight-up", keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)on_key_volume_up, self, NULL);
	// meta_display_add_keybinding(display, "kb-backlight-down", keybindings, META_KEY_BINDING_NONE, (MetaKeyHandlerFunc)on_key_volume_up, self, NULL);

	// meta_keybindings_set_custom_handler("panel-main-menu", (MetaKeyHandlerFunc)on_panel_main_menu, self, NULL);
	// meta_keybindings_set_custom_handler("switch-windows", switch_windows);
	// meta_keybindings_set_custom_handler("switch-applications", switch_windows);
}

static void on_panel_main_menu(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	g_message("panel");
}

static void on_key_volume_up(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	SoundDevice *device = sound_settings_get_active_output_device(self->soundSettings);
	float vol = sound_device_get_volume(device) + 0.08334;
	sound_device_set_volume(device, (vol > 1) ? 1 : vol);
}

static void on_key_volume_down(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	SoundDevice *device = sound_settings_get_active_output_device(self->soundSettings);
	sound_device_set_volume(device, sound_device_get_volume(device) - 0.08334);
}

static void on_key_volume_mute(MetaDisplay *display, MetaScreen *screen, MetaWindow *window, ClutterKeyEvent *event, MetaKeyBinding *binding, GrapheneWM *self)
{
	SoundDevice *device = sound_settings_get_active_output_device(self->soundSettings);
	sound_device_set_muted(device, !sound_device_get_muted(device));
}
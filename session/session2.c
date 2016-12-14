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
 *
 * session.c
 * Session Manager for Graphene Desktop. Follows most of the specifications put
 * forth by GNOME at wiki.gnome.org/Projects/SessionManagement/NewGnomeSession.
 *
 * CSM = Graphene Session Manager (Because GSM = GNOME SM)
 * C is for Carbon because graphene is made of carbon. Get it? Yay.
 *
 * Session Manager Phases:
 * 0. Init
 *    Get a DBus connection and obtain a well-known name for the SM. Export
 *    SM interface. On success, move to Startup. If the connection is ever
 *    lost (in any phase), this is a fatal error, and quit.
 * 1. Startup
 *    Spawn base processes listed in .desktop files. This includes the Panel,
 *    File Manager, etc. Wait for all of these to register or complete before
 *    moving to phase two.
 * 2. Running
 *    Spawn any remaining .desktop files (mostly user-specified startup apps)
 *    and idle. Listen for clients to register/unregister or to set inhibits
 *    on the session.
 * 3. Logout
 *    Triggered by a DBus signal. Send end-session requests to all registered
 *    clients, and wait for them to reject end-session or close. If any clients
 *    reject, return to Running phase and inform user about the client(s).
 */

#include "config.h"
#include "session.h"
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "client.h"
#include "util.h"
#include "status-notifier-watcher.h"
#include <session-dbus-iface.h>
#include <stdio.h>

#define SESSION_DBUS_NAME "org.gnome.SessionManager"
#define SESSION_DBUS_PATH "/org/gnome/SessionManager"
#define SHOW_ALL_OUTPUT TRUE // Set to TRUE for release; FALSE only shows output from .desktop files with 'Graphene-ShowOutput=true'

typedef enum {
	SESSION_PHASE_INIT = 0,
	SESSION_PHASE_STARTUP,
	SESSION_PHASE_RUNNING,
	SESSION_PHASE_LOGOUT,
} SessionPhase;

typedef struct {
	CSMStartupCompleteCallback startupCb;
	CSMQuitCallback quitCb;
	void *cbUserdata;

	GDBusConnection *connection;
	guint dbusNameId;
	DBusSessionManager *dbusSMSkeleton;

	SessionPhase phase;
	GList *clients;
} GrapheneSession;



static gboolean graphene_session_exit_internal(gboolean failed);

static void on_dbus_connection_acquired(GDBusConnection *connection, const gchar *name, void *userdata);
static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, void *userdata);
static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, void *userdata);

static void run_phase(SessionPhase phase);
static void check_startup_complete();

static gboolean on_client_register(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *appId, const gchar *startupId, gpointer userdata);
static void on_client_ready(GrapheneSessionClient *client);
static gboolean on_client_unregister(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *clientObjectPath, gpointer userdata);
static void on_client_complete(GrapheneSessionClient *client);

static void launch_desktop();
static void launch_apps();
static void launch_autostart(GDesktopAppInfo *desktopInfo);



static GrapheneSession *session = NULL;

/*
 * GrapheneSession
 */

void graphene_session_init(CSMStartupCompleteCallback startupCb, CSMQuitCallback quitCb, void *cbUserdata)
{
	if(session || !startupCb || !quitCb)
		return;
	
	session = g_new0(GrapheneSession, 1);
	
	session->startupCb = startupCb;
	session->quitCb = quitCb;
	session->cbUserdata = cbUserdata;

	session->dbusSMSkeleton = dbus_session_manager_skeleton_new();
	session->dbusNameId = g_bus_own_name(G_BUS_TYPE_SESSION,
		SESSION_DBUS_NAME,
		G_BUS_NAME_OWNER_FLAGS_REPLACE,
		on_dbus_connection_acquired,
		on_dbus_name_acquired,
		on_dbus_name_lost,
		NULL,
		NULL);
}

static gboolean graphene_session_exit_internal(gboolean failed)
{
	if(!session)
		return G_SOURCE_REMOVE;
	g_message("Session exiting...");
	if(session->connection && session->dbusNameId)
		g_bus_unown_name(session->dbusNameId);
	session->dbusNameId = 0;

	// This automatically closes the connection, which might be blocking
	g_clear_object(&session->connection);

	g_clear_object(&session->dbusSMSkeleton);
	
	g_list_free_full(session->clients, g_object_unref);
	session->clients = NULL;
	
	CSMQuitCallback quitCb = session->quitCb;
	gpointer cbUserdata = session->cbUserdata;
	g_clear_pointer(&session, g_free);
	
	if(quitCb)
		quitCb(failed, cbUserdata);
	return G_SOURCE_REMOVE;
}

static void graphene_session_exit_internal_on_idle(gboolean failed)
{
	// G_PRIORITY_HIGH-10 is higher than G_PRIORITY_HIGH
	// This "on idle" exit is so that the session can be exited from
	// callbacks, such as DBus method handlers, without breaking things.
	g_idle_add_full(G_PRIORITY_HIGH - 10, (GSourceFunc)graphene_session_exit_internal, GINT_TO_POINTER(failed), NULL);
}

void graphene_session_exit()
{
	if(!session)
		return;

	graphene_session_exit_internal(TRUE);
}

void graphene_session_logout()
{
	if(!session)
		return;

	run_phase(SESSION_PHASE_LOGOUT);
}

static void on_dbus_connection_acquired(GDBusConnection *connection, const gchar *name, void *userdata)
{
	g_message("Acquired DBus connection"); 
	session->connection = connection;

	// Export objects
	if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(session->dbusSMSkeleton), connection, SESSION_DBUS_PATH, NULL))
	{
		g_critical("Failed to export SM dbus object. Aborting SM.");
		graphene_session_exit_internal(TRUE);
		return;
	}

	g_signal_connect(session->dbusSMSkeleton, "handle-register-client", G_CALLBACK(on_client_register), NULL);
	g_signal_connect(session->dbusSMSkeleton, "handle-unregister-client", G_CALLBACK(on_client_unregister), NULL);
}

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, void *userdata)
{
	g_message("Acquired DBus name");
	session->connection = connection;
	run_phase(SESSION_PHASE_STARTUP);
}

static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, void *userdata)
{
	session->connection = connection;

	if(connection == NULL)
	{
		// Lost connection to DBus, nothing to do
		g_critical("Lost DBus connection. Aborting SM.");
		graphene_session_exit_internal(TRUE);
		return;
	}
	
	// Not necessarily fatal if the name is lost but connection isn't, so
	// keep the session alive. No new clients will be able to register, but
	// existing clients should be fine, and logout should work.
	g_critical("Lost DBus name");
}


static void run_phase(SessionPhase phase)
{
	SessionPhase prevPhase = session->phase;
	session->phase = phase;
	switch(phase)
	{
	case SESSION_PHASE_STARTUP:
		if(prevPhase != SESSION_PHASE_INIT)
			return;	
		g_message("Running startup phase");
		//launch_desktop();
		check_startup_complete();
		break;
	case SESSION_PHASE_RUNNING:
		g_message("Running idle phase");
		//if(prevPhase == SESSION_PHASE_STARTUP)
			//launch_apps();
		break;
	case SESSION_PHASE_LOGOUT:
		g_message("Running logout phase");
		graphene_session_exit_internal(FALSE);
		break;
	}
}

static void check_startup_complete()
{
	if(session->phase != SESSION_PHASE_STARTUP)
		return;

	//for(GList *it = session->clients; it != NULL; it = it->next)
	//	if(!graphene_session_client_is_ready(it->data))
	//		return;
	
	run_phase(SESSION_PHASE_RUNNING);
}



/*
 * Client Events
 * Some of these are sent back from the GrapheneSessionClient object, while
 * some are sent from DBus to the Client object. 
 */

static GrapheneSessionClient * find_client_from_given_info(const gchar *id, const gchar *objectPath, const gchar *appId, const gchar *dbusName)
{
	for(GList *clients=session->clients;clients!=NULL;clients=clients->next)
	{
		GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
		
		const gchar *clientId = graphene_session_client_get_id(client);
		const gchar *clientObjectPath = graphene_session_client_get_object_path(client);
		const gchar *clientAppId = graphene_session_client_get_app_id(client);
		const gchar *clientDbusName = graphene_session_client_get_dbus_name(client);
		
		if((clientId && id && g_strcmp0(clientId, id) == 0)
		|| (clientObjectPath && objectPath && g_strcmp0(clientObjectPath, objectPath) == 0)
		|| (clientAppId && appId && g_strcmp0(clientAppId, appId) == 0)
		|| (clientDbusName && dbusName && g_strcmp0(clientDbusName, dbusName) == 0))
			return client;
	}
	
	return NULL;
}

static gboolean on_client_register(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *appId, const gchar *startupId, gpointer userdata)
{
	const gchar *sender = g_dbus_method_invocation_get_sender(invocation);
	GrapheneSessionClient *client = find_client_from_given_info(startupId, NULL, appId, sender);

	if(!client)
	{
		client = graphene_session_client_new(session->connection, NULL);
		g_object_connect(client,
			"signal::complete", on_client_complete, NULL,
			//"signal::end-session-response", on_client_end_session_response, NULL,
			NULL);
		session->clients = g_list_prepend(session->clients, client);
	}

	graphene_session_client_register(client, sender, appId);
	dbus_session_manager_complete_register_client(object, invocation, graphene_session_client_get_object_path(client));
	g_message("Client %s registered.", graphene_session_client_get_best_name(client));
	return TRUE;
}

static void on_client_ready(GrapheneSessionClient *client)
{
	g_message("Client %s is ready.", graphene_session_client_get_best_name(client));
	check_startup_complete();
}

static gboolean on_client_unregister(DBusSessionManager *object, GDBusMethodInvocation *invocation, const gchar *clientObjectPath, gpointer userdata)
{
	GrapheneSessionClient *client = find_client_from_given_info(NULL, clientObjectPath, NULL, NULL);
	if(!client)
		return TRUE;
	g_message("Client %s unregistered.", graphene_session_client_get_best_name(client));
	graphene_session_client_unregister(client);
	dbus_session_manager_complete_unregister_client(object, invocation);
	return TRUE;
}

static void on_client_complete(GrapheneSessionClient *client)
{
	g_message("Client %s is complete.", graphene_session_client_get_best_name(client));
	session->clients = g_list_remove(session->clients, client);
	g_object_unref(client);

	// If all clients die, exit
	// This will happen at the end of a successful Logout
	// Exit on idle because on_client_complete can be called indirectly from
	// on_client_register, a DBus callback.
	if(session->clients == NULL)
		graphene_session_exit_internal_on_idle(FALSE);
}



/*
 * Autostarting Clients
 */

/*
 * Gets a GHashTable of name->GDesktopAppInfo* containing all autostart .desktop files
 * in all system/user config directories. Also includes Graphene-specific .desktop files.
 *
 * Does not include any .desktop files with the Hidden attribute set to true, or
 * any that have the OnlyShowIn attribute set to something other than "Graphene" or "GNOME".
 *
 * Free the returned GHashTable by calling g_hash_table_unref() on the table itself.
 * All keys and values are automatically destroyed.
 */
static GHashTable * list_autostarts() 
{
  GHashTable *desktopInfoTable = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
  
  gchar **configDirsSys = strv_append(g_get_system_config_dirs(), GRAPHENE_DATA_DIR);
  gchar **configDirs = strv_append((const gchar * const *)configDirsSys, g_get_user_config_dir()); // Important that the user config dir comes last (for overwriting)
  g_strfreev(configDirsSys);

  guint numConfigDirs = g_strv_length(configDirs);
  for(guint i=0;i<numConfigDirs;++i)
  {
    gchar *searchPath = g_strconcat(configDirs[i], "/autostart", NULL);
    GFile *dir = g_file_new_for_path(searchPath);
    
    GFileEnumerator *iter = g_file_enumerate_children(dir, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if(!iter)
    {
      g_warning("Failed to search the directory '%s' for .desktop files.", searchPath);
      continue;
    }
    
    GFileInfo *info = NULL;
    while(g_file_enumerator_iterate(iter, &info, NULL, NULL, NULL) && info != NULL)
    {
      const gchar *_name = g_file_info_get_name(info);
      if(!g_str_has_suffix(_name, ".desktop"))
        continue;
        
      gchar *name = g_strdup(_name);
      
      gchar *desktopInfoPath = g_strconcat(searchPath, "/", name, NULL);
      GDesktopAppInfo *desktopInfo = g_desktop_app_info_new_from_filename(desktopInfoPath);
      
      if(desktopInfo)
      {
        // "Hidden should have been called Deleted. ... It's strictly equivalent to the .desktop file not existing at all."
        // https://specifications.freedesktop.org/desktop-entry-spec/latest/ar01s05.html
        gboolean deleted = g_desktop_app_info_get_is_hidden(desktopInfo);
        gboolean shouldShow = g_desktop_app_info_get_show_in(desktopInfo, "GNOME")
                              || g_desktop_app_info_get_show_in(desktopInfo, "Graphene");
                              
        if(deleted || !shouldShow) // Hidden .desktops should be completely ignored
        {
          g_message("Skipping '%s' because it is hidden or not available for Graphene.", name);
          g_object_unref(desktopInfo);
          g_hash_table_remove(desktopInfoTable, name); // Overwrite previous entries of the same name
        }
        else
        {
          g_hash_table_insert(desktopInfoTable, name, desktopInfo); // Overwrite previous entries of the same name
        }
      }
      
      g_free(desktopInfoPath);
      
      info = NULL; // Automatically freed
    }
    
    g_object_unref(iter);
    g_free(searchPath);
  }
  g_strfreev(configDirs);
  return desktopInfoTable;
}

static void launch_desktop()
{
	GHashTable *autostarts = list_autostarts();
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, autostarts);
	while(g_hash_table_iter_next(&iter, &key, &value))
	{    
		GDesktopAppInfo *desktopInfo = G_DESKTOP_APP_INFO(value);
    	gchar *phase = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Phase");
	
		// Just launch all of the startup phases at once. Maybe give it order later,
		// but it doesn't make much difference.
		if(g_strcmp0(phase, "Initialization") == 0
		|| g_strcmp0(phase, "Panel") == 0
		|| g_strcmp0(phase, "Desktop") == 0)
		{
			launch_autostart(desktopInfo);
		}
	}
}

static void launch_apps()
{
	GHashTable *autostarts = list_autostarts();
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, autostarts);
	while(g_hash_table_iter_next(&iter, &key, &value))
	{    
		GDesktopAppInfo *desktopInfo = G_DESKTOP_APP_INFO(value);
    	gchar *phase = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Phase");

		// Only launch applications not launched in launch_desktop
		if(g_strcmp0(phase, "Initialization") != 0
		&& g_strcmp0(phase, "WindowManager") != 0
		&& g_strcmp0(phase, "Panel") != 0
		&& g_strcmp0(phase, "Desktop") != 0)
		{
			launch_autostart(desktopInfo);
		}
	}
}

static void launch_autostart(GDesktopAppInfo *desktopInfo)
{
	GrapheneSessionClient *client = graphene_session_client_new(session->connection, NULL);
	session->clients = g_list_prepend(session->clients, client);

	CSMClientAutoRestart autoRestart;
	autoRestart = g_desktop_app_info_get_boolean(desktopInfo, "X-GNOME-AutoRestart") ? CSM_CLIENT_RESTART_FAIL_ONLY : CSM_CLIENT_RESTART_NEVER;
	if(g_desktop_app_info_has_key(desktopInfo, "Graphene-AutoRestart"))
	{
		autoRestart = CSM_CLIENT_RESTART_NEVER;
		gchar *autoRestartStr = g_desktop_app_info_get_string(desktopInfo, "Graphene-AutoRestart");
		if(g_strcmp0(autoRestartStr, "fail-only") == 0)
			autoRestart = CSM_CLIENT_RESTART_FAIL_ONLY;
		else if(g_strcmp0(autoRestartStr, "always") == 0)
			autoRestart = CSM_CLIENT_RESTART_ALWAYS;
	}

	gchar *delayString = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Delay");
	gint64 delay = 0;
	if(delayString)
		delay = g_ascii_strtoll(delayString, NULL, 0) * 1000; // seconds to milliseconds
	g_free(delayString);

	g_object_connect(client,
		"signal::ready", on_client_ready, NULL,
		"signal::complete", on_client_complete, NULL,
		//"signal::end-session-response", on_client_end_session_response, NULL,
		NULL);
	g_object_set(client,
		"name", g_app_info_get_display_name(G_APP_INFO(desktopInfo)),
		"args", g_app_info_get_commandline(G_APP_INFO(desktopInfo)),
		"auto-restart", autoRestart,
		"silent", SHOW_ALL_OUTPUT ? FALSE : !g_desktop_app_info_get_boolean(desktopInfo, "Graphene-ShowOutput"),
		"delay", delay,
		"condition", g_desktop_app_info_get_string(desktopInfo, "AutostartCondition"),
		NULL);
	
	//graphene_session_client_spawn(client); // Ignored if autostart condition is false
}

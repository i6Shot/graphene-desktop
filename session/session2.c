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
 * session2.c
 * Session manager for Graphene. Launches the panel, window manager, and other tasks, and exits on logout.
 */

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <signal.h>
#include <sys/wait.h>
#include <gtk/gtk.h>

#define SESSION_MANAGER_APP_ID "org.gnome.SessionManager"
#define INHIBITOR_OBJECT_PATH "/org/gnome/SessionManager/Inhibitor"
#define CLIENT_OBJECT_PATH "/org/gnome/SessionManager/Client"
#define SHOW_ALL_OUTPUT TRUE // Set to TRUE for release; FALSE only shows output from .desktop files with 'Graphene-ShowOutput=true'
#define MAX_RESTARTS 5
#define DEBUG TRUE

typedef enum {
  SESSION_PHASE_STARTUP = 0,
  SESSION_PHASE_INITIALIZATION,
  SESSION_PHASE_WINDOWMANAGER,
  SESSION_PHASE_PANEL,
  SESSION_PHASE_DESKTOP,
  SESSION_PHASE_APPLICATION,
  SESSION_PHASE_RUNNING,
  SESSION_PHASE_QUERY_END_SESSION,
  SESSION_PHASE_END_SESSION,
  SESSION_PHASE_EXIT,
  
} SessionPhase;

typedef struct
{
  // Unique client ID. Given to new processes so that they can register themselves properly (startupId).
  // Appended to the end of CLIENT_OBJECT_PATH to make a DBus object path for this
  // client once it's been registered.
  gchar *id;
  
  // Registration info (all of this is only set once the client registers itself)
  gboolean registered; // Set to true if the client reigsters itself via the DBus interface.
  gchar *objectPath; // Convenience: CLIENT_OBJECT_PATH + id
  guint objectRegistrationId;
  guint privateObjectRegistrationId;
  guint busWatchId;
  gchar *appId;
  gchar *dbusName; // The dbus name that the program registered from ("sender")
  
  // Program info
  gchar *launchArgs; // Only set if the SM spawned this process
  GPid processId; // Can be set once the process has spawned if SM-launched, or once the client registers itself
  guint childWatchId;
  gboolean autoRestart;
  guint restartCount; // Number of times the process has crashed and been restarted
  gboolean silent; // If true, redirects stdout and stderr to dev/null
  
  SessionPhase phase; // Phase that this client was added in
  
} Client;

typedef struct
{
  // Appended to the end of INHIBITOR_OBJECT_PATH to make a DBus object path for this
  guint32 id; // "cookie"
  
  Client *client;
  gchar *reason;
  guint flags;
  guint xid;

  guint objectRegistrationId;

} Inhibitor;

typedef struct
{
  GApplication *app;
  guint InterfaceRegistrationId;
  GHashTable *Clients;
  GHashTable *Inhibitors;
  guint32 InhibitCookieCounter;
  SessionPhase phase;
  guint phaseTimerId;
  
} Session;

static void activate(GApplication *app, gpointer userdata);
static void shutdown(GApplication *application, gpointer userdata);
static void on_sigterm_or_sigint(gpointer userdata);
static gboolean run_phase(guint phase);
static Client * add_client(const gchar *id, const gchar *appId, const gchar *dbusName);
static void free_client(Client *client);
static void remove_client(Client *client);
static gchar * register_client(const gchar *sender, const gchar *appId, const gchar *startupId);
static void unregister_client(const gchar *clientObjectPath);
static void on_client_vanished(GDBusConnection *connection, const gchar *name, Client *client);
static void on_client_exit(Client *client, gint status);
static Client * find_client_from_given_info(const gchar *id, const gchar *objectPath, const gchar *appId, const gchar *dbusName);
static GHashTable * list_autostarts();
static void launch_autostart_phase(const gchar *phase, GHashTable *autostarts);
static void launch_process(Client *client, const gchar *args, gboolean autoRestart, guint restartCount, guint delay, gboolean silent);
static void on_process_exit(GPid pid, gint status, Client *client);
static void free_inhibitor(Inhibitor *inhibitor);
static guint32 inhibit(const gchar *sender, const gchar *appId, guint32 toplevelXId, const gchar *reason, guint32 flags);
static void uninhibit(guint32 id);
static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender, const gchar *objectPath, const gchar *interfaceName, const gchar *methodName, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer *userdata);
GVariant *on_dbus_get_property(GDBusConnection *connection,
                                  const gchar *sender,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *property_name,
                                  GError **error,
                                  gpointer user_data);
static gchar ** strv_append(const gchar * const *list, const gchar *str);
static gchar * str_trim(const gchar *str);

static const gchar *SessionManagerInterfaceXML;
static const gchar *ClientInterfaceXML;
static const gchar *InhibitorInterfaceXML;
static GDBusNodeInfo *ClientInterfaceInfo;
static GDBusNodeInfo *InhibitorInterfaceInfo;

static const GDBusInterfaceVTable SessionManagerInterfaceCallbacks = {on_dbus_method_call, on_dbus_get_property, NULL};
static const GDBusInterfaceVTable ClientInterfaceCallbacks = {on_dbus_method_call, on_dbus_get_property, NULL};
static const GDBusInterfaceVTable ClientPrivateInterfaceCallbacks = {on_dbus_method_call, on_dbus_get_property, NULL};
static const GDBusInterfaceVTable InhibitorInterfaceCallbacks = {on_dbus_method_call, on_dbus_get_property, NULL};

static Session *self;
GHashTable *autostarts;

int main(int argc, char **argv)
{
  // Make sure X is running before starting anything
  if(g_getenv("DISPLAY") == NULL)
  {
    g_critical("Cannot start vossession without an active X server. Try running startx, or starting vossession from a login manager such as LightDM.");
    return 1;
  }

#if DEBUG
  g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
#endif

  g_unix_signal_add(SIGTERM, on_sigterm_or_sigint, NULL);
  g_unix_signal_add(SIGINT, on_sigterm_or_sigint, NULL);

  // Start app
  GApplication *app = g_application_new(SESSION_MANAGER_APP_ID, G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}

static void activate(GApplication *app, gpointer userdata)
{
  ClientInterfaceInfo = g_dbus_node_info_new_for_xml(ClientInterfaceXML, NULL);
  InhibitorInterfaceInfo = g_dbus_node_info_new_for_xml(InhibitorInterfaceXML, NULL);

  self = g_new0(Session, 1);
  self->Clients = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_client);
  self->Inhibitors = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_inhibitor);
  self->app = app;
  self->InhibitCookieCounter = 1;
  self->phase = SESSION_PHASE_STARTUP;
  
  // Register Session Banager DBus path 
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(app));
  const gchar *objectPath = g_application_get_dbus_object_path(G_APPLICATION(app));
  const GDBusNodeInfo *interfaceInfo = g_dbus_node_info_new_for_xml(SessionManagerInterfaceXML, NULL);
  if(!interfaceInfo)
    return;
  static const GDBusInterfaceVTable interfaceCallbacks = {on_dbus_method_call, NULL, NULL};
  self->InterfaceRegistrationId = g_dbus_connection_register_object(connection, objectPath, interfaceInfo->interfaces[0], &interfaceCallbacks, NULL, NULL, NULL);
  
  // Run autostarts by phase
  // https://wiki.gnome.org/Projects/SessionManagement/NewGnomeSession
  autostarts = list_autostarts();
  // launch_autostart_phase("Initialization", autostarts);// Important GNOME stuff
  // launch_autostart_phase("WindowManager", autostarts); // This starts graphene-wm
  // launch_autostart_phase("Panel", autostarts);         // This starts graphene-panel
  // launch_autostart_phase("Desktop", autostarts);       // This starts nautilus
  // launch_autostart_phase("Applications", autostarts);  // Everything else
  // g_hash_table_unref(autostarts);
  
  run_phase(SESSION_PHASE_STARTUP);
  
  g_application_hold(app);
}

static void shutdown(GApplication *application, gpointer userdata)
{
  g_free(self);
}

static void on_sigterm_or_sigint(gpointer userdata)
{
  if(self && self->app)
  {
    // TODO: Exit cleanly
    g_debug("handling sigterm/int cleanly");
    g_application_quit(self->app);
  }
  else
  {
    exit(0);
  }
}

static gint end_session_responses = 0;

static gboolean run_phase(guint phase)
{
  g_debug("STARTING PHASE %i", phase);

  self->phase = phase;
  
  if(self->phaseTimerId)
    g_source_remove(self->phaseTimerId);
  self->phaseTimerId = 0;
  
  gint waitTime = 10;
  
  switch(self->phase)
  {
    case SESSION_PHASE_STARTUP:
      run_phase(self->phase+1);
      waitTime = -1;
      break;
    case SESSION_PHASE_INITIALIZATION: // Important GNOME stuff
      launch_autostart_phase("Initialization", autostarts);
      break;
    case SESSION_PHASE_WINDOWMANAGER: // This starts graphene-wm
      launch_autostart_phase("WindowManager", autostarts);
      break;
    case SESSION_PHASE_PANEL: // This starts graphene-panel
      launch_autostart_phase("Panel", autostarts);
      break;
    case SESSION_PHASE_DESKTOP: // This starts nautilus
      launch_autostart_phase("Desktop", autostarts);
      break;
    case SESSION_PHASE_APPLICATION: // Everything else 
      launch_autostart_phase("Applications", autostarts);
      waitTime = 0;
      break;
    case SESSION_PHASE_RUNNING:
      waitTime = -1;
      break;
    case SESSION_PHASE_QUERY_END_SESSION:
      waitTime = 1;
      break;
    case SESSION_PHASE_END_SESSION:
    {
      end_session_responses = 0;
      GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));

      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, self->Clients);
      while(g_hash_table_iter_next(&iter, &key, &value))
      {
        Client *client = (Client *)value;
        if(client && client->objectPath)
        {
          end_session_responses++;
          g_dbus_connection_emit_signal(connection, client->dbusName, client->objectPath, "org.gnome.SessionManager.ClientPrivate", "EndSession", g_variant_new("(u)", TRUE), NULL);
        }
      }
      break;
    }
    case SESSION_PHASE_EXIT:
      g_application_quit(self->app);
      waitTime = -1;
      break;
  }
  
  if(waitTime >= 0)
    self->phaseTimerId = g_timeout_add_seconds(waitTime, run_phase, self->phase+1);
    
  return FALSE; // stops timers
}

static void run_next_phase_if_ready()
{
  switch(self->phase)
  {
    case SESSION_PHASE_STARTUP:
    case SESSION_PHASE_INITIALIZATION:
    case SESSION_PHASE_WINDOWMANAGER:
    case SESSION_PHASE_PANEL:
    case SESSION_PHASE_DESKTOP:
    case SESSION_PHASE_APPLICATION:
    {
      gboolean allRegistered = TRUE;
      
      GHashTableIter iter;
      gpointer key, value;
      g_hash_table_iter_init(&iter, self->Clients);
      while(g_hash_table_iter_next(&iter, &key, &value))
      {
        Client *client = (Client *)value;
        // In order to be counted as ready, the app should have either died, 
        // exited, or registered already. Dead/exited clients get removed 
        // from the list, so if every client left is registered, we're ready.
        if(client->phase == self->phase && !client->registered)
        {
          allRegistered = FALSE;
          break;
        }
      }
      
      if(allRegistered)
      {
        g_debug("Phase %i complete", self->phase);
        g_idle_add(run_phase, self->phase+1);
      }
      break;
    }
    case SESSION_PHASE_QUERY_END_SESSION:
    case SESSION_PHASE_END_SESSION:
      if(end_session_responses == 0)
      {
        g_debug("Phase %i complete", self->phase);
        g_idle_add(run_phase, self->phase+1);
      }
      break;
    default:
      return;
  }
}


static void query_end_session(gboolean force)
{
  run_phase(SESSION_PHASE_QUERY_END_SESSION);

  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  end_session_responses = 0;
  
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, self->Clients);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    Client *client = (Client *)value;
    if(client && client->objectPath)
    {
      end_session_responses++;
      g_dbus_connection_emit_signal(connection, client->dbusName, client->objectPath, "org.gnome.SessionManager.ClientPrivate", "QueryEndSession", g_variant_new("(u)", force == TRUE), NULL);
    }
  }
}

// static void logout()
// {
//   
// }

static gchar * generate_client_id()
{
  // TODO: Generate ID according to XSMP standard
  
  static const guint32 length = 17;
  gchar *id = g_new(gchar, 17+1);
  id[0] = '0';
  id[length] = NULL;
  
  for(guint32 i=1;i<length;++i)
    g_sprintf(id + i, "%x", g_random_int() % 16);
  
  return id;
}

/*
 * Called when a new client is registered, or when the Session Manager launches a client of its own.
 * Uses the id, appId, and dbusName parameters to find an existing client object. All three are optional parameters.
 * If an existing client cannot be found via the parameters, a new client object is created. The new client will use
 * the given id, or if id is NULL, generate a new random id.
 */
static Client * add_client(const gchar *id, const gchar *appId, const gchar *dbusName)
{
  Client *client = find_client_from_given_info(id, NULL, appId, dbusName);
  
  if(!client)
  {
    client = g_new0(Client, 1);
    if(!client)
      return NULL;
    
    if(!id || strlen(id) == 0 )
      client->id = generate_client_id();
    else
      client->id = g_strdup(id);
    
    client->phase = self->phase;
    
    g_hash_table_insert(self->Clients, client->id, client);
    
    g_application_hold(self->app);
    g_debug("Added client with startupId '%s'", client->id);
  }
  
  return client;
}

/*
 * Automatically called by the self->Clients hashtable value destroy function.
 * This should not be called manually! Use remove_client instead.
 */
static void free_client(Client *c)
{
  unregister_client(c->objectPath);
  
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, self->Inhibitors);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    if(value && ((Inhibitor *)value)->client == c)
      uninhibit(((Inhibitor *)value)->id);
  }
  
  // id freed by key destroy function
  if(c->childWatchId)
    g_source_remove(c->childWatchId);
  g_free(c->launchArgs);
  g_free(c);
  g_application_release(self->app);
}

static void remove_client(Client *client)
{
  if(client)
    g_hash_table_remove(self->Clients, client->id);
  run_next_phase_if_ready();
}

/*
 * Directly called from DBus org.gnome.SessionManager.RegisterClient.
 * Registers a client with the given startupId. If the client cannot be found, it is created using add_client.
 * Returns the client's object path. Do not free it yourself; freed when the client is destroyed.
 * sender is the DBus name of the process that is asking to be registered.
 */
static gchar * register_client(const gchar *sender, const gchar *appId, const gchar *startupId)
{
  Client *client = add_client(startupId, appId, sender);
  
  if(!client)
    return "";
  
  client->dbusName = g_strdup(sender);
  client->appId = g_strdup(appId);
  
  // TODO: Make sure everything is up to date instead of just returning the cached value
  if(client->registered)
    return client->objectPath;
    
  if(!ClientInterfaceInfo)
    return "";
  
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  client->objectPath = g_strdup_printf("%s%s", CLIENT_OBJECT_PATH, client->id);

  GError *error = NULL;
  client->objectRegistrationId = g_dbus_connection_register_object(connection, client->objectPath, ClientInterfaceInfo->interfaces[0], &ClientInterfaceCallbacks, client, NULL, &error);
  if(error)
  {
    g_warning("Failed to register client '%s': %s", appId, error->message);
    g_clear_error(&error);
    return "";
  }
  
  client->privateObjectRegistrationId = g_dbus_connection_register_object(connection, client->objectPath, ClientInterfaceInfo->interfaces[1], &ClientPrivateInterfaceCallbacks, client, NULL, error);
  if(error)
  {
    g_warning("Failed to register client private '%s': %s", appId, error->message);
    g_clear_error(&error);
    return "";
  }
  
  if(!client->processId && client->dbusName)
  {
    GVariant *vpid = g_dbus_connection_call_sync(connection,
                       "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                       "GetConnectionUnixProcessID", g_variant_new("(s)", client->dbusName), G_VARIANT_TYPE("(u)"),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if(error)
    {
      g_warning("Failed to obtain process id of '%s': %s", appId, error->message);
      g_clear_error(&error);
    }
    else
    {
      g_variant_get(vpid, "(u)", &client->processId);
      
      gchar *processArgs = NULL;
      g_spawn_command_line_sync(g_strdup_printf("ps --pid %i -o args=", client->processId), &processArgs, NULL, NULL, NULL);
      if(processArgs)
      {
        client->launchArgs = str_trim(processArgs);
        g_debug("Got registered process args: '%s'", client->launchArgs);
        g_free(processArgs);
      }
    }
  }

  client->busWatchId = g_bus_watch_name(G_BUS_TYPE_SESSION, client->dbusName, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, on_client_vanished, client, NULL);
  if(!client->busWatchId)
    g_warning("Failed to watch bus name of process '%s' (%s)", appId, client->dbusName);
    
  g_debug("Registered client '%s' at path '%s'", appId, client->objectPath);
  client->registered = TRUE;
  run_next_phase_if_ready();
  return client->objectPath;
}

/*
 * Directly called from DBus org.gnome.SessionManager.UnregisterClient.
 * Unregisters a client, but does not remove it from the Client hashtable.
 * Note: Don't use client->id in here because unregister_client gets called from free_client
 */
static void unregister_client(const gchar *clientObjectPath)
{
  Client *client = find_client_from_given_info(NULL, clientObjectPath, NULL, NULL);
  if(!client)
    return;
  
  client->registered = FALSE;
  
  g_bus_unwatch_name(client->busWatchId);
  client->busWatchId = 0;
  
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  if(client->objectRegistrationId)
    g_dbus_connection_unregister_object(connection, client->objectRegistrationId);
  client->objectRegistrationId = 0;
  if(client->privateObjectRegistrationId)
    g_dbus_connection_unregister_object(connection, client->privateObjectRegistrationId);
  client->privateObjectRegistrationId = 0;
  
  g_clear_pointer(&client->objectPath, g_free);
  g_clear_pointer(&client->appId, g_free);
  g_clear_pointer(&client->dbusName, g_free);
}

/*
 * Called when a registered client's DBus connection vanishes.
 * This is effectively the same as the process exiting, and therefore the client should be removed.
 */ 
static void on_client_vanished(GDBusConnection *connection, const gchar *name, Client *client)
{
  if(!client)
    return;
  g_debug("client %s, %s vanished", name, client->appId);
  // If the client successfully unregistered itself, then on_client_vanished wouldn't be called.
  // So this is almost certainly an EXIT_FAILURE (1).
  on_client_exit(client, 1);
}

/*
 * Called when an autostarted client process exits, or when a client's DBus connection vanishes.
 * Should auto-restart the client if necessary, or completely abort the session in some severe cases.
 */
static void on_client_exit(Client *client, gint status)
{
  if(!client)
    return;
  
  g_debug("should restart? %i, %i", client->autoRestart, status);

  // Restart it
  if(client->autoRestart && client->launchArgs && status != 0 && self->phase == SESSION_PHASE_RUNNING)
  {
    // Make sure on_client_exit can't be called twice (once from dbus, once from child watch)
    unregister_client(client->objectPath); // Also need to unregister the client anyways
    if(client->childWatchId)
      g_source_remove(client->childWatchId);
    client->childWatchId = 0;
    
    g_debug("restarting client with args %s", client->launchArgs);
    gboolean isPanel = g_str_has_prefix(client->launchArgs, "/usr/share/graphene/graphene-panel");
    if(isPanel && WEXITSTATUS(status) == 120)
    {
      launch_process(client, client->launchArgs, TRUE, 0, 0, client->silent);
    }
    else if(client->restartCount < MAX_RESTARTS)
    {
      launch_process(client, client->launchArgs, TRUE, client->restartCount+1, 0, client->silent);
    }
    else if(isPanel)
    {
      g_critical("The system panel has crashed too many times! Exiting session...");
      // TODO: Quit
      // quit();
    }
    else
    {
      g_critical("The application with args '%s' has crashed too many times, and will not be automatically restarted.", client->launchArgs);
    }
  }
  else
  {
    // If it shouldn't be auto-restarted, then completely remove the client
    remove_client(client);
  }
}

static Client * find_client_from_given_info(const gchar *id, const gchar *objectPath, const gchar *appId, const gchar *dbusName)
{
  GHashTableIter iter;
  gpointer key, value;
  
  g_hash_table_iter_init(&iter, self->Clients);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    Client *client = (Client *)value;
    if(client)
    {
      if((client->id && id && g_strcmp0(client->id, id) == 0) ||
         (client->objectPath && objectPath && g_strcmp0(client->objectPath, objectPath) == 0) ||
         (client->appId && appId && g_strcmp0(client->appId, appId) == 0) ||
         (client->dbusName && dbusName && g_strcmp0(client->dbusName, dbusName) == 0))
        return client;
    }
  }
  
  return NULL;
}



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
  
  gchar **configDirsSys = strv_append(g_get_system_config_dirs(), VDE_DATA_DIR);
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
        
      // TODO: Don't want caribou on right now. Not sure how to stop it without just getting rid of the .desktop file.
      if(g_strcmp0(_name, "caribou-autostart.desktop") == 0)
        continue;
      
      gchar *name = g_strdup(_name); // Not sure if g_file_info_get_name's return value is new memory or belongs to the object. This might be a memory leak.
      
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

/*
 * Launch a specific phase (all autostart .desktop files that have the X-GNOME-Autostart-Phase attribute equal to <phase>)
 * Each launched .desktop is removedd from the autostart array.
 * If <phase> is "Applications", ALL .desktop files are launched.
 */
static void launch_autostart_phase(const gchar *phase, GHashTable *autostarts)
{
  GHashTableIter iter;
  gpointer key, value;
  
  g_hash_table_iter_init(&iter, autostarts);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {    
    GDesktopAppInfo *desktopInfo = G_DESKTOP_APP_INFO(value);
    
    char *thisPhase = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Phase");
    if(g_strcmp0(phase, thisPhase) == 0 || g_strcmp0(phase, "Applications") == 0)
    {
      const gchar *commandline = g_app_info_get_commandline(G_APP_INFO(desktopInfo));
      gboolean autoRestart = g_desktop_app_info_get_boolean(desktopInfo, "X-GNOME-AutoRestart");
      const gchar *delayString = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Delay");
      gint64 delay = 0;
      if(delayString)
        delay = g_ascii_strtoll(delayString, NULL, 0);
      
      gboolean showOutput = g_desktop_app_info_get_boolean(desktopInfo, "Graphene-ShowOutput"); // For debugging, ignore output from unwanted programs
      if(SHOW_ALL_OUTPUT)
        showOutput = TRUE;
        
      launch_process(NULL, commandline, autoRestart, 0, (guint)delay, !showOutput);
      g_hash_table_iter_remove(&iter);
    }
    
    g_free(thisPhase);
  }
  
  run_next_phase_if_ready();
}

static gboolean process_launch_delay_cb(Client *client);

/*
 * Launches a process, and adds it as a client.
 * <args> contains the entire command line.
 * If <autostart> is true, the process will restart automatically if it exits with a non-zero exit status.
 * <delay> number of seconds to wait before starting
 */
static void launch_process(Client *client, const gchar *args, gboolean autoRestart, guint restartCount, guint delay, gboolean silent)
{
  if(!client)
    client = add_client(NULL, NULL, NULL); // This assignes the new process a startupId so that it can correctly register itself later
  if(client->launchArgs != args)
  {
    g_free(client->launchArgs);
    client->launchArgs = g_strdup(args);
  }
  client->autoRestart = autoRestart;
  client->restartCount = restartCount;
  client->silent = silent;
  
  if(delay > 0)
    // TODO: Stop this timeout if the client gets removed before it completes
    g_timeout_add_seconds(delay, process_launch_delay_cb, client);
  else
    process_launch_delay_cb(client);
}

/*
 * Called from launch_process(), and actually does the launching.
 */
static gboolean process_launch_delay_cb(Client *client)
{
  if(!client)
    return;
  
  GSpawnFlags flags = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;
  if(client->silent)
    flags |= G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL;
  
  GPid pid = 0;
  GError *e = NULL;
  
  #if DEBUG
    g_unsetenv("G_MESSAGES_DEBUG"); // Don't let the child process get the DEBUG setting
  #endif
  
  // TODO: Use g_shell_parse_argv
  gchar **argsSplit = g_strsplit(client->launchArgs, " ", -1);
  gchar *startupIdVar = g_strdup_printf("DESKTOP_AUTOSTART_ID=%s", client->id);
  gchar **env = strv_append(g_get_environ(), startupIdVar);
  
  g_spawn_async(NULL, argsSplit, env, flags, NULL, NULL, &pid, &e);
  
  #if DEBUG
    g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
  #endif
  
  g_strfreev(env);
  g_free(startupIdVar);
  g_strfreev(argsSplit);
  
  if(e)
  {
    g_critical("Failed to start process with args '%s' (%s)", client->launchArgs, e->message);
    g_error_free(e);
    return FALSE;
  }
  
  client->processId = pid;
  
  if(client->processId)
    client->childWatchId = g_child_watch_add(client->processId, on_process_exit, client);
  
  return FALSE; // Don't continue to call this callback
}

/*
 * Called on process exit, removes the process as a client
 */
static void on_process_exit(GPid pid, gint status, Client *client)
{
  g_spawn_close_pid(pid);

  if(!client)
    return;
  g_debug("process exit %s, %s, %i", client->id, client->launchArgs, status);
  on_client_exit(client, status);
}



static guint32 inhibit(const gchar *sender, const gchar *appId, guint32 toplevelXId, const gchar *reason, guint32 flags)
{
  register_client(sender, appId, NULL); // Registers it if it wasn't already
  
  Inhibitor *inhibitor = g_new0(Inhibitor, 1);
  inhibitor->client = find_client_from_given_info(NULL, NULL, appId, sender);
  inhibitor->flags = flags;
  inhibitor->id = self->InhibitCookieCounter++;
  inhibitor->reason = g_strdup(reason);
  inhibitor->xid = toplevelXId;
  
  if(!InhibitorInterfaceInfo)
    return 0;
  
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  gchar *objectPath = g_strdup_printf("%s%i", INHIBITOR_OBJECT_PATH, inhibitor->id);
  
  GError *error = NULL;
  inhibitor->objectRegistrationId = g_dbus_connection_register_object(connection, objectPath, InhibitorInterfaceInfo->interfaces[0], &InhibitorInterfaceCallbacks, inhibitor, NULL, &error);
  if(error)
  {
    g_warning("Failed to set inhibit on '%s': %s", appId, error->message);
    g_clear_error(&error);
    free_inhibitor(inhibitor);
    return 0;
  }
  
  g_free(objectPath);
  g_hash_table_insert(self->Inhibitors, inhibitor->id, inhibitor);
  g_debug("Added inhibitor %i for %s,%s becasue of '%s'", inhibitor->id, sender, appId, inhibitor->reason);
  return inhibitor->id;
}

static void uninhibit(guint32 id)
{
  Inhibitor *inhibitor = g_hash_table_lookup(self->Inhibitors, id);
  if(!inhibitor)
    return;
    
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  if(inhibitor->objectRegistrationId)
    g_dbus_connection_unregister_object(connection, inhibitor->objectRegistrationId);
  inhibitor->objectRegistrationId = 0;
  
  g_hash_table_remove(self->Inhibitors, id);
  g_debug("Removed inhibitor %i", id);
}

static void free_inhibitor(Inhibitor *inhibitor)
{
  g_free(inhibitor->reason);
  g_free(inhibitor);
}


static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender,
              const gchar           *objectPath,
              const gchar           *interfaceName,
              const gchar           *methodName,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation,
              gpointer              *userdata)
{
  // TODO: Throw errors for failed calls instead of just returning NULL/0/""?
  
  g_debug("dbus method call: %s, %s.%s", sender, interfaceName, methodName);
  if(g_strcmp0(interfaceName, "org.gnome.SessionManager") == 0)
  {
    if(g_strcmp0(methodName, "RegisterClient") == 0)
    {
      gchar *appId, *startupId;
      g_variant_get(parameters, "(ss)", &appId, &startupId);
      gchar *clientObjectPath = register_client(sender, appId, startupId);
      g_free(appId);
      g_free(startupId);
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", clientObjectPath));
      return;
    }
    else if(g_strcmp0(methodName, "UnregisterClient") == 0)
    {
      gchar *clientObjectPath;
      g_variant_get(parameters, "(o)", &clientObjectPath);
      unregister_client(clientObjectPath);
      g_free(clientObjectPath);
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
    else if(g_strcmp0(methodName, "Inhibit") == 0)
    {
      gchar *appId, *reason;
      guint32 xId, flags;
      g_variant_get(parameters, "(susu)", &appId, &xId, &reason, &flags);
      guint32 cookie = inhibit(sender, appId, xId, reason, flags);
      g_free(appId);
      g_free(reason);
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", cookie));
      return;
    }
    else if(g_strcmp0(methodName, "Uninhibit") == 0)
    {
      guint32 cookie;
      g_variant_get(parameters, "(u)", &cookie);
      uninhibit(cookie);
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
    else if(g_strcmp0(methodName, "Shutdown") == 0)
    {
    }
    else if(g_strcmp0(methodName, "Reboot") == 0)
    {
    }
    else if(g_strcmp0(methodName, "CanShutdown") == 0)
    {
    }
    else if(g_strcmp0(methodName, "Logout") == 0)
    {
      query_end_session(FALSE);
    }
    else if(g_strcmp0(methodName, "IsSessionRunning") == 0)
    {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
  }
  else if(g_strcmp0(interfaceName, "org.gnome.SessionManager.Client") == 0)
  {
    Client *client = (Client*)userdata;
    if(!client)
    {
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
    else if(g_strcmp0(methodName, "GetAppId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", client->appId));
      return;
    }
    else if(g_strcmp0(methodName, "GetStartupId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", client->id));
      return;
    }
    else if(g_strcmp0(methodName, "GetRestartStyleHint") == 0) {
      // 0=never, 1=if-running, 2=anyway, 3=immediately
      // Not sure what 1 or 2 mean, but we'll go with 0 and 3 always (TODO: for now)
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", (guint32)client->autoRestart));
      return;
    }
    else if(g_strcmp0(methodName, "GetUnixProcessId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", (guint32)client->processId));
      return;
    }
    else if(g_strcmp0(methodName, "GetStatus") == 0) {
      // 0=unregistered, 1=registered, 2=finished, 3=failed
      // TODO: status 2 & 3
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", client->registered));
      return;
    }
  }
  else if(g_strcmp0(interfaceName, "org.gnome.SessionManager.ClientPrivate") == 0)
  {
    if(g_strcmp0(methodName, "EndSessionResponse") == 0) {
      end_session_responses--;
      run_next_phase_if_ready();
      return;
    }
  }
  else if(g_strcmp0(interfaceName, "org.gnome.SessionManager.Inhibitor") == 0)
  {
    Inhibitor *inhibitor = (Inhibitor*)userdata;
    if(!inhibitor)
    {
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
    else if(g_strcmp0(methodName, "GetAppId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", inhibitor->client ? inhibitor->client->appId : ""));
      return;
    }
    else if(g_strcmp0(methodName, "GetClientId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", inhibitor->client ? inhibitor->client->objectPath : ""));
      return;
    }
    else if(g_strcmp0(methodName, "GetReason") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", inhibitor->reason));
      return;
    }
    else if(g_strcmp0(methodName, "GetFlags") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", inhibitor->flags));
      return;
    }
    else if(g_strcmp0(methodName, "GetToplevelXid") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", inhibitor->xid));
      return;
    }
  }
  
  g_dbus_method_invocation_return_value(invocation, NULL);
}
GVariant *on_dbus_get_property(GDBusConnection *connection,
                                  const gchar *sender,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *property_name,
                                  GError **error,
                                  gpointer user_data)
{
  g_debug("dbus get property: %s, %s.%s", sender, interface_name, property_name);
}

static const gchar *SessionManagerInterfaceXML =
"<node>"
"  <interface name='org.gnome.SessionManager'>"
"    <method name='Setenv'>"
"      <arg type='s' direction='in' name='variable'/>"
"      <arg type='s' direction='in' name='value'/>"
"    </method>"
"    <method name='GetLocale'>"
"      <arg type='i' direction='in' name='category'/>"
"      <arg type='s' direction='out' name='value'/>"
"    </method>"
"    <method name='InitializationError'>"
"      <arg type='s' direction='in' name='message'/>"
"      <arg type='b' direction='in' name='fatal'/>"
"    </method>"
"    <method name='RegisterClient'>"
"      <arg type='s' direction='in' name='app_id'/>"
"      <arg type='s' direction='in' name='client_startup_id'/>"
"      <arg type='o' direction='out' name='client_id'/>"
"    </method>"
"    <method name='UnregisterClient'>"
"      <arg type='o' direction='in' name='client_id'/>"
"    </method>"
"    <method name='Inhibit'>"
"      <arg type='s' direction='in' name='app_id'/>"
"      <arg type='u' direction='in' name='toplevel_xid'/>"
"      <arg type='s' direction='in' name='reason'/>"
"      <arg type='u' direction='in' name='flags'/>"
"      <arg type='u' direction='out' name='cookie'/>"
"    </method>"
"    <method name='Uninhibit'>"
"      <arg type='u' direction='in' name='inhibit_cookie'/>"
"    </method>"
"    <method name='IsInhibited'>"
"      <arg type='u' direction='in' name='flags'/>"
"      <arg type='u' direction='out' name='is_inhibited'/>"
"    </method>"
"    <method name='GetClients'>"
"      <arg type='ao' direction='out' name='clients'/>"
"    </method>"
"    <method name='GetInhibitors'>"
"      <arg type='ao' direction='out' name='inhibitors'/>"
"    </method>"
"    <method name='IsAutostartConditionHandled'>"
"      <arg type='s' direction='in' name='condition'/>"
"      <arg type='b' direction='out' name='handled'/>"
"    </method>"
"    <method name='Shutdown'> </method>"
"    <method name='Reboot'> </method>"
"    <method name='CanShutdown'>"
"      <arg type='b' direction='out' name='is_available'/>"
"    </method>"
"    <method name='Logout'>"
"      <arg type='u' direction='in' name='mode'/>"
"    </method>"
"    <method name='IsSessionRunning'>"
"      <arg type='b' direction='out' name='running'/>"
"    </method>"
"    <signal name='ClientAdded'>"
"      <arg type='o' name='id'/>"
"    </signal>"
"    <signal name='ClientRemoved'>"
"      <arg type='o' name='id'/>"
"    </signal>"
"    <signal name='InhibitorAdded'>"
"      <arg type='o' name='id'/>"
"    </signal>"
"    <signal name='InhibitorRemoved'>"
"      <arg type='o' name='id'/>"
"    </signal>"
"    <signal name='SessionRunning'></signal>"
"    <signal name='SessionOver'></signal>"
"    <property name='SessionName' type='s' access='read'> </property>"
"    <property name='SessionIsActive' type='b' access='read'> </property>"
"    <property name='InhibitedActions' type='u' access='read'> </property>"
"  </interface>"
"</node>";

static const gchar *ClientInterfaceXML =
"<node>"
"  <interface name='org.gnome.SessionManager.Client'>"
"    <method name='GetAppId'>            <arg type='s' direction='out' name='app_id'/>     </method>"
"    <method name='GetStartupId'>        <arg type='s' direction='out' name='startup_id'/> </method>"
"    <method name='GetRestartStyleHint'> <arg type='u' direction='out' name='hint'/>       </method>"
"    <method name='GetUnixProcessId'>    <arg type='u' direction='out' name='pid'/>        </method>"
"    <method name='GetStatus'>           <arg type='u' direction='out' name='status'/>     </method>"
"    <method name='Stop'> </method>"
"  </interface>"
"  <interface name='org.gnome.SessionManager.ClientPrivate'>"
"    <method name='EndSessionResponse'>"
"      <arg type='b' direction='in' name='is_ok'/>"
"      <arg type='s' direction='in' name='reason'/>"
"    </method>"
"    <signal name='Stop'> </signal>"
"    <signal name='QueryEndSession'>  <arg type='u' name='flags'/> </signal>"
"    <signal name='EndSession'>       <arg type='u' name='flags'/> </signal>"
"    <signal name='CancelEndSession'> <arg type='u' name='flags'/> </signal>"
"  </interface>"
"</node>";

static const gchar *InhibitorInterfaceXML =
"<node>"
"  <interface name='org.gnome.SessionManager.Inhibitor'>"
"    <method name='GetAppId'>       <arg type='s' direction='out' name='app_id'/>    </method>"
"    <method name='GetClientId'>    <arg type='o' direction='out' name='client_id'/> </method>"
"    <method name='GetReason'>      <arg type='s' direction='out' name='reason'/>    </method>"
"    <method name='GetFlags'>       <arg type='u' direction='out' name='flags'/>     </method>"
"    <method name='GetToplevelXid'> <arg type='u' direction='out' name='xid'/>       </method>"
"  </interface>"
"</node>";

/*
 * Takes a list of strings and a string to append.
 * If <list> is NULL, a new list of strings is returned containing only <str>.
 * If <str> is NULL, a duplicated <list> is returned.
 * If both are NULL, a new, empty list of strings is returned.
 * The returned list is NULL-terminated (even if both parameters are NULL).
 * The returned list should be freed with g_strfreev().
 */
static gchar ** strv_append(const gchar * const *list, const gchar *str)
{
  guint listLength = 0;
  
  if(list != NULL)
  {
    while(list[listLength])
      ++listLength;
  }
  
  gchar **newList = g_new(gchar*, listLength + ((str==NULL)?0:1) + 1); // +1 for new str, +1 for ending NULL
  
  for(guint i=0;i<listLength;++i)
    newList[i] = g_strdup(list[i]);
    
  if(str != NULL)
    newList[listLength] = g_strdup(str);
    
  newList[listLength + ((str==NULL)?0:1)] = NULL;
  
  return newList;
}

/*
 * Removes trailing and leading whitespace from a string.
 * Returns a newly allocated string. Parameter is unmodified.
 */
static gchar * str_trim(const gchar *str)
{
  if(!str)
    return NULL;
    
  guint32 len=0;
  for(;str[len]!=NULL;++len);
  
  guint32 start=0;
  for(;str[start]!=NULL;++start)
    if(!g_ascii_isspace(str[start]))
      break;
      
  guint32 end=len;
  for(;end>start;--end)
    if(!g_ascii_isspace(str[end-1]))
      break;
      
  gchar *newstr = g_new(gchar, end-start+1);
  for(guint32 i=0;(start+i)<end;++i)
    newstr[i] = str[start+i];
  newstr[end-start]=NULL;
  return newstr;
}
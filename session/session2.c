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
#define SHOW_ALL_OUTPUT FALSE // Set to TRUE for release; FALSE only shows output from .desktop files with 'Graphene-ShowOutput=true'

typedef struct
{
  // Unique client ID. Given to new processes so that they can register themselves properly.
  // Appended to the end of CLIENT_OBJECT_PATH to make a DBus object path for this
  // client once it's been registered.
  gchar *id;
  
  // Registration info
  gboolean registered; // Set to true if the client reigsters itself via the DBus interface.
  gchar *objectPath; // Convenience: CLIENT_OBJECT_PATH + id
  guint objectRegistrationId; 
  guint privateObjectRegistrationId;
  gchar *appId;
  
} Client;

typedef struct
{
  gchar *inhibitID;
  
  gchar *clientID;
  gchar *reason;
  guint flags;
  guint xid;
  
} Inhibitor;

typedef struct
{
  GtkApplication *app;
  guint InterfaceRegistrationId;
  GHashTable *Clients;

} Session;

static void activate(GtkApplication *app, gpointer userdata);
static void shutdown(GtkApplication *application, gpointer userdata);
static Client * add_client(const gchar *startupId, gboolean *existed);
static void remove_client(Client *c);
static gchar * register_client(const gchar *appId, const gchar *startupId);
static void unregister_client(const gchar *clientObjectPath);
static Client * find_client_by_object_path(const gchar *clientObjectPath);
static GHashTable * list_autostarts();
static void launch_autostart_phase(const gchar *phase, GHashTable *autostarts);
static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender,
              const gchar           *objectPath,
              const gchar           *interfaceName,
              const gchar           *methodName,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation,
              gpointer              *userdata);
static gchar ** strv_append(const gchar * const *list, const gchar *str);

static const gchar *SessionManagerInterfaceXML;
static const gchar *ClientInterfaceXML;
static GDBusNodeInfo *ClientInterfaceInfo;

static const GDBusInterfaceVTable SessionManagerInterfaceCallbacks = {on_dbus_method_call, NULL, NULL};
static const GDBusInterfaceVTable ClientInterfaceCallbacks = {on_dbus_method_call, NULL, NULL};
static const GDBusInterfaceVTable ClientPrivateInterfaceCallbacks = {on_dbus_method_call, NULL, NULL};

static Session *self;

int main(int argc, char **argv)
{
  // Make sure X is running before starting anything
  if(g_getenv("DISPLAY") == NULL)
  {
    g_critical("Cannot start vossession without an active X server. Try running startx, or starting vossession from a login manager such as LightDM.");
    return 1;
  }

  // g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

  // Start app
  GtkApplication *app = gtk_application_new(SESSION_MANAGER_APP_ID, G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}

static void activate(GtkApplication *app, gpointer userdata)
{
  ClientInterfaceInfo = g_dbus_node_info_new_for_xml(ClientInterfaceXML, NULL);
  
  self = g_new0(Session, 1);
  self->Clients = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, remove_client);
  self->app = app;
  
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
  GHashTable *autostarts = list_autostarts();
  launch_autostart_phase("Initialization", autostarts);// Important GNOME stuff
  launch_autostart_phase("WindowManager", autostarts); // This starts graphene-wm
  launch_autostart_phase("Panel", autostarts);         // This starts graphene-panel
  launch_autostart_phase("Desktop", autostarts);       // This starts nautilus
  launch_autostart_phase("Applications", autostarts);  // Everything else
  g_hash_table_unref(autostarts);
}

static void shutdown(GtkApplication *application, gpointer userdata)
{
  g_free(self);
}

static void quit()
{
  
}



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
 * If the client is already added, it is not added again.
 * A client can be removed by removing its entry in the self->Clients hashtable. It's data will
 * automatically be freed.
 */
static Client * add_client(const gchar *startupId, gboolean *existed)
{
  g_debug("Adding client with startupId '%s'", startupId);
  Client *client = (Client *)g_hash_table_lookup(self->Clients, startupId);
  
  if(existed)
    *existed = (client != NULL);
  
  if(!client)
  {
    client = g_new0(Client, 1);
    if(!client)
      return;
    
    if(!startupId || strlen(startupId) == 0 )
      client->id = generate_client_id();
    else
      client->id = g_strdup(startupId);
    
    g_hash_table_insert(self->Clients, client->id, client);
    
    g_application_hold(self->app);
  }
  return client;
}

/*
 * Automatically called by the self->Clients hashtable value destroy function.
 * This should not be called manually! Remove the client from the hastable instead.
 */
static void remove_client(Client *c)
{
  unregister_client(c->objectPath);
  
  // id freed by key destroy function
  g_free(c->appId);
  g_free(c->objectPath);
  g_free(c);
  g_application_release(self->app);
}

/*
 * Directly called from DBus org.gnome.SessionManager.RegisterClient.
 * Registers a client with the given startupId. If the client cannot be found, it is created using add_client.
 * Returns the client's object path. Do not free it yourself; freed when the client is destroyed.
 */
static gchar * register_client(const gchar *appId, const gchar *startupId)
{
  g_debug("Registering client '%s'", appId);
  Client *client = add_client(startupId, NULL);
  
  if(!client)
    return "";
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
    g_error_free(error);
    return "";
  }
  
  client->privateObjectRegistrationId = g_dbus_connection_register_object(connection, client->objectPath, ClientInterfaceInfo->interfaces[1], &ClientPrivateInterfaceCallbacks, client, NULL, error);
  if(error)
  {
    g_warning("Failed to register client private '%s': %s", appId, error->message);
    g_error_free(error);
    return "";
  }
  
  g_debug("Registered client at path '%s'", client->objectPath);
  client->registered = TRUE;
  client->appId = g_strdup(appId);
  return client->objectPath;
}

/*
 * Directly called from DBus org.gnome.SessionManager.UnregisterClient.
 * Unregisters a client, but does not remove it from the Client hashtable.
 */
static void unregister_client(const gchar *clientObjectPath)
{
  Client *client = find_client_by_object_path(clientObjectPath);
  if(!client)
    return;
  
  client->registered = FALSE;
  
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  g_dbus_connection_unregister_object(connection, client->objectRegistrationId);
  g_dbus_connection_unregister_object(connection, client->privateObjectRegistrationId);
  client->objectRegistrationId = 0;
  client->privateObjectRegistrationId = 0;
  
  g_clear_pointer(&client->objectPath, g_free);
  g_clear_pointer(&client->appId, g_free);
}

static Client * find_client_by_object_path(const gchar *clientObjectPath)
{
  GHashTableIter iter;
  gpointer key, value;
  
  g_hash_table_iter_init(&iter, self->Clients);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    Client *client = (Client *)value;
    if(client && client->objectPath && g_strcmp0(client->objectPath, clientObjectPath) == 0)
      return client;
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
  // TODO: Wait for phase to complete loading before returning / contining to the next phase
  
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
        
      g_message("launch process %s, %i, %i, %i, %i", commandline, autoRestart, 0, (guint)delay, !showOutput);
      g_hash_table_iter_remove(&iter);
    }
    
    g_free(thisPhase);
  }
}







static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender,
              const gchar           *objectPath,
              const gchar           *interfaceName,
              const gchar           *methodName,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation,
              gpointer              *userdata)
{
  g_message("method call: %s.%s", interfaceName, methodName);
  if(g_strcmp0(interfaceName, "org.gnome.SessionManager") == 0)
  {
    if(g_strcmp0(methodName, "RegisterClient") == 0)
    {
      gchar *appId, *startupId;
      g_variant_get(parameters, "(ss)", &appId, &startupId);
      gchar *clientObjectPath = register_client(appId, startupId);
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
  }
  else if(g_strcmp0(interfaceName, "org.gnome.SessionManager.Client") == 0)
  {
    Client *client = (Client*)userdata;
    if(!client)
    {
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }

    if(g_strcmp0(methodName, "GetAppId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", client->appId));
      return;
    }
    else if(g_strcmp0(methodName, "GetStartupId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", client->id));
      return;
    }
    else if(g_strcmp0(methodName, "GetRestartStyleHint") == 0) {
      // TODO
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 0));
      return;
    }
    else if(g_strcmp0(methodName, "GetUnixProcessId") == 0) {
      // TODO
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 0));
      return;
    }
    else if(g_strcmp0(methodName, "GetStatus") == 0) {
      // TODO
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 0));
      return;
    }
  }
  
  g_dbus_method_invocation_return_value(invocation, NULL);
}


static const gchar *SessionManagerInterfaceXML =
"<node>"
"  <interface name='org.gnome.SessionManager'>"
"    <method name='Setenv'>"
"      <arg type='s' direction='in' name='variable'/>"
"      <arg type='s' direction='in' name='value'/>"
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
"    <method name='Shutdown'></method>"
"    <method name='CanShutdown'>"
"      <arg type='b' direction='out' name='is_available'/>"
"    </method>"
"    <method name='Logout'>"
"      <arg type='u' direction='in' name='mode'/>"
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
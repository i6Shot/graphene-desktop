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
 */

#include "client.h"
#include <sys/wait.h>

struct _GrapheneSessionClient
{
  GObject parent;

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
  
  GDBusConnection *connection;
  
  gboolean stopping;
};

enum
{
  PROP_0,
  PROP_BUS_CONNECTION,
  PROP_ID,
  PROP_LAUNCH_ARGS,
  PROP_SILENT,
  PROP_AUTO_RESTART,
  PROP_REGISTERED,
  PROP_LAUNCHED,
  PROP_LAST
};

enum
{
  SIGNAL_0,
  SIGNAL_READY,
  SIGNAL_EXIT,
  SIGNAL_END_SESSION_RESPONSE,
  SIGNAL_LAST
};

#define CLIENT_OBJECT_PATH "/org/gnome/SessionManager/Client"
#define MAX_RESTARTS 5

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

static const gchar *ClientInterfaceXML;
static void graphene_session_client_constructed(GrapheneSessionClient *self);
static void graphene_session_client_finalize(GrapheneSessionClient *self);
static void graphene_session_client_set_property(GrapheneSessionClient *self, guint propertyId, const GValue *value, GParamSpec *pspec);
static void graphene_session_client_get_property(GrapheneSessionClient *self, guint propertyId, GValue *value, GParamSpec *pspec);
static gchar * generate_client_id();
void graphene_session_client_register(GrapheneSessionClient *self, const gchar *sender, const gchar *appId);

void graphene_session_client_unregister(GrapheneSessionClient *self);
void graphene_session_client_spawn(GrapheneSessionClient *self, guint delay);
static void graphene_session_client_spawn_delay_cb(GrapheneSessionClient *self);
static void on_process_exit(GPid pid, gint status, GrapheneSessionClient *self);
static void on_client_vanished(GDBusConnection *connection, const gchar *name, GrapheneSessionClient *self);
static void on_client_exit(GrapheneSessionClient *self, guint status);

static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender,
              const gchar           *objectPath,
              const gchar           *interfaceName,
              const gchar           *methodName,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation,
              gpointer              *userData);
static gchar ** strv_append(const gchar * const *list, const gchar *str);
static gchar * str_trim(const gchar *str);

static const GDBusInterfaceVTable ClientInterfaceCallbacks = {on_dbus_method_call, NULL, NULL};
static const GDBusInterfaceVTable ClientPrivateInterfaceCallbacks = {on_dbus_method_call, NULL, NULL};
static const gchar *ClientInterfaceXML;
static GDBusNodeInfo *ClientInterfaceInfo;    


G_DEFINE_TYPE(GrapheneSessionClient, graphene_session_client, G_TYPE_OBJECT);

static void graphene_session_client_class_init(GrapheneSessionClientClass *klass)
{
  ClientInterfaceInfo = g_dbus_node_info_new_for_xml(ClientInterfaceXML, NULL);

  GObjectClass *objectClass = G_OBJECT_CLASS(klass);
  objectClass->constructed = graphene_session_client_constructed;
  objectClass->finalize = graphene_session_client_finalize;
  objectClass->set_property = graphene_session_client_set_property;
  objectClass->get_property = graphene_session_client_get_property;
  
  properties[PROP_BUS_CONNECTION] = 
    g_param_spec_object("bus", "bus", "bus connection", G_TYPE_DBUS_CONNECTION, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_ID] = 
    g_param_spec_string("id", "id", "aka startup id", NULL, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_LAUNCH_ARGS] = 
    g_param_spec_string("launch-args", "launch args", "launch args", NULL, G_PARAM_READWRITE);

  properties[PROP_SILENT] = 
    g_param_spec_boolean("silent", "silent", "if all output is redirected to /dev/null", FALSE, G_PARAM_READWRITE);

  properties[PROP_AUTO_RESTART] =
    g_param_spec_boolean("auto-restart", "auto restart", "if the client should auto restart", FALSE, G_PARAM_READWRITE);
  
  properties[PROP_REGISTERED] =
    g_param_spec_boolean("registered", "registered", "if the client has been registered", FALSE, G_PARAM_READABLE);

  properties[PROP_LAUNCHED] =
    g_param_spec_boolean("launched", "launched", "if the client has been launched", FALSE, G_PARAM_READABLE);

  g_object_class_install_properties(objectClass, PROP_LAST, properties);

  /*
   * Emitted when the client is ready for the next phase of the SM's startup process.
   * This can happen when the client process exits, fails, or registers.
   * If the client exits to be ready, 'exit' (or 'fail') will be emitted immediately before 'ready'.
   */ 
  signals[SIGNAL_READY] = g_signal_new("ready", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    NULL, NULL, NULL, NULL, G_TYPE_NONE, 0);
  
  /*
   * Emitted when the client exits, vanishes, or unregisters itself. Not called if the client is successfully auto-restarted.
   * The first parameter is TRUE if the client exited successfully, FALSE otherwise.
   * It is safe to release this client object on this signal (assuming its the last callback).
   */
  signals[SIGNAL_EXIT] = g_signal_new("exit", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    NULL, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  
  /*
   * Emitted from a DBus call to org.gnome.SessionManager.ClientPrivate.EndSessionResponse.
   * First parameter is isOk, if it is okay to proceed with end session.
   * Second parameter is reason, which specifies a reason for not proceeding if isOk is false.
   */
  signals[SIGNAL_EXIT] = g_signal_new("end-session-response", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    NULL, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_STRING);
}

GrapheneSessionClient* graphene_session_client_new(GDBusConnection *connection, const gchar *clientId)
{
  return GRAPHENE_SESSION_CLIENT(g_object_new(GRAPHENE_TYPE_SESSION_CLIENT, "bus", connection, "id", clientId, NULL));
}

static void graphene_session_client_init(GrapheneSessionClient *self)
{
  self->childWatchId = 0;
}

static void graphene_session_client_constructed(GrapheneSessionClient *self)
{
  G_OBJECT_CLASS(graphene_session_client_parent_class)->constructed(G_OBJECT(self));
}

static void graphene_session_client_finalize(GrapheneSessionClient *self)
{
  graphene_session_client_unregister(self);
  
  if(self->childWatchId)
    g_source_remove(self->childWatchId);
  g_clear_pointer(&self->launchArgs, g_free);
  g_clear_pointer(&self->id, g_free);

  G_OBJECT_CLASS(graphene_session_client_parent_class)->finalize(G_OBJECT(self));
}

static void graphene_session_client_set_property(GrapheneSessionClient *self, guint propertyId, const GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
    
  switch(propertyId)
  {
  case PROP_BUS_CONNECTION:
    self->connection = G_DBUS_CONNECTION(g_value_get_object(value));
    break;
  case PROP_ID:
    g_clear_pointer(&self->id, g_free);
    const gchar *valStr = g_value_get_string(value);
    self->id = valStr ? g_strdup(valStr) : generate_client_id();
    break;
  case PROP_LAUNCH_ARGS:
    g_clear_pointer(&self->launchArgs, g_free);
    self->launchArgs = g_strdup(g_value_get_string(value));
    break;
  case PROP_SILENT:
    self->silent = g_value_get_boolean(value);
    break;
  case PROP_AUTO_RESTART:
    self->autoRestart = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
    break;
  }
}

static void graphene_session_client_get_property(GrapheneSessionClient *self, guint propertyId, GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
  
  switch(propertyId)
  {
  case PROP_BUS_CONNECTION:
    g_value_set_object(value, self->connection);
    break;
  case PROP_ID:
    g_value_set_string(value, self->id);
    break;
  case PROP_LAUNCH_ARGS:
    g_value_set_string(value, self->launchArgs);
    break;
  case PROP_SILENT:
    g_value_set_boolean(value, self->silent);
    break;
  case PROP_AUTO_RESTART:
    g_value_set_boolean(value, self->autoRestart);
    break;
  case PROP_REGISTERED:
    g_value_set_boolean(value, self->registered);
    break;
  case PROP_LAUNCHED:
    // TODO
    g_value_set_boolean(value, FALSE);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
    break;
  }
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

void graphene_session_client_register(GrapheneSessionClient *self, const gchar *sender, const gchar *appId)
{
  g_clear_pointer(&self->dbusName, g_free);
  g_clear_pointer(&self->appId, g_free);
  self->dbusName = g_strdup(sender);
  self->appId = g_strdup(appId);
  
  // TODO: Make sure everything is up to date instead of just returning the cached value
  if(self->registered)
    return self->objectPath;
    
  if(!ClientInterfaceInfo)
    return "";
  
  self->objectPath = g_strdup_printf("%s%s", CLIENT_OBJECT_PATH, self->id);
  
  GError *error = NULL;
  self->objectRegistrationId = g_dbus_connection_register_object(self->connection, self->objectPath, ClientInterfaceInfo->interfaces[0], &ClientInterfaceCallbacks, self, NULL, &error);
  if(error)
  {
    g_warning("Failed to register client '%s': %s", appId, error->message);
    g_clear_error(&error);
    return "";
  }
  
  self->privateObjectRegistrationId = g_dbus_connection_register_object(self->connection, self->objectPath, ClientInterfaceInfo->interfaces[1], &ClientPrivateInterfaceCallbacks, self, NULL, error);
  if(error)
  {
    g_warning("Failed to register client private '%s': %s", appId, error->message);
    g_clear_error(&error);
    return "";
  }
  
  if(!self->processId && self->dbusName)
  {
    GVariant *vpid = g_dbus_connection_call_sync(self->connection,
                       "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                       "GetConnectionUnixProcessID", g_variant_new("(s)", self->dbusName), G_VARIANT_TYPE("(u)"),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if(error)
    {
      g_warning("Failed to obtain process id of '%s': %s", appId, error->message);
      g_clear_error(&error);
    }
    else
    {
      g_variant_get(vpid, "(u)", &self->processId);
      
      gchar *processArgs = NULL;
      g_spawn_command_line_sync(g_strdup_printf("ps --pid %i -o args=", self->processId), &processArgs, NULL, NULL, NULL);
      if(processArgs)
      {
        self->launchArgs = str_trim(processArgs);
        g_free(processArgs);
        g_object_notify(self, "launch-args");
        g_debug("Got registered process args: '%s'", self->launchArgs);
      }
    }
  }

  self->busWatchId = g_bus_watch_name(G_BUS_TYPE_SESSION, self->dbusName, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, on_client_vanished, self, NULL);
  if(!self->busWatchId)
    g_warning("Failed to watch bus name of process '%s' (%s)", appId, self->dbusName);
    
  self->registered = TRUE;
  g_debug(" + Registered client '%s' at path '%s'", appId, self->objectPath);
  g_object_notify(self, "registered");
  g_signal_emit_by_name(self, "ready");
}

void graphene_session_client_unregister(GrapheneSessionClient *self)
{
  self->registered = FALSE;
  
  if(self->busWatchId)
    g_bus_unwatch_name(self->busWatchId);
  self->busWatchId = 0;
  
  if(self->objectRegistrationId)
    g_dbus_connection_unregister_object(self->connection, self->objectRegistrationId);
  self->objectRegistrationId = 0;
  if(self->privateObjectRegistrationId)
    g_dbus_connection_unregister_object(self->connection, self->privateObjectRegistrationId);
  self->privateObjectRegistrationId = 0;
  
  g_clear_pointer(&self->objectPath, g_free);
  g_clear_pointer(&self->appId, g_free);
  g_clear_pointer(&self->dbusName, g_free);
  
  g_object_notify(self, "registered");
}

void graphene_session_client_spawn(GrapheneSessionClient *self, guint delay)
{
  if(delay > 0)
    // TODO: Stop this timeout if the client gets removed before it completes
    g_timeout_add_seconds(delay, graphene_session_client_spawn_delay_cb, self);
  else
    graphene_session_client_spawn_delay_cb(self);
}

static void graphene_session_client_spawn_delay_cb(GrapheneSessionClient *self)
{
  if(!self)
    return;
  
  if(!self->launchArgs)
  {
    g_warning("Cannot spawn client %s because launchArgs is not set", self->id);
  }    
  
  GSpawnFlags flags = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;
  if(self->silent)
    flags |= G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL;
  
  GPid pid = 0;
  GError *e = NULL;
  
  // #if DEBUG
    g_unsetenv("G_MESSAGES_DEBUG"); // Don't let the child process get the DEBUG setting
  // #endif
  
  // TODO: Use g_shell_parse_argv
  gchar **argsSplit = g_strsplit(self->launchArgs, " ", -1);
  gchar *startupIdVar = g_strdup_printf("DESKTOP_AUTOSTART_ID=%s", self->id);
  gchar **env = strv_append(g_get_environ(), startupIdVar);
  
  g_spawn_async(NULL, argsSplit, env, flags, NULL, NULL, &pid, &e);
  
  // #if DEBUG
    g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
  // #endif
  
  g_strfreev(env);
  g_free(startupIdVar);
  g_strfreev(argsSplit);
  
  if(e)
  {
    g_critical("Failed to start process with args '%s' (%s)", self->launchArgs, e->message);
    g_error_free(e);
    return FALSE;
  }
  
  self->processId = pid;
  
  if(self->processId)
    self->childWatchId = g_child_watch_add(self->processId, on_process_exit, self);
  
  g_debug(" + Spawned client with args '%s' with id '%s' and pId %i", self->launchArgs, self->id, self->processId);
  
  return FALSE; // Don't continue to call this callback
}

static void on_process_exit(GPid pid, gint status, GrapheneSessionClient *self)
{
  if(!self)
    return;
  g_debug(" - Process %i, %s exited", pid, self->id);
  on_client_exit(self, status);
}

/*
 * Called when a registered client's DBus connection vanishes.
 * This is effectively the same as the process exiting, and therefore the client should be removed.
 */ 
static void on_client_vanished(GDBusConnection *connection, const gchar *name, GrapheneSessionClient *self)
{
  if(!self)
    return;
  g_debug(" - Client %s, %s vanished", name, self->appId);
  // If the client successfully unregistered itself, then on_client_vanished wouldn't be called.
  // So this is almost certainly an EXIT_FAILURE (1).
  on_client_exit(self, 1);
}

/*
 * Called when an autostarted client process exits, or when a client's DBus connection vanishes.
 * Should auto-restart the client if necessary, or completely abort the session in some severe cases.
 */
static void on_client_exit(GrapheneSessionClient *self, guint status)
{
  gboolean wasRegistered = self->registered;
  
  // Make sure on_client_exit can't be called twice (once from dbus, once from child watch)
  graphene_session_client_unregister(self); // Needs to be unregistered anyways
  if(self->childWatchId)
    g_source_remove(self->childWatchId);
  self->childWatchId = 0;
  
  // Restart it
  g_debug("should restart? %i, %s, %i, %i", self->autoRestart, self->launchArgs, status, self->stopping);
  if(self->autoRestart && self->launchArgs && status != 0 && !self->stopping)
  {
    g_debug("restarting client with args %s", self->launchArgs);
    gboolean isPanel = g_str_has_prefix(self->launchArgs, "/usr/share/graphene/graphene-panel");
    if(isPanel && WEXITSTATUS(status) == 120)
    {
      graphene_session_client_spawn(self, 0);
    }
    else if(self->restartCount < MAX_RESTARTS)
    {
      self->restartCount++;
      graphene_session_client_spawn(self, 0);
    }
    else
    {
      g_warning("The application with args '%s' has crashed too many times, and will not be automatically restarted.", self->launchArgs);
      g_signal_emit_by_name(self, "exit", FALSE);
    }
  }
  else
  {
    if(!wasRegistered && !self->stopping)
      g_signal_emit_by_name(self, "ready");
    g_signal_emit_by_name(self, "exit", status == 0);
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
  GrapheneSessionClient *client = (GrapheneSessionClient*)userdata;
  g_debug("client dbus method call: %s, %s.%s", sender, interfaceName, methodName);

  if(g_strcmp0(interfaceName, "org.gnome.SessionManager.Client") == 0)
  {
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
    else if(g_strcmp0(methodName, "Stop") == 0) {
      graphene_session_client_stop(client);
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
  }
  else if(g_strcmp0(interfaceName, "org.gnome.SessionManager.ClientPrivate") == 0)
  {
    if(g_strcmp0(methodName, "EndSessionResponse") == 0) {
      gboolean isOk;
      gchar *reason;
      g_variant_get(parameters, "(bs)", &isOk, &reason);
      g_signal_emit_by_name(client, "end-session-response", isOk, reason);
      g_free(reason);
      return;
    }
  }
  
  g_dbus_method_invocation_return_value(invocation, NULL);
}

gboolean graphene_session_client_query_end_session(GrapheneSessionClient *self, gboolean forced)
{
  if(self->objectPath)
  {
    g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
      "org.gnome.SessionManager.ClientPrivate", "QueryEndSession", g_variant_new("(u)", forced == TRUE), NULL);
    return TRUE;
  }
  
  return FALSE;
}
void graphene_session_client_end_session(GrapheneSessionClient *self, gboolean forced)
{
  self->stopping = TRUE;
  if(self->objectPath)
  {
    g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
      "org.gnome.SessionManager.ClientPrivate", "EndSession", g_variant_new("(u)", forced == TRUE), NULL);
  }
  else
  {
    g_message("Client %s is not registered. Sending SIGKILL to %i signal to end session.", self->id, self->processId);
    if(self->processId)
      kill(self->processId, SIGKILL);
    else
      g_warning("Process id not available. Cannot stop client.", self->appId, self->id);
  }
}
void graphene_session_client_stop(GrapheneSessionClient *self)
{
  g_debug("stopping client %s (%s)", self->appId, self->id);
  self->stopping = TRUE;
  if(self->objectPath)
  {
    g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
      "org.gnome.SessionManager.ClientPrivate", "Stop", NULL, NULL);
  }
  else if(self->processId)
  {
    kill(self->processId, SIGKILL);
  }
  else
  {
    g_warning("Process id nor dbus object not available. Cannot stop client %s.", self->id);
  }
}




const gchar * graphene_session_client_get_id(GrapheneSessionClient *self)
{
  g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
  return self->id;
}
const gchar * graphene_session_client_get_object_path(GrapheneSessionClient *self)
{
  g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
  return self->objectPath;
}
const gchar * graphene_session_client_get_app_id(GrapheneSessionClient *self)
{
  g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
  return self->appId;
}
const gchar * graphene_session_client_get_dbus_name(GrapheneSessionClient *self)
{
  g_return_val_if_fail(GRAPHENE_IS_SESSION_CLIENT(self), NULL);
  return self->dbusName;
}

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






// TODO: Utilties file for these methods. (Dupliated from session2.c)

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
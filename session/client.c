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
 */

#include <glib/gprintf.h>
#include <sys/wait.h>
#include "client.h"
#include "util.h"

struct _GrapheneSessionClient
{
  GObject parent;

  // Unique client ID. Given to new processes so that they can register themselves properly (startupId).
  // Appended to the end of CLIENT_OBJECT_PATH to make a DBus object path for this
  // client once it's been registered.
  gchar *id;
  
  // Program info (set if available)
  gchar *name; // Human-readable name of program
  gchar *args;
  gchar *condition; // Condition for launching the program (https://lists.freedesktop.org/archives/xdg/2007-January/007436.html)
                    // Also supports gnome-session keys (https://github.com/GNOME/gnome-session/blob/865a6da78d23bee85f3c7bd72157974a3a918c86/gnome-session/gsm-autostart-app.c)
  gchar *icon;
  gboolean silent; // If true, redirects stdout and stderr to dev/null
  gboolean autoRestart;
  
  // Registration info (set when registered)
  gchar *objectPath; // Convenience: CLIENT_OBJECT_PATH + id
  gchar *appId; // How the application identifies itself
  gchar *dbusName; // The dbus name that the program registered from ("sender")
  guint objectRegistrationId;
  guint privateObjectRegistrationId;
  guint busWatchId;
  GDBusConnection *connection;
  
  // Process info (set when spawned or if available)
  GPid processId;
  guint spawnDelaySourceId;
  guint childWatchId;
  guint restartCount; // Number of times the process has crashed and been restarted
  
  GObject *conditionMonitor; // Set if monitoring the condition (free and NULL this to stop monitoring)
  gboolean forceNextRestart;
};

enum
{
  PROP_0,
  PROP_ID,
  PROP_NAME,
  PROP_ARGS,
  PROP_CONDITION,
  PROP_ICON,
  PROP_SILENT,
  PROP_AUTO_RESTART,
  PROP_BUS_CONNECTION,
  PROP_REGISTERED,
  PROP_LAST
};

enum
{
  SIGNAL_0,
  SIGNAL_READY,
  SIGNAL_END_SESSION_RESPONSE,
  SIGNAL_COMPLETE,
  SIGNAL_LAST
};

#define CLIENT_OBJECT_PATH "/org/gnome/SessionManager/Client"
#define MAX_RESTARTS 5

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

static const gchar *ClientInterfaceXML;
static void graphene_session_client_dispose(GrapheneSessionClient *self);
static void graphene_session_client_set_property(GrapheneSessionClient *self, guint propertyId, const GValue *value, GParamSpec *pspec);
static void graphene_session_client_get_property(GrapheneSessionClient *self, guint propertyId, GValue *value, GParamSpec *pspec);
static gchar * generate_client_id();
void graphene_session_client_register(GrapheneSessionClient *self, const gchar *sender, const gchar *appId);

void graphene_session_client_unregister(GrapheneSessionClient *self);
static void graphene_session_client_unregister_internal(GrapheneSessionClient *self);
void graphene_session_client_spawn(GrapheneSessionClient *self, guint delay);
static gboolean graphene_session_client_spawn_delay_cb(GrapheneSessionClient *self);
static void on_process_exit(GPid pid, gint status, GrapheneSessionClient *self);
static void on_client_vanished(GDBusConnection *connection, const gchar *name, GrapheneSessionClient *self);
static void on_client_exit(GrapheneSessionClient *self, guint status);

static gboolean test_condition(GrapheneSessionClient *self);
static void update_condition(GrapheneSessionClient *self);

static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender,
              const gchar           *objectPath,
              const gchar           *interfaceName,
              const gchar           *methodName,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation,
              gpointer              userdata);

static const GDBusInterfaceVTable ClientInterfaceCallbacks = {on_dbus_method_call, NULL, NULL};
static const GDBusInterfaceVTable ClientPrivateInterfaceCallbacks = {on_dbus_method_call, NULL, NULL};
static const gchar *ClientInterfaceXML;
static GDBusNodeInfo *ClientInterfaceInfo;    


G_DEFINE_TYPE(GrapheneSessionClient, graphene_session_client, G_TYPE_OBJECT);

static void graphene_session_client_class_init(GrapheneSessionClientClass *klass)
{
  ClientInterfaceInfo = g_dbus_node_info_new_for_xml(ClientInterfaceXML, NULL);

  GObjectClass *objectClass = G_OBJECT_CLASS(klass);
  objectClass->dispose = graphene_session_client_dispose;
  objectClass->set_property = graphene_session_client_set_property;
  objectClass->get_property = graphene_session_client_get_property;
  
  properties[PROP_ID] =
    g_param_spec_string("id", "id", "aka startup id", NULL, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  properties[PROP_NAME] =
    g_param_spec_string("name", "name", "readable name", NULL, G_PARAM_READWRITE);
  properties[PROP_ARGS] =
    g_param_spec_string("args", "args", "args passed for spawning the process", NULL, G_PARAM_READWRITE);
  properties[PROP_CONDITION] =
    g_param_spec_string("condition", "condition", "only launch of this condition is met (.desktop format)", NULL, G_PARAM_READWRITE);
  properties[PROP_ICON] =
    g_param_spec_string("icon", "icon", "icon", NULL, G_PARAM_READWRITE);
  properties[PROP_SILENT] =
    g_param_spec_boolean("silent", "silent", "if all output is redirected to /dev/null", FALSE, G_PARAM_READWRITE);
  properties[PROP_AUTO_RESTART] =
    g_param_spec_boolean("auto-restart", "auto restart", "if the client should auto restart", FALSE, G_PARAM_READWRITE);
  properties[PROP_BUS_CONNECTION] =
    g_param_spec_object("bus", "bus", "bus connection", G_TYPE_DBUS_CONNECTION, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  properties[PROP_REGISTERED] =
    g_param_spec_boolean("registered", "registered", "if the client has been registered", FALSE, G_PARAM_READABLE);  
      
  g_object_class_install_properties(objectClass, PROP_LAST, properties);

  /*
   * Emitted when the client is ready.
   * This can happen when the client process exits, is put on hold (condition not met), or registers.
   * If the client exits to be ready, 'exit' will be emitted immediately after 'ready'.
   */ 
  signals[SIGNAL_READY] = g_signal_new("ready", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  
  /*
   * Emitted from a DBus call to org.gnome.SessionManager.ClientPrivate.EndSessionResponse.
   * First parameter is isOk, if it is okay to proceed with end session.
   * Second parameter is reason, which specifies a reason for not proceeding if isOk is false.
   * Can release this client object on this event if this event is in response to the EndSession signal (not QueryEndSession)
   */
  signals[SIGNAL_END_SESSION_RESPONSE] = g_signal_new("end-session-response", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_STRING);
    
  /*
   * Emitted when the client permanently exits. It is safe to release the client object on this event.
   * After this signal is emitted, the client can only be restarted by an outside command, such as calling spawn.
   */
  signals[SIGNAL_COMPLETE] = g_signal_new("complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 Creates a new, empty client, optionally with an inital clientId.
 If clientId is NULL, a default one will be generated.
 connection should be the application's default GDBusConnection. Must not be NULL.
 */
GrapheneSessionClient* graphene_session_client_new(GDBusConnection *connection, const gchar *clientId)
{
  return GRAPHENE_SESSION_CLIENT(g_object_new(GRAPHENE_TYPE_SESSION_CLIENT, "bus", connection, "id", clientId, NULL));
}

static void graphene_session_client_init(GrapheneSessionClient *self)
{
  self->childWatchId = 0;
}

static void graphene_session_client_dispose(GrapheneSessionClient *self)
{
  graphene_session_client_unregister_internal(self);
  
  if(self->childWatchId)
    g_source_remove(self->childWatchId);
  self->childWatchId = 0;
  if(self->spawnDelaySourceId)
    g_source_remove(self->spawnDelaySourceId);
  self->spawnDelaySourceId = 0;
  g_clear_pointer(&self->name, g_free);
  g_clear_pointer(&self->args, g_free);
  g_clear_pointer(&self->condition, g_free);
  g_clear_pointer(&self->icon, g_free);
  g_clear_pointer(&self->id, g_free);

  G_OBJECT_CLASS(graphene_session_client_parent_class)->dispose(G_OBJECT(self));
}

static void graphene_session_client_set_property(GrapheneSessionClient *self, guint propertyId, const GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GRAPHENE_IS_SESSION_CLIENT(self));
  
  switch(propertyId)
  {
  case PROP_ID:
    g_clear_pointer(&self->id, g_free);
    const gchar *valStr = g_value_get_string(value);
    self->id = valStr ? g_strdup(valStr) : generate_client_id();
    break;
  case PROP_NAME:
    self->name = g_strdup(g_value_get_string(value));
    break;
  case PROP_ARGS:
    g_clear_pointer(&self->args, g_free);
    self->args = g_strdup(g_value_get_string(value));
    break;
  case PROP_CONDITION:
    g_clear_pointer(&self->condition, g_free);
    self->condition = g_strdup(g_value_get_string(value));
    update_condition(self);
    break;
  case PROP_ICON:
    self->icon = g_strdup(g_value_get_string(value));
    break;
  case PROP_SILENT:
    self->silent = g_value_get_boolean(value);
    break;
  case PROP_AUTO_RESTART:
    self->autoRestart = g_value_get_boolean(value);
    break;
  case PROP_BUS_CONNECTION:
    self->connection = G_DBUS_CONNECTION(g_value_get_object(value));
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
  case PROP_ID:
    g_value_set_string(value, self->id);
    break;
  case PROP_NAME:
    g_value_set_string(value, self->name);
    break;
  case PROP_ARGS:
    g_value_set_string(value, self->args);
    break;
  case PROP_CONDITION:
    g_value_set_string(value, self->condition);
    break;
  case PROP_ICON:
    g_value_set_string(value, self->icon);
    break;
  case PROP_SILENT:
    g_value_set_boolean(value, self->silent);
    break;
  case PROP_AUTO_RESTART:
    g_value_set_boolean(value, self->autoRestart);
    break;
  case PROP_BUS_CONNECTION:
    g_value_set_object(value, self->connection);
    break;
  case PROP_REGISTERED:
    g_value_set_boolean(value, self->objectPath != NULL);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
    break;
  }
}

/*
 * Attempts to get a human-readable name for this client.
 * The returned string is owned by the client; do not free it.
 */
const gchar * graphene_session_client_get_best_name(GrapheneSessionClient *self)
{
  if(self->name)           return self->name;
  else if(self->appId)     return self->appId;
  else if(self->dbusName)  return self->dbusName;
  else if(self->args)      return self->args;
  else                     return self->id;
}

static gchar * generate_client_id()
{
  // TODO: Generate ID according to XSMP standard
  
  static const guint32 length = 17;
  gchar *id = g_new(gchar, 17+1);
  id[0] = '0';
  id[length] = '\0';
  
  for(guint32 i=1;i<length;++i)
    g_sprintf(id + i, "%x", g_random_int() % 16);
  
  return id;
}

void graphene_session_client_register(GrapheneSessionClient *self, const gchar *sender, const gchar *appId)
{
  GError *error = NULL;

  g_clear_pointer(&self->dbusName, g_free);
  g_clear_pointer(&self->appId, g_free);
  self->dbusName = g_strdup(sender);
  self->appId = g_strdup(appId);
  
  if(!ClientInterfaceInfo)
    return;
  
  if(!self->objectPath)
    self->objectPath = g_strdup_printf("%s%s", CLIENT_OBJECT_PATH, self->id);
  
  if(!self->objectRegistrationId)
  {
    self->objectRegistrationId = g_dbus_connection_register_object(self->connection, self->objectPath, ClientInterfaceInfo->interfaces[0], &ClientInterfaceCallbacks, self, NULL, &error);
    if(error)
    {
      g_warning("Failed to register client '%s': %s", graphene_session_client_get_best_name(self), error->message);
      g_clear_error(&error);
      return;
    }
  }
  
  if(!self->privateObjectRegistrationId)
  {
    self->privateObjectRegistrationId = g_dbus_connection_register_object(self->connection, self->objectPath, ClientInterfaceInfo->interfaces[1], &ClientPrivateInterfaceCallbacks, self, NULL, &error);
    if(error)
    {
      g_warning("Failed to register client private '%s': %s", graphene_session_client_get_best_name(self), error->message);
      g_clear_error(&error);
      return;
    }
  }

  if(!self->processId && self->dbusName)
  {
    GVariant *vpid = g_dbus_connection_call_sync(self->connection,
                       "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                       "GetConnectionUnixProcessID", g_variant_new("(s)", self->dbusName), G_VARIANT_TYPE("(u)"),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if(error)
    {
      g_warning("Failed to obtain process id of '%s': %s", graphene_session_client_get_best_name(self), error->message);
      g_clear_error(&error);
    }
    else
    {
      g_variant_get(vpid, "(u)", &self->processId);
      
      gchar *processArgs = NULL;
      g_spawn_command_line_sync(g_strdup_printf("ps --pid %i -o args=", self->processId), &processArgs, NULL, NULL, NULL);
      if(processArgs)
      {
        self->args = str_trim(processArgs);
        g_free(processArgs);
        g_object_notify(G_OBJECT(self), "args");
        g_debug("Got registered process args: '%s'", self->args);
      }
    }
  }

  self->busWatchId = g_bus_watch_name(G_BUS_TYPE_SESSION, self->dbusName, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, (GBusNameVanishedCallback)on_client_vanished, self, NULL);
  if(!self->busWatchId)
    g_warning("Failed to watch bus name of process '%s' (%s)", graphene_session_client_get_best_name(self), self->dbusName);
    
  g_debug(" + Registered client '%s' at path '%s'", graphene_session_client_get_best_name(self), self->objectPath);
  g_signal_emit_by_name(self, "ready");
}

void graphene_session_client_unregister(GrapheneSessionClient *self)
{
  g_debug("unregister %s", graphene_session_client_get_best_name(self));
  graphene_session_client_unregister_internal(self);
  on_client_exit(self, 0); // EXIT_SUCCESS
}

static void graphene_session_client_unregister_internal(GrapheneSessionClient *self)
{
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
}

void graphene_session_client_spawn(GrapheneSessionClient *self, guint delay)
{
  if(delay > 0)
    self->spawnDelaySourceId = g_timeout_add_seconds(delay, (GSourceFunc)graphene_session_client_spawn_delay_cb, self);
  else
    graphene_session_client_spawn_delay_cb(self);
}

static gboolean graphene_session_client_spawn_delay_cb(GrapheneSessionClient *self)
{
  if(!self)
    return FALSE; // FALSE to not continue to call this callback
    
  if(self->spawnDelaySourceId)
    g_source_remove(self->spawnDelaySourceId);
  self->spawnDelaySourceId = 0;
  
  if(self->processId)
  {
    return FALSE;
  }
  else if(!self->args)
  {
    g_warning("Cannot spawn client '%s' because args is not set", graphene_session_client_get_best_name(self));
    return FALSE;
  }
  else if(!test_condition(self))
  {
    g_debug("Cannot spawn client '%s' immediately because condition is not met (might spawn later)", graphene_session_client_get_best_name(self));
    g_signal_emit_by_name(self, "ready");
    return FALSE;
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
  gchar **argsSplit = g_strsplit(self->args, " ", -1);
  gchar *startupIdVar = g_strdup_printf("DESKTOP_AUTOSTART_ID=%s", self->id);
  gchar **env = strv_append((const char * const *)g_get_environ(), startupIdVar);
  
  g_spawn_async(NULL, argsSplit, env, flags, NULL, NULL, &pid, &e);
  
  // #if DEBUG
    g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
  // #endif
  
  g_strfreev(env);
  g_free(startupIdVar);
  g_strfreev(argsSplit);
  
  if(e)
  {
    g_critical("Failed to start process with args '%s' (%s)", self->args, e->message);
    g_error_free(e);
    return FALSE;
  }
  
  self->processId = pid;
  
  if(self->processId)
    self->childWatchId = g_child_watch_add(self->processId, (GChildWatchFunc)on_process_exit, self);
  
  g_debug(" + Spawned client with args '%s' with id '%s' and pId %i", self->args, self->id, self->processId);
  
  return FALSE;
}

static void on_process_exit(GPid pid, gint status, GrapheneSessionClient *self)
{
  if(!self)
    return;
  g_debug(" - Process %i, %s exited", pid, graphene_session_client_get_best_name(self));
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
  g_debug(" - Client '%s', %s vanished", graphene_session_client_get_best_name(self), self->appId);
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
  gboolean wasRegistered = self->objectPath != NULL;
  
  // Make sure on_client_exit can't be called twice (once from dbus, once from child watch)
  graphene_session_client_unregister_internal(self); // Needs to be unregistered anyways
  if(self->childWatchId)
    g_source_remove(self->childWatchId);
  self->childWatchId = 0;
  self->processId = 0;
  
  // Restart it
  g_debug("should restart? auto: %i, args: %s, status: %i, force: %i", self->autoRestart, self->args, status, self->forceNextRestart);
  if(self->args && (self->forceNextRestart || (self->autoRestart && status != 0)))
  {
    g_debug("restarting client with args %s", self->args);
    if(self->restartCount < MAX_RESTARTS)
    {
      if(!self->forceNextRestart)
        self->restartCount++;
      graphene_session_client_spawn(self, 0);
    }
    else
    {
      g_warning("The application with args '%s' has crashed too many times, and will not be automatically restarted.", self->args);
      if(!wasRegistered)
        g_signal_emit_by_name(self, "ready");
      g_signal_emit_by_name(self, "complete");
    }
  }
  else
  {
    if(!wasRegistered)
      g_signal_emit_by_name(self, "ready");
    if(self->condition == NULL)
      g_signal_emit_by_name(self, "complete");
  }
  
  self->forceNextRestart = FALSE;
}

static gboolean test_condition(GrapheneSessionClient *self)
{
  if(!self->condition)
    return TRUE;
  
  gchar **tokens = g_strsplit(self->condition, " ", -1);
  guint numTokens = g_strv_length(tokens);
  gboolean result = FALSE;
  
  if(numTokens >= 3 && g_ascii_strcasecmp(tokens[0], "gsettings") == 0)
  {
    GVariant *variant = get_gsettings_value(tokens[1], tokens[2]);
    if(g_variant_type_equal(g_variant_get_type(variant), G_VARIANT_TYPE_BOOLEAN))
      result = g_variant_get_boolean(variant);
    g_variant_unref(variant);
  }
  else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "if-exists") == 0)
  {
    // TODO
  }
  else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "unless-exists") == 0)
  {
    // TODO
  }
  else if(numTokens >= 3 && g_ascii_strcasecmp(tokens[0], "gnome3") == 0)
  {
    if(g_ascii_strcasecmp(tokens[1], "if-session"))
      result = g_ascii_strcasecmp(tokens[2], "graphene") == 0;
    else if(g_ascii_strcasecmp(tokens[1], "unless-session"))
      result = g_ascii_strcasecmp(tokens[2], "graphene") != 0;
  }

  g_strfreev(tokens);
  
  if(!result)
    g_debug("condition not met for client '%s'", graphene_session_client_get_best_name(self));
  return result;
}

static void run_condition(GrapheneSessionClient *self)
{
  g_debug("xxcondition on %s", graphene_session_client_get_best_name(self));
  if(test_condition(self))
  {
    // g_debug("start");
    graphene_session_client_spawn(self, 0);
  }
  else
  {
    // g_debug("stop");
    graphene_session_client_stop(self);
  }
}

static void update_condition(GrapheneSessionClient *self)
{
  gboolean wasMonitoring = self->conditionMonitor != NULL;
  if(self->conditionMonitor)
    g_clear_object(&self->conditionMonitor);
  
  if(!self->condition)
  {
    if(wasMonitoring && !graphene_session_client_get_is_active(self))
      g_signal_emit_by_name(self, "complete");
    return;
  }
  
  gchar **tokens = g_strsplit(self->condition, " ", -1);
  guint numTokens = g_strv_length(tokens);
  
  if(numTokens >= 3 && g_ascii_strcasecmp(tokens[0], "gsettings") == 0)
  {
    self->conditionMonitor = monitor_gsettings_key(tokens[1], tokens[2], run_condition, self);
  }
  else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "if-exists") == 0)
  {
    // TODO
  }
  else if(numTokens >= 2 && g_ascii_strcasecmp(tokens[0], "unless-exists") == 0)
  {
    // TODO
  }
  
  g_strfreev(tokens);
  
  run_condition(self);
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
  g_debug("end session on %s", graphene_session_client_get_best_name(self));
  g_clear_pointer(&self->condition, g_free); // Clear condition to ensure 'complete' will be sent
  self->forceNextRestart = FALSE;
  
  if(self->objectPath)
    g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
      "org.gnome.SessionManager.ClientPrivate", "EndSession", g_variant_new("(u)", forced == TRUE), NULL);
  else if(self->processId)
    graphene_session_client_stop(self);
  else
    g_signal_emit_by_name(self, "complete");
}

void graphene_session_client_stop(GrapheneSessionClient *self)
{
  g_debug("stopping client '%s'", graphene_session_client_get_best_name(self));
  if(self->objectPath)
  {
    g_dbus_connection_emit_signal(self->connection, self->dbusName, self->objectPath,
      "org.gnome.SessionManager.ClientPrivate", "Stop", NULL, NULL);
  }
  else if(self->processId)
  {
    // TODO: Probably won't work right if forceNextRestart is TRUE
    // Maybe maybe an instance enum such as [0: never restart, 1: auto restart, 2: always restart]
    g_debug(" - Client '%s' is not registered. Sending SIGKILL to %i signal to stop client.", graphene_session_client_get_best_name(self), self->processId);
    GPid pid = self->processId;
    on_client_exit(self, 0);
    kill(pid, SIGKILL);
  }
  else
  {
    g_debug("Process id nor dbus object not available. Cannot stop client '%s'.", graphene_session_client_get_best_name(self));
  }
}

/*
 * Forces the client to restart when it next closes, regardless if exit status or .desktop autoRestart flags.
 * (Except for on EndSession)
 * Only applies until the next exit of the program.
 */
void graphene_session_client_force_next_restart(GrapheneSessionClient *self)
{
  self->forceNextRestart = TRUE;
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
gboolean graphene_session_client_get_is_active(GrapheneSessionClient *self)
{
  return self->objectPath != NULL || self->processId != 0;
}


static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender,
              const gchar           *objectPath,
              const gchar           *interfaceName,
              const gchar           *methodName,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation,
              gpointer              userdata)
{
  GrapheneSessionClient *client = (GrapheneSessionClient*)userdata;
  g_debug("client dbus method call: %s, %s, %s.%s", sender, graphene_session_client_get_best_name(client), interfaceName, methodName);

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
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", client->objectPath != NULL));
      return;
    }
    else if(g_strcmp0(methodName, "Stop") == 0) {
      graphene_session_client_stop(client);
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
    else if(g_strcmp0(methodName, "Restart") == 0) {
      graphene_session_client_force_next_restart(client);
      g_dbus_connection_emit_signal(connection, sender, objectPath,
        "org.gnome.SessionManager.ClientPrivate", "Stop", NULL, NULL);
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
  }
  else if(g_strcmp0(interfaceName, "org.gnome.SessionManager.ClientPrivate") == 0)
          // && sender == client->dbusName)
  {
    if(g_strcmp0(methodName, "EndSessionResponse") == 0) {
      gboolean isOk;
      gchar *reason;
      g_variant_get(parameters, "(bs)", &isOk, &reason);
      g_signal_emit_by_name(client, "end-session-response", isOk, reason);
      g_free(reason);
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }
  }
  
  g_dbus_method_invocation_return_value(invocation, NULL);
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
"    <method name='Restart'> </method>"
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
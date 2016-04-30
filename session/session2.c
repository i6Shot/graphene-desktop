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
 * https://wiki.gnome.org/Projects/SessionManagement/NewGnomeSession
 */

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <signal.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#include "client.h"

#define SESSION_MANAGER_APP_ID "org.gnome.SessionManager"
#define INHIBITOR_OBJECT_PATH "/org/gnome/SessionManager/Inhibitor"
#define SHOW_ALL_OUTPUT TRUE // Set to TRUE for release; FALSE only shows output from .desktop files with 'Graphene-ShowOutput=true'
#define DEBUG TRUE

typedef enum
{
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
  SESSION_PHASE_PAUSE_END_SESSION,

} SessionPhase;

typedef struct
{
  // Appended to the end of INHIBITOR_OBJECT_PATH to make a DBus object path for this
  guint32 id; // "cookie"
  
  GrapheneSessionClient *client;
  gchar *reason;
  guint flags;
  guint xid;

  guint objectRegistrationId;

} Inhibitor;

typedef struct
{
  GApplication *app;
  guint interfaceRegistrationId;
  SessionPhase phase;
  guint phaseTimerId;
  gboolean forcedExit;
  
  GList *clients;
  GList *phaseTaskList; // Contains a list of clients that still need to respond for that phase (if applicable)
  gboolean phaseHasTasks; // If true, the next phase will be started when phaseTaskList becomes NULL (0 elements)
  GHashTable *autostarts;

  GHashTable *inhibitors;
  guint32 inhibitCookieCounter;
  
} Session;


// APPLICATION
static void activate(GApplication *app, gpointer userdata);
static void shutdown(GApplication *application, gpointer userdata);
static void on_sigterm_or_sigint(gpointer userdata);
static gboolean run_phase(guint phase);
static void run_autostart_phase(const gchar *phase);
static void begin_end_session(gboolean force);
static void end_session();

// CLIENT
static gchar * register_client(const gchar *sender, const gchar *appId, const gchar *startupId);
static void unregister_client(const gchar *clientObjectPath);
static void on_client_ready(GrapheneSessionClient *client);
static void on_client_exit(GrapheneSessionClient *client, gboolean success);
static void on_client_end_session_response(GrapheneSessionClient *client, gboolean isOk, const gchar *string);

// INHIBITOR
static void free_inhibitor(Inhibitor *inhibitor);
static guint32 inhibit(const gchar *sender, const gchar *appId, guint32 toplevelXId, const gchar *reason, guint32 flags);
static void uninhibit(guint32 id);

// UTILITIES
static void on_dbus_method_call(GDBusConnection *connection, const gchar* sender, const gchar *objectPath, const gchar *interfaceName, const gchar *methodName, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer *userdata);
static GVariant *on_dbus_get_property(GDBusConnection *connection, const gchar *sender, const gchar *objectPath, const gchar *interfaceName, const gchar *propertyName, GError **error, gpointer userdata);
static gchar ** strv_append(const gchar * const *list, const gchar *str);
static gchar * str_trim(const gchar *str);
static GHashTable * list_autostarts();
static GrapheneSessionClient * find_client_from_given_info(const gchar *id, const gchar *objectPath, const gchar *appId, const gchar *dbusName);

// DBus vars
static const gchar *SessionManagerInterfaceXML;
static const gchar *InhibitorInterfaceXML;
static GDBusNodeInfo *InhibitorInterfaceInfo;
static const GDBusInterfaceVTable SessionManagerInterfaceCallbacks = {on_dbus_method_call, on_dbus_get_property, NULL};
static const GDBusInterfaceVTable InhibitorInterfaceCallbacks = {on_dbus_method_call, on_dbus_get_property, NULL};


static Session *self;


int main(int argc, char **argv)
{
  // Make sure X is running before starting anything
  if(g_getenv("DISPLAY") == NULL)
  {
    g_critical("Cannot start graphene-session without an active X server. Try running startx, or running from a login manager such as LightDM.");
    return 1;
  }

#if DEBUG
  g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
#endif

  // g_unix_signal_add(SIGTERM, on_sigterm_or_sigint, NULL);
  // g_unix_signal_add(SIGINT, on_sigterm_or_sigint, NULL);

  GApplication *app = g_application_new(SESSION_MANAGER_APP_ID, G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}

static void activate(GApplication *app, gpointer userdata)
{
  const GDBusNodeInfo *interfaceInfo = g_dbus_node_info_new_for_xml(SessionManagerInterfaceXML, NULL);
  if(!interfaceInfo)
  {
    g_error("Failed to get dbus interface info from XML.");
    return;
  }
  
  InhibitorInterfaceInfo = g_dbus_node_info_new_for_xml(InhibitorInterfaceXML, NULL);

  self = g_new0(Session, 1);
  self->app = app;
  self->inhibitors = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_inhibitor);
  self->inhibitCookieCounter = 1;
  self->phase = SESSION_PHASE_STARTUP;
  
  // Register Session Banager DBus path 
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(app));
  const gchar *objectPath = g_application_get_dbus_object_path(G_APPLICATION(app));
  static const GDBusInterfaceVTable interfaceCallbacks = {on_dbus_method_call, NULL, NULL};
  self->interfaceRegistrationId = g_dbus_connection_register_object(connection, objectPath, interfaceInfo->interfaces[0], &interfaceCallbacks, NULL, NULL, NULL);
  
  // Gets a list of all desktop files to autostart
  self->autostarts = list_autostarts();
  
  // Start the first phase
  g_application_hold(app); // Hold until the running phase has been reached
  run_phase(SESSION_PHASE_STARTUP);
}

static void shutdown(GApplication *application, gpointer userdata)
{
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  if(self->interfaceRegistrationId)
    g_dbus_connection_unregister_object(connection, self->interfaceRegistrationId);
    
  g_list_free_full(self->clients, g_object_unref);
  g_hash_table_unref(self->inhibitors);
  g_hash_table_unref(self->autostarts);
  
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

static gboolean run_phase(guint phase)
{
  g_debug("starting phase %i", phase);

  self->phase = phase;
  
  if(self->phaseTimerId)
    g_source_remove(self->phaseTimerId);
  self->phaseTimerId = 0;
  
  if(self->phaseTaskList)
    g_list_free(self->phaseTaskList);
  self->phaseTaskList = NULL;
  self->phaseHasTasks = FALSE;
  
  // GNOME standard is 10 seconds, however many applications never bother to register which causes
  // the startup phase to hang for way too long. For any applications that DO register, 1 second is fine.
  // TODO: This might not be enough time for slow-booting media like CDs.
  gint waitTime = 1;
  
  switch(self->phase)
  {
    case SESSION_PHASE_STARTUP:
      waitTime = 0;
      break;
    case SESSION_PHASE_INITIALIZATION: // Important GNOME stuff
      run_autostart_phase("Initialization");
      break;
    case SESSION_PHASE_WINDOWMANAGER: // This starts graphene-wm
      run_autostart_phase("WindowManager");
      break;
    case SESSION_PHASE_PANEL: // This starts graphene-panel
      run_autostart_phase("Panel");
      break;
    case SESSION_PHASE_DESKTOP: // This starts nautilus
      run_autostart_phase("Desktop");
      break;
    case SESSION_PHASE_APPLICATION: // Everything else 
      run_autostart_phase("Applications");
      waitTime = 0;
      break;
    case SESSION_PHASE_RUNNING:
      g_application_release(self->app); // Release the hold that was added in activate()
      waitTime = -1;
      break;
    case SESSION_PHASE_QUERY_END_SESSION:
      waitTime = 1;
      break;
    case SESSION_PHASE_PAUSE_END_SESSION:
      g_message("End session paused.");
      waitTime = 5;
      break;
    case SESSION_PHASE_END_SESSION:
      end_session();
      break;
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
  if(self->phaseHasTasks && !self->phaseTaskList)
  {
    g_debug("phase %i complete", self->phase);
    g_idle_add(run_phase, self->phase+1);
  }
}

/*
 * Launch a specific phase (all autostart .desktop files that have the X-GNOME-Autostart-Phase attribute equal to <phase>)
 * Each launched .desktop is removedd from the autostart array.
 * If <phase> is "Applications", ALL .desktop files are launched.
 */
static void run_autostart_phase(const gchar *phase)
{
  self->phaseHasTasks = TRUE;
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));

  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, self->autostarts);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {    
    GDesktopAppInfo *desktopInfo = G_DESKTOP_APP_INFO(value);
    
    const gchar *thisPhase = g_desktop_app_info_get_string(desktopInfo, "X-GNOME-Autostart-Phase");
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
    
      g_application_hold(self->app);
      GrapheneSessionClient *client = graphene_session_client_new(connection, NULL);
      self->clients = g_list_prepend(self->clients, client);
      self->phaseTaskList = g_list_prepend(self->phaseTaskList, client);
      g_object_set(client,
        "launch-args", commandline,
        "auto-restart", autoRestart,
        "silent", !showOutput, NULL);
      g_object_connect(client,
        "signal::ready", on_client_ready, NULL,
        "signal::exit", on_client_exit, NULL,
        "signal::end-session-response", on_client_end_session_response, NULL, NULL);
      graphene_session_client_spawn(client, (guint)delay);
      
      g_hash_table_iter_remove(&iter);
    }
    
    g_free(thisPhase);
  }
  
  run_next_phase_if_ready();
}

/*
 * Called to begin the process of cleanly ending the session (logout/shutdown).
 * Changes to the QUERY_END_SESSION phase.
 */
static void begin_end_session(gboolean force)
{
  self->forcedExit = force;
  
  run_phase(SESSION_PHASE_QUERY_END_SESSION);
  self->phaseHasTasks = TRUE;

  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  
  for(GList *clients=self->clients;clients!=NULL;clients=clients->next)
  {
    GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
    if(graphene_session_client_query_end_session(client, self->forcedExit))
      self->phaseTaskList = g_list_prepend(self->phaseTaskList, client);
  }
}

/*
 * Called by run_phase on the END_SESSION phase. Do not call directly.
 * Tells all processes to end (given 10 seconds).
 * Processes are supposed to respond with EndSessionResponse according to GNOME's spec
 * but this actually just waits to make sure that all processes exit (or unregister).
 * TODO: Only kill the window manager and panel once everything else dies to make it look nicer on the user.
 */
static void end_session()
{
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  
  for(GList *clients=self->clients;clients!=NULL;clients=clients->next)
  {
    GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
    graphene_session_client_end_session(client, self->forcedExit);
  }
}

/*
 * Directly called from DBus org.gnome.SessionManager.RegisterClient.
 * Registers a client with the given startupId. If the client cannot be found, it is created.
 * Returns the client's object path. Do not free it yourself; freed when the client is destroyed.
 * sender is the DBus name of the process that is asking to be registered.
 */
static gchar * register_client(const gchar *sender, const gchar *appId, const gchar *startupId)
{
  GrapheneSessionClient *client = find_client_from_given_info(startupId, NULL, appId, sender);

  if(!client)
  {
    GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
    g_application_hold(self->app);
    client = graphene_session_client_new(connection, (g_strcmp0(startupId, "") == 0) ? NULL : startupId);
    g_object_connect(client,
      "signal::ready", on_client_ready, NULL, // TODO: Need to connect 'ready'?. Shouldn't hurt though.
      "signal::exit", on_client_exit, NULL,
      "signal::end-session-response", on_client_end_session_response, NULL, NULL);
    g_list_append(self->clients, client);
  }
  
  graphene_session_client_register(client, sender, appId);
  return graphene_session_client_get_object_path(client);
}

/*
 * Directly called from DBus org.gnome.SessionManager.UnregisterClient.
 * Unregisters a client, but does not remove it from the Client list.
 */
static void unregister_client(const gchar *clientObjectPath)
{
  GrapheneSessionClient *client = find_client_from_given_info(NULL, clientObjectPath, NULL, NULL);
  if(!client)
    return;
  
  graphene_session_client_unregister(client);
}

static void on_client_ready(GrapheneSessionClient *client)
{
  g_debug("client ready");
  if(self->phase < SESSION_PHASE_RUNNING)
  {
    self->phaseTaskList = g_list_remove(self->phaseTaskList, client);
    run_next_phase_if_ready();
  }
}

static void on_client_exit(GrapheneSessionClient *client, gboolean success)
{
  g_debug("client exited (success? %i)", success);
  g_application_release(self->app);
  self->clients = g_list_remove(self->clients, client);
  g_object_unref(client);
}

static void on_client_end_session_response(GrapheneSessionClient *client, gboolean isOk, const gchar *string)
{
  if(self->phase == SESSION_PHASE_QUERY_END_SESSION)
  {
    self->phaseTaskList = g_list_remove(self->phaseTaskList, client);
    run_next_phase_if_ready();
  }
}

static guint32 inhibit(const gchar *sender, const gchar *appId, guint32 toplevelXId, const gchar *reason, guint32 flags)
{
  register_client(sender, appId, NULL); // Registers it if it wasn't already
  
  Inhibitor *inhibitor = g_new0(Inhibitor, 1);
  inhibitor->client = find_client_from_given_info(NULL, NULL, appId, sender);
  inhibitor->flags = flags;
  inhibitor->id = self->inhibitCookieCounter++;
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
  g_hash_table_insert(self->inhibitors, inhibitor->id, inhibitor);
  g_debug("Added inhibitor %i for %s,%s becasue of '%s'", inhibitor->id, sender, appId, inhibitor->reason);
  return inhibitor->id;
}

static void uninhibit(guint32 id)
{
  Inhibitor *inhibitor = g_hash_table_lookup(self->inhibitors, id);
  if(!inhibitor)
    return;
    
  GDBusConnection *connection = g_application_get_dbus_connection(G_APPLICATION(self->app));
  if(inhibitor->objectRegistrationId)
    g_dbus_connection_unregister_object(connection, inhibitor->objectRegistrationId);
  inhibitor->objectRegistrationId = 0;
  
  g_hash_table_remove(self->inhibitors, id);
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
    else if(g_strcmp0(methodName, "GetClients") == 0)
    {
      guint count = 0;
      for(GList *clients=self->clients;clients!=NULL;clients=clients->next)
      {
        GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
        const gchar *path = graphene_session_client_get_object_path(client);
        if(path)
          count++;
      }
      
      gchar **arr = g_new(gchar*, count+1);
      count = 0;
      for(GList *clients=self->clients;clients!=NULL;clients=clients->next)
      {
        GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
        const gchar *path = graphene_session_client_get_object_path(client);
        if(path)
          arr[count++] = g_strdup(path);
      }
      arr[count] = NULL;
      
      GVariant **va = g_new(GVariant*, 1);
      va[0] = g_variant_new_objv(arr, -1);
      GVariant *v = g_variant_new_tuple(va, 1);
      
      g_dbus_method_invocation_return_value(invocation, v);
      
      g_free(va);
      g_strfreev(arr);
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
      begin_end_session(FALSE);
    }
    else if(g_strcmp0(methodName, "IsSessionRunning") == 0)
    {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", self->phase == SESSION_PHASE_RUNNING));
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
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", inhibitor->client ? graphene_session_client_get_app_id(inhibitor->client) : ""));
      return;
    }
    else if(g_strcmp0(methodName, "GetClientId") == 0) {
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", inhibitor->client ? graphene_session_client_get_object_path(inhibitor->client) : ""));
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

static GrapheneSessionClient * find_client_from_given_info(const gchar *id, const gchar *objectPath, const gchar *appId, const gchar *dbusName)
{
  for(GList *clients=self->clients;clients!=NULL;clients=clients->next)
  {
    GrapheneSessionClient *client = (GrapheneSessionClient *)clients->data;
    
    const gchar *clientId = graphene_session_client_get_id(client);
    const gchar *clientObjectPath = graphene_session_client_get_object_path(client);
    const gchar *clientAppId = graphene_session_client_get_app_id(client);
    const gchar *clientDbusName = graphene_session_client_get_dbus_name(client);

    if((clientId && id && g_strcmp0(clientId, id) == 0) ||
       (clientObjectPath && objectPath && g_strcmp0(clientObjectPath, objectPath) == 0) ||
       (clientAppId && appId && g_strcmp0(clientAppId, appId) == 0) ||
       (clientDbusName && dbusName && g_strcmp0(clientDbusName, dbusName) == 0))
      return client;
  }
  
  return NULL;
}
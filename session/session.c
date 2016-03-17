/*
 * vos-desktop
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

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <signal.h>
#include <sys/wait.h>

#define MAX_RESTARTS 5

typedef struct {
  GPid pid;
  const gchar *args;
  gboolean autorestart;
  guint restartCount;
} ProcessInfo;

static void activate(GApplication *app, gpointer userdata);
static void shutdown(GApplication *application, gpointer userdata);
static void quit();

static GHashTable *list_autostarts();
static void launch_autostart_phase(const gchar *phase, GHashTable *autostarts);
static void launch_process(const gchar *args, gboolean autorestart, guint restartCount, guint delay);
static gchar ** strv_append(const gchar * const *list, const gchar *str);

static gboolean Activated = FALSE;
static gboolean Closing = FALSE;
static gboolean Closed = FALSE;
GHashTable *LaunchedProcessesTable = NULL;


int main(int argc, char **argv)
{
  // Make sure X is running before starting anything
  if(g_getenv("DISPLAY") == NULL)
  {
    g_critical("Cannot start vossession without an active X server. Try running startx, or starting vossession from a login manager such as LightDM.");
    return 1;
  }

  // Start app
  GApplication *app = g_application_new("io.velt.graphene-session", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}

static void activate(GApplication *app, gpointer userdata)
{
  if(Activated)
    return;
  Activated = TRUE;
  
  LaunchedProcessesTable = g_hash_table_new(NULL, NULL); // Do not auto destroy values
  
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

static void shutdown(GApplication *application, gpointer userdata)
{
  // All processes should be closed by this point
  g_hash_table_unref(LaunchedProcessesTable);
}

static gboolean quit_wait_cb(gpointer userdata);

/*
 * Called to cleanly exit the session (when the panel crashes too many times or from logout).
 * Sends any remaining processes a SIGTERM message to close them, while will then be handled
 * by on_process_exit(). If all processes close at this point, the session will exit due
 * to all holds being released.
 * Also puts a timer on quit_wait_cb(), to force kill child processes and exit the session
 * if it takes too long for the child processes to exit cleanly.
 */
static void quit()
{
  if(g_hash_table_size(LaunchedProcessesTable) == 0)
  {
    g_application_quit(g_application_get_default());
    return;
  }

  if(Closing)
    return;

  Closing = TRUE;

  GHashTableIter iter;
  gpointer key, value;
  
  g_hash_table_iter_init(&iter, LaunchedProcessesTable);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    ProcessInfo *info = (ProcessInfo *)value;
    kill(info->pid, SIGTERM);
    // Don't dealloc info, it will be deallocated in on_process_exit() if it exits
  }
  
  // Note that g_timeout_add_seconds() does NOT hold the application
  // meaning that the app can quit while this is waiting. (Which is good!)
  g_timeout_add_seconds(2, quit_wait_cb, NULL);
}

/*
 * Called 2 seconds after quit(), and only if the application hasn't already exited due to all the
 * child process holds being released in on_process_exit().
 * Forcefully kills (SIGKILL) all remaining child processes and exits the session immediately.
 */
static gboolean quit_wait_cb(gpointer userdata)
{
  Closed = TRUE; // Don't let on_process_exit() run.
  
  if(g_hash_table_size(LaunchedProcessesTable) > 0)
  {
    g_warning("Not all processes closed before timeout. SIGKILLing them.");
    
    GHashTableIter iter;
    gpointer key, value;
    
    g_hash_table_iter_init(&iter, LaunchedProcessesTable);
    while(g_hash_table_iter_next(&iter, &key, &value))
    {
      ProcessInfo *info = (ProcessInfo *)value;
      kill(info->pid, SIGKILL);
      g_free((gpointer)info->args);
      g_free(info);
    }
  }

  g_application_quit(g_application_get_default());
  return FALSE;
}


void desktop_info_table_key_destroy(gpointer data) {
  g_free(data); // gchar *name
}
void desktop_info_table_value_destroy(gpointer data) {
  g_object_unref(data); // GDesktopAppInfo *
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
  GHashTable *desktopInfoTable = g_hash_table_new_full(g_str_hash, g_str_equal, desktop_info_table_key_destroy, desktop_info_table_value_destroy);
  
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
      
      launch_process(commandline, autoRestart, 0, (guint)delay);
      g_hash_table_iter_remove(&iter);
    }
    
    g_free(thisPhase);
  }
}

static gboolean process_launch_delay_cb(gpointer userdata);
static void on_process_exit(GPid pid, gint status, gpointer userdata);

/*
 * Launches a process.
 * <args> contains the entire command line.
 * If <autostart> is true, the process will restart automatically if it exits with a non-zero exit status.
 * <delay> number of seconds to wait before starting
 */
static void launch_process(const gchar *args, gboolean autorestart, guint restartCount, guint delay)
{
  ProcessInfo *info = g_new(ProcessInfo, 1);
  info->pid = 0;
  info->args = g_strdup(args);
  info->autorestart = autorestart;
  info->restartCount = restartCount;
  
  // Hold for the processes (released when process closes)
  g_application_hold(g_application_get_default());
  
  if(delay > 0)
    g_timeout_add_seconds(delay, process_launch_delay_cb, info);
  else
    process_launch_delay_cb(info);
}

/*
 * Called from launch_process(), and actually does the launching.
 * Puts a hold on the session application so that it doesn't close while the child process is open.
 * Adds the process to the launched process table.
 */
static gboolean process_launch_delay_cb(gpointer userdata)
{
  if(!userdata)
  {
    g_application_hold(g_application_get_default());
    return FALSE;
  }
  
  ProcessInfo *info = (ProcessInfo *)userdata;
  
  GPid pid = 0;
  GError *e = NULL;

  gchar **argsSplit = g_strsplit(info->args, " ", -1);
  g_spawn_async(NULL, argsSplit, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &e);
  g_strfreev(argsSplit);
  
  if(e)
  {
    g_critical("Failed to start process with args '%s' (%s)", info->args, e->message);
    g_error_free(e);
    g_application_hold(g_application_get_default());
    g_free((gpointer)info->args);
    g_free(info);
    return FALSE;
  }
  
  info->pid = pid;
  g_hash_table_insert(LaunchedProcessesTable, (gpointer)(gintptr)pid, (gpointer)info);
  
  if(pid)
    g_child_watch_add(pid, on_process_exit, info);
  
  return FALSE; // Don't continue to call this callback
}

/*
 * Called when a process exits.
 * Removes the process from the launched processes table, releases a hold on the session application, 
 * and also takes care of restarting the process if it crashed (and it hasn't crashed too many times
 * already).
 * If the process exiting is the graphite-panel, and it exited due to a crash, and it has already been
 * restarted the maximum number of times, the session will quit(). If the panel exits cleanly via
 * logout, the session will also quit().
 * The panel can also exit with a code of 120 to specify that it should be restarted without counting
 * as a crash (for reloading applets).
 */
static void on_process_exit(GPid pid, gint status, gpointer userdata)
{
  g_hash_table_remove(LaunchedProcessesTable, (gpointer)(gintptr)pid);
  g_spawn_close_pid(pid);
  g_application_release(g_application_get_default());

  if(!userdata || Closed)
    return;
  
  ProcessInfo *info = (ProcessInfo *)userdata;
  gboolean isPanel = g_str_has_prefix(info->args, "/usr/share/graphene/graphene-panel");
  
  // g_message("Process '%s' exited with status %i (restart? %i rc? %i ispanel? %i closing? %i)", info->args, status, info->autorestart, info->restartCount, isPanel, Closing);
  
  if(status != 0 && info->autorestart && !Closing)
  {
    if(isPanel && WEXITSTATUS(status) == 120) // Special exit code for rebooting the panel (not an error)
                                              // WEXITSTATUS gets the lower 8 bits of the status, which is the actual value the program returned from main().
    {
      launch_process(info->args, TRUE, 0, 0); // Reset restartCount
    }
    else if(info->restartCount < MAX_RESTARTS)
    {
      launch_process(info->args, TRUE, info->restartCount+1, 1);
    }
    else if(isPanel)
    {
      // Panel has crashed too many times! Quit session.
      g_critical("The system panel has crashed too many times! Exiting session...");
      quit(); 
    }
    else
    {
      g_critical("The application with args '%s' has crashed too many times, and will not be automatically restarted.", info->args);
    }
  }
  else if(status == 0 && isPanel && !Closing)
  {
    // Panel has quit cleanly (indicating a logout). Quit session.
    quit();
  }
  
  g_free((gpointer)info->args);
  g_free(info);
}

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
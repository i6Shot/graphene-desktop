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

#include "notifications.h"
#include <notifications-dbus-iface.h>
#include <gtk/gtk.h>

#define NOTIFICATION_DEFAULT_SHOW_TIME 5000 // ms
#define NOTIFICATION_URGENCY_LOW 0
#define NOTIFICATION_URGENCY_NORMAL 1
#define NOTIFICATION_URGENCY_CRITICAL 2
#define NOTIFICATION_SPACING 20 // pixels
#define NOTIFICATION_WIDTH 320
#define NOTIFICATION_HEIGHT 60
#define NOTIFICATION_DBUS_IFACE "org.freedesktop.Notifications"
#define NOTIFICATION_DBUS_PATH "/org/freedesktop/Notifications"

struct _GrapheneNotificationManager
{
  GObject parent;
  
  guint dbusNameId;
  DBusNotifications *dbusObject;
  guint32 nextNotificationId;
  GHashTable *notifications;
  guint32 failNotificationId;
};

typedef struct {
  guint32 id;
  gchar *appName;
  gchar *icon;
  gchar *summary;
  gchar *body;
  gchar *category;
  gint timeout;
  gint urgency;
  // Do not set these when creating a notification
  guint timeoutSourceTag;
  GtkWindow *window; // do not set before calling show_notification
  GrapheneNotificationManager *manager;
} NotificationInfo;


G_DEFINE_TYPE(GrapheneNotificationManager, graphene_notification_manager, G_TYPE_OBJECT)


static void graphene_notification_manager_dispose(GObject *self_);
static void notification_info_free(NotificationInfo *info);
static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, GrapheneNotificationManager *self);
static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, GrapheneNotificationManager *self);
static gboolean on_dbus_call_get_capabilities(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation, DBusNotifications *object);
static gboolean on_dbus_call_notify(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation,
  const gchar *app_name, guint replaces_id, const gchar *app_icon, const gchar *summary, const gchar *body,
  const gchar * const *actions, GVariant *hints, gint expire_timeout, DBusNotifications *object);
static gboolean on_dbus_call_close_notification(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation, guint id, DBusNotifications *object);
static gboolean on_dbus_call_get_server_information(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation, DBusNotifications *object);
static void show_notification(GrapheneNotificationManager *self, NotificationInfo *info);
static void post_server_fail_notification(GrapheneNotificationManager *self);
static void remove_notification(GrapheneNotificationManager *self, guint32 id);
static void update_notification_windows(GrapheneNotificationManager *self);
static gboolean on_notification_clicked(GtkWindow *window, GdkEventButton *event, NotificationInfo *notificationInfo);


GrapheneNotificationManager* graphene_notification_manager_new(void)
{
  return GRAPHENE_NOTIFICATION_MANAGER(g_object_new(GRAPHENE_TYPE_NOTIFICATION_MANAGER, NULL));
}

GrapheneNotificationManager * graphene_notification_manager_get_default(void)
{
  static GrapheneNotificationManager *manager = NULL;
  if(!GRAPHENE_IS_NOTIFICATION_MANAGER(manager))
  {
    manager = graphene_notification_manager_new();
    return manager;
  }
  return g_object_ref(manager);
}

static void graphene_notification_manager_class_init(GrapheneNotificationManagerClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->dispose = graphene_notification_manager_dispose;
}

static void graphene_notification_manager_init(GrapheneNotificationManager *self)
{
  self->nextNotificationId = 1;
  self->notifications = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)notification_info_free);

  self->dbusObject = dbus_notifications_skeleton_new();

  self->dbusNameId = g_bus_own_name(G_BUS_TYPE_SESSION, NOTIFICATION_DBUS_IFACE, G_BUS_NAME_OWNER_FLAGS_REPLACE,
    NULL, (GBusNameAcquiredCallback)on_dbus_name_acquired, (GBusNameLostCallback)on_dbus_name_lost, self, NULL);
    
  if(!self->dbusNameId)
    post_server_fail_notification(self);
}

static void graphene_notification_manager_dispose(GObject *self_)
{
  GrapheneNotificationManager *self = GRAPHENE_NOTIFICATION_MANAGER(self_);
  
  g_clear_object(&self->dbusObject);
  
  if(self->dbusNameId)
    g_bus_unown_name(self->dbusNameId);
  self->dbusNameId = 0;
  
  G_OBJECT_CLASS(graphene_notification_manager_parent_class)->dispose(self_);
}

static void notification_info_free(NotificationInfo *info)
{
  gtk_widget_destroy(GTK_WIDGET(info->window));
  g_free(info->appName);
  g_free(info->icon);
  g_free(info->summary);
  g_free(info->body);
  g_free(info->category);
  g_free(info);
}

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, GrapheneNotificationManager *self)
{
  if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(self->dbusObject), connection, NOTIFICATION_DBUS_PATH, NULL))
    post_server_fail_notification(self);
  
  g_signal_connect_swapped(self->dbusObject, "handle-get-capabilities", G_CALLBACK(on_dbus_call_get_capabilities), self);
  g_signal_connect_swapped(self->dbusObject, "handle-notify", G_CALLBACK(on_dbus_call_notify), self);
  g_signal_connect_swapped(self->dbusObject, "handle-close-notification", G_CALLBACK(on_dbus_call_close_notification), self);
  g_signal_connect_swapped(self->dbusObject, "handle-get-server-information", G_CALLBACK(on_dbus_call_get_server_information), self);
}

static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, GrapheneNotificationManager *self)
{
  post_server_fail_notification(self);
}

static gboolean on_dbus_call_get_capabilities(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation,
  DBusNotifications *object)
{
  const gchar * const capabilities[] = {"body", NULL};
  dbus_notifications_complete_get_capabilities(object, invocation, capabilities);
  return TRUE;
}

static gboolean on_dbus_call_notify(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation,
  const gchar *app_name, guint replaces_id, const gchar *app_icon, const gchar *summary, const gchar *body,
  const gchar * const *actions, GVariant *hints, gint expire_timeout, DBusNotifications *object)
{
  NotificationInfo *info = g_new0(NotificationInfo, 1);
  info->id = replaces_id;
  info->appName = g_strdup(app_name);
  info->icon = g_strdup(app_icon);
  info->summary = g_strdup(summary);
  info->body = g_strdup(body);
  info->category = NULL; // TODO: Get from hints
  info->timeout = expire_timeout;
  info->urgency = NOTIFICATION_URGENCY_NORMAL; // TODO: Get from hints
  
  show_notification(self, info);
  dbus_notifications_complete_notify(object, invocation, info->id);
  return TRUE;
}

static gboolean on_dbus_call_close_notification(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation,
  guint id, DBusNotifications *object)
{
  remove_notification(self, id);
  dbus_notifications_complete_close_notification(object, invocation);
  return TRUE;
}

static gboolean on_dbus_call_get_server_information(GrapheneNotificationManager *self, GDBusMethodInvocation *invocation,
  DBusNotifications *object)
{
  dbus_notifications_complete_get_server_information(object, invocation, "Graphene Notifications", "Velt", "0.2", "1.2");
  return TRUE;
}

static gboolean show_notification_remove_cb(NotificationInfo *info)
{
  remove_notification(info->manager, info->id);
  return FALSE; // Don't call again
}

/*
 * Shows a notification on the screen. Takes a NotificationInfo *info, which must be heap-allocated.
 * The memory for info will be automatically freed when the notification is removed. All strings in
 * info must also be heap-allocated, and will also be freed when the notification is removed.
 *
 * The id value in NotificationInfo should be 0 for a new ID, or an existing ID to replace a
 * notification.
 *
 * Values in info will be changed to their default values if an 'unspecified' value is passed.
 * For example, 0 for id goes to the new ID, and -1 for timeout goes to default number of seconds.
 * NULLs for strings are also allowed. Defaults will be used in their place.
 *
 * See https://developer.gnome.org/notification-spec/ for more info.
 */
static void show_notification(GrapheneNotificationManager *self, NotificationInfo *info)
{
  if(info->id == 0)
  {
    if(self->nextNotificationId == 0)
      self->nextNotificationId = 1;

    info->id = self->nextNotificationId++;
  }
  
  if(info->timeout < 0)
    info->timeout = NOTIFICATION_DEFAULT_SHOW_TIME; // milliseconds
  
  info->manager = self;
  
  // Create popup window
  {
    info->window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_POPUP));
    GtkStyleContext *style = gtk_widget_get_style_context(GTK_WIDGET(info->window));
    gtk_style_context_add_class(style, "notification");
    
    g_signal_connect(info->window, "button_press_event", G_CALLBACK(on_notification_clicked), info);
    
    GtkBox *hBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_box_set_homogeneous(hBox, FALSE);

    GtkImage *icon = GTK_IMAGE(gtk_image_new_from_icon_name(info->icon, GTK_ICON_SIZE_DIALOG));
    gtk_box_pack_start(hBox, GTK_WIDGET(icon), FALSE, FALSE, 5);
    
    GtkBox *vBox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_box_set_homogeneous(vBox, FALSE);
    gtk_box_pack_start(hBox, GTK_WIDGET(vBox), TRUE, TRUE, 5);
    
    GtkLabel *summaryLabel = GTK_LABEL(gtk_label_new(NULL));
    gtk_label_set_markup(summaryLabel, info->summary);
    gtk_widget_set_halign(GTK_WIDGET(summaryLabel), GTK_ALIGN_START);
    gtk_label_set_ellipsize(summaryLabel, PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(vBox, GTK_WIDGET(summaryLabel), TRUE, TRUE, 0);
    
    GtkLabel *bodyLabel = GTK_LABEL(gtk_label_new(NULL));
    gchar *bodyMarkup = g_strdup_printf("<span size='smaller'>%s</span>", info->body);
    gtk_label_set_markup(bodyLabel, bodyMarkup);
    g_free(bodyMarkup);
    gtk_widget_set_halign(GTK_WIDGET(bodyLabel), GTK_ALIGN_START);
    gtk_label_set_ellipsize(bodyLabel, PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(vBox, GTK_WIDGET(bodyLabel), TRUE, TRUE, 0);
    
    gtk_window_resize(info->window, NOTIFICATION_WIDTH, NOTIFICATION_HEIGHT);

    gtk_container_add(GTK_CONTAINER(info->window), GTK_WIDGET(hBox));
    gtk_widget_show_all(GTK_WIDGET(info->window));
  }

  // Add to table
  g_hash_table_insert(self->notifications, GUINT_TO_POINTER(info->id), info);
  
  info->timeoutSourceTag = 0;
  if(info->timeout > 0 && info->urgency != NOTIFICATION_URGENCY_CRITICAL)
    info->timeoutSourceTag = g_timeout_add(info->timeout, (GSourceFunc)show_notification_remove_cb, info);
  
  update_notification_windows(self);
  
  // If a notification was just posted, the server can't be failed
  if(self->failNotificationId)
    remove_notification(self, self->failNotificationId);
}

static void post_server_fail_notification(GrapheneNotificationManager *self)
{
  if(self->failNotificationId)
    remove_notification(self, self->failNotificationId);
  self->failNotificationId = 0;
  
  NotificationInfo *info = g_new0(NotificationInfo, 1);
  info->icon = g_strdup("dialog-error");
  info->summary = g_strdup("System Notification Server Failed");
  info->body = g_strdup("You may not receive any notifications until you relog.");
  info->urgency = NOTIFICATION_URGENCY_CRITICAL;
  show_notification(self, info);
  self->failNotificationId = info->id;
}

static void remove_notification(GrapheneNotificationManager *self, guint32 id)
{
  NotificationInfo *info = g_hash_table_lookup(self->notifications, GUINT_TO_POINTER(id));
  if(info && info->timeoutSourceTag > 0)
    g_source_remove(info->timeoutSourceTag);
  
  if(id == self->failNotificationId)
    self->failNotificationId = 0;
  
  g_hash_table_remove(self->notifications, GUINT_TO_POINTER(id)); // Destroys the window
  update_notification_windows(self);
}

static gint notification_compare_func(gconstpointer a, gconstpointer b)
{
  // TODO: Sort "critical" notifications to the top
  return (((const NotificationInfo *)a)->id < ((const NotificationInfo *)b)->id) ? 1 : -1; // Sort newest to the top
}

static void update_notification_windows(GrapheneNotificationManager *self)
{
  GdkRectangle monitorRect;
  GdkScreen *screen = gdk_screen_get_default();
  gdk_screen_get_monitor_geometry(screen, gdk_screen_get_primary_monitor(screen), &monitorRect);

  GList *notificationList = g_hash_table_get_values(self->notifications); // NotificationInfo* list
  notificationList = g_list_sort(notificationList, (GCompareFunc)notification_compare_func);
    
  guint i=0;
  for(GList *notification=notificationList; notification; notification=notification->next)
  {
    NotificationInfo *n = notification->data;
    gtk_window_move(n->window, monitorRect.x + NOTIFICATION_SPACING, monitorRect.y + NOTIFICATION_SPACING + i*(NOTIFICATION_HEIGHT + NOTIFICATION_SPACING));
    ++i;
  }
}

static gboolean on_notification_clicked(GtkWindow *window, GdkEventButton *event, NotificationInfo *notificationInfo)
{
  if(event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_PRIMARY)
  {
    remove_notification(notificationInfo->manager, notificationInfo->id);
    return GDK_EVENT_STOP;
  }
  return GDK_EVENT_PROPAGATE;
}
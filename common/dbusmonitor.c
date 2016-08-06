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

#include "dbusmonitor.h"

struct _GrapheneDBusMonitor
{
  GObject parent;
  
  GDBusConnection *connection;
  GBusType busType;
  GCancellable *cancellable;
  
  gchar *name, *path, *iface;
  GHashTable *signals;
  
  GList *updateOnConnect;
  gboolean updateAllOnConnect;
};

typedef struct
{
  gchar *signalIface;
  gchar *signal;
  gchar *propertyIface;
  gchar *property; // The property/iface updated by the signal, or NULL for all
  GDBusSignalCallback callback; // Function to call when signal is sent
  guint signalSubId; // Signal subscription id (can be 0 for PropertiesChanged)
  GrapheneDBusMonitor *monitor;
} SignalInfo;

typedef struct
{
  gchar *iface;
  gchar *name;
  GrapheneDBusMonitor *monitor;
} PropertyInfo;

enum
{
  PROP_0,
  PROP_BUS_TYPE,
  PROP_NAME,
  PROP_PATH,
  PROP_IFACE,
  PROP_LAST
};

enum
{
  SIGNAL_0,
  SIGNAL_UPDATE,
  SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

static void graphene_dbus_monitor_constructed(GObject *_self);
static void graphene_dbus_monitor_finalize(GObject *object);
static void graphene_dbus_monitor_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void graphene_dbus_monitor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void free_signal_info(SignalInfo *info);
static void on_bus_ready(GObject *sourceObject, GAsyncResult *res, GrapheneDBusMonitor *self);
static void add_signal_watch(GrapheneDBusMonitor *self, SignalInfo *info);
static void on_properties_changed_signal(GDBusConnection *connection, const gchar *senderName, const gchar *objectPath, const gchar *interfaceName, const gchar *signalName, GVariant *parameters, SignalInfo *info);
static void on_general_property_signal(GDBusConnection *connection, const gchar *senderName, const gchar *objectPath, const gchar *interfaceName, const gchar *signalName, GVariant *parameters, SignalInfo *info);
static void on_property_get(GDBusConnection *connection, GAsyncResult *res, PropertyInfo *info);
static void on_property_get_all(GDBusConnection *connection, GAsyncResult *res, GrapheneDBusMonitor *self);

G_DEFINE_TYPE(GrapheneDBusMonitor, graphene_dbus_monitor, G_TYPE_OBJECT)


GrapheneDBusMonitor* graphene_dbus_monitor_new(GBusType bustype, const gchar *name, const gchar *path, const gchar *iface)
{
  return GRAPHENE_DBUS_MONITOR(g_object_new(GRAPHENE_TYPE_DBUS_MONITOR, "bustype", bustype, "name", name, "path", path, "iface", iface, NULL));
}
static void graphene_dbus_monitor_class_init(GrapheneDBusMonitorClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  
  gobjectClass->constructed = graphene_dbus_monitor_constructed;
  gobjectClass->finalize = graphene_dbus_monitor_finalize;
  gobjectClass->set_property = graphene_dbus_monitor_set_property;
  gobjectClass->get_property = graphene_dbus_monitor_get_property;
  
  properties[PROP_BUS_TYPE] = g_param_spec_int("bustype", "gbustype", "The GBusType that should be connected to.", -1, 2, 2, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  properties[PROP_NAME] = g_param_spec_string("name", "name", "Well-known or unique name to monitor.", NULL, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  properties[PROP_PATH] = g_param_spec_string("path", "path", "Path of object on name", NULL, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  properties[PROP_IFACE] = g_param_spec_string("iface", "iface", "Interface to monitor on object at path", NULL, G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties(gobjectClass, PROP_LAST, properties);
  
  /*
   * params (in order): name (s), path (s), iface (s), property (s), propertyValue (GVariant)
   */
  signals[SIGNAL_UPDATE] = g_signal_new("update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_VARIANT);
}

static void graphene_dbus_monitor_init(GrapheneDBusMonitor *self)
{
}

static void graphene_dbus_monitor_constructed(GObject *_self)
{
  GrapheneDBusMonitor *self = GRAPHENE_DBUS_MONITOR(_self);
  
  // Make sure name and path are set and valid
  if(!self->name || !self->path ||
     !g_dbus_is_name(self->name) ||
     !g_variant_is_object_path(self->path))
  {
    g_warning("GrapheneDBusMonitor: Invalid name/path");
    return;
  }
  
  // Make sure iface is valid if it is set
  if(self->iface && !g_dbus_is_interface_name(self->iface))
  {
    g_warning("GrapheneDBusMonitor: Invalid interface");
    return;
  }
  
  self->signals = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)free_signal_info);
  self->cancellable = g_cancellable_new();
  
  g_bus_get(self->busType, self->cancellable, (GAsyncReadyCallback)on_bus_ready, self);
}

static void graphene_dbus_monitor_finalize(GObject *object)
{
  GrapheneDBusMonitor *self = GRAPHENE_DBUS_MONITOR(object);
  g_cancellable_cancel(self->cancellable);
  g_clear_object(&self->cancellable);
  if(self->signals)
    g_hash_table_unref(self->signals);
  self->signals = NULL;
  g_clear_object(&self->connection);
  g_clear_pointer(&self->name, g_free);
  g_clear_pointer(&self->path, g_free);
  g_clear_pointer(&self->iface, g_free);
  G_OBJECT_CLASS(graphene_dbus_monitor_parent_class)->finalize(object);
}

static void graphene_dbus_monitor_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GrapheneDBusMonitor *self = GRAPHENE_DBUS_MONITOR(object);
  
  switch (prop_id)
  {
  case PROP_BUS_TYPE: self->busType = g_value_get_int(value); break;
  case PROP_NAME: self->name = g_strdup(g_value_get_string(value)); break;
  case PROP_PATH: self->path = g_strdup(g_value_get_string(value)); break;
  case PROP_IFACE: self->iface = g_strdup(g_value_get_string(value)); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); break;
  }
}

static void graphene_dbus_monitor_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GrapheneDBusMonitor *self = GRAPHENE_DBUS_MONITOR(object);

  switch (prop_id)
  {
  case PROP_BUS_TYPE: g_value_set_int(value, self->busType); break;
  case PROP_NAME: g_value_set_string(value, self->name); break;
  case PROP_PATH: g_value_set_string(value, self->path); break;
  case PROP_IFACE: g_value_set_string(value, self->iface); break;
  default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec); break;
  }
}

void graphene_dbus_monitor_update_all(GrapheneDBusMonitor *monitor)
{
  if(!monitor || !monitor->iface)
    return;
  
  if(!monitor->connection)
  {
    monitor->updateAllOnConnect = TRUE;
    return;
  }
  
  g_dbus_connection_call(monitor->connection,
    monitor->name,
    monitor->path,
    "org.freedesktop.DBus.Properties",
    "GetAll",
    g_variant_new("(s)", monitor->iface),
    G_VARIANT_TYPE("(a{sv})"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    monitor->cancellable,
    (GAsyncReadyCallback)on_property_get_all,
    monitor);  
}

void graphene_dbus_monitor_update_property(GrapheneDBusMonitor *monitor, const gchar *property)
{
  if(!monitor || !monitor->iface)
    return;
  
  if(!monitor->connection)
  {
    // Only add the property if it hasn't been added yet
    if(!g_list_find_custom(monitor->updateOnConnect, property, (GCompareFunc)g_strcmp0))    
      monitor->updateOnConnect = g_list_prepend(monitor->updateOnConnect, g_strdup(property));
    return;
  }
  
  PropertyInfo *propInfo = g_new0(PropertyInfo, 1); // Freed in on_property_get
  propInfo->iface = g_strdup(monitor->iface);
  propInfo->name = g_strdup(property);
  propInfo->monitor = monitor;
    
  g_dbus_connection_call(monitor->connection,
    monitor->name,
    monitor->path,
    "org.freedesktop.DBus.Properties",
    "Get",
    g_variant_new("(ss)", monitor->iface, property),
    G_VARIANT_TYPE("(v)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    monitor->cancellable,
    (GAsyncReadyCallback)on_property_get,
    propInfo);
}

void graphene_dbus_monitor_add_update_signal(GrapheneDBusMonitor *monitor, const gchar *signal, const gchar *property)
{
  if(!monitor || !monitor->iface)
    return;
  
  SignalInfo *info = g_new0(SignalInfo, 1);
  info->signalIface = g_strdup(monitor->iface);
  info->propertyIface = g_strdup(monitor->iface);
  info->signal = g_strdup(signal);
  info->property = g_strdup(property);
  info->callback = (GDBusSignalCallback)on_general_property_signal;
  info->monitor = monitor;
  
  g_hash_table_insert(monitor->signals, g_strdup(signal), info);
  add_signal_watch(monitor, info);
}

static void free_signal_info(SignalInfo *info)
{
  g_clear_pointer(&info->signalIface, g_free);
  g_clear_pointer(&info->signal, g_free);
  g_clear_pointer(&info->propertyIface, g_free);
  g_clear_pointer(&info->property, g_free);
  if(info->signalSubId && info->monitor && info->monitor->connection)
    g_dbus_connection_signal_unsubscribe(info->monitor->connection, info->signalSubId);
  info->signalSubId = 0;
  g_free(info);
}

static void on_bus_ready(GObject *sourceObject, GAsyncResult *res, GrapheneDBusMonitor *self)
{
  self->connection = g_bus_get_finish(res, NULL);
  if(!self->connection)
    return;

  // Setup a special monitor for the PropertiesChanged method
  SignalInfo *info = g_new0(SignalInfo, 1);
  info->signalIface = g_strdup("org.freedesktop.DBus.Properties");
  info->signal = g_strdup("PropertiesChanged");
  info->callback = (GDBusSignalCallback)on_properties_changed_signal;
  info->monitor = self;
  g_hash_table_insert(self->signals, g_strdup(info->signal), info);
  
  // Start watching all setup signals
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, self->signals);
  while(g_hash_table_iter_next(&iter, &key, &value))
    add_signal_watch(self, (SignalInfo *)value);
  
  // Update requested properties
  if(self->updateAllOnConnect)
  {
    graphene_dbus_monitor_update_all(self);
    self->updateAllOnConnect = FALSE;
  }
  else
  {
    self->updateOnConnect = g_list_reverse(self->updateOnConnect);
    for(GList *update=self->updateOnConnect; update != NULL; update = update->next)
      graphene_dbus_monitor_update_property(self, update->data);
    g_list_free_full(self->updateOnConnect, g_free);
  }
}

static void add_signal_watch(GrapheneDBusMonitor *self, SignalInfo *info)
{
  if(self->connection && info->callback && info->signalIface && info->signal &&
     g_dbus_is_interface_name(info->signalIface) &&
     g_dbus_is_member_name(info->signal))
  {
    info->signalSubId = g_dbus_connection_signal_subscribe(self->connection,
      self->name, info->signalIface, info->signal,
      self->path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
      info->callback, info, NULL);
  }
}

static void on_properties_changed_signal(GDBusConnection *connection,
  const gchar *senderName, const gchar *objectPath, const gchar *interfaceName,
  const gchar *signalName, GVariant *parameters, SignalInfo *info)
{
  if(!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sa{sv}as)")))
    return;
  
  const gchar *changedIface = NULL;
  GVariant *changedProperties = NULL;
  const gchar **invalidatedProperties = NULL; // strings const, but array must be freed
  g_variant_get(parameters, "(&s@a{sv}^a&s)", &changedIface, &changedProperties, &invalidatedProperties);
  
  // Only show changed properties matching the set interface
  if(info->monitor->iface && g_strcmp0(changedIface, info->monitor->iface) != 0)
  {
    g_clear_pointer(&changedProperties, g_variant_unref);
    g_clear_pointer(&invalidatedProperties, g_free);
    return;
  }
  
  // Emit update for each changed property
  GVariantIter iter;
  gchar *key;
  GVariant *value;
  g_variant_iter_init(&iter, changedProperties);
  while(g_variant_iter_loop(&iter, "{sv}", &key, &value))
  {
    g_signal_emit_by_name(info->monitor, "update",
      info->monitor->name,
      info->monitor->path,
      changedIface,
      key,
      value);
  }
  
  // Get each invalidated property
  for(gsize i=0;invalidatedProperties[i]!=NULL;++i)
  {
    PropertyInfo *propInfo = g_new0(PropertyInfo, 1);// Freed in on_property_get
    propInfo->iface = g_strdup(changedIface);
    propInfo->name = g_strdup(invalidatedProperties[i]);
    propInfo->monitor = info->monitor;
    
    g_dbus_connection_call(connection,
      info->monitor->name,
      info->monitor->path,
      "org.freedesktop.DBus.Properties",
      "Get",
      g_variant_new("(ss)", propInfo->iface, propInfo->name),
      G_VARIANT_TYPE("(v)"),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      info->monitor->cancellable,
      (GAsyncReadyCallback)on_property_get,
      propInfo);
  }
  
  g_clear_pointer(&changedProperties, g_variant_unref);
  g_clear_pointer(&invalidatedProperties, g_free);
}

static void on_general_property_signal(GDBusConnection *connection,
  const gchar *senderName, const gchar *objectPath, const gchar *interfaceName,
  const gchar *signalName, GVariant *parameters, SignalInfo *info)
{
  if(!info)
    return;
  
  if(!info->property)
  {
    graphene_dbus_monitor_update_all(info->monitor);
    return;
  }
  
  if(!g_dbus_is_interface_name(info->propertyIface) || !g_dbus_is_member_name(info->property))
    return;
  
  PropertyInfo *propInfo = g_new0(PropertyInfo, 1); // Freed in on_property_get
  propInfo->iface = g_strdup(info->propertyIface);
  propInfo->name = g_strdup(info->property);
  propInfo->monitor = info->monitor;
    
  g_dbus_connection_call(connection,
    info->monitor->name,
    info->monitor->path,
    "org.freedesktop.DBus.Properties",
    "Get",
    g_variant_new("(ss)", propInfo->iface, propInfo->name),
    G_VARIANT_TYPE("(v)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    info->monitor->cancellable,
    (GAsyncReadyCallback)on_property_get,
    propInfo);
}

/*
 * The PropertyInfo passed and its data are freed when this function completes.
 */
static void on_property_get(GDBusConnection *connection, GAsyncResult *res, PropertyInfo *propInfo)
{
  GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL); // type: (v)
  
  if(!propInfo)
  {
    if(ret)
      g_variant_unref(ret);
    return;
  }
  
  if(ret)
  {
    GVariant *boxed = g_variant_get_child_value(ret, 0); // (v) -> v
    GVariant *propVal = g_variant_get_variant(boxed); // v -> [prop type]
    
    g_signal_emit_by_name(propInfo->monitor, "update",
      propInfo->monitor->name,
      propInfo->monitor->path,
      propInfo->iface,
      propInfo->name,
      propVal);
      
    g_variant_unref(propVal);
    g_variant_unref(boxed);
    g_variant_unref(ret);
  }
  
  g_free(propInfo->iface);
  g_free(propInfo->name);
  g_free(propInfo);
}

static void on_property_get_all(GDBusConnection *connection, GAsyncResult *res, GrapheneDBusMonitor *self)
{
  GVariant *ret = g_dbus_connection_call_finish(connection, res, NULL);

  if(!self)
  {
    if(ret)
      g_variant_unref(ret);
    return;
  }
  
  if(!ret)
    return;
  
  GVariant *properties = NULL;
  g_variant_get(ret, "(@a{sv})", &properties);
  
  // Emit update for each changed property
  GVariantIter iter;
  gchar *prop;
  GVariant *value;
  g_variant_iter_init(&iter, properties);
  while(g_variant_iter_loop(&iter, "{sv}", &prop, &value))
  {
    g_signal_emit_by_name(self, "update",
      self->name,
      self->path,
      self->iface,
      prop,
      value);
  }
  
  g_variant_unref(properties);
  g_variant_unref(ret);
}

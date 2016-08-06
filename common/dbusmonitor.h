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
 * dbusmonitor.h/.c
 *
 * Setup this class on a well-known or unique DBus name, an object path, and
 * optionally an interface to monitor all properties changed on that object/interface.
 * An 'update' signal will be sent with the new property and its value when
 * it changes.
 * This class does not cache properties. Consequently, it is possible for an
 * update signal to be sent on a property without it actually changing.
 */

#ifndef __GRAPHENE_DBUS_MONITOR_H__
#define __GRAPHENE_DBUS_MONITOR_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_DBUS_MONITOR  graphene_dbus_monitor_get_type()
G_DECLARE_FINAL_TYPE(GrapheneDBusMonitor, graphene_dbus_monitor, GRAPHENE, DBUS_MONITOR, GObject)

/*
 * bustype: Bus to connect to
 * name: A well-known or unique DBus name
 * path: An object path found at 'name'
 * iface: An interface to monitor, or NULL to monitor all interfaces at 'path'
 */
GrapheneDBusMonitor * graphene_dbus_monitor_new(GBusType bustype, const gchar *name, const gchar *path, const gchar *iface);

/*
 * By default, this class won't send the update signal on any properties
 * until they change. Calling this method will retreieve all properties
 * specified by the iface set at the monitor's construction, or do nothing
 * if NULL was specified for the iface.
 */
void graphene_dbus_monitor_update_all(GrapheneDBusMonitor *monitor);

/*
 * Similar to graphene_dbus_monitor_update_all, but only updates a singal
 * property on the interface specified at the monitor's construction.
 */
void graphene_dbus_monitor_update_property(GrapheneDBusMonitor *monitor, const gchar *property);

/*
 * Some DBus objects, instead of notifying property updates with
 * the standard org.freedesktop.DBus.Properties.PropertiesChanged signal,
 * provide their own signal for updates to all or specific properties.
 *
 * This method adds a signal watch for 'signal' on the interface specified at
 * the monitor's construction, or does nothing if NULL was specified for the
 * iface. When the signal is emitted, the value of 'property' is updated,
 * or all properties on the interface are updated if 'property' is NULL.
 */
void graphene_dbus_monitor_add_update_signal(GrapheneDBusMonitor *monitor, const gchar *signal, const gchar *property);

G_END_DECLS

#endif /* __GRAPHENE_DBUS_MONITOR_H__ */
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
 * battery.h/.c
 */

#ifndef __GRAPHENE_BATTERY_H__
#define __GRAPHENE_BATTERY_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_BATTERY_INFO  graphene_battery_info_get_type()
G_DECLARE_FINAL_TYPE(GrapheneBatteryInfo, graphene_battery_info, GRAPHENE, BATTERY_INFO, GObject)
GrapheneBatteryInfo * graphene_battery_info_get_default(void); // Free with g_object_unref
gboolean graphene_battery_info_is_available(GrapheneBatteryInfo *self);
gdouble graphene_battery_info_get_percent(GrapheneBatteryInfo *self);
guint32 graphene_battery_info_get_state(GrapheneBatteryInfo *self);
const gchar * graphene_battery_info_get_state_string(GrapheneBatteryInfo *self);
gchar * graphene_battery_info_get_icon_name(GrapheneBatteryInfo *self); // Returns a newly-allocated string
gint64 graphene_battery_info_get_time(GrapheneBatteryInfo *self); // Time until charged or time until empty, depending on state

#define GRAPHENE_TYPE_BATTERY_ICON  graphene_battery_icon_get_type()
G_DECLARE_FINAL_TYPE(GrapheneBatteryIcon, graphene_battery_icon, GRAPHENE, BATTERY_ICON, GtkImage)
GrapheneBatteryIcon * graphene_battery_icon_new(void);

G_END_DECLS

#endif /* __GRAPHENE_BATTERY_H__ */
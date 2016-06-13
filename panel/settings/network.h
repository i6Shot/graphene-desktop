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
 * network.h/.c
 */

#ifndef __GRAPHENE_NETWORK_H__
#define __GRAPHENE_NETWORK_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_NETWORK_CONTROL  graphene_network_control_get_type()
G_DECLARE_FINAL_TYPE(GrapheneNetworkControl, graphene_network_control, GRAPHENE, NETWORK_CONTROL, GObject)
GrapheneNetworkControl * graphene_network_control_get_default(void); // Free with g_object_unref

guint32 graphene_network_control_get_status(GrapheneNetworkControl *net);
const gchar * graphene_network_control_get_ip(GrapheneNetworkControl *net);
gint graphene_network_control_get_signal_strength(GrapheneNetworkControl *net);
const gchar * graphene_network_control_get_essid(GrapheneNetworkControl *net);
const gchar * graphene_network_control_get_icon_name(GrapheneNetworkControl *net);

#define GRAPHENE_TYPE_NETWORK_ICON  graphene_network_icon_get_type()
G_DECLARE_FINAL_TYPE(GrapheneNetworkIcon, graphene_network_icon, GRAPHENE, NETWORK_ICON, GtkImage)
GrapheneNetworkIcon * graphene_network_icon_new();


G_END_DECLS

#endif /* __GRAPHENE_NETWORK_H__ */
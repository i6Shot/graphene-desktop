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
 * client.h/.c
 */

#ifndef __GRAPHENE_SESSION_CLIENT_H__
#define __GRAPHENE_SESSION_CLIENT_H__

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_SESSION_CLIENT  graphene_session_client_get_type()
G_DECLARE_FINAL_TYPE(GrapheneSessionClient, graphene_session_client, GRAPHENE, SESSION_CLIENT, GObject)

GrapheneSessionClient * graphene_session_client_new(GDBusConnection *connection, const gchar *clientId);

void          graphene_session_client_spawn(GrapheneSessionClient *self, guint delay);
void          graphene_session_client_register(GrapheneSessionClient *self, const gchar *sender, const gchar *appId);
void          graphene_session_client_unregister(GrapheneSessionClient *self);

gboolean      graphene_session_client_query_end_session(GrapheneSessionClient *self, gboolean forced);
void          graphene_session_client_end_session(GrapheneSessionClient *self, gboolean forced);
void          graphene_session_client_stop(GrapheneSessionClient *self);

const gchar * graphene_session_client_get_best_name(GrapheneSessionClient *self);
const gchar * graphene_session_client_get_id(GrapheneSessionClient *self);
const gchar * graphene_session_client_get_object_path(GrapheneSessionClient *self);
const gchar * graphene_session_client_get_app_id(GrapheneSessionClient *self);
const gchar * graphene_session_client_get_dbus_name(GrapheneSessionClient *self);
gboolean      graphene_session_client_get_is_active(GrapheneSessionClient *self);

G_END_DECLS

#endif /* __GRAPHENE_SESSION_CLIENT_H__ */

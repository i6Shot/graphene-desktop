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

#ifndef __GRAPHENE_PK_AUTH_DIALOG_H__
#define __GRAPHENE_PK_AUTH_DIALOG_H__

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_PK_AUTH_DIALOG graphene_pk_auth_dialog_get_type()
G_DECLARE_FINAL_TYPE(GraphenePKAuthDialog, graphene_pk_auth_dialog, GRAPHENE, PK_AUTH_DIALOG, ClutterActor);

/*
 * The Polkit Authentication Dialog fully handles authentication, and emits
 * the 'completed' signal when the request has either been successfully
 * authenticated, failed, or cancelled.
 */
GraphenePKAuthDialog * graphene_pk_auth_dialog_new(const gchar *actionId, const gchar *message, const gchar *iconName, const gchar *cookie, GVariant *identitiesV, GError **error);

void graphene_pk_auth_dialog_cancel(GraphenePKAuthDialog *dialog);

G_END_DECLS

#endif /* __GRAPHENE_PK_AUTH_DIALOG_H__ */


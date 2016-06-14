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
 * users.h/.c
 */

#ifndef __GRAPHENE_USERS_H__
#define __GRAPHENE_USERS_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_PROFILE_NAME_LABEL  graphene_profile_name_label_get_type()
G_DECLARE_FINAL_TYPE(GrapheneProfileNameLabel, graphene_profile_name_label, GRAPHENE, PROFILE_NAME_LABEL, GtkLabel)
GrapheneProfileNameLabel * graphene_profile_name_label_new(void);
void graphene_profile_name_label_set_user(GrapheneProfileNameLabel *label, const gchar *username);

#define GRAPHENE_TYPE_PROFILE_PICTURE  graphene_profile_picture_get_type()
G_DECLARE_FINAL_TYPE(GrapheneProfilePicture, graphene_profile_picture, GRAPHENE, PROFILE_PICTURE, GtkDrawingArea)
GrapheneProfilePicture * graphene_profile_picture_new(void);
void graphene_profile_picture_set_user(GrapheneProfilePicture *picture, const gchar *username);

G_END_DECLS

#endif /* __GRAPHENE_USERS_H__ */
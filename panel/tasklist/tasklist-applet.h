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
 * tasklist-applet.h/.c
 */

#ifndef __GRAPHENE_TASKLIST_APPLET_H__
#define __GRAPHENE_TASKLIST_APPLET_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_TASKLIST_APPLET  graphene_tasklist_applet_get_type()
G_DECLARE_FINAL_TYPE(GrapheneTasklistApplet, graphene_tasklist_applet, GRAPHENE, TASKLIST_APPLET, GtkBox)
GrapheneTasklistApplet * graphene_tasklist_applet_new();

G_END_DECLS

#endif /* __GRAPHENE_TASKLIST_APPLET_H__ */

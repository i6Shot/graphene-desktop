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
 * launcher-applet.h/.c
 */

#ifndef __GRAPHENE_LAUNCHER_APPLET_H__
#define __GRAPHENE_LAUNCHER_APPLET_H__

#include <gtk/gtk.h>
#include "panel.h"

G_BEGIN_DECLS

#define GRAPHENE_TYPE_LAUNCHER_APPLET  graphene_launcher_applet_get_type()
G_DECLARE_FINAL_TYPE(GrapheneLauncherApplet, graphene_launcher_applet, GRAPHENE, LAUNCHER_APPLET, GtkButton)

GrapheneLauncherApplet * graphene_launcher_applet_new();
void graphene_launcher_applet_set_panel(GrapheneLauncherApplet *self, GraphenePanel *panel);


#define GRAPHENE_TYPE_LAUNCHER_POPUP  graphene_launcher_popup_get_type()
G_DECLARE_FINAL_TYPE(GrapheneLauncherPopup, graphene_launcher_popup, GRAPHENE, LAUNCHER_POPUP, GtkWindow)

GrapheneLauncherPopup * graphene_launcher_popup_new();
void graphene_launcher_popup_set_panel(GrapheneLauncherPopup *self, GraphenePanel *panel);

G_END_DECLS

#endif /* __GRAPHENE_LAUNCHER_APPLET_H__ */

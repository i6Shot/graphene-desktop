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
 * settings-applet.h/.c
 */

#ifndef __GRAPHENE_SETTINGS_APPLET_H__
#define __GRAPHENE_SETTINGS_APPLET_H__

#include <gtk/gtk.h>
#include <panel.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_SETTINGS_APPLET  graphene_settings_applet_get_type()
G_DECLARE_FINAL_TYPE(GrapheneSettingsApplet, graphene_settings_applet, GRAPHENE, SETTINGS_APPLET, GtkButton)
GrapheneSettingsApplet * graphene_settings_applet_new();
void graphene_settings_applet_set_panel(GrapheneSettingsApplet *self, GraphenePanel *panel);

G_END_DECLS

#endif /* __GRAPHENE_SETTINGS_APPLET_H__ */

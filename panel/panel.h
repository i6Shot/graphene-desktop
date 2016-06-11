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
 * panel.h/.c
 * The Panel window, which displays itself at a docked position on the screen and automatically loads plugins to provide applets.
 */

#ifndef __GRAPHENE_PANEL_H__
#define __GRAPHENE_PANEL_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

// Declare the GraphenePanel class
#define GRAPHENE_TYPE_PANEL  graphene_panel_get_type()
G_DECLARE_FINAL_TYPE(GraphenePanel, graphene_panel, GRAPHENE, PANEL, GtkWindow)

// Public methods for GraphenePanel
GraphenePanel * graphene_panel_get_default       (void);

int             graphene_panel_capture_screen    (GraphenePanel *self);
int             graphene_panel_end_capture       (GraphenePanel *self);
void            graphene_panel_clear_capture     (GraphenePanel *self);

gint            graphene_panel_get_monitor       (GraphenePanel *self);
gint            graphene_panel_get_height        (GraphenePanel *self);

void            graphene_panel_logout            (GraphenePanel *self);
void            graphene_panel_shutdown          (GraphenePanel *self, gboolean reboot);

G_END_DECLS

#endif /* __GRAPHENE_PANEL_H__ */

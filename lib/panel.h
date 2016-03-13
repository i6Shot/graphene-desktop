/*
 * vos-desktop
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
 * The Panel class, which displays itself at a docked position on the screen and automatically loads plugins to provide applets.
 */

#ifndef __VOS_PANEL_H__
#define __VOS_PANEL_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

// Declare the VosPanel class
#define VOS_TYPE_PANEL  vos_panel_get_type()
G_DECLARE_FINAL_TYPE(VosPanel, vos_panel, VOS, PANEL, GtkWindow)

// Public methods for VosPanel
int             vos_panel_capture_screen    (VosPanel *self);
int             vos_panel_end_capture       (VosPanel *self);
void            vos_panel_clear_capture     (VosPanel *self);

gint            vos_panel_get_monitor       (VosPanel *self);
gint            vos_panel_get_height        (VosPanel *self);

void            vos_panel_logout            (VosPanel *self);
void            vos_panel_shutdown          (VosPanel *self, gboolean reboot);

G_END_DECLS

#endif /* __VOS_PANEL_H__ */

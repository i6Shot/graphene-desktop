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
 * system-volume-control.h/.c
 * Provides a simple interface for controlling system volume and mute (mostly a VERY simple interface for PulseAudio).
 */

#ifndef __GRAPHENE_SYSTEM_VOLUME_CONTROL_H__
#define __GRAPHENE_SYSTEM_VOLUME_CONTROL_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * Type declaration.
 */
#define GRAPHENE_TYPE_SYSTEM_VOLUME_CONTROL graphene_system_volume_control_get_type()
G_DECLARE_FINAL_TYPE(GrapheneSystemVolumeControl, graphene_system_volume_control, GRAPHENE, SYSTEM_VOLUME_CONTROL, GObject)

// Public methods
GrapheneSystemVolumeControl*  graphene_system_volume_control_new              (void)  G_GNUC_CONST;
gfloat                   graphene_system_volume_control_get_volume       (GrapheneSystemVolumeControl *self);
gboolean                 graphene_system_volume_control_get_is_muted     (GrapheneSystemVolumeControl *self);
gint                     graphene_system_volume_control_get_state        (GrapheneSystemVolumeControl *self);
void                     graphene_system_volume_control_set_volume       (GrapheneSystemVolumeControl *self, gfloat volume);
void                     graphene_system_volume_control_set_is_muted     (GrapheneSystemVolumeControl *self, gboolean is_muted);

G_END_DECLS

#endif /* __GRAPHENE_SYSTEM_VOLUME_CONTROL_H__ */

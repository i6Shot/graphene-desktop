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
 * system-volume-control.h/.c
 * Provides a simple interface for controlling system volume and mute (mostly a VERY simple interface for PortAudio).
 */

#ifndef __VOS_SYSTEM_VOLUME_CONTROL_H__
#define __VOS_SYSTEM_VOLUME_CONTROL_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * Type declaration.
 */
#define VOS_TYPE_SYSTEM_VOLUME_CONTROL vos_system_volume_control_get_type()
G_DECLARE_FINAL_TYPE(VosSystemVolumeControl, vos_system_volume_control, VOS, SYSTEM_VOLUME_CONTROL, GObject)

// Public methods
VosSystemVolumeControl*  vos_system_volume_control_new              (void)  G_GNUC_CONST;
gfloat                   vos_system_volume_control_get_volume       (VosSystemVolumeControl *self);
gboolean                 vos_system_volume_control_get_is_muted     (VosSystemVolumeControl *self);
gint                     vos_system_volume_control_get_state        (VosSystemVolumeControl *self);
void                     vos_system_volume_control_set_volume       (VosSystemVolumeControl *self, gfloat volume);
void                     vos_system_volume_control_set_is_muted     (VosSystemVolumeControl *self, gboolean is_muted);

G_END_DECLS

#endif /* __VOS_SYSTEM_VOLUME_CONTROL_H__ */

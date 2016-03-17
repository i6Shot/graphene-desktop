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
 * background.h/.c
 * The window manager's background actor. One of these is created for each monitor and assigned in on_monitors_changed() in wm.c.
 */

#ifndef __VOS_WM_BACKGROUND_H__
#define __VOS_WM_BACKGROUND_H__

#include <meta/main.h>
#include <meta/meta-background-group.h>
#include <meta/meta-background-actor.h>

G_BEGIN_DECLS

// Declare the VosWMBackground class
#define VOS_TYPE_WM_BACKGROUND  vos_wm_background_get_type()

#pragma GCC diagnostic ignored "-Wimplicit-function-declaration" // Same problem as in wm.h
G_DECLARE_FINAL_TYPE(VosWMBackground, vos_wm_background, VOS, WM_BACKGROUND, MetaBackgroundGroup)
#pragma GCC diagnostic warning "-Wimplicit-function-declaration"

// Public methods for VosWMBackground
VosWMBackground*       vos_wm_background_new               (MetaScreen *screen, int screenIndex);

G_END_DECLS

#endif /* __VOS_WM_BACKGROUND_H__ */
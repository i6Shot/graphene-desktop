/*
 * graphene-desktop
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
 * wm.h/.c
 * Graphene's window manager (a Mutter plugin)
 */

#ifndef __VOS_WM_H__
#define __VOS_WM_H__

#include <meta/main.h>
#include <meta/meta-plugin.h>

G_BEGIN_DECLS

// Declare the VosWM class
#define VOS_TYPE_WM  vos_wm_get_type()

#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
// G_DECLARE_FINAL_TYPE with MetaPlugin gives a warning for the unknown function 'glib_autoptr_cleanup_MetaPlugin'.
// I don't know how to fix that, but it doesn't seem to be a problem.
G_DECLARE_FINAL_TYPE(VosWM, vos_wm, VOS, WM, MetaPlugin)
#pragma GCC diagnostic warning "-Wimplicit-function-declaration"

// Public methods for VosWM
VosWM*       vos_wm_new               (void)  G_GNUC_CONST;

G_END_DECLS

#endif /* __VOS_WM_H__ */
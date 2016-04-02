/*
 * graphene-desktop
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
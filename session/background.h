/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
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
 * background.h/.c
 * The window manager's background actor. One of these is created for each monitor and assigned in on_monitors_changed() in wm.c.
 */

#ifndef __GRAPHENE_WM_BACKGROUND_H__
#define __GRAPHENE_WM_BACKGROUND_H__

#include <meta/main.h>
#include <meta/meta-background-group.h>
#include <meta/meta-background-actor.h>

G_BEGIN_DECLS

// Declare the GrapheneWMBackground class
#define GRAPHENE_TYPE_WM_BACKGROUND  graphene_wm_background_get_type()

#pragma GCC diagnostic ignored "-Wimplicit-function-declaration" // Same problem as in wm.h
G_DECLARE_FINAL_TYPE(GrapheneWMBackground, graphene_wm_background, GRAPHENE, WM_BACKGROUND, MetaBackgroundGroup)
#pragma GCC diagnostic warning "-Wimplicit-function-declaration"

// Public methods for GrapheneWMBackground
GrapheneWMBackground*       graphene_wm_background_new               (MetaScreen *screen, int screenIndex);

G_END_DECLS

#endif /* __GRAPHENE_WM_BACKGROUND_H__ */
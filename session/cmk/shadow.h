/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __CMK_SHADOW_H__
#define __CMK_SHADOW_H__

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define CMK_TYPE_SHADOW cmk_shadow_get_type()
G_DECLARE_FINAL_TYPE(CMKShadow, cmk_shadow, CMK, SHADOW, ClutterActor);

CMKShadow * cmk_shadow_new();

void cmk_shadow_set_blur(CMKShadow *shadow, gfloat radius);
void cmk_shadow_set_vblur(CMKShadow *shadow, gfloat radius);
void cmk_shadow_set_hblur(CMKShadow *shadow, gfloat radius);
gfloat cmk_shadow_get_vblur(CMKShadow *shadow);
gfloat cmk_shadow_get_hblur(CMKShadow *shadow);

G_END_DECLS

#endif


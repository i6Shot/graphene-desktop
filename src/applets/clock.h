/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __GRAPHENE_CLOCK_APPLET_H__
#define __GRAPHENE_CLOCK_APPLET_H__

#include "../cmk/cmk-widget.h"

G_BEGIN_DECLS

#define GRAPHENE_TYPE_CLOCK_APPLET graphene_clock_applet_get_type()
G_DECLARE_FINAL_TYPE(GrapheneClockApplet, graphene_clock_applet, GRAPHENE, CLOCK_APPLET, CmkWidget);

GrapheneClockApplet * graphene_clock_applet_new(void);

G_END_DECLS

#endif


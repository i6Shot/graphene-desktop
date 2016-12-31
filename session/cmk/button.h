/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __CMK_BUTTON_H__
#define __CMK_BUTTON_H__

#include <clutter/clutter.h>
#include "cmk-widget.h"

G_BEGIN_DECLS

#define CMK_TYPE_BUTTON cmk_button_get_type()
G_DECLARE_FINAL_TYPE(CMKButton, cmk_button, CMK, BUTTON, CMKWidget);

CMKButton *cmk_button_new();
CMKButton *cmk_button_new_with_text(const gchar *text);

void cmk_button_set_text(CMKButton *button, const gchar *text);
const gchar * cmk_button_get_text(CMKButton *button);

G_END_DECLS

#endif


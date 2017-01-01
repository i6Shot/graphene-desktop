/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * This class creates a generic message dialog with button responses, but does
 * not handle displaying it to the user / centering it on screen.
 */

#ifndef __GRAPHENE_MESSAGE_DIALOG_H__
#define __GRAPHENE_MESSAGE_DIALOG_H__

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_MESSAGE_DIALOG graphene_message_dialog_get_type()
G_DECLARE_FINAL_TYPE(GrapheneMessageDialog, graphene_message_dialog, GRAPHENE, MESSAGE_DIALOG, ClutterActor);

GrapheneMessageDialog * graphene_message_dialog_new();
GrapheneLogoutDialog * graphene_logout_dialog_new();

G_END_DECLS

#endif

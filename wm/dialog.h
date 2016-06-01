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
 * dialog.h/.c
 * A ClutterActor subclass to display a modal dialog.
 */

#ifndef __GRAPHENE_WM_DIALOG_H__
#define __GRAPHENE_WM_DIALOG_H__

#include <meta/main.h>
#include <meta/meta-plugin.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_WM_DIALOG  graphene_wm_dialog_get_type()
G_DECLARE_FINAL_TYPE(GrapheneWMDialog, graphene_wm_dialog, GRAPHENE, WM_DIALOG, ClutterActor)

GrapheneWMDialog * graphene_wm_dialog_new(ClutterActor *content, const gchar **buttons);
void               graphene_wm_dialog_show(GrapheneWMDialog *dialog, MetaScreen *screen, int monitorIndex);
void               graphene_wm_dialog_set_buttons(GrapheneWMDialog *dialog, const gchar **buttons);

G_END_DECLS

#endif /* __GRAPHENE_WM_DIALOG_H__ */

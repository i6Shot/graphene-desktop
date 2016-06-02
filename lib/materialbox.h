/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
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
 * materialbox.h/.c
 * A GTK container for displaying a center widget (usually another container), along with
 * widgets (containers) that can slide in from each edge and overlay the center.
 */
 
#ifndef __GRAPHENE_MATERIAL_BOX__
#define __GRAPHENE_MATERIAL_BOX__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GRAPHENE_TYPE_MATERIAL_BOX  graphene_material_box_get_type()
G_DECLARE_FINAL_TYPE(GrapheneMaterialBox, graphene_material_box, GRAPHENE, MATERIAL_BOX, GtkContainer)

/**
 * GrapheneMaterialBoxPosition:
 * @GRAPHENE_MATERIAL_BOX_POSITION_TOP: The material slides in from the top of the box.
 * @GRAPHENE_MATERIAL_BOX_POSITION_BOTTOM: The material slides in from the bottom of the box.
 * @GRAPHENE_MATERIAL_BOX_POSITION_LEFT: The material slides in from the left of the box.
 * @GRAPHENE_MATERIAL_BOX_POSITION_RIGHT: The material slides in from the right of the box.
 * @GRAPHENE_MATERIAL_BOX_POSITION_CENTER: The material is the main background sheet.
 *
 * Material added to this box can use this to adjust their initial position.
 */
typedef enum
{
  GRAPHENE_MATERIAL_BOX_LOCATION_TOP,
  GRAPHENE_MATERIAL_BOX_LOCATION_BOTTOM,
  GRAPHENE_MATERIAL_BOX_LOCATION_LEFT,
  GRAPHENE_MATERIAL_BOX_LOCATION_RIGHT,
  GRAPHENE_MATERIAL_BOX_LOCATION_CENTER,
} GrapheneMaterialBoxSheetLocation;

typedef GtkWidget GrapheneMaterialSheet;
#define GRAPHENE_IS_MATERIAL_SHEET GTK_IS_WIDGET
#define GRAPHENE_MATERIAL_SHEET GTK_WIDGET

GtkWidget* graphene_material_box_new         (void);
void       graphene_material_box_add_sheet   (GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet, GrapheneMaterialBoxSheetLocation location);
void       graphene_material_box_show_sheet  (GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet);
void       graphene_material_box_hide_sheet  (GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet);

G_END_DECLS

#endif /* __GRAPHENE_MATERIAL_BOX__ */
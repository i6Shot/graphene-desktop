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
 * materialbox.h/.c
 * A GTK container for displaying a center widget (usually another container), along with
 * widgets (containers) that can slide in from each edge and overlay the center.
 */
 
#ifndef __VOS_MATERIAL_BOX__
#define __VOS_MATERIAL_BOX__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define VOS_TYPE_MATERIAL_BOX  vos_material_box_get_type()
G_DECLARE_FINAL_TYPE(VosMaterialBox, vos_material_box, VOS, MATERIAL_BOX, GtkContainer)

/**
 * VosMaterialBoxPosition:
 * @VOS_MATERIAL_BOX_POSITION_TOP: The material slides in from the top of the box.
 * @VOS_MATERIAL_BOX_POSITION_BOTTOM: The material slides in from the bottom of the box.
 * @VOS_MATERIAL_BOX_POSITION_LEFT: The material slides in from the left of the box.
 * @VOS_MATERIAL_BOX_POSITION_RIGHT: The material slides in from the right of the box.
 * @VOS_MATERIAL_BOX_POSITION_CENTER: The material is the main background sheet.
 *
 * Material added to this box can use this to adjust their initial position.
 */
typedef enum
{
  VOS_MATERIAL_BOX_LOCATION_TOP,
  VOS_MATERIAL_BOX_LOCATION_BOTTOM,
  VOS_MATERIAL_BOX_LOCATION_LEFT,
  VOS_MATERIAL_BOX_LOCATION_RIGHT,
  VOS_MATERIAL_BOX_LOCATION_CENTER,
} VosMaterialBoxSheetLocation;

typedef GtkWidget VosMaterialSheet;
#define VOS_IS_MATERIAL_SHEET GTK_IS_WIDGET
#define VOS_MATERIAL_SHEET GTK_WIDGET

GtkWidget* vos_material_box_new         (void);
void       vos_material_box_add_sheet   (VosMaterialBox *self, VosMaterialSheet *sheet, VosMaterialBoxSheetLocation location);
void       vos_material_box_show_sheet  (VosMaterialBox *self, VosMaterialSheet *sheet);
void       vos_material_box_hide_sheet  (VosMaterialBox *self, VosMaterialSheet *sheet);

G_END_DECLS

#endif /* __VOS_MATERIAL_BOX__ */
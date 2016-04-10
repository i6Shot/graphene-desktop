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
 * This should be compiled into libvos for GIntrospection, and NOT compiled into the panel application binary.
 */
 
#include "materialbox.h"

#define VOS_SHEET_TRANSITION_TIME (150*1000) // time in microseconds

typedef struct
{
  VosMaterialSheet *sheet;
  VosMaterialBoxSheetLocation location;
  gint64 animStartTime; // gdk_frame_clock_get_frame_time for the start of the animation
  gint64 animOffsetTime; // widget offset measured in units of time. 0=fully hidden, VOS_SHEET_TRANSITION_TIME=fully shown
  guint tickCallbackID;
  
} VosMaterialBoxSheetInfo;

struct _VosMaterialBox
{
  GtkContainer parent;
  
  GList *children;
  VosMaterialBoxSheetInfo *currentCenter;
};

enum {
  CHILD_PROP_0,
  CHILD_PROP_LOCATION
};

static GType vos_material_box_child_type(VosMaterialBox *self);
static void vos_material_box_add(VosMaterialBox *self, VosMaterialSheet *sheet);
static void vos_material_box_remove(VosMaterialBox *self, VosMaterialSheet *sheet);
static void vos_material_box_size_allocate(VosMaterialBox *self, GtkAllocation *allocation);
static void vos_material_box_get_preferred_width(VosMaterialBox *self, gint *minimum, gint *natural);
static void vos_material_box_get_preferred_height(VosMaterialBox *self, gint *minimum, gint *natural);
static void vos_material_box_forall(VosMaterialBox *self, gboolean include_internals, GtkCallback callback, gpointer callback_data);
static gboolean vos_material_box_draw(VosMaterialBox *self, cairo_t *cr);
static void vos_material_box_set_child_property(VosMaterialBox *self, VosMaterialSheet *sheet, guint property_id, const GValue *value, GParamSpec *pspec);
static void vos_material_box_get_child_property(VosMaterialBox *self, VosMaterialSheet *sheet, guint property_id, GValue *value, GParamSpec *pspec);
static void vos_material_box_show_all(VosMaterialBox *self);
static VosMaterialBoxSheetInfo * get_primary_sheet_info(VosMaterialBox *self);
static VosMaterialBoxSheetInfo * get_sheet_info_from_sheet(VosMaterialBox *self, VosMaterialSheet *sheet);

static void sheet_on_show(VosMaterialBox *self, VosMaterialSheet *sheet);
static void sheet_on_hide(VosMaterialBox *self, VosMaterialSheet *sheet);

G_DEFINE_TYPE(VosMaterialBox, vos_material_box, GTK_TYPE_CONTAINER)

static void vos_material_box_class_init (VosMaterialBoxClass *class)
{
  GtkWidgetClass *widgetClass = GTK_WIDGET_CLASS(class);
  GtkContainerClass *containerClass = GTK_CONTAINER_CLASS(class);

  widgetClass->get_preferred_width = vos_material_box_get_preferred_width;
  widgetClass->get_preferred_height = vos_material_box_get_preferred_height;
  widgetClass->size_allocate = vos_material_box_size_allocate;
  widgetClass->draw = vos_material_box_draw;
  widgetClass->show_all = vos_material_box_show_all;
  
  containerClass->add = vos_material_box_add;
  containerClass->remove = vos_material_box_remove;
  containerClass->forall = vos_material_box_forall;
  containerClass->child_type = vos_material_box_child_type;
  containerClass->set_child_property = vos_material_box_set_child_property;
  containerClass->get_child_property = vos_material_box_get_child_property;
  gtk_container_class_handle_border_width(containerClass);

  gtk_container_class_install_child_property(containerClass, CHILD_PROP_LOCATION,
                                              g_param_spec_int("location",
                                                                "location",
                                                                "location of material sheet in the box",
                                                                VOS_MATERIAL_BOX_LOCATION_TOP, VOS_MATERIAL_BOX_LOCATION_CENTER,
                                                                VOS_MATERIAL_BOX_LOCATION_CENTER,
                                                                G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB)); // GTK_PARAM_READWRITE
}

/**
 * vos_material_box_new:
 *
 * Creates a new #VosMaterialBox.
 *
 * Returns: a new #VosMaterialBox.
 */
GtkWidget* vos_material_box_new(void)
{
  return g_object_new(VOS_TYPE_MATERIAL_BOX, NULL);
}

static void vos_material_box_init(VosMaterialBox *self)
{
  gtk_widget_set_has_window(GTK_WIDGET(self), FALSE);
  self->currentCenter = NULL;
}

static GType vos_material_box_child_type(VosMaterialBox *self)
{
  return GTK_TYPE_WIDGET;
}

/**
 * vos_material_box_add_sheet:
 * @self: a #VosMaterialBox.
 * @sheet: the sheet (GtkContainer) to add.
 * @location: the VosMaterialBoxSheetLocation location to add the sheet at
 *
 * Adds a sheet to a #VosMaterialBox container at the given location.
 * The sheet is automatically hidden (gtk_widget_hide called on the sheet).
 */
void vos_material_box_add_sheet(VosMaterialBox *self, VosMaterialSheet *sheet, VosMaterialBoxSheetLocation location)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheet));

  VosMaterialBoxSheetInfo *sheetInfo = g_new0(VosMaterialBoxSheetInfo, 1);
  sheetInfo->sheet = sheet;
  sheetInfo->location = location;
  self->children = g_list_append(self->children, sheetInfo);
  
  gtk_widget_hide(GTK_WIDGET(sheet));
  
  g_signal_connect_swapped(sheet, "show", G_CALLBACK(sheet_on_show), self);
  g_signal_connect_swapped(sheet, "hide", G_CALLBACK(sheet_on_hide), self);
  
  gtk_widget_set_parent(GTK_WIDGET(sheetInfo->sheet), GTK_WIDGET(self));
}

static void vos_material_box_add(VosMaterialBox *self, VosMaterialSheet *sheet)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheet));
  
  vos_material_box_add_sheet(self, sheet, VOS_MATERIAL_BOX_LOCATION_LEFT);
}

static void vos_material_box_remove(VosMaterialBox *self, VosMaterialSheet *sheet)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheet));
  
  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;

    if(sheetInfo->sheet == sheet)
    {
      // TODO: Remove show/hide signal
      gtk_widget_unparent(sheetInfo->sheet);
      
      self->children = g_list_remove_link(self->children, children);
      g_list_free(children);
      g_free(sheetInfo);
      break;
    }
  }
}

// https://github.com/warrenm/AHEasing/blob/master/AHEasing/easing.c
gfloat cubic_ease_out(gfloat p)
{
	gfloat f = (p - 1);
	return f * f * f + 1;
}

static void vos_material_box_size_allocate(VosMaterialBox *self, GtkAllocation *allocation)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));

  gtk_widget_set_allocation(GTK_WIDGET(self), allocation);

  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;

    if(!gtk_widget_get_visible(sheetInfo->sheet))
      continue;

    GtkRequisition childRequisition = {0};
    gtk_widget_get_preferred_size(GTK_WIDGET(sheetInfo->sheet), &childRequisition, NULL);

    gdouble delta = 0;
    
    if(sheetInfo->location != VOS_MATERIAL_BOX_LOCATION_CENTER)
    {
      delta = MIN(MAX((gdouble)sheetInfo->animOffsetTime / VOS_SHEET_TRANSITION_TIME, 0), 1);
      delta = cubic_ease_out(delta);
    }

    GtkAllocation childAllocation = {0};
    if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_LEFT)
    {
      childAllocation.width = childRequisition.width;
      childAllocation.height = allocation->height;
      childAllocation.x = (delta*childAllocation.width) - childAllocation.width;
    }
    else if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_RIGHT)
    {
      childAllocation.width = childRequisition.width;
      childAllocation.height = allocation->height;
      childAllocation.x = allocation->width - (delta*childAllocation.width);
    }
    else if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_TOP)
    {
      childAllocation.width = allocation->width;
      childAllocation.height = childRequisition.height;
      childAllocation.y = (delta*childAllocation.height) - childAllocation.height;
    }
    else if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_BOTTOM)
    {
      childAllocation.width = allocation->width;
      childAllocation.height = childRequisition.height;
      childAllocation.y = allocation->height - (delta*childAllocation.width);
    }
    else if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_CENTER)
    {
      // TODO: Animate center widget (fade in using opacity)
      childAllocation.width = allocation->width;
      childAllocation.height = allocation->height;
    }
    
    // if(!gtk_widget_get_has_window(GTK_WIDGET(self)))
    // {
      childAllocation.x += allocation->x;
      childAllocation.y += allocation->y;
    // }
    
    gtk_widget_size_allocate(GTK_WIDGET(sheetInfo->sheet), &childAllocation);
  }
}

static void vos_material_box_get_preferred_width(VosMaterialBox *self, gint *minimum, gint *natural)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));

  VosMaterialBoxSheetInfo *sheetInfo = self->currentCenter;
  if(!sheetInfo)
  {
    sheetInfo = get_primary_sheet_info(self);
    if(!sheetInfo)
    {
      *minimum = 0;
      *natural = 0;
      return;
    }
  }
  
  gtk_widget_get_preferred_width(sheetInfo->sheet, minimum, natural);
}

static void vos_material_box_get_preferred_height(VosMaterialBox *self, gint *minimum, gint *natural)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));

  VosMaterialBoxSheetInfo *sheetInfo = self->currentCenter;
  if(!sheetInfo)
  {
    sheetInfo = get_primary_sheet_info(self);
    if(!sheetInfo)
    {
      *minimum = 0;
      *natural = 0;
      return;
    }
  }
  
  gtk_widget_get_preferred_height(sheetInfo->sheet, minimum, natural);
}

static void vos_material_box_forall(VosMaterialBox *self, gboolean include_internals, GtkCallback callback, gpointer callback_data)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));

  GList *children = self->children;
  while(children)
  {
    GtkWidget *sheet = GTK_WIDGET(((VosMaterialBoxSheetInfo *)children->data)->sheet);
    children = children->next;
    (* callback) (sheet, callback_data);
  }
}

static gboolean vos_material_box_draw(VosMaterialBox *self, cairo_t *cr)
{
  g_return_val_if_fail(VOS_IS_MATERIAL_BOX(self), FALSE);
  
  // Draw centers first
  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;
    if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_CENTER)
      gtk_container_propagate_draw(GTK_CONTAINER(self), ((VosMaterialBoxSheetInfo *)children->data)->sheet, cr);
  }
  
  // Draw overlay sheets next
  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;
    if(sheetInfo->location != VOS_MATERIAL_BOX_LOCATION_CENTER)
      gtk_container_propagate_draw(GTK_CONTAINER(self), ((VosMaterialBoxSheetInfo *)children->data)->sheet, cr);
  }
  
  return FALSE;
}

static void vos_material_box_set_child_property(VosMaterialBox *self, VosMaterialSheet *sheet, guint property_id, const GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));
  g_return_if_fail(VOS_IS_MATERIAL_BOX(sheet));
  
  VosMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheetInfo));
  
  switch(property_id)
  {
  case CHILD_PROP_LOCATION:
    sheetInfo->location = g_value_get_int(value);
    g_value_set_int(value, (gint)sheetInfo->location);
    break;
  default:
    GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID(self, property_id, pspec);
    break;
  }
}

static void vos_material_box_get_child_property(VosMaterialBox *self, VosMaterialSheet *sheet, guint property_id, GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheet));
  
  VosMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheetInfo));
  
  switch(property_id)
  {
  case CHILD_PROP_LOCATION:
    g_value_set_int(value, (gint)sheetInfo->location);
    break;
  default:
    GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID(self, property_id, pspec);
    break;
  }
}

static void vos_material_box_show_all(VosMaterialBox *self)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));

  gtk_widget_show(GTK_WIDGET(self));
  
  VosMaterialBoxSheetInfo *sheetInfo = get_primary_sheet_info(self);
  if(!sheetInfo)
    return;
  
  gtk_widget_show_all(sheetInfo->sheet);
}

static gboolean sheet_animate_open(VosMaterialSheet *sheet, GdkFrameClock *frameClock, VosMaterialBoxSheetInfo *sheetInfo)
{
  sheetInfo->animOffsetTime = gdk_frame_clock_get_frame_time(frameClock) - sheetInfo->animStartTime;
  gtk_widget_queue_resize(sheet);
  
  if(sheetInfo->animOffsetTime >= VOS_SHEET_TRANSITION_TIME)
  {
    sheetInfo->animOffsetTime = VOS_SHEET_TRANSITION_TIME;
    sheetInfo->tickCallbackID = 0;
    return G_SOURCE_REMOVE;
  }
  
  return G_SOURCE_CONTINUE;
}

static gboolean sheet_animate_close(VosMaterialSheet *sheet, GdkFrameClock *frameClock, VosMaterialBoxSheetInfo *sheetInfo)
{
  sheetInfo->animOffsetTime = VOS_SHEET_TRANSITION_TIME - (gdk_frame_clock_get_frame_time(frameClock) - sheetInfo->animStartTime);
  gtk_widget_queue_resize(sheet);
  
  if(sheetInfo->animOffsetTime <= 0)
  {
    sheetInfo->animOffsetTime = 0;
    sheetInfo->tickCallbackID = 0;
    gtk_widget_hide(GTK_WIDGET(sheet));
    return G_SOURCE_REMOVE;
  }
  
  return G_SOURCE_CONTINUE;
}

/**
 * vos_material_box_show_sheet:
 * @self: a #VosMaterialBox.
 * @sheet: the sheet (GtkContainer) to show.
 *
 * Shows the sheet using a easing animation.
 * NOT the same effect as calling gtk_widge_show on the sheet; calling show will immediately
 * show the sheet with no animation.
 */
void vos_material_box_show_sheet(VosMaterialBox *self, VosMaterialSheet *sheet)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheet));

  // Hide everything but the current center and this
  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;
    if(sheetInfo && sheetInfo->sheet != sheet && sheetInfo != self->currentCenter)
    {
      vos_material_box_hide_sheet(self, sheetInfo->sheet);
    }
  }
  
  // Fade in this sheet
  VosMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(!sheetInfo)
    return;
  
  g_signal_handlers_block_by_func(sheet, G_CALLBACK(sheet_on_show), self);
  gtk_widget_show(GTK_WIDGET(sheet));
  g_signal_handlers_unblock_by_func(sheet, G_CALLBACK(sheet_on_show), self);

  if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_CENTER)
  {
    if(self->currentCenter)
      vos_material_box_hide_sheet(self, self->currentCenter->sheet);
    self->currentCenter = sheetInfo;
  }
  
  // Animate
  if(gtk_widget_is_visible(self))
  {
    GdkFrameClock *frameClock = gtk_widget_get_frame_clock(GTK_WIDGET(sheet));
    sheetInfo->animStartTime = gdk_frame_clock_get_frame_time(frameClock) - sheetInfo->animOffsetTime;
    
    if(sheetInfo->tickCallbackID > 0)
      gtk_widget_remove_tick_callback(GTK_WIDGET(sheet), sheetInfo->tickCallbackID);
    sheetInfo->tickCallbackID = gtk_widget_add_tick_callback(GTK_WIDGET(sheet), sheet_animate_open, sheetInfo, NULL);
  }
  else
  {
    sheetInfo->animOffsetTime = VOS_SHEET_TRANSITION_TIME;
  }
}

/**
 * vos_material_box_hide_sheet:
 * @self: a #VosMaterialBox.
 * @sheet: the sheet (GtkContainer) to hide.
 *
 * Hides the sheet using a easing animation.
 * NOT the same effect as calling gtk_widge_hide on the sheet; calling hide will immediately
 * hide the sheet with no animation.
 */
void vos_material_box_hide_sheet(VosMaterialBox *self, VosMaterialSheet *sheet)
{
  g_return_if_fail(VOS_IS_MATERIAL_BOX(self));
  g_return_if_fail(VOS_IS_MATERIAL_SHEET(sheet));
  
  VosMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(!sheetInfo || sheetInfo->animOffsetTime == 0)
    return;
  
  if(sheetInfo == self->currentCenter)
    self->currentCenter = NULL;
    
  // Animate
  if(gtk_widget_is_visible(self))
  {
    GdkFrameClock *frameClock = gtk_widget_get_frame_clock(GTK_WIDGET(sheet));
    sheetInfo->animStartTime = gdk_frame_clock_get_frame_time(frameClock) - (VOS_SHEET_TRANSITION_TIME-sheetInfo->animOffsetTime);
    
    if(sheetInfo->tickCallbackID > 0)
      gtk_widget_remove_tick_callback(GTK_WIDGET(sheet), sheetInfo->tickCallbackID);
    sheetInfo->tickCallbackID = gtk_widget_add_tick_callback(GTK_WIDGET(sheet), sheet_animate_close, sheetInfo, NULL);
  }
  else
  {
    sheetInfo->animOffsetTime = 0;
    gtk_widget_hide(GTK_WIDGET(sheet));
  }
}

static void sheet_on_show(VosMaterialBox *self, VosMaterialSheet *sheet)
{
  VosMaterialBoxSheetInfo *thisSheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(!thisSheetInfo)
    return;
    
  // Hide everything but the current center and this
  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;
    if(sheetInfo && sheetInfo->sheet != sheet && sheetInfo != self->currentCenter)
    {
      gtk_widget_hide(GTK_WIDGET(sheetInfo->sheet));
    }
  }
  
  if(thisSheetInfo->location == VOS_MATERIAL_BOX_LOCATION_CENTER)
  {
    if(self->currentCenter)
      gtk_widget_hide(GTK_WIDGET(self->currentCenter->sheet));
    self->currentCenter = thisSheetInfo;
  }
  
  thisSheetInfo->animOffsetTime = VOS_SHEET_TRANSITION_TIME;
  gtk_widget_queue_resize(sheet);
}

static void sheet_on_hide(VosMaterialBox *self, VosMaterialSheet *sheet)
{
  VosMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(sheetInfo)
  {
    if(sheetInfo == self->currentCenter)
      self->currentCenter = NULL;
    sheetInfo->animOffsetTime = 0;
  }
  gtk_widget_queue_resize(sheet);
}

static VosMaterialBoxSheetInfo * get_primary_sheet_info(VosMaterialBox *self)
{
  if(self->currentCenter)
    return self->currentCenter;
    
  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;

    if(sheetInfo->location == VOS_MATERIAL_BOX_LOCATION_CENTER)
      return sheetInfo;
  }
  
  return NULL;
}

static VosMaterialBoxSheetInfo * get_sheet_info_from_sheet(VosMaterialBox *self, VosMaterialSheet *sheet)
{
  for(GList *children=self->children; children; children=children->next)
  {
    VosMaterialBoxSheetInfo *sheetInfo = (VosMaterialBoxSheetInfo *)children->data;

    if(sheetInfo->sheet == sheet)
      return sheetInfo;
  }
  
  return NULL;
}
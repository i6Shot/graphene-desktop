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
 * This should be compiled into libgraphene for GIntrospection, and NOT compiled into the panel application binary.
 */
 
#include "materialbox.h"

#define GRAPHENE_SHEET_TRANSITION_TIME (150*1000) // time in microseconds

typedef struct
{
  GrapheneMaterialSheet *sheet;
  GrapheneMaterialBoxSheetLocation location;
  gint64 animStartTime; // gdk_frame_clock_get_frame_time for the start of the animation
  gint64 animOffsetTime; // widget offset measured in units of time. 0=fully hidden, GRAPHENE_SHEET_TRANSITION_TIME=fully shown
  guint tickCallbackID;
  
} GrapheneMaterialBoxSheetInfo;

struct _GrapheneMaterialBox
{
  GtkContainer parent;
  
  GList *children;
  GrapheneMaterialBoxSheetInfo *currentCenter;
};

enum {
  CHILD_PROP_0,
  CHILD_PROP_LOCATION
};

static GType graphene_material_box_child_type(GtkContainer *self_);
static void graphene_material_box_add(GtkContainer *self_, GrapheneMaterialSheet *sheet);
static void graphene_material_box_remove(GtkContainer *self_, GrapheneMaterialSheet *sheet);
static void graphene_material_box_size_allocate(GtkWidget *self_, GtkAllocation *allocation);
static void graphene_material_box_get_preferred_width(GtkWidget *self_, gint *minimum, gint *natural);
static void graphene_material_box_get_preferred_height(GtkWidget *self_, gint *minimum, gint *natural);
static void graphene_material_box_forall(GtkContainer *self_, gboolean include_internals, GtkCallback callback, gpointer callback_data);
static gboolean graphene_material_box_draw(GtkWidget *self_, cairo_t *cr);
static void graphene_material_box_set_child_property(GtkContainer *self_, GrapheneMaterialSheet *sheet, guint property_id, const GValue *value, GParamSpec *pspec);
static void graphene_material_box_get_child_property(GtkContainer *self_, GrapheneMaterialSheet *sheet, guint property_id, GValue *value, GParamSpec *pspec);
static void graphene_material_box_show_all(GtkWidget *self_);
static GrapheneMaterialBoxSheetInfo * get_primary_sheet_info(GrapheneMaterialBox *self);
static GrapheneMaterialBoxSheetInfo * get_sheet_info_from_sheet(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet);

static void sheet_on_show(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet);
static void sheet_on_hide(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet);

G_DEFINE_TYPE(GrapheneMaterialBox, graphene_material_box, GTK_TYPE_CONTAINER)

static void graphene_material_box_class_init (GrapheneMaterialBoxClass *class)
{
  GtkWidgetClass *widgetClass = GTK_WIDGET_CLASS(class);
  GtkContainerClass *containerClass = GTK_CONTAINER_CLASS(class);

  widgetClass->get_preferred_width = graphene_material_box_get_preferred_width;
  widgetClass->get_preferred_height = graphene_material_box_get_preferred_height;
  widgetClass->size_allocate = graphene_material_box_size_allocate;
  widgetClass->draw = graphene_material_box_draw;
  widgetClass->show_all = graphene_material_box_show_all;
  
  containerClass->add = graphene_material_box_add;
  containerClass->remove = graphene_material_box_remove;
  containerClass->forall = graphene_material_box_forall;
  containerClass->child_type = graphene_material_box_child_type;
  containerClass->set_child_property = graphene_material_box_set_child_property;
  containerClass->get_child_property = graphene_material_box_get_child_property;
  gtk_container_class_handle_border_width(containerClass);

  gtk_container_class_install_child_property(containerClass, CHILD_PROP_LOCATION,
                                              g_param_spec_int("location",
                                                                "location",
                                                                "location of material sheet in the box",
                                                                GRAPHENE_MATERIAL_BOX_LOCATION_TOP, GRAPHENE_MATERIAL_BOX_LOCATION_CENTER,
                                                                GRAPHENE_MATERIAL_BOX_LOCATION_CENTER,
                                                                G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB)); // GTK_PARAM_READWRITE
}

/**
 * graphene_material_box_new:
 *
 * Creates a new #GrapheneMaterialBox.
 *
 * Returns: a new #GrapheneMaterialBox.
 */
GtkWidget* graphene_material_box_new(void)
{
  return g_object_new(GRAPHENE_TYPE_MATERIAL_BOX, NULL);
}

static void graphene_material_box_init(GrapheneMaterialBox *self)
{
  gtk_widget_set_has_window(GTK_WIDGET(self), FALSE);
  self->currentCenter = NULL;
}

static GType graphene_material_box_child_type(GtkContainer *self_)
{
  return GTK_TYPE_WIDGET;
}

/**
 * graphene_material_box_add_sheet:
 * @self: a #GrapheneMaterialBox.
 * @sheet: the sheet (GtkContainer) to add.
 * @location: the GrapheneMaterialBoxSheetLocation location to add the sheet at
 *
 * Adds a sheet to a #GrapheneMaterialBox container at the given location.
 * The sheet is automatically hidden (gtk_widget_hide called on the sheet).
 */
void graphene_material_box_add_sheet(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet, GrapheneMaterialBoxSheetLocation location)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self));
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheet));

  GrapheneMaterialBoxSheetInfo *sheetInfo = g_new0(GrapheneMaterialBoxSheetInfo, 1);
  sheetInfo->sheet = sheet;
  sheetInfo->location = location;
  self->children = g_list_append(self->children, sheetInfo);
  
  gtk_widget_hide(GTK_WIDGET(sheet));
  
  g_signal_connect_swapped(sheet, "show", G_CALLBACK(sheet_on_show), self);
  g_signal_connect_swapped(sheet, "hide", G_CALLBACK(sheet_on_hide), self);
  
  gtk_widget_set_parent(GTK_WIDGET(sheetInfo->sheet), GTK_WIDGET(self));
}

static void graphene_material_box_add(GtkContainer *self_, GrapheneMaterialSheet *sheet)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheet));

  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  graphene_material_box_add_sheet(self, sheet, GRAPHENE_MATERIAL_BOX_LOCATION_LEFT);
}

static void graphene_material_box_remove(GtkContainer *self_, GrapheneMaterialSheet *sheet)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheet));
  
  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;

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

static void graphene_material_box_size_allocate(GtkWidget *self_, GtkAllocation *allocation)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));
  
  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  gtk_widget_set_allocation(GTK_WIDGET(self), allocation);

  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;

    if(!gtk_widget_get_visible(sheetInfo->sheet))
      continue;

    GtkRequisition childRequisition = {0};
    gtk_widget_get_preferred_size(GTK_WIDGET(sheetInfo->sheet), &childRequisition, NULL);

    gdouble delta = 0;
    
    if(sheetInfo->location != GRAPHENE_MATERIAL_BOX_LOCATION_CENTER)
    {
      delta = MIN(MAX((gdouble)sheetInfo->animOffsetTime / GRAPHENE_SHEET_TRANSITION_TIME, 0), 1);
      delta = cubic_ease_out(delta);
    }

    GtkAllocation childAllocation = {0};
    if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_LEFT)
    {
      childAllocation.width = childRequisition.width;
      childAllocation.height = allocation->height;
      childAllocation.x = (delta*childAllocation.width) - childAllocation.width;
    }
    else if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_RIGHT)
    {
      childAllocation.width = childRequisition.width;
      childAllocation.height = allocation->height;
      childAllocation.x = allocation->width - (delta*childAllocation.width);
    }
    else if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_TOP)
    {
      childAllocation.width = allocation->width;
      childAllocation.height = childRequisition.height;
      childAllocation.y = (delta*childAllocation.height) - childAllocation.height;
    }
    else if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_BOTTOM)
    {
      childAllocation.width = allocation->width;
      childAllocation.height = childRequisition.height;
      childAllocation.y = allocation->height - (delta*childAllocation.width);
    }
    else if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_CENTER)
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

static void graphene_material_box_get_preferred_width(GtkWidget *self_, gint *minimum, gint *natural)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));

  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  GrapheneMaterialBoxSheetInfo *sheetInfo = self->currentCenter;
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

static void graphene_material_box_get_preferred_height(GtkWidget *self_, gint *minimum, gint *natural)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));
  
  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  GrapheneMaterialBoxSheetInfo *sheetInfo = self->currentCenter;
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

static void graphene_material_box_forall(GtkContainer *self_, gboolean include_internals, GtkCallback callback, gpointer callback_data)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));
  
  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  GList *children = self->children;
  while(children)
  {
    GtkWidget *sheet = GTK_WIDGET(((GrapheneMaterialBoxSheetInfo *)children->data)->sheet);
    children = children->next;
    (* callback) (sheet, callback_data);
  }
}

static gboolean graphene_material_box_draw(GtkWidget *self_, cairo_t *cr)
{
  g_return_val_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_), FALSE);
  
  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);

  // Draw centers first
  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;
    if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_CENTER)
      gtk_container_propagate_draw(GTK_CONTAINER(self), ((GrapheneMaterialBoxSheetInfo *)children->data)->sheet, cr);
  }
  
  // Draw overlay sheets next
  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;
    if(sheetInfo->location != GRAPHENE_MATERIAL_BOX_LOCATION_CENTER)
      gtk_container_propagate_draw(GTK_CONTAINER(self), ((GrapheneMaterialBoxSheetInfo *)children->data)->sheet, cr);
  }
  
  return FALSE;
}

static void graphene_material_box_set_child_property(GtkContainer *self_, GrapheneMaterialSheet *sheet, guint property_id, const GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(sheet));
  
  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  GrapheneMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheetInfo));
  
  switch(property_id)
  {
  case CHILD_PROP_LOCATION:
    sheetInfo->location = g_value_get_int(value);
    break;
  default:
    GTK_CONTAINER_WARN_INVALID_CHILD_PROPERTY_ID(self, property_id, pspec);
    break;
  }
}

static void graphene_material_box_get_child_property(GtkContainer *self_, GrapheneMaterialSheet *sheet, guint property_id, GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheet));
  
  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  GrapheneMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheetInfo));
  
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

static void graphene_material_box_show_all(GtkWidget *self_)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self_));

  GrapheneMaterialBox *self = GRAPHENE_MATERIAL_BOX(self_);
  gtk_widget_show(GTK_WIDGET(self));
  
  GrapheneMaterialBoxSheetInfo *sheetInfo = get_primary_sheet_info(self);
  if(!sheetInfo)
    return;
  
  gtk_widget_show_all(sheetInfo->sheet);
}

static gboolean sheet_animate_open(GrapheneMaterialSheet *sheet, GdkFrameClock *frameClock, GrapheneMaterialBoxSheetInfo *sheetInfo)
{
  sheetInfo->animOffsetTime = gdk_frame_clock_get_frame_time(frameClock) - sheetInfo->animStartTime;
  gtk_widget_queue_resize(sheet);
  
  if(sheetInfo->animOffsetTime >= GRAPHENE_SHEET_TRANSITION_TIME)
  {
    sheetInfo->animOffsetTime = GRAPHENE_SHEET_TRANSITION_TIME;
    sheetInfo->tickCallbackID = 0;
    return G_SOURCE_REMOVE;
  }
  
  return G_SOURCE_CONTINUE;
}

static gboolean sheet_animate_close(GrapheneMaterialSheet *sheet, GdkFrameClock *frameClock, GrapheneMaterialBoxSheetInfo *sheetInfo)
{
  sheetInfo->animOffsetTime = GRAPHENE_SHEET_TRANSITION_TIME - (gdk_frame_clock_get_frame_time(frameClock) - sheetInfo->animStartTime);
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
 * graphene_material_box_show_sheet:
 * @self: a #GrapheneMaterialBox.
 * @sheet: the sheet (GtkContainer) to show.
 *
 * Shows the sheet using a easing animation.
 * NOT the same effect as calling gtk_widge_show on the sheet; calling show will immediately
 * show the sheet with no animation.
 */
void graphene_material_box_show_sheet(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self));
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheet));

  // Hide everything but the current center and this
  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;
    if(sheetInfo && sheetInfo->sheet != sheet && sheetInfo != self->currentCenter)
    {
      graphene_material_box_hide_sheet(self, sheetInfo->sheet);
    }
  }
  
  // Fade in this sheet
  GrapheneMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(!sheetInfo)
    return;
  
  g_signal_handlers_block_by_func(sheet, G_CALLBACK(sheet_on_show), self);
  gtk_widget_show(GTK_WIDGET(sheet));
  g_signal_handlers_unblock_by_func(sheet, G_CALLBACK(sheet_on_show), self);

  if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_CENTER)
  {
    if(self->currentCenter)
      graphene_material_box_hide_sheet(self, self->currentCenter->sheet);
    self->currentCenter = sheetInfo;
  }
  
  // Animate
  if(gtk_widget_is_visible(GTK_WIDGET(self)))
  {
    GdkFrameClock *frameClock = gtk_widget_get_frame_clock(GTK_WIDGET(sheet));
    sheetInfo->animStartTime = gdk_frame_clock_get_frame_time(frameClock) - sheetInfo->animOffsetTime;
    
    if(sheetInfo->tickCallbackID > 0)
      gtk_widget_remove_tick_callback(GTK_WIDGET(sheet), sheetInfo->tickCallbackID);
    sheetInfo->tickCallbackID = gtk_widget_add_tick_callback(GTK_WIDGET(sheet), (GtkTickCallback)sheet_animate_open, sheetInfo, NULL);
  }
  else
  {
    sheetInfo->animOffsetTime = GRAPHENE_SHEET_TRANSITION_TIME;
  }
}

/**
 * graphene_material_box_hide_sheet:
 * @self: a #GrapheneMaterialBox.
 * @sheet: the sheet (GtkContainer) to hide.
 *
 * Hides the sheet using a easing animation.
 * NOT the same effect as calling gtk_widge_hide on the sheet; calling hide will immediately
 * hide the sheet with no animation.
 */
void graphene_material_box_hide_sheet(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet)
{
  g_return_if_fail(GRAPHENE_IS_MATERIAL_BOX(self));
  g_return_if_fail(GRAPHENE_IS_MATERIAL_SHEET(sheet));
  
  GrapheneMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(!sheetInfo || sheetInfo->animOffsetTime == 0)
    return;
  
  if(sheetInfo == self->currentCenter)
    self->currentCenter = NULL;
    
  // Animate
  if(gtk_widget_is_visible(GTK_WIDGET(self)))
  {
    GdkFrameClock *frameClock = gtk_widget_get_frame_clock(GTK_WIDGET(sheet));
    sheetInfo->animStartTime = gdk_frame_clock_get_frame_time(frameClock) - (GRAPHENE_SHEET_TRANSITION_TIME-sheetInfo->animOffsetTime);
    
    if(sheetInfo->tickCallbackID > 0)
      gtk_widget_remove_tick_callback(GTK_WIDGET(sheet), sheetInfo->tickCallbackID);
    sheetInfo->tickCallbackID = gtk_widget_add_tick_callback(GTK_WIDGET(sheet), (GtkTickCallback)sheet_animate_close, sheetInfo, NULL);
  }
  else
  {
    sheetInfo->animOffsetTime = 0;
    gtk_widget_hide(GTK_WIDGET(sheet));
  }
}

static void sheet_on_show(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet)
{
  GrapheneMaterialBoxSheetInfo *thisSheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(!thisSheetInfo)
    return;
    
  // Hide everything but the current center and this
  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;
    if(sheetInfo && sheetInfo->sheet != sheet && sheetInfo != self->currentCenter)
    {
      gtk_widget_hide(GTK_WIDGET(sheetInfo->sheet));
    }
  }
  
  if(thisSheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_CENTER)
  {
    if(self->currentCenter)
      gtk_widget_hide(GTK_WIDGET(self->currentCenter->sheet));
    self->currentCenter = thisSheetInfo;
  }
  
  thisSheetInfo->animOffsetTime = GRAPHENE_SHEET_TRANSITION_TIME;
  gtk_widget_queue_resize(sheet);
}

static void sheet_on_hide(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet)
{
  GrapheneMaterialBoxSheetInfo *sheetInfo = get_sheet_info_from_sheet(self, sheet);
  if(sheetInfo)
  {
    if(sheetInfo == self->currentCenter)
      self->currentCenter = NULL;
    sheetInfo->animOffsetTime = 0;
  }
  gtk_widget_queue_resize(sheet);
}

static GrapheneMaterialBoxSheetInfo * get_primary_sheet_info(GrapheneMaterialBox *self)
{
  if(self->currentCenter)
    return self->currentCenter;
    
  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;

    if(sheetInfo->location == GRAPHENE_MATERIAL_BOX_LOCATION_CENTER)
      return sheetInfo;
  }
  
  return NULL;
}

static GrapheneMaterialBoxSheetInfo * get_sheet_info_from_sheet(GrapheneMaterialBox *self, GrapheneMaterialSheet *sheet)
{
  for(GList *children=self->children; children; children=children->next)
  {
    GrapheneMaterialBoxSheetInfo *sheetInfo = (GrapheneMaterialBoxSheetInfo *)children->data;

    if(sheetInfo->sheet == sheet)
      return sheetInfo;
  }
  
  return NULL;
}
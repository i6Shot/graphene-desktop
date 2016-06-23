/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Conner Novicki <connernovicki@gmail.com>
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
 */

#include "volume.h"
#include "system-volume-control.h"
#include <glib.h>

struct _GrapheneVolumeIcon
{
  GtkImage parent;

  GrapheneSystemVolumeControl *volumeControl;
};

static void graphene_volume_icon_finalize(GObject *self_);
static void icon_on_update(GrapheneVolumeIcon *self, GrapheneSystemVolumeControl *volumeControl);


G_DEFINE_TYPE(GrapheneVolumeIcon, graphene_volume_icon, GTK_TYPE_IMAGE)


GrapheneVolumeIcon* graphene_volume_icon_new(void)
{
  return GRAPHENE_VOLUME_ICON(g_object_new(GRAPHENE_TYPE_VOLUME_ICON, NULL));
}

static void graphene_volume_icon_class_init(GrapheneVolumeIconClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = graphene_volume_icon_finalize;
}

static void graphene_volume_icon_init(GrapheneVolumeIcon *self)
{
  gtk_image_set_from_icon_name(GTK_IMAGE(self), "audio-volume-high-symbolic", GTK_ICON_SIZE_MENU);

  self->volumeControl = graphene_system_volume_control_new();
  g_signal_connect_swapped(self->volumeControl, "notify::volume", G_CALLBACK(icon_on_update), self);
  g_signal_connect_swapped(self->volumeControl, "notify::muted", G_CALLBACK(icon_on_update), self);
}

static void graphene_volume_icon_finalize(GObject *self_)
{
  GrapheneVolumeIcon *self = GRAPHENE_VOLUME_ICON(self_);
  g_clear_object(&self->volumeControl);
}

static void icon_on_update(GrapheneVolumeIcon *self, GrapheneSystemVolumeControl *volumeControl)
{
  const gchar* iconName = "";
  gfloat volPercent = graphene_system_volume_control_get_volume(self->volumeControl);

  if(volPercent == 0 || graphene_system_volume_control_get_is_muted(self->volumeControl))
    iconName = "audio-volume-muted-symbolic";
  else if(volPercent >= (gfloat)2/3)
    iconName = "audio-volume-high-symbolic";
  else if(volPercent >= (gfloat)1/3)
    iconName = "audio-volume-medium-symbolic";
  else
    iconName = "audio-volume-low-symbolic";

  gtk_image_set_from_icon_name(GTK_IMAGE(self), iconName, GTK_ICON_SIZE_MENU);
}



/*
 ******* Slider ******
 */



struct _GrapheneVolumeSlider
{
  GtkBox parent;

  GrapheneSystemVolumeControl *volumeControl;
  GrapheneVolumeIcon *volumeIcon;
  GtkScale *slider;
};

static void graphene_volume_slider_finalize(GObject *self_);
static void slider_on_value_changed(GrapheneVolumeSlider *self, GtkScale *scale);
static void slider_on_update(GrapheneVolumeSlider *self, GrapheneSystemVolumeControl *volumeControl);


G_DEFINE_TYPE(GrapheneVolumeSlider, graphene_volume_slider, GTK_TYPE_BOX)


GrapheneVolumeSlider* graphene_volume_slider_new(void)
{
  return GRAPHENE_VOLUME_SLIDER(g_object_new(GRAPHENE_TYPE_VOLUME_SLIDER, NULL));
}

static void graphene_volume_slider_class_init(GrapheneVolumeSliderClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = graphene_volume_slider_finalize;
}

static void graphene_volume_slider_init(GrapheneVolumeSlider *self)
{
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "graphene-volume-slider");
  
  // Margin in CSS
  
  self->volumeControl = graphene_system_volume_control_new();
  g_signal_connect_swapped(self->volumeControl, "notify::volume", G_CALLBACK(slider_on_update), self);

  self->volumeIcon = graphene_volume_icon_new();
  gtk_widget_set_valign(GTK_WIDGET(self->volumeIcon), GTK_ALIGN_START);

  self->slider = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.5, 0.1));
  gtk_scale_set_draw_value(self->slider, FALSE);
  gtk_scale_set_digits(self->slider, 1);
  gtk_scale_add_mark(self->slider, 1.0, GTK_POS_BOTTOM, "100%");
  g_signal_connect_swapped(self->slider, "value-changed", G_CALLBACK(slider_on_value_changed), self);

  gtk_box_pack_start(GTK_BOX(self), GTK_WIDGET(self->volumeIcon), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self), GTK_WIDGET(self->slider), TRUE, TRUE, 0);
}

static void graphene_volume_slider_finalize(GObject *self_)
{
  GrapheneVolumeSlider *self = GRAPHENE_VOLUME_SLIDER(self_);
  g_clear_object(&self->volumeControl);
}

static void slider_on_value_changed(GrapheneVolumeSlider *self, GtkScale *scale)
{
  gfloat value = gtk_range_get_value(GTK_RANGE(scale));
  graphene_system_volume_control_set_volume(self->volumeControl, value);

  if(value == 0)
    graphene_system_volume_control_set_is_muted(self->volumeControl, TRUE);
  else if(graphene_system_volume_control_get_is_muted(self->volumeControl))
    graphene_system_volume_control_set_is_muted(self->volumeControl, FALSE);
}

static void slider_on_update(GrapheneVolumeSlider *self, GrapheneSystemVolumeControl *volumeControl)
{
  gtk_range_set_value(GTK_RANGE(self->slider), graphene_system_volume_control_get_volume(self->volumeControl));
}
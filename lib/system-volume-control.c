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

#include "system-volume-control.h"
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <stdio.h>

/**
 * SECTION:vos-system-volume-control
 * @short_description: Provides a simple interface for controlling system volume and mute (mostly a VERY simple Python interface for PulseAudio).
 **/

struct _VosSystemVolumeControl
{
  GObject parent;
  
  pa_glib_mainloop *mainloop;
  pa_mainloop_api *mainloop_api;
  pa_context *context;
  
  pa_context_state_t state;
  gboolean is_muted;
  pa_cvolume volume;
  gboolean got_sink_info;
};

enum
{
  PROP_0,
  PROP_STATE,
  PROP_IS_MUTED,
  PROP_VOLUME,
  PROP_LAST
};

#define DEFAULT_SINK_INDEX  0

static GParamSpec *properties[PROP_LAST];

static void on_pa_context_state_change(pa_context *context, void *userdata);
static void on_pa_context_subscribe(pa_context *context, pa_subscription_event_type_t t, uint32_t index, void *userdata);
static void on_sink_get_info(pa_context *c, const pa_sink_info *i, int eol, void *userdata);
static void vos_system_volume_control_set_property (GObject *object, guint prop_id, const GValue  *value, GParamSpec *pspec);
static void vos_system_volume_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void vos_system_volume_control_finalize(GObject *object);

// Create the VosSystemVolumeControl class
G_DEFINE_TYPE(VosSystemVolumeControl, vos_system_volume_control, G_TYPE_OBJECT)
VosSystemVolumeControl* vos_system_volume_control_new(void)
{
  return VOS_SYSTEM_VOLUME_CONTROL(g_object_new(VOS_TYPE_SYSTEM_VOLUME_CONTROL, NULL));
}
static void vos_system_volume_control_class_init(VosSystemVolumeControlClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  
  gobject_class->finalize = vos_system_volume_control_finalize;
  gobject_class->set_property = vos_system_volume_control_set_property;
  gobject_class->get_property = vos_system_volume_control_get_property;
  
  properties[PROP_STATE] = g_param_spec_int("state",
    "state",
    "The state of the connection to PulseAudio. Can be 0 (not ready), 1 (ready), or -1 (fail).",
    -10, 10, 0,
    G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY); // TODO: G_PARAM_READONLY throws critical error for not having G_PARAM_WRITABLE... ??
  
  properties[PROP_VOLUME] = g_param_spec_float("volume",
    "Volume",
    "The system volume",
    0, 2, 0,
    G_PARAM_READWRITE|G_PARAM_CONSTRUCT);
  
  properties[PROP_IS_MUTED] = g_param_spec_boolean ("muted",
    "is muted",
    "Whether system audio is muted",
    TRUE,
    G_PARAM_READWRITE|G_PARAM_CONSTRUCT);
  
  g_object_class_install_property(gobject_class, PROP_STATE, properties[PROP_STATE]);                  
  g_object_class_install_property(gobject_class, PROP_VOLUME, properties[PROP_VOLUME]);
  g_object_class_install_property(gobject_class, PROP_IS_MUTED, properties[PROP_IS_MUTED]);
}


static void vos_system_volume_control_init(VosSystemVolumeControl *self)
{
  self->got_sink_info = FALSE;
  self->mainloop = pa_glib_mainloop_new(g_main_context_default());
  g_return_if_fail(self->mainloop);
  self->mainloop_api = pa_glib_mainloop_get_api(self->mainloop);
  g_return_if_fail(self->mainloop_api);
  
  GApplication *application = g_application_get_default();
  g_return_if_fail(application);

  pa_proplist *proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "vos-system-volume-control");
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, g_application_get_application_id(application));
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "multimedia-volume-control-symbolic");
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, "1.0");

  self->context = pa_context_new_with_proplist(self->mainloop_api, NULL, proplist);
  pa_proplist_free(proplist);
  g_return_if_fail(self->context);

  pa_context_set_state_callback(self->context, on_pa_context_state_change, self);
  
  int connect = pa_context_connect(self->context, NULL, (pa_context_flags_t)PA_CONTEXT_NOFAIL, NULL);
  if (connect < 0)
    g_warning("Failed to connect context: %s", pa_strerror(pa_context_errno(self->context)));
}

static void vos_system_volume_control_finalize(GObject *object)
{
  VosSystemVolumeControl *self = VOS_SYSTEM_VOLUME_CONTROL(object);

  if (self->context)
  {
    pa_context_unref(self->context);
    self->context = NULL;
  }

  if (self->mainloop != NULL)
  {
    pa_glib_mainloop_free(self->mainloop);
    self->mainloop = NULL;
  }

  G_OBJECT_CLASS(vos_system_volume_control_parent_class)->finalize(object);
}

static void on_pa_context_state_change(pa_context *context, void *userdata)
{
  g_return_if_fail(VOS_IS_SYSTEM_VOLUME_CONTROL(userdata));
  VosSystemVolumeControl *self = VOS_SYSTEM_VOLUME_CONTROL(userdata);
  
  gint prevState = vos_system_volume_control_get_state(self);
  self->state = pa_context_get_state(context);
  gint newState = vos_system_volume_control_get_state(self);

  if(self->state == PA_CONTEXT_READY)
  {
    pa_context_set_subscribe_callback(self->context, on_pa_context_subscribe, self);
    
    pa_operation *o = pa_context_subscribe(self->context, (pa_subscription_mask_t)PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
    if (o == NULL) g_warning ("Failed to subscribe: %s", pa_strerror(pa_context_errno(self->context)));
    else pa_operation_unref(o);
    
    o = pa_context_get_sink_info_by_index(self->context, DEFAULT_SINK_INDEX, on_sink_get_info, self);
    if (o == NULL) g_warning("Failed to get sink info: %s", pa_strerror(pa_context_errno(self->context)));
    else pa_operation_unref (o);
  }
  else if(self->state == PA_CONTEXT_FAILED)
  {
    self->got_sink_info = FALSE;
    g_warning ("PA Context state FAILED: %s", pa_strerror(pa_context_errno(self->context)));
  }
  else
  {
    self->got_sink_info = FALSE;
  }
  
  if(prevState != newState)
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_STATE]);
}

static void on_pa_context_subscribe(pa_context *context, pa_subscription_event_type_t type, uint32_t index, void *userdata)
{
  g_return_if_fail(VOS_IS_SYSTEM_VOLUME_CONTROL(userdata));
  VosSystemVolumeControl *self = VOS_SYSTEM_VOLUME_CONTROL(userdata);

  // Only listen for changes to the default sink. Not sure if this is always the default system output, but it seems to be.
  if(index == DEFAULT_SINK_INDEX && (type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK)
  {
    pa_operation *o = pa_context_get_sink_info_by_index(self->context, index, on_sink_get_info, self);

    if (o == NULL) g_warning("Failed to get subscription sink info: %s", pa_strerror(pa_context_errno(self->context)));
    else pa_operation_unref (o);
  }
}

static void on_sink_get_info(pa_context *c, const pa_sink_info *info, int eol, void *userdata)
{
  g_return_if_fail(VOS_IS_SYSTEM_VOLUME_CONTROL(userdata));
  VosSystemVolumeControl *self = VOS_SYSTEM_VOLUME_CONTROL(userdata);

  if(eol == 0 && info)
  {
    pa_volume_t originalMaxVol = pa_cvolume_max(&self->volume);
    pa_volume_t newMaxVol = pa_cvolume_max(&info->volume);
    
    gboolean originalMute = self->is_muted;
    
    self->is_muted = info->mute;
    self->volume = info->volume;
    
    gboolean prev_got_info = self->got_sink_info;
    if(!self->got_sink_info)
    {
      self->got_sink_info = TRUE;  
      g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_STATE]);
    }
    
    if(originalMaxVol != newMaxVol || !prev_got_info)
      g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_VOLUME]);
    if(originalMute != self->is_muted || !prev_got_info)
      g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_IS_MUTED]);
  }
}

static void vos_system_volume_control_set_property(GObject *object, guint prop_id, const GValue  *value, GParamSpec *pspec)
{
  VosSystemVolumeControl *self = VOS_SYSTEM_VOLUME_CONTROL(object);

  switch (prop_id)
  {
  case PROP_VOLUME:
    vos_system_volume_control_set_volume(self, g_value_get_float(value));
    break;
  case PROP_IS_MUTED:
    vos_system_volume_control_set_is_muted(self, g_value_get_boolean(value));
    break;
  case PROP_STATE:
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void vos_system_volume_control_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  VosSystemVolumeControl *self = VOS_SYSTEM_VOLUME_CONTROL(object);

  switch (prop_id)
  {
  case PROP_VOLUME:
    g_value_set_float(value, vos_system_volume_control_get_volume(self));
    break;
  case PROP_IS_MUTED:
    g_value_set_boolean(value, vos_system_volume_control_get_is_muted(self));
    break;
  case PROP_STATE:
    g_value_set_int(value, vos_system_volume_control_get_state(self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

gfloat vos_system_volume_control_get_volume(VosSystemVolumeControl *self)
{
  return ((gfloat)(pa_cvolume_max(&self->volume) - PA_VOLUME_MUTED))/(PA_VOLUME_NORM - PA_VOLUME_MUTED);
}
gboolean vos_system_volume_control_get_is_muted(VosSystemVolumeControl *self)
{
  return self->is_muted;
}
gint vos_system_volume_control_get_state(VosSystemVolumeControl *self)
{
  if(self->state == PA_CONTEXT_READY && self->got_sink_info) return 1;
  else if(self->state == PA_CONTEXT_FAILED) return -1;
  else return 0;
}
void vos_system_volume_control_set_volume(VosSystemVolumeControl *self, gfloat volume)
{
  if(vos_system_volume_control_get_state(self) != 1)
    return;
  
  pa_volume_t newVol = (pa_volume_t)(volume * (PA_VOLUME_NORM - PA_VOLUME_MUTED) + PA_VOLUME_MUTED);
  pa_cvolume_scale(&self->volume, newVol);

  pa_operation *o = pa_context_set_sink_volume_by_index(self->context, DEFAULT_SINK_INDEX, &self->volume, NULL, NULL);
  if (o == NULL) g_warning ("Failed to set volume: %s", pa_strerror(pa_context_errno(self->context)));
  else pa_operation_unref(o);
}
void vos_system_volume_control_set_is_muted(VosSystemVolumeControl *self, gboolean is_muted)
{
  if(vos_system_volume_control_get_state(self) != 1)
    return;
  
  pa_operation *o = pa_context_set_sink_mute_by_index(self->context, DEFAULT_SINK_INDEX, is_muted, NULL, NULL);
  if (o == NULL) g_warning ("Failed to set mute: %s", pa_strerror(pa_context_errno(self->context)));
  else pa_operation_unref(o);
}
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
 * See header for function documentation.
 */
 
#include "sound.h"
#include <stdio.h>
#include <string.h>

#define MAX_DEVICE_NAME_LENGTH 75 // These include NULL terminator
#define MAX_DEVICE_DESCRIPTION_LENGTH 100

struct _SoundSettings
{
  int ref;
  
  DestroyPAMainloopNotify destroyMainloopCallback;
  
  SoundSettingsEventCallback eventCallback;
  void *eventUserdata;
  
  void *mainloop;
  pa_mainloop_api *mainloopAPI;
  pa_context *context;
  SoundSettingsState state;
  
  char *defaultSinkName;
  char *defaultSourceName;
  
  SoundDevice *firstDevice;
};

struct _SoundDevice
{
  int ref;
  uint32_t index; // PulseAudio sink/source index
  SoundDeviceType type;
  char name[MAX_DEVICE_NAME_LENGTH];
  char description[MAX_DEVICE_DESCRIPTION_LENGTH];
  float volume;
  float balance;
  bool mute;
  bool active; // translates to PA's "default sink/source"
  
  pa_cvolume cvolume;
  
  SoundDevice *prev; // Linked lists are easier to handle.
  SoundDevice *next;
  SoundSettings *owner;
};

static void on_pa_state_change(pa_context *context, SoundSettings *self);
static void on_pa_event(pa_context *context, pa_subscription_event_type_t type, uint32_t index, SoundSettings *self);
static void on_server_get_info(pa_context *context, const pa_server_info *server, SoundSettings *self);
static void on_sink_get_info(pa_context *context, const pa_sink_info *sink, int eol, SoundSettings *self);
static void on_source_get_info(pa_context *context, const pa_source_info *source, int eol, SoundSettings *self);
static SoundDevice * get_device_at_index_with_type(SoundSettings *self, uint32_t index, SoundDeviceType type, bool create, bool *created);
static void sound_device_invalidate(SoundDevice *device);
static void unref_all_devices(SoundSettings *settings);


static SoundSettings *defaultSoundSettings = NULL;


SoundSettings * sound_settings_init(void *mainloop, pa_mainloop_api *mainloopAPI, pa_proplist *props, DestroyPAMainloopNotify destroyMainloopCallback)
{
  if(!mainloop || !props)
    return NULL;
  
  SoundSettings *settings = malloc(sizeof(SoundSettings));
  if(!settings)
    return NULL;
  
  memset(settings, 0, sizeof(SoundSettings));
  
  settings->destroyMainloopCallback = destroyMainloopCallback;
  settings->mainloop = mainloop;
  settings->mainloopAPI = mainloopAPI;
  settings->context = pa_context_new_with_proplist(mainloopAPI, NULL, props);
  settings->state = SOUND_SETTINGS_STATE_UNCONNECTED;
  
  if(!settings->context)
  {
    free(settings);
    return NULL;
  }
  
  pa_context_set_state_callback(settings->context, (pa_context_notify_cb_t)on_pa_state_change, settings);
  pa_context_set_subscribe_callback(settings->context, (pa_context_subscribe_cb_t)on_pa_event, settings);

  pa_context_connect(settings->context, NULL, PA_CONTEXT_NOFAIL, NULL);
    
  if(defaultSoundSettings == NULL)
    defaultSoundSettings = settings;
  return sound_settings_ref(settings);
}

SoundSettings * sound_settings_get_default()
{
  return sound_settings_ref(defaultSoundSettings);
}

SoundSettingsState sound_settings_get_state(SoundSettings *settings)
{
  if(!settings) return SOUND_SETTINGS_STATE_UNCONNECTED;
  return settings->state;
}

SoundSettings * sound_settings_ref(SoundSettings *settings)
{
  if(!settings) return NULL;
  settings->ref ++;
  return settings;
}

bool sound_settings_unref(SoundSettings *settings)
{
  if(!settings) return false;
  settings->ref --;
  
  if(settings->ref > 0)
    return false;
  else if(settings->ref < 0)
    return true;
  
  if(settings == defaultSoundSettings)
    defaultSoundSettings = NULL;
  
  unref_all_devices(settings);
  if(settings->context)
  {
    pa_context_set_subscribe_callback(settings->context, NULL, NULL);
    pa_context_set_state_callback(settings->context, NULL, NULL);
    pa_context_disconnect(settings->context);
    pa_context_unref(settings->context);
    settings->context = NULL;
  }
  
  free(settings->defaultSinkName);
  free(settings->defaultSourceName);
  settings->defaultSinkName = NULL;
  settings->defaultSourceName = NULL;

  if(settings->destroyMainloopCallback && settings->mainloop)
  {
    DestroyPAMainloopNotify cb = settings->destroyMainloopCallback;
    settings->destroyMainloopCallback = NULL;
    cb(settings->mainloop);
  }

  settings->mainloop = NULL;
  settings->mainloopAPI = NULL;
  
  free(settings);
  return true;
}

void sound_settings_set_event_callback(SoundSettings *settings, SoundSettingsEventCallback callback, void *userdata)
{
  if(!settings) return;
  settings->eventCallback = callback;
  settings->eventUserdata = userdata;
}

static void on_pa_state_change(pa_context *context, SoundSettings *self)
{
  int state = pa_context_get_state(context);
  bool stateChanged = false;
  
  if(state == PA_CONTEXT_CONNECTING || state == PA_CONTEXT_AUTHORIZING || state == PA_CONTEXT_SETTING_NAME)
  {
    if(self->state != SOUND_SETTINGS_STATE_CONNECTING)
      stateChanged = true;
    self->state = SOUND_SETTINGS_STATE_CONNECTING;
  }
  else if(state == PA_CONTEXT_READY)
  {
    pa_operation *o = pa_context_subscribe(self->context, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
    if(o) pa_operation_unref(o);
    o = pa_context_get_server_info(self->context, (pa_server_info_cb_t)on_server_get_info, self); // The server info callback also gets the sink/source lists
    if(o) pa_operation_unref(o);
    
    if(self->state != SOUND_SETTINGS_STATE_READY)
      stateChanged = true;
    self->state = SOUND_SETTINGS_STATE_READY;
  }
  else if(state == PA_CONTEXT_FAILED)
  {
    unref_all_devices(self);

    if(self->state != SOUND_SETTINGS_STATE_FAILED)
      stateChanged = true;
    self->state = SOUND_SETTINGS_STATE_FAILED;
  }
  else if(state == PA_CONTEXT_TERMINATED)
  {
    if(self->state != SOUND_SETTINGS_STATE_TERMINATED)
      stateChanged = true;
    self->state = SOUND_SETTINGS_STATE_TERMINATED;
  }
  
  if(stateChanged && self->eventCallback)
    self->eventCallback(self, SOUND_SETTINGS_EVENT_TYPE_STATE_CHANGED, NULL, self->eventUserdata);
}

static void on_pa_event(pa_context *context, pa_subscription_event_type_t type, uint32_t index, SoundSettings *self)
{
  pa_subscription_event_type_t efacility = (type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
  pa_subscription_event_type_t etype = (type & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
  
  if(efacility == PA_SUBSCRIPTION_EVENT_SERVER)
  {
    pa_operation *o = pa_context_get_server_info(self->context, (pa_server_info_cb_t)on_server_get_info, self);
    if(o) pa_operation_unref(o);
  }
  else if(etype == PA_SUBSCRIPTION_EVENT_NEW || etype == PA_SUBSCRIPTION_EVENT_CHANGE)
  {
    pa_operation *o = NULL;
    if(efacility == PA_SUBSCRIPTION_EVENT_SINK)
      o = pa_context_get_sink_info_by_index(self->context, index, (pa_sink_info_cb_t)on_sink_get_info, self);
    else if(efacility == PA_SUBSCRIPTION_EVENT_SOURCE)
      o = pa_context_get_source_info_by_index(self->context, index, (pa_source_info_cb_t)on_source_get_info, self);

    if(o) pa_operation_unref(o);
  }
  else if(etype == PA_SUBSCRIPTION_EVENT_REMOVE)
  {
    SoundDevice *device = get_device_at_index_with_type(self, index,
      (efacility == PA_SUBSCRIPTION_EVENT_SINK) ? SOUND_DEVICE_TYPE_OUTPUT : SOUND_DEVICE_TYPE_INPUT, false, NULL);
    
    sound_device_ref(device);
    sound_device_invalidate(device);
    if(self->eventCallback)
      self->eventCallback(self, SOUND_SETTINGS_EVENT_TYPE_DEVICE_REMOVED, device, self->eventUserdata);
    else
      sound_device_unref(device);
  }
}

static void on_server_get_info(pa_context *context, const pa_server_info *server, SoundSettings *self)
{
  if(!server || !self) return;
  
  self->defaultSinkName = strdup(server->default_sink_name);
  self->defaultSourceName = strdup(server->default_source_name);
  
  // TODO: There aren't that many server updates, but refreshing all the sinks and sources each time might be too laggy
  //       Avoid if possible
  
  // Refresh all the sinks and sources
  // These will repeatedly call on_*_get_info for each sink/source
  pa_operation *o = pa_context_get_sink_info_list(self->context, (pa_sink_info_cb_t)on_sink_get_info, self);
  if(o) pa_operation_unref(o);
  o = pa_context_get_source_info_list(self->context, (pa_source_info_cb_t)on_source_get_info, self);
  if(o) pa_operation_unref(o);
}

static void on_sink_get_info(pa_context *context, const pa_sink_info *sink, int eol, SoundSettings *self)
{
  if(!sink || !self) return; // When listing devices, a final NULL device will be sent (with eol = 1)
  
  
  // printf("   NAME: %s\n", sink->name);
  // printf("   DESC: %s\n", sink->description);
  // 
  // void *it = NULL;
  // const char *key;
  // while((key = pa_proplist_iterate(sink->proplist, &it)) != NULL)
  // {
  //   const char *val = pa_proplist_gets(sink->proplist, key);
  //   printf("   key: %s, val: %s\n", key, val);
  // }
  // 
  // for(uint32_t i=0; i<sink->n_ports; ++i)
  // {
  //   printf("   port %i NAME: %s  %s\n", i, sink->ports[i]->name, (sink->active_port == sink->ports[i]) ? "[active]" : "");
  //   printf("   port %i DESC: %s\n", i, sink->ports[i]->description);
  // }
  // printf("\n\n");

  bool created;
  SoundDevice *device = get_device_at_index_with_type(self, sink->index, SOUND_DEVICE_TYPE_OUTPUT, true, &created);

  if(!device) { printf("no device\n"); return; }
  
  // Try to pick a good name and description
  const char *name = sink->description;
  const char *description;
  
  if(sink->active_port)
    description = sink->active_port->description;
  else if(pa_proplist_contains(sink->proplist, "device.profile.description"))
    description = pa_proplist_gets(sink->proplist, "device.profile.description");
  else
    description = sink->name;
  
  strncpy(device->name, name, MAX_DEVICE_NAME_LENGTH-1); // Subtract 1 for NULL terminator
  device->name[MAX_DEVICE_NAME_LENGTH-1] = '\0';
  strncpy(device->description, description, MAX_DEVICE_DESCRIPTION_LENGTH-1);
  device->description[MAX_DEVICE_DESCRIPTION_LENGTH-1] = '\0';
  device->volume = ((float)(pa_cvolume_max(&sink->volume) - PA_VOLUME_MUTED))/(PA_VOLUME_NORM - PA_VOLUME_MUTED);
  device->balance = pa_cvolume_get_balance(&sink->volume, &sink->channel_map);
  device->mute = sink->mute;
  device->cvolume = sink->volume;
  
  bool wasActive = device->active;
  device->active = (strcmp(sink->name, self->defaultSinkName) == 0);
  
  if(self->eventCallback)
    self->eventCallback(self, created ? SOUND_SETTINGS_EVENT_TYPE_DEVICE_ADDED : SOUND_SETTINGS_EVENT_TYPE_DEVICE_CHANGED, device, self->eventUserdata);
    
  if(self->eventCallback && device->active && !wasActive)
    self->eventCallback(self, SOUND_SETTINGS_EVENT_TYPE_ACTIVE_DEVICE_CHANGED, device, self->eventUserdata);
}

static void on_source_get_info(pa_context *context, const pa_source_info *source, int eol, SoundSettings *self)
{
  if(!source || !self) return;
  
  bool created;
  SoundDevice *device = get_device_at_index_with_type(self, source->index, SOUND_DEVICE_TYPE_INPUT, true, &created);
  if(!device) { printf("no device\n"); return; }
  
  // Try to pick a good name and description
  const char *name = source->description;
  const char *description;
  
  if(source->active_port)
    description = source->active_port->description;
  else if(pa_proplist_contains(source->proplist, "device.profile.description"))
    description = pa_proplist_gets(source->proplist, "device.profile.description");
  else
    description = source->name;
  
  strncpy(device->name, source->name, MAX_DEVICE_NAME_LENGTH-1); // Subtract 1 for NULL terminator
  device->name[MAX_DEVICE_NAME_LENGTH-1] = '\0';
  strncpy(device->description, source->description, MAX_DEVICE_DESCRIPTION_LENGTH-1);
  device->description[MAX_DEVICE_DESCRIPTION_LENGTH-1] = '\0';
  device->volume = ((float)(pa_cvolume_max(&source->volume) - PA_VOLUME_MUTED))/(PA_VOLUME_NORM - PA_VOLUME_MUTED);
  device->balance = pa_cvolume_get_balance(&source->volume, &source->channel_map);
  device->mute = source->mute;
  device->cvolume = source->volume;

  bool wasActive = device->active;
  device->active = (strcmp(source->name, self->defaultSourceName) == 0);

  if(self->eventCallback)
    self->eventCallback(self, created ? SOUND_SETTINGS_EVENT_TYPE_DEVICE_ADDED : SOUND_SETTINGS_EVENT_TYPE_DEVICE_CHANGED, device, self->eventUserdata);
    
  if(self->eventCallback && device->active && !wasActive)
    self->eventCallback(self, SOUND_SETTINGS_EVENT_TYPE_ACTIVE_DEVICE_CHANGED, device, self->eventUserdata);
}

/*
 * Attempts to find an existing device with the index and type. If it cannot find it and 'create'
 * is true, a new SoundDevice will be created and added to the device list. Returns NULL if no
 * device could be found or created.
 */
static SoundDevice * get_device_at_index_with_type(SoundSettings *self, uint32_t index, SoundDeviceType type, bool create, bool *created)
{
  if(created)
    *created = false;
  
  if(!self) return NULL;
  
  SoundDevice *device = NULL;
  for(SoundDevice *i = self->firstDevice; i!=NULL; i=i->next)
  {
    if(i->type == type && i->index == index)
    {
      device = i;
      break;
    }
  }

  if(!create)
    return device;
  
  if(!device)
  {
    device = malloc(sizeof(SoundDevice));
    if(!device)
      return NULL;
    
    if(created)
      *created = true;
    
    memset(device, 0, sizeof(SoundDevice));
    device->ref = 1;
    device->index = index;
    device->type = type;
    device->owner = self;
    
    // Prepend devie to linked list
    if(self->firstDevice)
      self->firstDevice->prev = device;
    device->next = self->firstDevice;
    self->firstDevice = device;
  }
  
  return device;
}

SoundDevice * sound_devices_iterate(SoundSettings *settings, SoundDevice *prev)
{
  if(!settings) return NULL;
  return prev ? prev->next : settings->firstDevice;
}

SoundDevice * sound_settings_get_default_output_device(SoundSettings *settings)
{
  return get_device_at_index_with_type(settings, 0, SOUND_DEVICE_TYPE_OUTPUT, false, NULL);
}
SoundDevice * sound_settings_get_default_input_device(SoundSettings *settings)
{
  return get_device_at_index_with_type(settings, 0, SOUND_DEVICE_TYPE_INPUT, false, NULL);
}

SoundDevice * sound_settings_get_active_output_device(SoundSettings *settings)
{
  if(!settings) return NULL;
  for(SoundDevice *i = settings->firstDevice; i!=NULL; i=i->next)
    if(i->type == SOUND_DEVICE_TYPE_OUTPUT && i->active)
      return i;
  return NULL;
}
SoundDevice * sound_settings_get_active_input_device(SoundSettings *settings)
{
  if(!settings) return NULL;
  for(SoundDevice *i = settings->firstDevice; i!=NULL; i=i->next)
    if(i->type == SOUND_DEVICE_TYPE_INPUT && i->active)
      return i;
  return NULL;
}

SoundDeviceType sound_device_get_type(SoundDevice *device)
{
  if(!device) return SOUND_DEVICE_TYPE_ERROR;
  return device->type;
}

const char * sound_device_get_name(SoundDevice *device)
{
  if(!device) return NULL;
  return device->name;
}

const char * sound_device_get_description(SoundDevice *device)
{
  if(!device) return NULL;
  return device->description;
}

float sound_device_get_volume(SoundDevice *device)
{
  if(!device) return 0;
  return device->volume;
}

float sound_device_get_balance(SoundDevice *device)
{
  if(!device) return 0;
  return device->balance;
}

bool sound_device_get_muted(SoundDevice *device)
{
  if(!device) return false;
  return device->mute;
}

bool sound_device_activate(SoundDevice *device)
{
  if(!sound_device_is_valid(device)) return false;
  
  // TODO
}

bool sound_device_is_active(SoundDevice *device)
{
  if(!device) return false;
  return device->active;
}

bool sound_device_is_valid(SoundDevice *device)
{
  if(!device) return false;
  return device->prev || device->next;
}

bool sound_device_set_volume(SoundDevice *device, float volume)
{
  if(!sound_device_is_valid(device) || device->cvolume.channels == 0)
    return false;
  if(volume < 0)
    volume = 0;
  
  pa_volume_t newVol = (pa_volume_t)(volume * (PA_VOLUME_NORM - PA_VOLUME_MUTED) + PA_VOLUME_MUTED);
  pa_cvolume_scale(&device->cvolume, newVol);

  pa_operation *o = pa_context_set_sink_volume_by_index(device->owner->context, device->index, &device->cvolume, NULL, NULL);
  bool success = (o != NULL);
  if(o) pa_operation_unref(o);
  return success;
}

bool sound_device_set_balance(SoundDevice *device, float balance)
{
  if(!sound_device_is_valid(device)) return false;
  if(balance < -1) balance = -1;
  if(balance > 1) balance = 1;
  
  // TODO
}

bool sound_device_set_muted(SoundDevice *device, bool muted)
{
  if(!device) return false;
  if(device->mute == muted) return true;
  
  pa_operation *o = pa_context_set_sink_mute_by_index(device->owner->context, device->index, muted, NULL, NULL);
  bool success = (o != NULL);
  if(o) pa_operation_unref(o);
  return success;
}

SoundDevice * sound_device_ref(SoundDevice *device)
{
  if(!device) return NULL;
  device->ref ++;
  return device;
}

static void sound_device_unlink(SoundDevice *device)
{
  if(!device) return;
  
  if(device->next)
    device->next->prev = device->prev;
  if(device->prev)
    device->prev->next = device->next;
  if(device->owner->firstDevice == device)
    device->owner->firstDevice = device->next;
  device->prev = NULL;
  device->next = NULL;
}

/*
 * Immediately unlinks the device, unrefs it, and then frees it if necessary.
 */
static void sound_device_invalidate(SoundDevice *device)
{
  if(!device) return;

  sound_device_unlink(device);

  device->ref --;
  if(device->ref <= 0)
    free(device);
}

/*
 * Decreases the ref count by one until it hits 1, but will only ever hit 0 and
 * free if the device is invalidated (sound_device_invalidate). This keeps the
 * user from (accidentally) freeing the device while it's still in use.
 */
void sound_device_unref(SoundDevice *device)
{
  if(!device) return;
  
  if(sound_device_is_valid(device))
  {
    if(device->ref > 1)
      device->ref --;
  }
  else
  {
    device->ref --;
    if(device->ref <= 0)
    {
      sound_device_unlink(device);
      free(device);
    }
  }
}

static void unref_all_devices(SoundSettings *settings)
{
  for(SoundDevice *i = settings->firstDevice; i!=NULL; )
  {
    SoundDevice *current = i;
    i=i->next;
    sound_device_invalidate(current);
  }
}

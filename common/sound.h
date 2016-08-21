/*
 * This file is part of graphene-desktop.
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
 * sound.h/c
 * Methods for controlling system sound, including volume and audio output devices.
 * This is not (currently) designed for advanced audio configuration. It's just enough
 * for a simple GUI mixer.
 * Requires PulseAudio. Will cleanly fail if PulseAudio is not installed on the system.
 *
 * NOTE: Not entirely functional yet. Should work for getting/setting volume on
 * the active output device.
 */

#ifndef __GRAPHENE_SOUND_H__
#define __GRAPHENE_SOUND_H__

#include <pulse/pulseaudio.h>
#include <stdbool.h>

struct _SoundSettings;
typedef struct _SoundSettings SoundSettings;

struct _SoundDevice;
typedef struct _SoundDevice SoundDevice;

typedef enum {
  SOUND_DEVICE_TYPE_ERROR,
  SOUND_DEVICE_TYPE_OUTPUT,
  // SOUND_DEVICE_TYPE_OUTPUT_CLIENT, // Represents a client who is outputting audio
  SOUND_DEVICE_TYPE_INPUT,
} SoundDeviceType;

typedef enum {
  SOUND_SETTINGS_EVENT_TYPE_STATE_CHANGED,
  SOUND_SETTINGS_EVENT_TYPE_ACTIVE_DEVICE_CHANGED,
  SOUND_SETTINGS_EVENT_TYPE_DEVICE_CHANGED,
  SOUND_SETTINGS_EVENT_TYPE_DEVICE_ADDED,
  SOUND_SETTINGS_EVENT_TYPE_DEVICE_REMOVED
} SoundSettingsEventType;

typedef enum {
  SOUND_SETTINGS_STATE_UNCONNECTED,
  SOUND_SETTINGS_STATE_CONNECTING,
  SOUND_SETTINGS_STATE_READY,
  SOUND_SETTINGS_STATE_FAILED,
  SOUND_SETTINGS_STATE_TERMINATED // Clean exit
} SoundSettingsState;

/*
 * Called when a mainloop object originally passed to sound_settings_init needs to be freed.
 * All this callback should do is free/unref mainloop.
 * You may pass a method such as pa_glib_mainloop_free directly to sound_settings_init.
 */
typedef void (*DestroyPAMainloopNotify)(void *mainloop);

/*
 * Called when an event happens.
 *
 * device: the affected device in the DEVICE_CHANGED, DEVICE_ADDED, DEVICE_REMOVED,
 * and ACTIVE_DEVICE_CHANGED events.
 * In only the case of DEVICE_REMOVED, 'device' will be an invalidated device,
 * and you must unref the device when you are finished with it.
 * For ACTIVE_DEVICE_CHANGED, 'device' is the now-active device.
 * For other events, device is NULL.
 */
typedef void (*SoundSettingsEventCallback)(SoundSettings *settings, SoundSettingsEventType type, SoundDevice *device, void *userdata);

/*
 * Creates a new instance of SoundSettings. If none yet exists, the return value of
 * sound_settings_get_default will be the SoundSettings created by this function
 * until it is freed. Consequently, you'll probably only need to call this once per
 * application. Unref the returned value with sound_settings_unref.
 *
 * Functions such as sound_settings_get_default_output_device probably won't work
 * immediately after this function returns, since a connection must be made to
 * PulseAudio first. Use sound_settings_set_changed_callback to listen for changes.
 *
 * mainloop: Create this using one of PA's mainloop implementations. pa_glib_mainloop
 * can be used for GLib/GTK applications. Must not be NULL. This value must be freed
 * from the destroyMainloopCallback.
 * 
 * mainloopAPI: The general representation of mainloop. For example, if using pa_glib_mainloop,
 * this would be pa_glib_mainloop_get_api(mainloop). In some cases, it might be the
 * same value as mainloop. This value does NOT get unrefed/freed in destroyMainloopCallback.
 *
 * props: Properties for the PA sound context. Create with pa_proplist_new and add
 * properties with pa_proplist_sets. Must not be NULL. You should free this value
 * after this function exits.
 *
 * destroyMainloopCallback: Called when the passed mainloop object needs to be freed.
 * See DestroyPAMainloopNotify documentation for more info. You probably just want to
 * pass pa_glib_mainloop_free for this.
 */
SoundSettings * sound_settings_init(void *mainloop, pa_mainloop_api *mainloopAPI, pa_proplist *props, DestroyPAMainloopNotify destroyMainloopCallback);
 
/*
 * Returns a ref of the default SoundSettings (the SoundSettings created by the first
 * call to sound_settings_init). Unref the returned value with sound_settings_unref.
 */
SoundSettings * sound_settings_get_default();

/*
 * Gets the current state of settings.
 */
SoundSettingsState sound_settings_get_state(SoundSettings *settings);

/*
 * Increases the ref count of settings by 1, and returns settings.
 * Does nothing and returns NULL if settings is NULL.
 */
SoundSettings * sound_settings_ref(SoundSettings *settings);

/*
 * Decrease the ref count of settings by 1, and free settings if it reaches 0.
 * Returns false if settings is NULL or if settings has not been freed, true otherwise.
 * When settings is freed, and SoundDevice you do not hold a ref to will be freed.
 *
 * DO NOT CALL this inside the callback set in sound_settings_set_event_callback.
 * Even if it's a state change to TERMINATED. If you do, you will have a bad day.
 */
bool sound_settings_unref(SoundSettings *settings);

/*
 * Set a callback for events. See the documentation for SoundSettingsChangedCallback
 * for more info.
 * In only the case of event type DEVICE_REMOVED, you must unref the device passed
 * to callback when you are finished with it.
 */
void sound_settings_set_event_callback(SoundSettings *settings, SoundSettingsEventCallback callback, void *userdata);

/*
 * Allows iterating through all sound devices. Pass NULL to 'prev' to get the first
 * device, and then pass the previous return value for the next device. NULL is returned
 * when there are no more devices.
 * Do not unref these values. Use sound_device_ref if you need to keep a reference.
 */
SoundDevice * sound_devices_iterate(SoundSettings *settings, SoundDevice *prev);

/*
 * Returns the default input/output device. Do not unref; use sound_device_ref if you
 * need to keep a reference. Returns NULL on failure or no default device.
 */
SoundDevice * sound_settings_get_default_output_device(SoundSettings *settings);
SoundDevice * sound_settings_get_default_input_device(SoundSettings *settings);

/*
 * Returns the currently active input/output device. Do not unref; use sound_device_ref
 * if you need to keep a reference. Returns NULL on failure or no active device.
 */
SoundDevice * sound_settings_get_active_output_device(SoundSettings *settings);
SoundDevice * sound_settings_get_active_input_device(SoundSettings *settings);

/*
 * Returns the type of the device, or SOUND_DEVICE_TYPE_ERROR on failure.
 * Is successful on invalid devices.
 */
SoundDeviceType sound_device_get_type(SoundDevice *device);

/*
 * Gets the human-readable name of the device. Do not free the returned string.
 */
const char * sound_device_get_name(SoundDevice *device);

/*
 * Gets the human-readable description of the device. Do not free the returned string.
 */
const char * sound_device_get_description(SoundDevice *device);

/*
 * Returns the volume of the device, a range from 0 to +infinity, where 1 is
 * "100%" and larger values are amplified.
 */
float sound_device_get_volume(SoundDevice *device);

/*
 * Returns the left/right balance of the device. The value is clamped to [-1, 1]
 * where -1 is completely left and 1 is completely right. On devices where
 * balance doesn't make sense (ex. mono input), this returns 0.
 */
float sound_device_get_balance(SoundDevice *device);

/*
 * Returns true if the device is muted, false otherise.
 */
bool sound_device_get_muted(SoundDevice *device);

/*
 * Sets this device as the currently active input or output device.
 * Returns true on success, false otherwise.
 * Has no effect on invalid devices and returns false.
 */
bool sound_device_activate(SoundDevice *device);

/*
 * Convenience methd for device == sound_settings_get_active_*_device
 */
bool sound_device_is_active(SoundDevice *device);

/*
 * Return true if this device is still a valid device, false otherwise. If it
 * is no longer valid, you should unref it and clear it from any GUI lists immediately.
 */
bool sound_device_is_valid(SoundDevice *device);

/*
 * Sets the volume of the device. See sound_device_get_volume for allowed values
 * for volume. Returns true on success, false otherwise.
 * Has no effect on invalid devices and returns false.
 */
bool sound_device_set_volume(SoundDevice *device, float volume);

/*
 * Sets the balance of the device. See sound_device_get_balance for allowed values
 * for balance. Returns true on success, false otherwise.
 * Has no effect on invalid devices and returns false.
 */
bool sound_device_set_balance(SoundDevice *device, float balance);

/*
 * Sets the balance of the device. See sound_device_get_balance for allowed values
 * for balance. Returns true on success, false otherwise.
 * Has no effect on invalid devices and returns false.
 * Has no effect on devices where balance doesn't make sense (ex. mono input) and
 * returns true.
 */
bool sound_device_set_balance(SoundDevice *device, float balance);

/*
 * Sets if the device is muted. Returns true on success, false on failure.
 */
bool sound_device_set_muted(SoundDevice *device, bool muted);

/*
 * Increases the ref count of device by 1, and returns device.
 * Does nothing and returns NULL if device is NULL.
 */
SoundDevice * sound_device_ref(SoundDevice *device);

/*
 * Decrease the ref count of device by 1. The ref count will not drop below 1
 * and be freed until the device has been invalidated by its SoundSettings owner.
 */
void sound_device_unref(SoundDevice *device);

#endif // __GRAPHENE_SOUND_H__
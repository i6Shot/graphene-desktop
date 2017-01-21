/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#ifndef __CSK_AUDIO_H__
#define __CSK_AUDIO_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define CSK_TYPE_AUDIO_DEVICE csk_audio_device_get_type()
G_DECLARE_FINAL_TYPE(CskAudioDevice, csk_audio_device, CSK, AUDIO_DEVICE, GObject)

typedef enum
{
	CSK_AUDIO_DEVICE_TYPE_INVALID,
	CSK_AUDIO_DEVICE_TYPE_OUTPUT,
	CSK_AUDIO_DEVICE_TYPE_OUTPUT_CLIENT, // Represents a client who is outputting audio
	CSK_AUDIO_DEVICE_TYPE_INPUT,
	CSK_AUDIO_DEVICE_TYPE_INPUT_CLIENT, // A client who is listening to audio
} CskAudioDeviceType;

/*
 * Returns the type of the device. If the device has been removed, but a
 * CmkAudioDevice for it still exists, it will obtain the INVALID type.
 * Monitor the "type" property to see when a device becomes invalid, and
 * if it does, unref it and remove it from any GUI lists.
 */
CskAudioDeviceType csk_audio_device_get_type_(CskAudioDevice *device);

/*
 * Gets the human-readable name of the device.
 */
const char * csk_audio_device_get_name(CskAudioDevice *device);

/*
 * Gets the human-readable description of the device.
 */
const char * csk_audio_device_get_description(CskAudioDevice *device);

/*
 * Returns the volume of the device, a range from 0 to +infinity, where 1 is
 * "100%" and larger values are amplified. Returns 0 on failure.
 */
float csk_audio_device_get_volume(CskAudioDevice *device);

/*
 * Sets the volume of the device, 0 to +infinity, where 1 is "100%"
 */
void csk_audio_device_set_volume(CskAudioDevice *device, float volume);

/*
 * Returns the left/right balance of the device. The value is clamped to 
 * [-1, 1] where -1 is completely left and 1 is completely right. On
 * devices where balance doesn't make sense (ex. mono input), this returns 0.
 */
float csk_audio_device_get_balance(CskAudioDevice *device);

/*
 * Sets the balance of the device, -1 to 1 (completely left to completely
 * right)
 */
void csk_audio_device_set_balance(CskAudioDevice *device, float balance);

/*
 * Returns TRUE if the device is muted, FALSE otherise. Returns TRUE
 * on failure.
 */
gboolean csk_audio_device_get_muted(CskAudioDevice *device);

/*
 * Sets if the device is muted.
 */
void csk_audio_device_set_muted(CskAudioDevice *device, gboolean muted);

/*
 * Returns TRUE if this device is the default output or input device.
 * This is always FALSE for client devices.
 */
gboolean csk_audio_device_is_default(CskAudioDevice *device);

/*
 * Sets this device as the default input or output device.
 * Returns TRUE on success, FALSE otherwise.
 * This always fails on client devices.
 */
gboolean csk_audio_device_set_default(CskAudioDevice *device);



#define CSK_TYPE_AUDIO_DEVICE_MANAGER csk_audio_device_manager_get_type()
G_DECLARE_FINAL_TYPE(CskAudioDeviceManager, csk_audio_device_manager, CSK, AUDIO_DEVICE_MANAGER, GObject)

/*
 * Returns a reference to the default audio device manager. Free with
 * g_object_unref. You must wait for the manager's state to become
 * READY before getting any audio devices.
 */
CskAudioDeviceManager * csk_audio_device_manager_get_default();

/*
 * Returns TRUE if the manager is ready. You should not attempt to get
 * any audio devices if the manager is not ready. See "ready" property.
 */
gboolean csk_audio_device_manager_is_ready(CskAudioDeviceManager *manager);

/*
 * Gets the current default audio output device (transfer none, call
 * g_object_ref if you need to keep the device around).
 * Returns NULL on failure.
 */
CskAudioDevice * csk_audio_device_manager_get_default_output(CskAudioDeviceManager *manager);

/*
 * Gets the current default audio input device (transfer none, call
 * g_object_ref if you need to keep the device around).
 * Returns NULL on failure.
 */
CskAudioDevice * csk_audio_device_manager_get_default_output(CskAudioDeviceManager *manager);

/*
 * Gets a list of all audio input devices. [transfer none]
 * Returns NULL on failure.
 */
GList * csk_audio_device_manager_get_devices(CskAudioDeviceManager *manager);

#endif // __GRAPHENE_AUDIO_H__

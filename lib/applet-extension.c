/*
 * vos-desktop
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

#include "applet-extension.h"

/**
 * SECTION:vos-applet-extension
 * @short_description: Interface for applet extension plugins.
 **/

G_DEFINE_INTERFACE(VosAppletExtension, vos_applet_extension, G_TYPE_OBJECT)

static void vos_applet_extension_default_init(VosAppletExtensionInterface *iface)
{
}

/**
 * vos_applet_extension_get_widget:
 * @extension: A #VosAppletExtension.
 *
 * Called when the extension is loaded. The extension should
 * create a GtkWidget of any kind to return. This widget is
 * automatically placed into the panel at the best location.
 *
 * If the plugin is removed, the applet is destroyed and the
 * destroy signal is sent.
 *
 * Returns: (transfer none): A #GtkWidget to automatically place
 * into the panel.
 */
GtkWidget * vos_applet_extension_get_widget(VosAppletExtension *extension, VosPanel *panel)
{
  VosAppletExtensionInterface *iface;
  
  g_return_val_if_fail(VOS_IS_APPLET_EXTENSION(extension), NULL);

  iface = VOS_APPLET_EXTENSION_GET_IFACE(extension);
  g_return_val_if_fail(iface->get_widget != NULL, NULL);

  return iface->get_widget(extension, panel);
}
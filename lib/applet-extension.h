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
 * applet-extension.h/.c
 * Provides an interface for LibPeas plugins to extend for adding widgets into the Panel.
 */

#ifndef __VOS_APPLET_EXTENSION_H__
#define __VOS_APPLET_EXTENSION_H__

#include <gtk/gtk.h>
#include "panel.h"

G_BEGIN_DECLS

/*
 * Type declaration.
 */
#define VOS_TYPE_APPLET_EXTENSION  vos_applet_extension_get_type()
G_DECLARE_INTERFACE(VosAppletExtension, vos_applet_extension, VOS, APPLET_EXTENSION, GObject)

/**
 * VosAppletExtensionInterface:
 * @g_iface: The parent interface.
 * @create_applet: Gets the widget created by the extension.
 *
 * Provides an interface for applet extension plugins.
 */
struct _VosAppletExtensionInterface {
  GTypeInterface g_iface;

  /* Virtual public methods */
  GtkWidget*  (*get_widget)            (VosAppletExtension *extension, VosPanel *panel);
};

/*
 * Public methods
 */
GtkWidget *         vos_applet_extension_get_widget       (VosAppletExtension *extension, VosPanel *panel);

G_END_DECLS

#endif /* __VOS_APPLET_EXTENSION_H__ */

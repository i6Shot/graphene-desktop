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
 * applet-extension.h/.c
 * Provides an interface for LibPeas plugins to extend for adding widgets into the Panel.
 */

#ifndef __GRAPHENE_APPLET_EXTENSION_H__
#define __GRAPHENE_APPLET_EXTENSION_H__

#include <gtk/gtk.h>
#include "panel.h"

G_BEGIN_DECLS

/*
 * Type declaration.
 */
#define GRAPHENE_TYPE_APPLET_EXTENSION  graphene_applet_extension_get_type()
G_DECLARE_INTERFACE(GrapheneAppletExtension, graphene_applet_extension, GRAPHENE, APPLET_EXTENSION, GObject)

/**
 * GrapheneAppletExtensionInterface:
 * @g_iface: The parent interface.
 * @create_applet: Gets the widget created by the extension.
 *
 * Provides an interface for applet extension plugins.
 */
struct _GrapheneAppletExtensionInterface {
  GTypeInterface g_iface;

  /* Virtual public methods */
  GtkWidget*  (*get_widget)            (GrapheneAppletExtension *extension, GraphenePanel *panel);
};

/*
 * Public methods
 */
GtkWidget *         graphene_applet_extension_get_widget       (GrapheneAppletExtension *extension, GraphenePanel *panel);

G_END_DECLS

#endif /* __GRAPHENE_APPLET_EXTENSION_H__ */

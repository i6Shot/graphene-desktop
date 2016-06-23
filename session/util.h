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
 * util.h/c
 */
 
#include <glib.h>
#include <gio/gio.h>

gchar ** strv_append(const gchar * const *list, const gchar *str);
gchar * str_trim(const gchar *str);
gint str_indexof(const gchar *str, const gchar c);

GVariant * get_gsettings_value(const gchar *schemaId, const gchar *key);
GObject * monitor_gsettings_key(const gchar *schemaId, const gchar *key, GCallback callback, gpointer userdata);
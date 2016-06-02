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
 * util.c
 */

#include "util.h"

/*
 * Takes a list of strings and a string to append.
 * If <list> is NULL, a new list of strings is returned containing only <str>.
 * If <str> is NULL, a duplicated <list> is returned.
 * If both are NULL, a new, empty list of strings is returned.
 * The returned list is NULL-terminated (even if both parameters are NULL).
 * The returned list should be freed with g_strfreev().
 */
gchar ** strv_append(const gchar * const *list, const gchar *str)
{
  guint listLength = 0;
  
  if(list != NULL)
  {
    while(list[listLength])
      ++listLength;
  }
  
  gchar **newList = g_new(gchar*, listLength + ((str==NULL)?0:1) + 1); // +1 for new str, +1 for ending NULL
  
  for(guint i=0;i<listLength;++i)
    newList[i] = g_strdup(list[i]);
    
  if(str != NULL)
    newList[listLength] = g_strdup(str);
    
  newList[listLength + ((str==NULL)?0:1)] = NULL;
  
  return newList;
}

/*
 * Removes trailing and leading whitespace from a string.
 * Returns a newly allocated string. Parameter is unmodified.
 */
gchar * str_trim(const gchar *str)
{
  if(!str)
    return NULL;
    
  guint32 len=0;
  for(;str[len]!='\0';++len);
  
  guint32 start=0;
  for(;str[start]!='\0';++start)
    if(!g_ascii_isspace(str[start]))
      break;
      
  guint32 end=len;
  for(;end>start;--end)
    if(!g_ascii_isspace(str[end-1]))
      break;
      
  gchar *newstr = g_new(gchar, end-start+1);
  for(guint32 i=0;(start+i)<end;++i)
    newstr[i] = str[start+i];
  newstr[end-start]='\0';
  return newstr;
}

/*
 * Returns the index of the first occurrence of c in str. Return -1 if not found or if str is NULL.
 * Only for ASCII.
 */
gint str_indexof(const gchar *str, const gchar c)
{
  if(!str)
    return -1;
    
  for(gint i=0;str[i]!=NULL;++i)
    if(str[i] == c)
      return i;
  
  return -1;
}

static GSettings * get_gsettings_from_schema_with_key(const gchar *schemaId, const gchar *key)
{
  GSettingsSchemaSource *source = g_settings_schema_source_get_default();
  if(!source)
    return NULL;
  
  GSettingsSchema *schema = g_settings_schema_source_lookup(source, schemaId, TRUE);
  if(!schema)
    return NULL;
    
  if(!g_settings_schema_has_key(schema, key))
  {
    g_settings_schema_unref(schema);
    return NULL;
  }
  
  GSettings *settings = g_settings_new_full(schema, NULL, NULL);
  g_settings_schema_unref(schema);
  return settings;
}

/*
 * Gets the value of a given GSetting key in the given schema using the default settings source.
 * If the schema or key does not exit, or settings are unavailable, this returns NULL.
 * The return value, if non-NULL, must be freed.
 */
GVariant * get_gsettings_value(const gchar *schemaId, const gchar *key)
{
  GSettings *settings = get_gsettings_from_schema_with_key(schemaId, key);
  if(!settings)
    return NULL;
  
  GVariant *value = g_settings_get_value(settings, key);
  g_object_unref(settings);
  return value;
}

/*
 * Monitors a given GSetting key in the given schema using the default settings source.
 * If the schema or key does not exit, or settings are unavailable, this returns NULL.
 * The return value, if non-NULL, should be freed to stop monitoring.
 * Callback is connected to the GSetting changed::<key> signal *swapped*.
 */
GObject * monitor_gsettings_key(const gchar *schemaId, const gchar *key, GCallback callback, gpointer userdata)
{
  GObject *settings = G_OBJECT(get_gsettings_from_schema_with_key(schemaId, key));
  if(!settings)
    return NULL;
  
  gchar *signalName = g_strdup_printf("changed::%s", key);
  g_signal_connect_swapped(settings, signalName, callback, userdata);
  g_free(signalName);
  return settings;
}

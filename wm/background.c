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
 */
 
#include "background.h"

// VosWMBackground class (private)
struct _VosWMBackground {
  MetaBackgroundGroup parent;
  MetaScreen *Screen;
  int ScreenIndex;
  MetaBackgroundActor *Actor;
  GSettings *Settings;
};

static void vow_wm_background_init_after(VosWMBackground *backgroundGroup);
static void vos_wm_background_dispose(GObject *gobject);
static void settings_changed(GSettings *settings, guint key, VosWMBackground *backgroundGroup);
static void update(VosWMBackground *backgroundGroup);

G_DEFINE_TYPE (VosWMBackground, vos_wm_background, META_TYPE_BACKGROUND_GROUP);

static void vos_wm_background_class_init(VosWMBackgroundClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = vos_wm_background_dispose;
}

VosWMBackground* vos_wm_background_new(MetaScreen *screen, int screenIndex)
{
  VosWMBackground *backgroundGroup = VOS_WM_BACKGROUND(g_object_new(VOS_TYPE_WM_BACKGROUND, NULL));
  backgroundGroup->Screen = g_object_ref(screen);
  backgroundGroup->ScreenIndex = screenIndex;
  vow_wm_background_init_after(backgroundGroup);
  return backgroundGroup;
}

static void vos_wm_background_init(VosWMBackground *backgroundGroup)
{
}

static void vow_wm_background_init_after(VosWMBackground *backgroundGroup)
{
  backgroundGroup->Actor = NULL;
  
  MetaRectangle rect = meta_rect(0,0,0,0);
  meta_screen_get_monitor_geometry(backgroundGroup->Screen, backgroundGroup->ScreenIndex, &rect);
  clutter_actor_set_position(CLUTTER_ACTOR(backgroundGroup), rect.x, rect.y);
  clutter_actor_set_size(CLUTTER_ACTOR(backgroundGroup), rect.width, rect.height);
  
  backgroundGroup->Settings = g_settings_new("org.gnome.desktop.background");
  g_signal_connect(backgroundGroup->Settings, "changed", G_CALLBACK(settings_changed), backgroundGroup);
  update(backgroundGroup);
}

static void vos_wm_background_dispose(GObject *gobject)
{
  g_clear_object(&VOS_WM_BACKGROUND(gobject)->Screen);
  g_clear_object(&VOS_WM_BACKGROUND(gobject)->Actor);
  G_OBJECT_CLASS(vos_wm_background_parent_class)->dispose(gobject);
}



static void settings_changed(GSettings *settings, guint key, VosWMBackground *backgroundGroup)
{
  update(backgroundGroup);
}

static void update_done(MetaBackgroundActor *newActor, VosWMBackground *backgroundGroup)
{
  clutter_actor_remove_all_transitions(CLUTTER_ACTOR(newActor));
  clutter_actor_set_opacity(CLUTTER_ACTOR(newActor), 255);
  g_signal_handlers_disconnect_by_func(newActor, update_done, NULL);

  if(backgroundGroup->Actor)
    clutter_actor_remove_child(CLUTTER_ACTOR(backgroundGroup), CLUTTER_ACTOR(backgroundGroup->Actor));
  backgroundGroup->Actor = newActor;
}

static void update(VosWMBackground *backgroundGroup)
{
  MetaBackgroundActor *newActor = META_BACKGROUND_ACTOR(meta_background_actor_new(backgroundGroup->Screen, backgroundGroup->ScreenIndex));
  MetaBackground *newBackground = meta_background_new(backgroundGroup->Screen);
  meta_background_actor_set_background(newActor, newBackground);
  
  MetaRectangle rect = meta_rect(0,0,0,0);
  meta_screen_get_monitor_geometry(backgroundGroup->Screen, backgroundGroup->ScreenIndex, &rect);
  clutter_actor_set_position(CLUTTER_ACTOR(newActor), 0, 0); // x,y 0,0 relative to this monitor's background group
  clutter_actor_set_size(CLUTTER_ACTOR(newActor), rect.width, rect.height);
  clutter_actor_set_opacity(CLUTTER_ACTOR(newActor), 0);
  clutter_actor_insert_child_at_index(CLUTTER_ACTOR(backgroundGroup), CLUTTER_ACTOR(newActor), -1);
  clutter_actor_show(CLUTTER_ACTOR(newActor));
  
  ClutterColor *primaryColor = clutter_color_new(255, 255, 255, 255);
  ClutterColor *secondaryColor = clutter_color_new(255, 255, 255, 255);
  clutter_color_from_string(primaryColor, g_settings_get_string(backgroundGroup->Settings, "primary-color"));
  clutter_color_from_string(secondaryColor, g_settings_get_string(backgroundGroup->Settings, "secondary-color"));
  GDesktopBackgroundShading shading = g_settings_get_enum(backgroundGroup->Settings, "color-shading-type");
  meta_background_set_gradient(newBackground, shading, primaryColor, secondaryColor);
  
  char *backgroundImageFileName = g_settings_get_string(backgroundGroup->Settings, "picture-uri");
  GDesktopBackgroundStyle style = g_settings_get_enum(backgroundGroup->Settings, "picture-options");
  GFile *backgroundImageFile = g_file_new_for_uri(backgroundImageFileName);
  meta_background_set_file(newBackground, backgroundImageFile, style);
  
  g_signal_connect(newActor, "transitions_completed", G_CALLBACK(update_done), backgroundGroup);
  clutter_actor_save_easing_state(CLUTTER_ACTOR(newActor));
  clutter_actor_set_easing_mode(CLUTTER_ACTOR(newActor), CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(CLUTTER_ACTOR(newActor), 1000);
  clutter_actor_set_opacity(CLUTTER_ACTOR(newActor), 255);
  clutter_actor_restore_easing_state(CLUTTER_ACTOR(newActor));
}


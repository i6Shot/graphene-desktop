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
 */

#include "users.h"
#include <gio/gio.h>
#include <act/act.h>

struct _GrapheneProfileNameLabel
{
  GtkLabel parent;
  gchar *username;
  ActUserManager *manager;
  ActUser *user;
  gulong userChangedHandlerID;
  gulong notifyIsLoadedID;
};

static void graphene_profile_name_label_finalize(GrapheneProfileNameLabel *self);
static void on_user_manager_notify_is_loaded(GrapheneProfileNameLabel *self);
static void on_user_updated(GrapheneProfileNameLabel *self, ActUser *user);


G_DEFINE_TYPE(GrapheneProfileNameLabel, graphene_profile_name_label, GTK_TYPE_LABEL)


GrapheneProfileNameLabel * graphene_profile_name_label_new()
{
  return GRAPHENE_PROFILE_NAME_LABEL(g_object_new(GRAPHENE_TYPE_PROFILE_NAME_LABEL, NULL));
}

static void graphene_profile_name_label_class_init(GrapheneProfileNameLabelClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = G_CALLBACK(graphene_profile_name_label_finalize);
}

static void graphene_profile_name_label_init(GrapheneProfileNameLabel *self)
{
  self->manager = act_user_manager_get_default();
  if(act_user_manager_no_service(self->manager))
  {
    g_critical("Cannot access AccountsSerivce. Make sure accounts-daemon is running."); // TODO: Try again?
    return;
  }
  
  gboolean isLoaded;
  g_object_get(self->manager, "is-loaded", &isLoaded, NULL);
  if(isLoaded)
    on_user_manager_notify_is_loaded(self);
  else
    self->notifyIsLoadedID = g_signal_connect_swapped(self->manager, "notify::is-loaded", G_CALLBACK(on_user_manager_notify_is_loaded), self);
    
  on_user_updated(self, self->user);
}

static void graphene_profile_name_label_finalize(GrapheneProfileNameLabel *self)
{
  g_clear_pointer(&self->username, g_free);
  
  if(self->user && self->userChangedHandlerID)
    g_signal_handler_disconnect(self->user, self->userChangedHandlerID);
  if(self->manager && self->notifyIsLoadedID)
    g_signal_handler_disconnect(self->manager, self->notifyIsLoadedID);
    
  self->user = NULL;
  self->manager = NULL;
  self->userChangedHandlerID = 0;
  self->notifyIsLoadedID = 0;
}

void graphene_profile_name_label_set_user(GrapheneProfileNameLabel *self, const gchar *username)
{
  g_clear_pointer(&self->username, g_free);
  self->username = g_strdup(username);
  
  if(!self->username)
    self->username = g_strdup(g_getenv("USER"));
    
  if(!self->username)
    g_critical("Cannot determine current user (env variable $USER).");
    
  on_user_manager_notify_is_loaded(self);
}

static void on_user_manager_notify_is_loaded(GrapheneProfileNameLabel *self)
{
  if(self->user && self->userChangedHandlerID)
    g_signal_handler_disconnect(self->user, self->userChangedHandlerID);
  self->user = NULL;
  self->userChangedHandlerID = 0;
  
  if(self->username)
  {
    self->user = act_user_manager_get_user(self->manager, self->username);
    self->userChangedHandlerID = g_signal_connect_swapped(self->user, "changed", G_CALLBACK(on_user_updated), self);
  }
  
  on_user_updated(self, self->user);
}

static void on_user_updated(GrapheneProfileNameLabel *self, ActUser *user)
{
  const gchar *realName = "";
  if(user)
    realName = act_user_get_real_name(user);
  gtk_label_set_text(GTK_LABEL(self), realName);
}
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

static void graphene_profile_name_label_finalize(GObject *self_);
static void label_on_user_manager_notify_is_loaded(GrapheneProfileNameLabel *self);
static void label_on_user_updated(GrapheneProfileNameLabel *self, ActUser *user);


G_DEFINE_TYPE(GrapheneProfileNameLabel, graphene_profile_name_label, GTK_TYPE_LABEL)


GrapheneProfileNameLabel * graphene_profile_name_label_new()
{
  return GRAPHENE_PROFILE_NAME_LABEL(g_object_new(GRAPHENE_TYPE_PROFILE_NAME_LABEL, NULL));
}

static void graphene_profile_name_label_class_init(GrapheneProfileNameLabelClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = graphene_profile_name_label_finalize;
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
    label_on_user_manager_notify_is_loaded(self);
  else
    self->notifyIsLoadedID = g_signal_connect_swapped(self->manager, "notify::is-loaded", G_CALLBACK(label_on_user_manager_notify_is_loaded), self);
    
  label_on_user_updated(self, self->user);
}

static void graphene_profile_name_label_finalize(GObject *self_)
{
  GrapheneProfileNameLabel *self = GRAPHENE_PROFILE_NAME_LABEL(self);
  g_clear_pointer(&self->username, g_free);
  
  if(self->user && self->userChangedHandlerID)
    g_signal_handler_disconnect(self->user, self->userChangedHandlerID);
  if(self->manager && self->notifyIsLoadedID)
    g_signal_handler_disconnect(self->manager, self->notifyIsLoadedID);
    
  self->user = NULL;
  self->manager = NULL;
  self->userChangedHandlerID = 0;
  self->notifyIsLoadedID = 0;
  
  G_OBJECT_CLASS(graphene_profile_name_label_parent_class)->finalize(self_);
}

void graphene_profile_name_label_set_user(GrapheneProfileNameLabel *self, const gchar *username)
{
  g_clear_pointer(&self->username, g_free);
  self->username = g_strdup(username);
  
  if(!self->username)
    self->username = g_strdup(g_getenv("USER"));
    
  if(!self->username)
    g_critical("Cannot determine current user (env variable $USER).");
    
  label_on_user_manager_notify_is_loaded(self);
}

static void label_on_user_manager_notify_is_loaded(GrapheneProfileNameLabel *self)
{
  if(self->user && self->userChangedHandlerID)
    g_signal_handler_disconnect(self->user, self->userChangedHandlerID);
  self->user = NULL;
  self->userChangedHandlerID = 0;
  
  if(self->username)
  {
    self->user = act_user_manager_get_user(self->manager, self->username);
    self->userChangedHandlerID = g_signal_connect_swapped(self->user, "changed", G_CALLBACK(label_on_user_updated), self);
  }
  
  label_on_user_updated(self, self->user);
}

static void label_on_user_updated(GrapheneProfileNameLabel *self, ActUser *user)
{
  const gchar *realName = "";
  if(user)
    realName = act_user_get_real_name(user);
  gtk_label_set_text(GTK_LABEL(self), realName);
}





struct _GrapheneProfilePicture
{
  GtkDrawingArea parent;
  gchar *username;
  ActUserManager *manager;
  ActUser *user;
  gulong userChangedHandlerID;
  gulong notifyIsLoadedID;
};

static void graphene_profile_picture_finalize(GObject *self_);
static void picture_on_user_manager_notify_is_loaded(GrapheneProfilePicture *self);
static gboolean picture_on_draw(GtkWidget *self_, cairo_t *cr);


G_DEFINE_TYPE(GrapheneProfilePicture, graphene_profile_picture, GTK_TYPE_DRAWING_AREA)


GrapheneProfilePicture * graphene_profile_picture_new()
{
  return GRAPHENE_PROFILE_PICTURE(g_object_new(GRAPHENE_TYPE_PROFILE_PICTURE, NULL));
}

static void graphene_profile_picture_class_init(GrapheneProfilePictureClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = graphene_profile_picture_finalize;
  GTK_WIDGET_CLASS(klass)->draw = picture_on_draw;
}

static void graphene_profile_picture_init(GrapheneProfilePicture *self)
{
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "graphene-profile-picture");

  self->manager = act_user_manager_get_default();
  if(act_user_manager_no_service(self->manager))
  {
    g_critical("Cannot access AccountsSerivce. Make sure accounts-daemon is running."); // TODO: Try again?
    return;
  }
  
  gboolean isLoaded;
  g_object_get(self->manager, "is-loaded", &isLoaded, NULL);
  if(isLoaded)
    picture_on_user_manager_notify_is_loaded(self);
  else
    self->notifyIsLoadedID = g_signal_connect_swapped(self->manager, "notify::is-loaded", G_CALLBACK(picture_on_user_manager_notify_is_loaded), self);
    
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void graphene_profile_picture_finalize(GObject *self_)
{
  GrapheneProfilePicture *self = GRAPHENE_PROFILE_PICTURE(self_);
  g_clear_pointer(&self->username, g_free);
  
  if(self->user && self->userChangedHandlerID)
    g_signal_handler_disconnect(self->user, self->userChangedHandlerID);
  if(self->manager && self->notifyIsLoadedID)
    g_signal_handler_disconnect(self->manager, self->notifyIsLoadedID);
    
  self->user = NULL;
  self->manager = NULL;
  self->userChangedHandlerID = 0;
  self->notifyIsLoadedID = 0;
  
  G_OBJECT_CLASS(graphene_profile_picture_parent_class)->finalize(self_);
}

void graphene_profile_picture_set_user(GrapheneProfilePicture *self, const gchar *username)
{
  g_clear_pointer(&self->username, g_free);
  self->username = g_strdup(username);
  
  if(!self->username)
    self->username = g_strdup(g_getenv("USER"));
    
  if(!self->username)
    g_critical("Cannot determine current user (env variable $USER).");
    
  picture_on_user_manager_notify_is_loaded(self);
}

static void picture_on_user_manager_notify_is_loaded(GrapheneProfilePicture *self)
{
  if(self->user && self->userChangedHandlerID)
    g_signal_handler_disconnect(self->user, self->userChangedHandlerID);
  self->user = NULL;
  self->userChangedHandlerID = 0;
  
  if(self->username)
  {
    self->user = act_user_manager_get_user(self->manager, self->username);
    self->userChangedHandlerID = g_signal_connect_swapped(self->user, "changed", G_CALLBACK(gtk_widget_queue_draw), self);
  }
  
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static GdkPixbuf * picture_get_picture_pixmap(GrapheneProfilePicture *self, gboolean *bg)
{
  // First try to get picture from AccountsService
  if(self->user)
  {
    const gchar *path = act_user_get_icon_file(self->user);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
    if(pixbuf)
    {
      if(bg) *bg = TRUE;
      return pixbuf;
    }
  }
  
  // Now try the user's .face file
  {
    gchar *path = g_strdup_printf("/home/%s/.face", self->username);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    if(pixbuf)
    {
      if(bg) *bg = TRUE;
      return pixbuf;
    }
  }
  
  // No user image found, so just get a default user icon
  {
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    int size = MIN(gtk_widget_get_allocated_width(GTK_WIDGET(self)), gtk_widget_get_allocated_height(GTK_WIDGET(self)));
    GdkPixbuf *pixbuf = gtk_icon_theme_load_icon(theme, "system-users", size, 0, NULL);
    if(pixbuf)
    {
      if(bg) *bg = FALSE;
      return pixbuf;
    }
  }
  
  // Well that sucks
  if(bg) *bg = TRUE;
  return NULL;
}

static gboolean picture_on_draw(GtkWidget *self_, cairo_t *cr)
{
  GrapheneProfilePicture *self = GRAPHENE_PROFILE_PICTURE(self_);
  
  // Render background
  GtkStyleContext *styleContext = gtk_widget_get_style_context(GTK_WIDGET(self));
  double width = gtk_widget_get_allocated_width(GTK_WIDGET(self));
  double height = gtk_widget_get_allocated_height(GTK_WIDGET(self));
  gtk_render_background(styleContext, cr, 0, 0, width, height);
  
  // The size of the final image
  double size = MIN(width, height);
  
  // Create an image surface with the profile image, applying a white background
  // Scale it up a bit for supersampling
  cairo_surface_t *db = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size*2, size*2);
  cairo_t *dbcr = cairo_create(db);
  
  double dbWidth = cairo_image_surface_get_width(db);
  double dbHeight = cairo_image_surface_get_height(db);
  
  cairo_arc(dbcr, dbWidth/2, dbHeight/2, MIN(dbWidth, dbHeight)/2, 0, G_PI*2);
  cairo_clip(dbcr);
  cairo_new_path(dbcr);
  
  gboolean bg;
  GdkPixbuf *img = picture_get_picture_pixmap(self, &bg);
  
  if(bg)
  {
    cairo_set_source_rgb(dbcr, 0.827, 0.827, 0.827);
    cairo_paint(dbcr);
  }
  
  if(img)
  {
    cairo_scale(dbcr, dbWidth/gdk_pixbuf_get_width(img), dbHeight/gdk_pixbuf_get_height(img));
    gdk_cairo_set_source_pixbuf(dbcr, img, 0, 0);
    cairo_paint(dbcr);
  }
  
  cairo_surface_flush(db);
  
  // TODO: Maybe cache db so it doesn't have to recreate it every time it draws? Not really necessary right now
  
  // Render the image to the widget
  cairo_scale(cr, size/dbWidth, size/dbHeight);
  cairo_set_source_surface(cr, db,
    (width/2)/(size/dbWidth)-dbWidth/2,
    (height/2)/(size/dbHeight)-dbHeight/2);
  cairo_paint(cr);
  
  return FALSE;
}
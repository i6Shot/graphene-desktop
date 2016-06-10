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

#define WNCK_I_KNOW_THIS_IS_UNSTABLE

#include "tasklist-applet.h"
#include <glib.h>
#include <libwnck/libwnck.h>

struct _GrapheneTasklistApplet
{
  GtkBox parent;
  
  GHashTable *buttons;
};


static void graphene_tasklist_applet_finalize(GrapheneTasklistApplet *self);
static void on_window_opened(GrapheneTasklistApplet *self, WnckWindow *window, WnckScreen *screen);
static void on_window_closed(GrapheneTasklistApplet *self, WnckWindow *window, WnckScreen *screen);
static void on_active_window_changed(GrapheneTasklistApplet *self, WnckWindow *window, WnckScreen *screen);
static void on_window_state_changed(WnckWindow *window, WnckWindowState *changedMask, WnckWindowState* newState, GtkButton *button);
static void on_button_clicked(GtkButton *button, WnckWindow *window);
static void on_button_size_allocate(GtkButton *button, GtkAllocation *allocation, WnckWindow *window);

G_DEFINE_TYPE(GrapheneTasklistApplet, graphene_tasklist_applet, GTK_TYPE_BOX)


GrapheneTasklistApplet* graphene_tasklist_applet_new(void)
{
  return GRAPHENE_TASKLIST_APPLET(g_object_new(GRAPHENE_TYPE_TASKLIST_APPLET, NULL));
}

static void graphene_tasklist_applet_class_init(GrapheneTasklistAppletClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = G_CALLBACK(graphene_tasklist_applet_finalize);
}

static void graphene_tasklist_applet_init(GrapheneTasklistApplet *self)
{  
  gtk_box_set_homogeneous(GTK_BOX(self), FALSE);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_HORIZONTAL);
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "graphene-tasklist-applet");

  self->buttons = g_hash_table_new(NULL, NULL);

  WnckScreen *screen = wnck_screen_get_default();
  g_signal_connect_swapped(screen, "window_opened", G_CALLBACK(on_window_opened), self);
  g_signal_connect_swapped(screen, "window_closed", G_CALLBACK(on_window_closed), self);
  g_signal_connect_swapped(screen, "active_window_changed", G_CALLBACK(on_active_window_changed), self);

  // On the first launch, this does nothing because Wnck hasn't loaded yet and the window_opened event
  // will take care of calling on_window_opened once it has loaded. (get_windows() returns empty)
  // On subsequent launches, window_opened won't be emitted at the start, so the windows have to be
  // loaded now (get_windows() does not return empty)
  GList *windows = wnck_screen_get_windows(screen);
  for(GList *window = windows; window != NULL; window = windows->next)
    on_window_opened(self, WNCK_WINDOW(window->data), screen);
  
  gtk_widget_show_all(GTK_WIDGET(self));
}

static void graphene_tasklist_applet_finalize(GrapheneTasklistApplet *self)
{
  g_hash_table_unref(self->buttons);
}

static void on_window_opened(GrapheneTasklistApplet *self, WnckWindow *window, WnckScreen *screen)
{
  if(!WNCK_IS_WINDOW(window) || wnck_window_is_skip_tasklist(window) || g_hash_table_contains(self->buttons, window))
    return;
    
  GtkButton *button = GTK_BUTTON(gtk_button_new());
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(button)), "tasklist-button");
  g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), window);
  g_signal_connect(button, "size_allocate", G_CALLBACK(on_button_size_allocate), window);
  
  const char *className = wnck_window_get_class_group_name(window);
  gchar *modClassName = NULL;
  if(className)
    modClassName = g_utf8_strdown(className, -1);
  else
    modClassName = g_new0(gchar, 1);
    
  GtkImage *icon = GTK_IMAGE(gtk_image_new_from_icon_name(modClassName, GTK_ICON_SIZE_LARGE_TOOLBAR));
  g_free(modClassName);
  gtk_button_set_image(button, GTK_WIDGET(icon));
  gtk_button_set_always_show_image(button, TRUE);
  gtk_widget_show(GTK_WIDGET(button));

  gtk_box_pack_start(GTK_BOX(self), GTK_WIDGET(button), FALSE, FALSE, 0);
  g_hash_table_insert(self->buttons, window, button);
  
  g_signal_connect(window, "state_changed", on_window_state_changed, button);
}

static void on_window_closed(GrapheneTasklistApplet *self, WnckWindow *window, WnckScreen *screen)
{
  GtkButton *button = GTK_BUTTON(g_hash_table_lookup(self->buttons, window));
  
  if(button)
  {
    gtk_container_remove(GTK_CONTAINER(self), GTK_WIDGET(button));
    g_hash_table_remove(self->buttons, window);
  }
}

static void on_active_window_changed(GrapheneTasklistApplet *self, WnckWindow *prevWindow, WnckScreen *screen)
{
  GtkButton *prevWindowButton = GTK_BUTTON(g_hash_table_lookup(self->buttons, prevWindow));

  if(prevWindowButton)
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(prevWindowButton)), "task-active");
  
  WnckWindow *window = wnck_screen_get_active_window(screen);
  if(!WNCK_IS_WINDOW(window) || wnck_window_is_skip_tasklist(window))
    return;
  
  GtkButton *button = GTK_BUTTON(g_hash_table_lookup(self->buttons, window));
  if(button)
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(button)), "task-active");
}

static void on_window_state_changed(WnckWindow *window, WnckWindowState *changedMask, WnckWindowState* newState, GtkButton *button)
{
  // TODO: Does this work correctly? Should highlight the window orange when it needs attention. Overriden by tasklist-active-window?
  if(wnck_window_needs_attention(window))
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(button)), "task-attention");
  else
    gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(button)), "task-attention");
    
  // TODO: Add/remove buttons from tasklist depending on state of skip_tasklist
}

static void on_button_clicked(GtkButton *button, WnckWindow *window)
{
  guint32 time = gtk_get_current_event_time();
  
  if(wnck_window_is_minimized(window))
  {
    wnck_window_unminimize(window, time);
    wnck_window_activate(window, time);
  }
  else if(wnck_window_is_active(window))
  {
    wnck_window_minimize(window);
  }
  else
  {
    wnck_window_activate(window, time);
  }
}

static void on_button_size_allocate(GtkButton *button, GtkAllocation *allocation, WnckWindow *window)
{
  // Set the window's icon geometry to the screen coordinates of the button
  GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(button));
  gint buttonX, buttonY;
  gtk_widget_translate_coordinates(GTK_WIDGET(button), toplevel, 0, 0, &buttonX, &buttonY);
  GdkWindow *rootWindow = gtk_widget_get_window(GTK_WIDGET(toplevel));
  gint rootButtonX, rootButtonY;
  if(GDK_IS_WINDOW(rootWindow))
  {
    gdk_window_get_root_coords(rootWindow, buttonX, buttonY, &rootButtonX, &rootButtonY);
    wnck_window_set_icon_geometry(window, rootButtonX, rootButtonY, allocation->width, allocation->height);
  }
}
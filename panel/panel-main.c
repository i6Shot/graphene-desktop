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
 * main.c
 * Main file for the panel application.
 * Initializes a GtkApplication with one window, the VosPanel.
 * Cannot be a part of panel.c since panel.c is compiled into libvos.
 */

#include <lib/panel.h>

static void      activate       (GtkApplication *app, gpointer userdata);
extern VosPanel* vos_panel_new  (void);
extern gboolean  vos_panel_is_rebooting(VosPanel *self);

static VosPanel *panel = NULL;

int main(int argc, char **argv)
{
  GtkApplication *app = gtk_application_new("io.velt.graphene-panel", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  if(VOS_IS_PANEL(panel) && vos_panel_is_rebooting(panel))
    status = 120; // 120 tells the session manager to restart the panel instead of logging out
  g_object_unref(panel);
  return status;
}

static void activate(GtkApplication *app, gpointer userdata)
{
  // Create the panel
  panel = vos_panel_new();
  gtk_application_add_window(app, GTK_WINDOW(panel));
  
  // Show the panel
  gtk_widget_show(GTK_WIDGET(panel));  
}
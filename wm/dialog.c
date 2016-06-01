/*
 * graphene-desktop
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
 // TODO: Implement content actor, highlighted button, and keyboard input
 
#include "dialog.h"
#include <glib.h>

struct _GrapheneWMDialog {
  ClutterActor parent;
  
  MetaScreen *screen;
  
  ClutterActor *backgroundGroup;
  ClutterActor *frameContainer;
  ClutterActor *frameBlurBg;
  ClutterActor *frame;
  ClutterActor *buttonBox;
  
  ClutterActor *content;
  gchar **buttons;
  gchar *highlighted;
  gboolean allowESC;
};

enum
{
  PROP_0,
  PROP_CONTENT,
  PROP_HIGHLIGHTED,
  PROP_ALLOW_ESC,
  PROP_LAST
};

enum
{
  SIGNAL_0,
  SIGNAL_CLOSE,
  SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

static void graphene_wm_dialog_dispose(GrapheneWMDialog *dialog);
static void graphene_wm_dialog_set_property(GrapheneWMDialog *self, guint propertyId, const GValue *value, GParamSpec *pspec);
static void graphene_wm_dialog_get_property(GrapheneWMDialog *self, guint propertyId, GValue *value, GParamSpec *pspec);
static void generate_background_group(GrapheneWMDialog *self, MetaScreen *screen);
static void generate_dialog(GrapheneWMDialog *self, MetaScreen *screen, gint monitorIndex);

G_DEFINE_TYPE(GrapheneWMDialog, graphene_wm_dialog, CLUTTER_TYPE_ACTOR);


GrapheneWMDialog * graphene_wm_dialog_new(ClutterActor *content, const gchar **buttons)
{
  GrapheneWMDialog *dialog = GRAPHENE_WM_DIALOG(g_object_new(GRAPHENE_TYPE_WM_DIALOG, "content", content, NULL));
  graphene_wm_dialog_set_buttons(dialog, buttons);
  return dialog;
}

static void graphene_wm_dialog_class_init(GrapheneWMDialogClass *klass)
{
  GObjectClass *objectClass = G_OBJECT_CLASS(klass);
  objectClass->dispose = graphene_wm_dialog_dispose;
  objectClass->set_property = graphene_wm_dialog_set_property;
  objectClass->get_property = graphene_wm_dialog_get_property;
  
  properties[PROP_CONTENT] =
    g_param_spec_object("content", "content", "content", CLUTTER_TYPE_ACTOR, G_PARAM_READWRITE);
  properties[PROP_HIGHLIGHTED] =
    g_param_spec_string("highlighted", "highlighted", "the highlighted button (should be used on enter key)", NULL, G_PARAM_READWRITE);
  properties[PROP_ALLOW_ESC] =
    g_param_spec_boolean("allow-esc", "allow esc", "if the dialog can be exited with the ESC key", TRUE, G_PARAM_READWRITE);
  
  g_object_class_install_properties(objectClass, PROP_LAST, properties);

  /*
   * Emitted when the dialog closes. The first parameter is the button pressed, or "_ESC_" or "_ENTER_" for
   * the escape and enter keys respectively. The "_ENTER_" response should usually map to the highlighted button.
   */ 
  signals[SIGNAL_CLOSE] = g_signal_new("close", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void graphene_wm_dialog_init(GrapheneWMDialog *self)
{
  self->backgroundGroup = clutter_actor_new();
  self->frameContainer = clutter_actor_new();
  self->frameBlurBg = clutter_actor_new();
  self->frame = clutter_actor_new();
  
  ClutterColor *frameColor = clutter_color_new(79, 88, 92, 255);
  clutter_actor_set_background_color(self->frame, frameColor);
  clutter_color_free(frameColor);
  
  ClutterLayoutManager *boxLayout = clutter_box_layout_new();
  clutter_box_layout_set_orientation(CLUTTER_BOX_LAYOUT(boxLayout), CLUTTER_ORIENTATION_HORIZONTAL);
  clutter_actor_set_layout_manager(self->frameContainer, boxLayout);
  
  clutter_actor_set_x_expand(self->frame, TRUE);
  clutter_actor_set_y_expand(self->frame, TRUE);
  clutter_actor_set_x_align(self->frame, CLUTTER_ACTOR_ALIGN_CENTER);
  clutter_actor_set_y_align(self->frame, CLUTTER_ACTOR_ALIGN_CENTER);
  clutter_actor_add_child(self->frameContainer, self->frame);
  
  clutter_actor_insert_child_below(self, self->backgroundGroup, NULL);
  // clutter_actor_insert_child_above(self, self->frameBlurBg, self->backgroundGroup);
  clutter_actor_insert_child_above(self, self->frameContainer, self->backgroundGroup);
  
  // ClutterEffect *blurEffect = clutter_blur_effect_new();
  // clutter_actor_add_effect(self->frameBlurBg, blurEffect);
  // g_object_unref(blurEffect);
}

static void graphene_wm_dialog_dispose(GrapheneWMDialog *self)
{
  // clutter_actor_free(self->frame);
  // clutter_actor_free(self->frameBlurBg);
  // clutter_actor_free(self->backgroundGroup);
  
  G_OBJECT_CLASS(graphene_wm_dialog_parent_class)->dispose(self);
}

static void graphene_wm_dialog_set_property(GrapheneWMDialog *self, guint propertyId, const GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GRAPHENE_IS_WM_DIALOG(self));
  switch(propertyId)
  {
    case PROP_CONTENT:
      g_clear_object(&self->content);
      self->content = g_object_ref_sink(g_value_get_object(value));
      break;
    case PROP_HIGHLIGHTED:
      g_clear_pointer(&self->highlighted, g_free);
      self->highlighted = g_strdup(g_value_get_string(value));
      break;
    case PROP_ALLOW_ESC:
      self->allowESC = g_value_get_boolean(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
      break;
  }
}

static void graphene_wm_dialog_get_property(GrapheneWMDialog *self, guint propertyId, GValue *value, GParamSpec *pspec)
{
  g_return_if_fail(GRAPHENE_IS_WM_DIALOG(self));
  
  switch(propertyId)
  {
    case PROP_CONTENT:
      g_value_set_object(value, g_object_ref(self->content));
      break;
    case PROP_HIGHLIGHTED:
      g_value_set_string(value, self->highlighted);
      break;
    case PROP_ALLOW_ESC:
      g_value_set_boolean(value, self->allowESC);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(self, propertyId, pspec);
      break;
  }
}

void graphene_wm_dialog_set_buttons(GrapheneWMDialog *self, const gchar **buttons)
{
  self->buttons = g_strdupv(buttons);
}

void graphene_wm_dialog_show(GrapheneWMDialog *self, MetaScreen *screen, gint monitorIndex)
{
  g_debug("show dialog");
  self->screen = screen;
  generate_background_group(self, screen);
  generate_dialog(self, screen, monitorIndex);
  
  ClutterActor *windowGroup = meta_get_window_group_for_screen(screen);

  clutter_actor_set_pivot_point(self->frame, 0.5, 0.5);
  clutter_actor_set_opacity(self->backgroundGroup, 0);
  clutter_actor_set_scale(self->frame, 0, 0);
  clutter_actor_insert_child_above(windowGroup, CLUTTER_ACTOR(self), NULL);
  
  clutter_actor_save_easing_state(self->backgroundGroup);
  clutter_actor_set_easing_mode(self->backgroundGroup, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(self->backgroundGroup, 200);
  clutter_actor_set_opacity(self->backgroundGroup, 255);
  clutter_actor_restore_easing_state(self->backgroundGroup);
  
  clutter_actor_save_easing_state(self->frame);
  clutter_actor_set_easing_mode(self->frame, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(self->frame, 200);
  clutter_actor_set_scale(self->frame, 1, 1);
  clutter_actor_restore_easing_state(self->frame);
}

static void generate_background_group(GrapheneWMDialog *self, MetaScreen *screen)
{
  clutter_actor_destroy_all_children(self->backgroundGroup);
  
  ClutterColor *bgColor = clutter_color_new(0, 0, 0, 140);

  gint numMonitors = meta_screen_get_n_monitors(screen);
  for(gint i=0;i<numMonitors;++i)
  {
    MetaRectangle rect = meta_rect(0,0,0,0);
    meta_screen_get_monitor_geometry(screen, i, &rect);
    
    ClutterActor *background = clutter_actor_new();
    clutter_actor_set_background_color(background, bgColor);
    clutter_actor_set_position(background, rect.x, rect.y);
    clutter_actor_set_size(background, rect.width, rect.height);
    
    clutter_actor_add_child(self->backgroundGroup, background);
  }
  
  clutter_color_free(bgColor);
}

static void close_complete(GrapheneWMDialog *self, ClutterActor *actor)
{
  clutter_actor_remove_all_transitions(actor);
  g_signal_handlers_disconnect_by_func(actor, close_complete, self);
  ClutterActor *windowGroup = meta_get_window_group_for_screen(self->screen);
  clutter_actor_remove_child(windowGroup, CLUTTER_ACTOR(self));
  self->screen = NULL;
}

static gboolean button_clicked(ClutterActor *button, ClutterButtonEvent *event, GrapheneWMDialog *self)
{
  g_signal_emit_by_name(self, "close", clutter_actor_get_name(button));
  
  clutter_actor_set_pivot_point(self->frame, 0.5, 0.5);
  clutter_actor_set_opacity(self->backgroundGroup, 255);
  clutter_actor_set_scale(self->frame, 1, 1);
  
  clutter_actor_save_easing_state(self->backgroundGroup);
  clutter_actor_set_easing_mode(self->backgroundGroup, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(self->backgroundGroup, 200);
  clutter_actor_set_opacity(self->backgroundGroup, 0);
  clutter_actor_restore_easing_state(self->backgroundGroup);
  
  g_signal_connect_swapped(self->frame, "transitions_completed", G_CALLBACK(close_complete), self);
  clutter_actor_save_easing_state(self->frame);
  clutter_actor_set_easing_mode(self->frame, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(self->frame, 200);
  clutter_actor_set_scale(self->frame, 0, 0);
  clutter_actor_restore_easing_state(self->frame);
  return TRUE;
}

static gboolean button_enter(ClutterActor *button, ClutterCrossingEvent *event, GrapheneWMDialog *self)
{
  ClutterActor *highlightColor = clutter_actor_get_first_child(button);
  clutter_actor_save_easing_state(highlightColor);
  clutter_actor_set_easing_mode(highlightColor, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(highlightColor, 300);
  clutter_actor_set_opacity(highlightColor, 255);
  clutter_actor_restore_easing_state(highlightColor);
  return TRUE;
}

static gboolean button_leave(ClutterActor *button, ClutterCrossingEvent *event, GrapheneWMDialog *self)
{
  ClutterActor *highlightColor = clutter_actor_get_first_child(button);
  clutter_actor_save_easing_state(highlightColor);
  clutter_actor_set_easing_mode(highlightColor, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(highlightColor, 300);
  clutter_actor_set_opacity(highlightColor, 0);
  clutter_actor_restore_easing_state(highlightColor);
  return TRUE;
}

static void generate_dialog(GrapheneWMDialog *self, MetaScreen *screen, gint monitorIndex)
{
  MetaRectangle rect = meta_rect(0,0,0,0);
  meta_screen_get_monitor_geometry(screen, monitorIndex, &rect);
  clutter_actor_set_position(self->frameContainer, rect.x, rect.y);
  clutter_actor_set_size(self->frameContainer, rect.width, rect.height);
  
  ClutterLayoutManager *boxLayout = clutter_box_layout_new();
  clutter_box_layout_set_orientation(CLUTTER_BOX_LAYOUT(boxLayout), CLUTTER_ORIENTATION_HORIZONTAL);
  clutter_actor_set_layout_manager(self->frame, boxLayout);
  
  clutter_actor_destroy_all_children(self->frame);
  
  guint numButtons = g_strv_length(self->buttons);
  for(guint i=0;i<numButtons;++i)
  {
    ClutterActor *button = clutter_actor_new();
    clutter_actor_set_name(button, self->buttons[i]);
    clutter_actor_set_height(button, 40);
    clutter_actor_set_layout_manager(button, clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_FILL, CLUTTER_BIN_ALIGNMENT_FILL));
    clutter_actor_set_reactive(button, TRUE);
    g_signal_connect(button, "button-press-event", button_clicked, self);
    g_signal_connect(button, "enter-event", button_enter, self);
    g_signal_connect(button, "leave-event", button_leave, self);

    ClutterActor *highlightColor = clutter_actor_new(); // Because animating background-color doesnt work very well
    clutter_actor_set_opacity(highlightColor, 0);
    clutter_actor_set_x_expand(highlightColor, TRUE);
    clutter_actor_set_y_expand(highlightColor, TRUE);
    ClutterColor *color = clutter_color_new(110, 124, 130, 255);
    clutter_actor_set_background_color(highlightColor, color);
    clutter_color_free(color);
    
    ClutterText *text = clutter_text_new_with_text(NULL, self->buttons[i]);
    clutter_text_set_use_markup(text, FALSE);
    clutter_text_set_selectable(text, FALSE);
    clutter_text_set_line_wrap(text, FALSE);
    clutter_text_set_ellipsize(text, PANGO_ELLIPSIZE_NONE);
    color = clutter_color_new(219, 222, 224, 204);
    clutter_text_set_color(text, color);
    clutter_color_free(color);
    clutter_actor_set_margin_left(CLUTTER_ACTOR(text), 15);
    clutter_actor_set_margin_right(CLUTTER_ACTOR(text), 15);
    clutter_actor_set_y_align(CLUTTER_ACTOR(text), CLUTTER_ACTOR_ALIGN_CENTER);
    
    clutter_actor_add_child(button, highlightColor); // Must be first child
    clutter_actor_add_child(button, text);
    clutter_actor_add_child(self->frame, button);
  }
}
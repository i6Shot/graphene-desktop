/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#include "dialog.h"
#include "cmk/shadow.h"
#include <glib.h>
#include <math.h>

struct _GrapheneDialog
{
	CmkWidget parent;
	
	ClutterActor *content;
	ClutterActor *buttonBox;
	GList *buttons; // List of CmkButton actors
	CmkButton *highlighted; // This button should also appear in the 'buttons' list
	gboolean allowEsc;
};

enum
{
	PROP_CONTENT = 1,
	PROP_ALLOW_ESC,
	PROP_LAST
};

enum
{
	SIGNAL_SELECT = 1,
	SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

static void graphene_dialog_set_property(GObject *self_, guint propertyId, const GValue *value, GParamSpec *pspec);
static void graphene_dialog_get_property(GObject *self_, guint propertyId, GValue *value, GParamSpec *pspec);
static void on_style_changed(CmkWidget *self_, CmkStyle *style);
static void on_size_changed(ClutterActor *self, GParamSpec *spec, ClutterCanvas *canvas);
static gboolean on_draw_canvas(ClutterCanvas *canvas, cairo_t *cr, int width, int height, GrapheneDialog *self);

G_DEFINE_TYPE(GrapheneDialog, graphene_dialog, CMK_TYPE_WIDGET);



GrapheneDialog * graphene_dialog_new()
{
	return GRAPHENE_DIALOG(g_object_new(GRAPHENE_TYPE_DIALOG, NULL));
}

GrapheneDialog * graphene_dialog_new_simple(const gchar *message, const gchar *icon, ...)
{
	GrapheneDialog *dialog = graphene_dialog_new();
	graphene_dialog_set_message(dialog, message);
	//graphene_dialog_set_icon(dialog, message);
	
	va_list args;
	va_start(args, icon);
	guint numButtons = 0;
	while(va_arg(args, const gchar *) != NULL)
		numButtons ++;
	va_end(args);
	
	const gchar **buttons = g_new(const gchar *, numButtons+1);
	buttons[numButtons] = NULL;
	
	va_start(args, icon);
	for(guint i=0;i<numButtons;++i)
	{
		const gchar *name = va_arg(args, const gchar *);
		buttons[i] = name;
	}
	va_end(args);

	graphene_dialog_set_buttons(dialog, buttons);
	g_free(buttons);
	return dialog;
}

static void graphene_dialog_get_preferred_width(ClutterActor *self_, gfloat forHeight, gfloat *minWidth, gfloat *natWidth)
{
	clutter_layout_manager_get_preferred_width(clutter_actor_get_layout_manager(self_), CLUTTER_CONTAINER(self_), forHeight, minWidth, natWidth);

	// TODO: Adjustable
	*minWidth = CLAMP(*minWidth, 200, 700);
	*natWidth = CLAMP(*natWidth, 200, 700);	

	// Make sure all the buttons have room
	gfloat minBBWidth, natBBWidth;
	clutter_actor_get_preferred_width(GRAPHENE_DIALOG(self_)->buttonBox, -1, &minBBWidth, &natBBWidth);
	if(minBBWidth > *minWidth)
	{
		*minWidth = minBBWidth;
		*natWidth = minBBWidth;
	}
}

static void graphene_dialog_get_preferred_height(ClutterActor *self_, gfloat forWidth, gfloat *minHeight, gfloat *natHeight)
{
	clutter_layout_manager_get_preferred_height(clutter_actor_get_layout_manager(self_), CLUTTER_CONTAINER(self_), forWidth, minHeight, natHeight);

	*natHeight = CLAMP(*minHeight, 100, 700);
	*natHeight = CLAMP(*natHeight, 100, 700);
}

static void graphene_dialog_class_init(GrapheneDialogClass *class)
{
	CLUTTER_ACTOR_CLASS(class)->get_preferred_width = graphene_dialog_get_preferred_width;
	CLUTTER_ACTOR_CLASS(class)->get_preferred_height = graphene_dialog_get_preferred_height;
	
	CMK_WIDGET_CLASS(class)->style_changed = on_style_changed;

	signals[SIGNAL_SELECT] = g_signal_new("select", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void graphene_dialog_init(GrapheneDialog *self)
{	
	clutter_actor_set_reactive(CLUTTER_ACTOR(self), TRUE);

	ClutterContent *canvas = clutter_canvas_new();
	g_signal_connect(canvas, "draw", G_CALLBACK(on_draw_canvas), self);
	clutter_actor_set_content(CLUTTER_ACTOR(self), canvas);

	ClutterLayoutManager *layout = clutter_box_layout_new();
	clutter_box_layout_set_orientation(CLUTTER_BOX_LAYOUT(layout), CLUTTER_ORIENTATION_VERTICAL);
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self), layout);

	self->content = clutter_actor_new();
	clutter_actor_set_layout_manager(self->content, clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_START, CLUTTER_BIN_ALIGNMENT_START));
	clutter_actor_set_x_expand(self->content, TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self), self->content);

	self->buttonBox = clutter_actor_new();
	ClutterLayoutManager *buttonLayout = clutter_box_layout_new();
	clutter_box_layout_set_orientation(CLUTTER_BOX_LAYOUT(buttonLayout), CLUTTER_ORIENTATION_HORIZONTAL);
	clutter_actor_set_layout_manager(self->buttonBox, buttonLayout);
	clutter_actor_set_x_expand(self->buttonBox, TRUE);
	clutter_actor_set_x_align(self->buttonBox, CLUTTER_ACTOR_ALIGN_END);
	clutter_actor_add_child(CLUTTER_ACTOR(self), self->buttonBox);

	cmk_widget_set_background_color(CMK_WIDGET(self), "background");

	g_signal_connect(CLUTTER_ACTOR(self), "notify::size", G_CALLBACK(on_size_changed), canvas);
}

static void on_style_changed(CmkWidget *self_, CmkStyle *style)
{
	clutter_content_invalidate(clutter_actor_get_content(CLUTTER_ACTOR(self_)));
	float padding = cmk_style_get_padding(style);
	ClutterMargin margin = {padding, padding, padding, padding};
	ClutterMargin margin2 = {padding*2, padding*2, padding*2, padding*2};
	clutter_actor_set_margin(GRAPHENE_DIALOG(self_)->content, &margin2);
	clutter_actor_set_margin(GRAPHENE_DIALOG(self_)->buttonBox, &margin);
	
	ClutterActor *content = clutter_actor_get_first_child(GRAPHENE_DIALOG(self_)->content);
	if(CLUTTER_IS_TEXT(content))
	{
		CmkColor color;
		cmk_style_get_font_color_for_background(style, cmk_widget_get_background_color(self_), &color);
		ClutterColor cc = cmk_to_clutter_color(&color);
		clutter_text_set_color(CLUTTER_TEXT(content), &cc);
	}

	CMK_WIDGET_CLASS(graphene_dialog_parent_class)->style_changed(self_, style);
}

static void on_size_changed(ClutterActor *self, GParamSpec *spec, ClutterCanvas *canvas)
{
	gfloat width, height;
	clutter_actor_get_size(self, &width, &height);
	clutter_canvas_set_size(CLUTTER_CANVAS(canvas), width, height);
}

static gboolean on_draw_canvas(ClutterCanvas *canvas, cairo_t *cr, int width, int height, GrapheneDialog *self)
{
	CmkStyle *style = cmk_widget_get_actual_style(CMK_WIDGET(self));
	double radius = cmk_style_get_bevel_radius(style);
	double degrees = M_PI / 180.0;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	cairo_new_sub_path(cr);
	cairo_arc(cr, width - radius, radius, radius, -90 * degrees, 0 * degrees);
	cairo_arc(cr, width - radius, height - radius, radius, 0 * degrees, 90 * degrees);
	cairo_arc(cr, radius, height - radius, radius, 90 * degrees, 180 * degrees);
	cairo_arc(cr, radius, radius, radius, 180 * degrees, 270 * degrees);
	cairo_close_path(cr);

	cairo_set_source_cmk_color(cr, cmk_style_get_color(style, cmk_widget_get_background_color(CMK_WIDGET(self))));
	cairo_fill(cr);
	return TRUE;
}

static void on_button_activate(GrapheneDialog *self, CmkButton *button)
{
	g_signal_emit(self, signals[SIGNAL_SELECT], 0, cmk_button_get_name(button));
}

void graphene_dialog_set_message(GrapheneDialog *self, const gchar *message)
{
	g_return_if_fail(GRAPHENE_IS_DIALOG(self));
	
	ClutterText *content = CLUTTER_TEXT(clutter_text_new());
	
	CmkColor color;
	cmk_style_get_font_color_for_background(cmk_widget_get_actual_style(CMK_WIDGET(self)), cmk_widget_get_background_color(CMK_WIDGET(self)), &color);
	ClutterColor cc = cmk_to_clutter_color(&color);
	clutter_text_set_color(content, &cc);
	
	clutter_text_set_text(content, message);
	clutter_text_set_line_wrap(content, TRUE);
	clutter_actor_set_x_align(CLUTTER_ACTOR(content), CLUTTER_ACTOR_ALIGN_START);
	clutter_actor_destroy_all_children(self->content);
	clutter_actor_add_child(self->content, CLUTTER_ACTOR(content));
}

void graphene_dialog_set_buttons(GrapheneDialog *self, const gchar * const *buttons)
{
	const gchar *name = NULL;
	guint i=0;
	while((name = buttons[i++]) != NULL)
	{
		CmkButton *button = cmk_beveled_button_new_with_text(name);
		cmk_widget_set_style_parent(CMK_WIDGET(button), CMK_WIDGET(self));
		g_signal_connect_swapped(button, "activate", G_CALLBACK(on_button_activate), self);
		clutter_actor_add_child(self->buttonBox, CLUTTER_ACTOR(button));
	}
}

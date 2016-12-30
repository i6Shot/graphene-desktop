/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "style.h"

struct _CMKStyle
{
	ClutterActor parent;
	GHashTable *colors;
	float bevelRadius;
	float padding;
};

static void cmk_style_dispose(GObject *self_);
static void free_table_color(gpointer color);

G_DEFINE_TYPE(CMKStyle, cmk_style, G_TYPE_OBJECT);



CMKStyle * cmk_style_new()
{
	return CMK_STYLE(g_object_new(CMK_TYPE_STYLE, NULL));
}


CMKStyle * cmk_style_get_default()
{
	static CMKStyle *globalStyle = NULL;
	if(globalStyle)
		return g_object_ref(globalStyle);
	globalStyle = cmk_style_new();
	return globalStyle;
}

static void cmk_style_class_init(CMKStyleClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = cmk_style_dispose;
}

static void cmk_style_init(CMKStyle *self)
{
	self->colors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_table_color);
	
	// Default colors
	CMKColor color;
	cmk_style_set_color(self, "primary", cmk_set_color(&color, 1, 1, 1, 1));
	cmk_style_set_color(self, "secondary", cmk_set_color(&color, 1, 1, 1, 1));
	cmk_style_set_color(self, "accent", cmk_set_color(&color, 0.5, 0, 0, 1));
	cmk_style_set_color(self, "hover", cmk_set_color(&color, 0, 0, 0, 0.1));
	cmk_style_set_color(self, "activate", cmk_set_color(&color, 0, 0, 0, 0.1));
	self->bevelRadius = 6.0;
	self->padding = 10;
}

static void cmk_style_dispose(GObject *self_)
{
	CMKStyle *self = CMK_STYLE(self_);
	g_clear_pointer(&self->colors, g_hash_table_unref);
	G_OBJECT_CLASS(cmk_style_parent_class)->dispose(self_);
}

static void free_table_color(gpointer color)
{
	g_slice_free(CMKColor, color);
}

CMKColor * cmk_copy_color(CMKColor *dest, const CMKColor *source)
{
	g_return_val_if_fail(dest, dest);
	g_return_val_if_fail(source, dest);
	dest->r = source->r;
	dest->g = source->g;
	dest->b = source->b;
	dest->a = source->a;
	return dest;
}

CMKColor * cmk_set_color(CMKColor *dest, float r, float g, float b, float a)
{
	g_return_val_if_fail(dest, dest);
	dest->r = r;
	dest->g = g;
	dest->b = b;
	dest->a = a;
	return dest;
}

ClutterColor cmk_to_clutter_color(const CMKColor * color)
{
	ClutterColor cc;
	clutter_color_init(&cc, 0, 0, 0, 0);
	g_return_val_if_fail(color, cc);

	clutter_color_init(&cc, color->r, color->g, color->b, color->a);
	return cc;
}

void cairo_set_source_cmk_color(cairo_t *cr, const CMKColor *color)
{
	g_return_if_fail(color);
	cairo_set_source_rgba(cr, color->r, color->g, color->b, color->a);
}

CMKColor * cmk_overlay_colors(CMKColor *dest, const CMKColor *a, const CMKColor *b)
{
	// TODO
	return dest;
}

const CMKColor * cmk_style_get_color(CMKStyle *self, const gchar *name)
{
	g_return_val_if_fail(CMK_IS_STYLE(self), NULL);
	return g_hash_table_lookup(self->colors, name);
}

void cmk_style_set_color(CMKStyle *self, const gchar *name, const CMKColor *color)
{
	g_return_if_fail(CMK_IS_STYLE(self));

	CMKColor *c = g_slice_new(CMKColor);
	cmk_copy_color(c, color);
	g_hash_table_insert(self->colors, g_strdup(name), c);
}

void cmk_style_set_bevel_radius(CMKStyle *self, float radius)
{
	g_return_if_fail(CMK_IS_STYLE(self));
	self->bevelRadius = radius;
}

float cmk_style_get_bevel_radius(CMKStyle *self)
{
	g_return_val_if_fail(CMK_IS_STYLE(self), 0.0);
	return self->bevelRadius;
}

void cmk_style_set_padding(CMKStyle *self, float padding)
{
	g_return_if_fail(CMK_IS_STYLE(self));
	self->padding = padding;
}

float cmk_style_get_padding(CMKStyle *self)
{
	g_return_val_if_fail(CMK_IS_STYLE(self), 0.0);
	return self->padding;
}

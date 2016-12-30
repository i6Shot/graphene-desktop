/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */

#include "shadow.h"
#include "style.h"
#include <math.h>

struct _CMKShadow
{
	ClutterActor parent;
	ClutterActor *shadow;
	ClutterCanvas *canvas;
	gfloat vRadius, hRadius;
};

static void cmk_shadow_dispose(GObject *self_);
static void on_size_changed(CMKShadow *self, GParamSpec *spec, gpointer userdata);
static gboolean on_draw_canvas(ClutterCanvas *canvas, cairo_t *cr, int width, int height, CMKShadow *self);

G_DEFINE_TYPE(CMKShadow, cmk_shadow, CLUTTER_TYPE_ACTOR);



CMKShadow * cmk_shadow_new()
{
	return CMK_SHADOW(g_object_new(CMK_TYPE_SHADOW, NULL));
}

static void cmk_shadow_class_init(CMKShadowClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	base->dispose = cmk_shadow_dispose;
}

static void cmk_shadow_init(CMKShadow *self)
{
	//ClutterColor c = {255,0,0,255};
	//clutter_actor_set_background_color(CLUTTER_ACTOR(self), &c);	

	self->canvas = CLUTTER_CANVAS(clutter_canvas_new());
	g_signal_connect(self->canvas, "draw", G_CALLBACK(on_draw_canvas), self);

	self->shadow = clutter_actor_new();
	clutter_actor_set_content_gravity(self->shadow, CLUTTER_CONTENT_GRAVITY_CENTER);
	clutter_actor_set_content(self->shadow, CLUTTER_CONTENT(self->canvas));

	g_signal_connect(self, "notify::size", G_CALLBACK(on_size_changed), NULL);
	clutter_actor_add_child(CLUTTER_ACTOR(self), self->shadow);
}

static void cmk_shadow_dispose(GObject *self_)
{
	CMKShadow *self = CMK_SHADOW(self_);
	G_OBJECT_CLASS(cmk_shadow_parent_class)->dispose(self_);
}

static void on_size_changed(CMKShadow *self, GParamSpec *spec, gpointer userdata)
{
	gfloat width, height;
	clutter_actor_get_size(CLUTTER_ACTOR(self), &width, &height);
	
	gint canvasWidth = width + self->hRadius*2;
	gint canvasHeight = height + self->vRadius*2;

	clutter_actor_set_position(self->shadow, -self->hRadius, -self->vRadius);
	clutter_actor_set_size(self->shadow, canvasWidth, canvasHeight);
	clutter_canvas_set_size(self->canvas, canvasWidth, canvasHeight);
}

//void boxesForGauss(float sigma, float *sizes, guint n)
//{
//	float wIdeal = sqrt((12*sigma*sigma/n)+1);  // Ideal averaging filter width 
//	guint wl = floor(wIdeal);
//	if(wl%2==0)
//		wl--;
//	guint wu = wl+2;
//
//	float mIdeal = (12*sigma*sigma - n*wl*wl - 4*n*wl - 3*n)/(-4*wl - 4);
//	float m = round(mIdeal);
//
//	for(guint i=0;i<n;++i)
//		sizes[i] = i<m?wl:wu;
//}

// http://blog.ivank.net/fastest-gaussian-blur.html
void boxBlurH_4(guchar *scl, guchar *tcl, guint w, guint h, guint r)
{
    float iarr = 1.0 / (r+r+1.0);
    for(guint i=0; i<h; i++) {
        guint ti = i*w, li = ti, ri = ti+r;
        guint fv = scl[ti], lv = scl[ti+w-1], val = (r+1)*fv;
        for(guint j=0; j<r; j++) val += scl[ti+j];
        for(guint j=0  ; j<=r ; j++) { val += scl[ri++] - fv       ;   tcl[ti++] = round(val*iarr); }
        for(guint j=r+1; j<w-r; j++) { val += scl[ri++] - scl[li++];   tcl[ti++] = round(val*iarr); }
        for(guint j=w-r; j<w  ; j++) { val += lv        - scl[li++];   tcl[ti++] = round(val*iarr); }
    }
}

void boxBlurT_4(guchar *scl, guchar *tcl, guint w, guint h, guint r)
{
    float iarr = 1.0 / (r+r+1.0);
    for(guint i=0; i<w; i++) {
        guint ti = i, li = ti, ri = ti+r*w;
        guint fv = scl[ti], lv = scl[ti+w*(h-1)], val = (r+1)*fv;
        for(guint j=0; j<r; j++) val += scl[ti+j*w];
        for(guint j=0  ; j<=r ; j++) { val += scl[ri] - fv     ;  tcl[ti] = round(val*iarr);  ri+=w; ti+=w; }
        for(guint j=r+1; j<h-r; j++) { val += scl[ri] - scl[li];  tcl[ti] = round(val*iarr);  li+=w; ri+=w; ti+=w; }
        for(guint j=h-r; j<h  ; j++) { val += lv - scl[li]; tcl[ti] = round(val*iarr); li+=w; ti+=w; }
    }
}

//static unsigned long getms()
//{
//	struct timeval tv;
//	gettimeofday(&tv, NULL);
//	return 1000000 * tv.tv_sec + tv.tv_usec;
//}

static gboolean on_draw_canvas(ClutterCanvas *canvas, cairo_t *cr, gint width, gint height, CMKShadow *self)
{
	//unsigned long start = getms();
	cairo_surface_t *surface = cairo_get_target(cr);

	if(cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32)
	{
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_restore(cr);
		return TRUE;
	}

	guchar *buffer = cairo_image_surface_get_data(surface);

	guint length = width*height;
	guchar *source = g_new0(guchar, length*2);
	guchar *dest = source + length;

	// Create a black rectangle that matches the area of the parent actor
	for(guint x=self->hRadius;x<width-self->hRadius;++x)
		for(guint y=self->vRadius;y<height-self->vRadius;++y)
			source[y*width+x] = 255;

	// Do a gaussian blur on the rectangle
	const guint passes = 2;
	for(guint i=0;i<passes;++i)
	{
		guchar *sc = i%2 ? dest:source;
		guchar *dt = i%2 ? source:dest;
		for(guint j=0; j<length; j++)
			dt[j] = sc[j];
		boxBlurH_4(dt, sc, width, height, self->hRadius/2);
		boxBlurT_4(sc, dt, width, height, self->vRadius/2);
	}
	
	// Copy result to cairo
	for(guint i=0;i<length;i++)
	{
		buffer[i*4+0]=0;
		buffer[i*4+1]=0;
		buffer[i*4+2]=0;
		buffer[i*4+3]=source[i];
	}

	g_free(source);
	//unsigned long end = getms();
	//unsigned long delta = end - start;
	//double deltams = delta / 1000.0;
	//printf("shadowtime: %f\n", deltams);
	return TRUE;
}

void cmk_shadow_set_blur(CMKShadow *self, gfloat radius)
{
	g_return_if_fail(CMK_IS_SHADOW(self));
	self->hRadius = radius;
	self->vRadius = radius;
	on_size_changed(self, NULL, NULL);
}


void cmk_shadow_set_vblur(CMKShadow *self, gfloat radius)
{
	g_return_if_fail(CMK_IS_SHADOW(self));
	self->vRadius = radius;
	on_size_changed(self, NULL, NULL);
}

void cmk_shadow_set_hblur(CMKShadow *self, gfloat radius)
{
	g_return_if_fail(CMK_IS_SHADOW(self));
	self->hRadius = radius;
	on_size_changed(self, NULL, NULL);
}

gfloat cmk_shadow_get_vblur(CMKShadow *self)
{
	g_return_val_if_fail(CMK_IS_SHADOW(self), 0);
	return self->vRadius;
}

gfloat cmk_shadow_get_hblur(CMKShadow *self)
{
	g_return_val_if_fail(CMK_IS_SHADOW(self), 0);
	return self->hRadius;
}

/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#include "panel.h"
#include "cmk/shadow.h"
#include "applets/clock.h"

#define PANEL_HEIGHT 32 // Pixels; multiplied by the window scale factor

struct _GraphenePanel
{
	CmkWidget parent;

	CPanelModalCallback modalCb;
	gpointer cbUserdata;

	CmkWidget *bar;
	CmkWidget *popup;
};

//static void graphene_panel_dispose(GObject *self_);
static void on_style_changed(CmkWidget *self_, CmkStyle *style);
//static void on_size_changed(ClutterActor *self, GParamSpec *spec, ClutterCanvas *canvas);
static void graphene_panel_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);

G_DEFINE_TYPE(GraphenePanel, graphene_panel, CMK_TYPE_WIDGET);



GraphenePanel * graphene_panel_new(CPanelModalCallback modalCb, gpointer userdata)
{
	GraphenePanel *panel = GRAPHENE_PANEL(g_object_new(GRAPHENE_TYPE_PANEL, NULL));
	if(panel)
	{
		panel->modalCb = modalCb;
		panel->cbUserdata = userdata;
	}
	return panel;
}

static void graphene_panel_class_init(GraphenePanelClass *class)
{
	GObjectClass *base = G_OBJECT_CLASS(class);
	//base->dispose = graphene_panel_dispose;
	
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_panel_allocate;
	
	CMK_WIDGET_CLASS(class)->style_changed = on_style_changed;
}

static void graphene_panel_init(GraphenePanel *self)
{
	self->bar = cmk_widget_new();
	clutter_actor_set_reactive(CLUTTER_ACTOR(self->bar), TRUE);
	cmk_widget_set_draw_background_color(self->bar, TRUE);
	cmk_widget_set_background_color(self->bar, "background");
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->bar));

	GrapheneClockApplet *clock = graphene_clock_applet_new();
	cmk_widget_set_style_parent(clock, self->bar);
	clutter_actor_add_child(self->bar, clock);
}	

static void graphene_panel_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GraphenePanel *self = GRAPHENE_PANEL(self_);
	
	ClutterActorBox barBox = {box->x1, box->y2-PANEL_HEIGHT, box->x2, box->y2};
	clutter_actor_allocate(CLUTTER_ACTOR(self->bar), &barBox, flags);

	CLUTTER_ACTOR_CLASS(graphene_panel_parent_class)->allocate(self_, box, flags);
}

static void on_style_changed(CmkWidget *self_, CmkStyle *style)
{
	GraphenePanel *panel = GRAPHENE_PANEL(self_);
	CMK_WIDGET_CLASS(graphene_panel_parent_class)->style_changed(self_, style);
}

ClutterActor * graphene_panel_get_input_actor(GraphenePanel *self)
{
	return CLUTTER_ACTOR(self->bar);
}

GraphenePanelSide graphene_panel_get_side(GraphenePanel *panel)
{
	return GRAPHENE_PANEL_SIDE_BOTTOM;
}

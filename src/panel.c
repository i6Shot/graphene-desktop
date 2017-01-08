/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#include "panel.h"
#include "cmk/shadow.h"
#include "cmk/button.h"
#include "applets/clock.h"
#include "applets/launcher.h"

#define PANEL_HEIGHT 64 // Pixels; multiplied by the window scale factor
#define SHADOW_HEIGHT 20

struct _GraphenePanel
{
	CmkWidget parent;

	CPanelModalCallback modalCb;
	gpointer cbUserdata;

	CmkShadowContainer *sdc;
	CmkWidget *bar;
	CmkWidget *clock;
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

	clutter_actor_set_layout_manager(self->bar, clutter_box_layout_new());

	self->sdc = cmk_shadow_container_new();
	cmk_shadow_container_set_vblur(self->sdc, SHADOW_HEIGHT);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->sdc));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->bar));

	// Sample launcher + tasklist
	GrapheneLauncherApplet *launcher = graphene_launcher_applet_new();
	clutter_actor_add_child(self->bar, CLUTTER_ACTOR(launcher));

	ClutterActor *tasklist = clutter_actor_new();
	clutter_actor_set_x_expand(tasklist, TRUE);
	clutter_actor_add_child(self->bar, tasklist);

	GrapheneClockApplet *clock = graphene_clock_applet_new();
	self->clock = clock;
	clutter_actor_add_child(self->bar, clock);
}

static void graphene_panel_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GraphenePanel *self = GRAPHENE_PANEL(self_);
	
	ClutterActorBox barBox = {box->x1, box->y2-PANEL_HEIGHT, box->x2, box->y2};
	ClutterActorBox sdcBox = {box->x1, box->y2-PANEL_HEIGHT-SHADOW_HEIGHT, box->x2, box->y2};

	clutter_actor_allocate(CLUTTER_ACTOR(self->sdc), &sdcBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->bar), &barBox, flags);

	CLUTTER_ACTOR_CLASS(graphene_panel_parent_class)->allocate(self_, box, flags);
}

static void on_style_changed(CmkWidget *self_, CmkStyle *style)
{
	GraphenePanel *panel = GRAPHENE_PANEL(self_);

	float padding = cmk_style_get_padding(style);
	ClutterMargin margin = {padding, padding, 0, 0};
	clutter_actor_set_margin(GRAPHENE_PANEL(self_)->clock, &margin);

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

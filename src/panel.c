/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#include "panel.h"
#include "panel-internal.h"
#include "cmk/shadow.h"
#include "cmk/button.h"
#include "cmk/cmk-icon.h"

#define PANEL_HEIGHT 64 // Pixels; multiplied by the window scale factor
#define SHADOW_HEIGHT 20

// See wm.c
#define TRANSITION_MEMLEAK_FIX(actor, tname) g_signal_connect_after(clutter_actor_get_transition(CLUTTER_ACTOR(actor), (tname)), "stopped", G_CALLBACK(g_object_unref), NULL)

struct _GraphenePanel
{
	CmkWidget parent;

	CPanelModalCallback modalCb;
	gpointer cbUserdata;

	// These are owned by Clutter, not refed
	CmkShadowContainer *sdc;
	CmkWidget *bar;
	CmkButton *launcher;
	GrapheneClockLabel *clock;
	CmkWidget *popup;

	CmkWidget *tasklist;
	GHashTable *windows; // GrapheneWindow * (not owned) to CmkWidget * (not refed)
};

static void graphene_panel_dispose(GObject *self_);
static void on_style_changed(CmkWidget *self_);
static void graphene_panel_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags);
static void on_launcher_button_activate(CmkButton *button, GraphenePanel *self);

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
	G_OBJECT_CLASS(class)->dispose = graphene_panel_dispose;
	CLUTTER_ACTOR_CLASS(class)->allocate = graphene_panel_allocate;
	CMK_WIDGET_CLASS(class)->style_changed = on_style_changed;
}

static void graphene_panel_init(GraphenePanel *self)
{
	self->bar = cmk_widget_new();
	clutter_actor_set_reactive(CLUTTER_ACTOR(self->bar), TRUE);
	cmk_widget_set_draw_background_color(self->bar, TRUE);
	cmk_widget_set_background_color_name(self->bar, "background");

	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->bar), clutter_box_layout_new());

	self->sdc = cmk_shadow_container_new();
	cmk_shadow_container_set_vblur(self->sdc, SHADOW_HEIGHT);
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->sdc));
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->bar));

	// Keep popup shadows from spilling onto other monitors
	clutter_actor_set_clip_to_allocation(CLUTTER_ACTOR(self), TRUE);

	// Launcher
	self->launcher = cmk_button_new();
	CmkIcon *launcherIcon = cmk_icon_new_full("open-menu-symbolic", "Adwaita", PANEL_HEIGHT);
	cmk_button_set_content(self->launcher, CMK_WIDGET(launcherIcon));
	g_signal_connect(self->launcher, "activate", G_CALLBACK(on_launcher_button_activate), self);
	clutter_actor_add_child(CLUTTER_ACTOR(self->bar), CLUTTER_ACTOR(self->launcher));

	// Tasklist
	self->windows = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)clutter_actor_destroy);
	self->tasklist = cmk_widget_new();
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self->tasklist), clutter_box_layout_new());
	clutter_actor_set_x_expand(CLUTTER_ACTOR(self->tasklist), TRUE);
	clutter_actor_add_child(CLUTTER_ACTOR(self->bar), CLUTTER_ACTOR(self->tasklist));

	// Clock
	self->clock = graphene_clock_label_new();
	clutter_actor_add_child(CLUTTER_ACTOR(self->bar), CLUTTER_ACTOR(self->clock));
}

static void graphene_panel_dispose(GObject *self_)
{
	GraphenePanel *self = GRAPHENE_PANEL(self_);
	g_hash_table_unref(self->windows);
	G_OBJECT_CLASS(graphene_panel_parent_class)->dispose(self_);
}

static void graphene_panel_allocate(ClutterActor *self_, const ClutterActorBox *box, ClutterAllocationFlags flags)
{
	GraphenePanel *self = GRAPHENE_PANEL(self_);
	
	ClutterActorBox barBox = {box->x1, box->y2-PANEL_HEIGHT, box->x2, box->y2};
	ClutterActorBox sdcBox = {box->x1, box->y2-PANEL_HEIGHT-SHADOW_HEIGHT, box->x2, box->y2};
	ClutterActorBox popupBox = {box->x1, box->y1, box->x2, box->y2 - PANEL_HEIGHT};

	clutter_actor_allocate(CLUTTER_ACTOR(self->sdc), &sdcBox, flags);
	clutter_actor_allocate(CLUTTER_ACTOR(self->bar), &barBox, flags);

	if(self->popup)
		clutter_actor_allocate(CLUTTER_ACTOR(self->popup), &popupBox, flags);

	CLUTTER_ACTOR_CLASS(graphene_panel_parent_class)->allocate(self_, box, flags);
}

static void on_style_changed(CmkWidget *self_)
{
	GraphenePanel *panel = GRAPHENE_PANEL(self_);

	float padding = cmk_widget_style_get_padding(self_);
	ClutterMargin margin = {padding, padding, 0, 0};
	clutter_actor_set_margin(CLUTTER_ACTOR(GRAPHENE_PANEL(self_)->clock), &margin);

	cmk_widget_style_set_padding(CMK_WIDGET(GRAPHENE_PANEL(self_)->launcher), cmk_widget_style_get_padding(self_) * 1.3);

	CMK_WIDGET_CLASS(graphene_panel_parent_class)->style_changed(self_);
}

ClutterActor * graphene_panel_get_input_actor(GraphenePanel *self)
{
	return CLUTTER_ACTOR(self->bar);
}

GraphenePanelSide graphene_panel_get_side(GraphenePanel *panel)
{
	return GRAPHENE_PANEL_SIDE_BOTTOM;
}

static void on_popup_destroy(CmkWidget *popup, GraphenePanel *self)
{
	self->popup = NULL;
	if(self->modalCb)
		self->modalCb(FALSE, self->cbUserdata);
}

static void close_popup(GraphenePanel *self)
{
	if(self->popup)
		clutter_actor_destroy(CLUTTER_ACTOR(self->popup));
}

static void on_launcher_button_activate(CmkButton *button, GraphenePanel *self)
{
	if(self->popup)
	{
		close_popup(self);
		return;
	}

	if(self->modalCb)
		self->modalCb(TRUE, self->cbUserdata);
	self->popup = CMK_WIDGET(graphene_launcher_popup_new());
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->popup));
	g_signal_connect(self->popup, "destroy", G_CALLBACK(on_popup_destroy), self);
}



/*
 * Tasklist
 */

static void on_tasklist_button_activate(CmkButton *button, GraphenePanel *self)
{
	GrapheneWindow *window = NULL;
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, self->windows);
	while(g_hash_table_iter_next(&iter, &key, &value))
	{
		if(value == button)
		{
			window = key;
			break;
		}
	}
	
	if(!window)
		return;

	if((window->flags & GRAPHENE_WINDOW_FLAG_MINIMIZED) || !(window->flags & GRAPHENE_WINDOW_FLAG_FOCUSED))
		window->show(window);
	else
		window->minimize(window);
}

static void on_tasklist_button_allocation_changed(CmkButton *button, ClutterActorBox *box, ClutterAllocationFlags flags, GrapheneWindow *window)
{
	gfloat x, y, width, height;
	clutter_actor_get_transformed_position(CLUTTER_ACTOR(button), &x, &y);
	clutter_actor_get_transformed_size(CLUTTER_ACTOR(button), &width, &height);
	window->setIconBox(window, x, y, width, height);
}

void graphene_panel_add_window(GraphenePanel *self, GrapheneWindow *window)
{
	if(window->flags & GRAPHENE_WINDOW_FLAG_SKIP_TASKBAR)
		return;
	CmkIcon *icon = cmk_icon_new();
	cmk_icon_set_size(icon, PANEL_HEIGHT * 3 / 4); // Icon is 75% of panel height. 64 -> 48, 32 -> 24, etc.

	CmkButton *button = cmk_button_new();
	g_signal_connect(button, "activate", G_CALLBACK(on_tasklist_button_activate), self);
	g_signal_connect(button, "allocation-changed", G_CALLBACK(on_tasklist_button_allocation_changed), window);
	cmk_button_set_content(button, CMK_WIDGET(icon));

	clutter_actor_add_child(CLUTTER_ACTOR(self->tasklist), CLUTTER_ACTOR(button));
	g_hash_table_insert(self->windows, window, button);
	
	ClutterActor *buttonActor = CLUTTER_ACTOR(button);
	clutter_actor_set_pivot_point(buttonActor, 0.5, 0.5);
	clutter_actor_set_scale(buttonActor, 0, 0);
	clutter_actor_save_easing_state(buttonActor);
	clutter_actor_set_easing_mode(buttonActor, CLUTTER_EASE_OUT_BACK);
	clutter_actor_set_easing_duration(buttonActor, 200);
	clutter_actor_set_scale(buttonActor, 1, 1);
	clutter_actor_restore_easing_state(buttonActor);
	TRANSITION_MEMLEAK_FIX(button, "scale-x");
	TRANSITION_MEMLEAK_FIX(button, "scale-y");

	graphene_panel_update_window(self, window);
}

static void remove_window_complete(GraphenePanel *self, CmkButton *button)
{
	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, self->windows);
	while(g_hash_table_iter_next(&iter, &key, &value))
	{
		if(value == button)
		{
			g_hash_table_iter_remove(&iter);
			return;
		}
	}
}

void graphene_panel_remove_window(GraphenePanel *self, GrapheneWindow *window)
{
	ClutterActor *button = CLUTTER_ACTOR(g_hash_table_lookup(self->windows, window));
	if(!button)
		return;
	g_signal_connect_swapped(button, "transitions_completed", G_CALLBACK(remove_window_complete), self);
	clutter_actor_save_easing_state(button);
	clutter_actor_set_easing_mode(button, CLUTTER_EASE_IN_BACK);
	clutter_actor_set_easing_duration(button, 200);
	clutter_actor_set_scale(button, 0, 0);
	clutter_actor_restore_easing_state(button);
	TRANSITION_MEMLEAK_FIX(button, "scale-x");
	TRANSITION_MEMLEAK_FIX(button, "scale-y");
}

void graphene_panel_update_window(GraphenePanel *self, GrapheneWindow *window)
{
	CmkButton *button = g_hash_table_lookup(self->windows, window);
	if(button)
	{
		CmkWidget *content = cmk_button_get_content(button);
		cmk_icon_set_icon(CMK_ICON(content), window->icon);
	}

	if(button)
		cmk_button_set_selected(button, (window->flags & GRAPHENE_WINDOW_FLAG_FOCUSED));

	if(!button && !(window->flags & GRAPHENE_WINDOW_FLAG_SKIP_TASKBAR))
		graphene_panel_add_window(self, window);
	else if(button && (window->flags & GRAPHENE_WINDOW_FLAG_SKIP_TASKBAR))
		graphene_panel_remove_window(self, window);
}

/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 */
 
#include "status-icons.h"
#include "network.h"

struct _GrapheneNetworkIcon
{
	CmkIcon parent;
	GrapheneNetworkControl *networkControl;
};

static void graphene_network_icon_dispose(GObject *self_);
static void network_icon_on_update(GrapheneNetworkIcon *self, GrapheneNetworkControl *networkControl);

G_DEFINE_TYPE(GrapheneNetworkIcon, graphene_network_icon, CMK_TYPE_ICON)


GrapheneNetworkIcon * graphene_network_icon_new(gfloat size)
{
	GrapheneNetworkIcon *self = GRAPHENE_NETWORK_ICON(g_object_new(GRAPHENE_TYPE_NETWORK_ICON, "use-foreground-color", TRUE, NULL));
	if(size > 0)
		cmk_icon_set_size(CMK_ICON(self), size);
	return self;
}

static void graphene_network_icon_class_init(GrapheneNetworkIconClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_network_icon_dispose;
}

static void graphene_network_icon_init(GrapheneNetworkIcon *self)
{
	self->networkControl = graphene_network_control_get_default();
	g_signal_connect_swapped(self->networkControl, "update", G_CALLBACK(network_icon_on_update), self);
	network_icon_on_update(self, self->networkControl);
}

static void graphene_network_icon_dispose(GObject *self_)
{
	g_clear_object(&GRAPHENE_NETWORK_ICON(self_)->networkControl);
	G_OBJECT_CLASS(graphene_network_icon_parent_class)->dispose(self_);
}

static void network_icon_on_update(GrapheneNetworkIcon *self, GrapheneNetworkControl *networkControl)
{
	cmk_icon_set_icon(CMK_ICON(self), graphene_network_control_get_icon_name(networkControl));
}


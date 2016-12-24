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

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE

#include "pkauthdialog.h"
#include <polkitagent/polkitagent.h>

struct _GraphenePKAuthDialog
{
	ClutterActor parent;
	gchar *actionId;
	gchar *message;
	gchar *iconName;
	gchar *cookie;
	GList *identities;
	PolkitAgentSession *agentSession;
	ClutterText *responseField;
};

enum
{
	SIGNAL_COMPLETE = 1,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST];

static void graphene_pk_auth_dialog_dispose(GObject *self_);
static void on_select_identity(GraphenePKAuthDialog *self, gpointer userdata);
static void on_auth_agent_completed(PolkitAgentSession *agentSession, gboolean gainedAuthorization, gpointer userdata);
static void on_auth_agent_request(PolkitAgentSession *agentSession, gchar *request, gboolean echoOn, gpointer userdata);
static void on_auth_agent_show_error(PolkitAgentSession *agentSession, gchar *text, gpointer userdata);
static void on_auth_agent_show_info(PolkitAgentSession *agentSession, gchar *text, gpointer userdata);
static GList * get_pkidentities_from_variant(GVariant *identitiesV, gchar **error);

G_DEFINE_TYPE(GraphenePKAuthDialog, graphene_pk_auth_dialog, CLUTTER_TYPE_ACTOR);


static void graphene_pk_auth_dialog_class_init(GraphenePKAuthDialogClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_pk_auth_dialog_dispose;
	
	/*
	 * Emitted when authentication has been completed or cancelled.
	 * You should close this GraphenePKAuthDialog instance on this signal.
	 * The first parameter is TRUE if the dialog was cancelled.
	 * The second parameter is TRUE if authentication was successful.
	 */
	signals[SIGNAL_COMPLETE] = g_signal_new("complete", G_TYPE_FROM_CLASS(class), G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
}

static void graphene_pk_auth_dialog_init(GraphenePKAuthDialog *self)
{
}

static void graphene_pk_auth_dialog_dispose(GObject *self_)
{
	GraphenePKAuthDialog *self = GRAPHENE_PK_AUTH_DIALOG(self_);
	
	g_clear_object(&self->agentSession);
	g_clear_pointer(&self->actionId, g_free);
	g_clear_pointer(&self->message, g_free);
	g_clear_pointer(&self->iconName, g_free);
	g_clear_pointer(&self->cookie, g_free);
	g_list_free_full(self->identities, (GDestroyNotify)g_object_unref);
	self->identities = NULL;

	G_OBJECT_CLASS(graphene_pk_auth_dialog_parent_class)->dispose(G_OBJECT(self));
}

static gboolean on_button_pressed(GraphenePKAuthDialog *self, ClutterButtonEvent *event, gpointer userdata)
{
	if(self->agentSession)
	{
		clutter_actor_set_reactive(self, FALSE);
		clutter_actor_set_reactive(self->responseField, FALSE);
		clutter_actor_set_opacity(self, 150);
		polkit_agent_session_response(self->agentSession, clutter_text_get_text(self->responseField));
	}
}

GraphenePKAuthDialog * graphene_pk_auth_dialog_new(const gchar *actionId, const gchar *message, const gchar *iconName, const gchar *cookie, GVariant *identitiesV)
{
	// The Polkit Authority sends a list of identities that are capable of
	// authorizing this particular action. These can either be users or
	// user groups (although there is room for new identity types).
	GList *identities = get_pkidentities_from_variant(identitiesV, NULL);
	if(!identities)
		return NULL;

	GraphenePKAuthDialog *self = GRAPHENE_PK_AUTH_DIALOG(g_object_new(GRAPHENE_TYPE_PK_AUTH_DIALOG, NULL));
	if(!GRAPHENE_IS_PK_AUTH_DIALOG(self))
		return NULL;

	self->actionId = g_strdup(actionId);
	self->message = g_strdup(message);
	self->iconName = g_strdup(iconName);
	self->cookie = g_strdup(cookie);
	self->identities = identities;

	ClutterActor *actor = CLUTTER_ACTOR(self);
	clutter_actor_set_width(actor, 800);
	clutter_actor_set_height(actor, 500);
	ClutterColor *frameColor = clutter_color_new(79, 88, 92, 255);
	clutter_actor_set_background_color(actor, frameColor);
	clutter_color_free(frameColor);

	ClutterText *passwordBox = CLUTTER_TEXT(clutter_text_new());
	clutter_text_set_password_char(passwordBox, 8226);
	clutter_text_set_activatable(passwordBox, TRUE);
	clutter_text_set_editable(passwordBox, TRUE);
	clutter_actor_set_size(CLUTTER_ACTOR(passwordBox), 300, 40);
	clutter_actor_set_position(CLUTTER_ACTOR(passwordBox), 40, 100);
	clutter_actor_add_child(actor, CLUTTER_ACTOR(passwordBox));
	clutter_actor_set_reactive(CLUTTER_ACTOR(passwordBox), TRUE);
	self->responseField = passwordBox;
	clutter_actor_grab_key_focus(passwordBox);

	frameColor = clutter_color_new(0, 255, 0, 255);
	clutter_actor_set_background_color(CLUTTER_ACTOR(passwordBox), frameColor);
	clutter_color_free(frameColor);

	ClutterActor *okay = clutter_actor_new();
	clutter_actor_set_size(okay, 100, 40);
	clutter_actor_set_position(okay, 660, 400);
	clutter_actor_add_child(actor, okay);
	clutter_actor_set_reactive(okay, TRUE);

	frameColor = clutter_color_new(255, 0, 0, 255);
	clutter_actor_set_background_color(okay, frameColor);
	clutter_color_free(frameColor);

	g_signal_connect_swapped(okay, "button-press-event", G_CALLBACK(on_button_pressed), self);
	g_signal_connect_swapped(passwordBox, "activate", G_CALLBACK(on_button_pressed), self);

	on_select_identity(self, NULL); // TEMP
	return self;
}

static void on_select_identity(GraphenePKAuthDialog *self, gpointer userdata)
{
	// TODO: Get selected identity
	PolkitIdentity *identity = POLKIT_IDENTITY(self->identities->data);
	
	self->agentSession = polkit_agent_session_new(identity, self->cookie);
	
	g_signal_connect(self->agentSession, "completed", G_CALLBACK(on_auth_agent_completed), self);
	g_signal_connect(self->agentSession, "request", G_CALLBACK(on_auth_agent_request), self);
	g_signal_connect(self->agentSession, "show-error", G_CALLBACK(on_auth_agent_show_error), self);
	g_signal_connect(self->agentSession, "show-info", G_CALLBACK(on_auth_agent_show_info), self);
	polkit_agent_session_initiate(self->agentSession);
}

static void on_auth_agent_completed(PolkitAgentSession *agentSession, gboolean gainedAuthorization, gpointer userdata)
{
	g_return_if_fail(GRAPHENE_IS_PK_AUTH_DIALOG(userdata));
	GraphenePKAuthDialog *self = GRAPHENE_PK_AUTH_DIALOG(userdata);
	g_clear_object(&self->agentSession);
	g_signal_emit(self, signals[SIGNAL_COMPLETE], 0, FALSE, gainedAuthorization);
}

static void on_auth_agent_request(PolkitAgentSession *agentSession, gchar *request, gboolean echoOn, gpointer userdata)
{
	g_message("Request: %s (echo: %i)", request, echoOn);
	// Hardcode password for testing
}

static void on_auth_agent_show_error(PolkitAgentSession *agentSession, gchar *text, gpointer userdata)
{
	g_warning("Authentication error: %s", text);
}

static void on_auth_agent_show_info(PolkitAgentSession *agentSession, gchar *text, gpointer userdata)
{
	g_warning("Authentication info: %s", text);
}

static GList * get_pkidentities_from_variant(GVariant *identitiesV, gchar **error)
{	
	GList *identities = NULL;

	gchar *kind;
	GVariantIter *propIter;
	GVariantIter iter;
	g_variant_iter_init(&iter, identitiesV);
	while(g_variant_iter_loop(&iter, "(sa{sv})", &kind, &propIter))
	{
		gchar *key;
		GVariant *val;
		while(g_variant_iter_loop(propIter, "{sv}", &key, &val)) 
		{
			PolkitIdentity *identity;
			if(g_strcmp0(kind, "unix-user") == 0
			&& g_strcmp0(key, "uid") == 0
			&& g_variant_check_format_string(val, "u", FALSE))
			{
				identity = polkit_unix_user_new(g_variant_get_uint32(val));
				gchar *dispname = g_strdup(polkit_unix_user_get_name(POLKIT_UNIX_USER(identity)));
				g_object_set_data_full(G_OBJECT(identity), "dispname", dispname, g_free);
			}
			else if(g_strcmp0(kind, "unix-group") == 0
			&& g_strcmp0(key, "gid") == 0
			&& g_variant_check_format_string(val, "u", FALSE))
			{
				guint32 gid = g_variant_get_uint32(val);
				identity = polkit_unix_group_new(gid);
				gchar *dispname = g_strdup_printf("Unix Group %u", gid);
				g_object_set_data_full(G_OBJECT(identity), "dispname", dispname, g_free);
			}
			else
			{
				g_variant_unref(val);
				g_free(key);
				g_variant_iter_free(propIter);
				g_free(kind);
				if(error)
					*error = g_strdup_printf("Invalid/unsupported user identity key: %s, %s", kind, key);
				g_list_free_full(identities, (GDestroyNotify)g_object_unref);
				return NULL;
			}

			identities = g_list_prepend(identities, identity);
		}
	}
	
	return identities;
}

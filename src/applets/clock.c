#include "clock.h"

#define FORMAT_STRING_LENGTH 25

struct _GrapheneClockApplet
{
	CmkWidget parent;
	GSettings *interfaceSettings;
	GSource *source;
	gchar *format;
	ClutterText *text; // Do not unref (clutter owns)
};

static void graphene_clock_applet_dispose(GObject *self_);
static void on_style_changed(CmkWidget *self_, CmkStyle *style);
static void on_background_changed(CmkWidget *self_);
static void on_interface_settings_changed(GrapheneClockApplet *self, gchar *key, GSettings *settings);
static gboolean update(GSource *source, GSourceFunc callback, gpointer userdata);

G_DEFINE_TYPE(GrapheneClockApplet, graphene_clock_applet, CMK_TYPE_WIDGET);

GrapheneClockApplet * graphene_clock_applet_new(void)
{
	return GRAPHENE_CLOCK_APPLET(g_object_new(GRAPHENE_TYPE_CLOCK_APPLET, NULL));
}

static void graphene_clock_applet_class_init(GrapheneClockAppletClass *class)
{
	G_OBJECT_CLASS(class)->dispose = graphene_clock_applet_dispose;
	CMK_WIDGET_CLASS(class)->style_changed = on_style_changed;
	CMK_WIDGET_CLASS(class)->background_changed = on_background_changed;
}

static void graphene_clock_applet_init(GrapheneClockApplet *self)
{
	clutter_actor_set_layout_manager(CLUTTER_ACTOR(self), clutter_bin_layout_new(CLUTTER_BIN_ALIGNMENT_CENTER, CLUTTER_BIN_ALIGNMENT_CENTER));
	self->text = CLUTTER_TEXT(clutter_text_new());
	clutter_actor_add_child(CLUTTER_ACTOR(self), CLUTTER_ACTOR(self->text));
	
	self->format = g_new(gchar, FORMAT_STRING_LENGTH);
	self->format[0] = '\0'; // Empty string
	
	self->interfaceSettings = g_settings_new("org.gnome.desktop.interface");
	g_signal_connect_swapped(self->interfaceSettings, "changed", G_CALLBACK(on_interface_settings_changed), self);
	on_interface_settings_changed(self, "clock-", self->interfaceSettings);
	
	static GSourceFuncs funcs = { NULL, NULL, update, NULL };
	self->source = g_source_new(&funcs, sizeof(GSource));
	g_source_set_callback(self->source, NULL, self, NULL); // Sets the userdata passed to update - the callback itself is ignored
	g_source_set_ready_time(self->source, 0);
	g_source_attach(self->source, NULL);
}

static void graphene_clock_applet_dispose(GObject *self_)
{
	GrapheneClockApplet *self = GRAPHENE_CLOCK_APPLET(self_);
	g_clear_object(&self->interfaceSettings);
	g_clear_pointer(&self->source, g_source_destroy);
	g_clear_pointer(&self->format, g_free);
	G_OBJECT_CLASS(graphene_clock_applet_parent_class)->dispose(self_);
}

static void on_style_changed(CmkWidget *self_, CmkStyle *style)
{
}

static void on_background_changed(CmkWidget *self_)
{
	const gchar *background = cmk_widget_get_background_color(self_);
	CmkColor color;
	cmk_style_get_font_color_for_background(cmk_widget_get_actual_style(self_), background, &color);
	ClutterColor cc = cmk_to_clutter_color(&color);
	clutter_text_set_color(GRAPHENE_CLOCK_APPLET(self_)->text, &cc);
}

static void on_interface_settings_changed(GrapheneClockApplet *self, gchar *key, GSettings *settings)
{
	if(!g_str_has_prefix(key, "clock-"))
		return;

	int format = g_settings_get_enum(settings, "clock-format");
	gboolean showDate = g_settings_get_boolean(settings, "clock-show-date");
	gboolean showSeconds = g_settings_get_boolean(settings, "clock-show-seconds");
	
	self->format[0] = '\0';
	
	if(showDate)
		g_strlcat(self->format, "%a %b %e ", FORMAT_STRING_LENGTH); // Mon Jan 1
	if(format == 1) // 12 hour time
		g_strlcat(self->format, "%l", FORMAT_STRING_LENGTH); // 5
	else
		g_strlcat(self->format, "%H", FORMAT_STRING_LENGTH); // 17
	g_strlcat(self->format, ":%M", FORMAT_STRING_LENGTH); // :30
	if(showSeconds)
		g_strlcat(self->format, ":%S", FORMAT_STRING_LENGTH); // :55
	if(format == 1)
		g_strlcat(self->format, " %p", FORMAT_STRING_LENGTH); // PM
	
	if(self->source)
		g_source_set_ready_time(self->source, 0); // Update label now
}

static gboolean update(GSource *source, GSourceFunc callback, gpointer userdata)
{
	GrapheneClockApplet *self = GRAPHENE_CLOCK_APPLET(userdata);
	
	// Get the time as a formatted string
	GDateTime *dt = g_date_time_new_now_local();
	gchar *formatted = g_date_time_format(dt, self->format);
	g_date_time_unref(dt);
	
	// Don't call set_text unless the string changed
	if(g_strcmp0(formatted, clutter_text_get_text(self->text)) == 0)
		g_free(formatted);
	else
		clutter_text_set_text(self->text, formatted);
	
	// Get monotonic time of the start of the next second
	// This keeps it from falling out of sync
	gint64 realNow = g_get_real_time(); // wall-clock time
	gint64 usUntilNextSecond = G_USEC_PER_SEC - (realNow - ((realNow / G_USEC_PER_SEC) * G_USEC_PER_SEC));
	usUntilNextSecond = CLAMP(usUntilNextSecond, 0, G_USEC_PER_SEC);
	gint64 updateTime = g_source_get_time(source) + usUntilNextSecond; // monotonic time
	
	// Set source to dispatch at the next second
	g_source_set_ready_time(source, updateTime);
	return G_SOURCE_CONTINUE;
}

#include <glib.h>
#include <gio/gio.h>

gchar ** strv_append(const gchar * const *list, const gchar *str);
gchar * str_trim(const gchar *str);
gint str_indexof(const gchar *str, const gchar c);

GVariant * get_gsettings_value(const gchar *schemaId, const gchar *key);
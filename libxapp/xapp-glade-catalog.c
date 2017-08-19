#include "config.h"
#include <glib-object.h>

#include "xapp-gtk-window.h"

void
xapp_glade_catalog_init (const gchar *catalog_name);

void
xapp_glade_catalog_init (const gchar *catalog_name)
{
  g_type_ensure (XAPP_TYPE_GTK_WINDOW);
}

#include "config.h"
#include <glib-object.h>

#include "xapp-gtk-window.h"
#include "xapp-icon-chooser-button.h"
#include "xapp-stack-sidebar.h"

void
xapp_glade_catalog_init (const gchar *catalog_name);

void
xapp_glade_catalog_init (const gchar *catalog_name)
{
  g_type_ensure (XAPP_TYPE_GTK_WINDOW);
  g_type_ensure (XAPP_TYPE_ICON_CHOOSER_BUTTON);
  g_type_ensure (XAPP_TYPE_STACK_SIDEBAR);
}

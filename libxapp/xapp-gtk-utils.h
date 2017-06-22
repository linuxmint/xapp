#ifndef __XAPP_GTK_UTILS_H__
#define __XAPP_GTK_UTILS_H__

#include <stdio.h>

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

void xapp_gtk_window_set_icon_name                                                  (GtkWindow   *window,
                                                                                     const gchar *icon_name);

G_END_DECLS

#endif  /* __XAPP_GTK_UTILS_H__ */

#ifndef __XAPP_STATUS_ICON_H__
#define __XAPP_STATUS_ICON_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_STATUS_ICON            (xapp_status_icon_get_type ())

G_DECLARE_FINAL_TYPE (XAppStatusIcon, xapp_status_icon, XAPP, STATUS_ICON, GObject)

XAppStatusIcon *xapp_status_icon_new                (void);
void            xapp_status_icon_set_name           (XAppStatusIcon *icon, const gchar *name);
void            xapp_status_icon_set_icon_name      (XAppStatusIcon *icon, const gchar *icon_name);
void            xapp_status_icon_set_tooltip_text   (XAppStatusIcon *icon, const gchar *tooltip_text);
void            xapp_status_icon_set_label          (XAppStatusIcon *icon, const gchar *label);
void            xapp_status_icon_set_visible        (XAppStatusIcon *icon, const gboolean visible);
void            xapp_status_icon_set_primary_menu   (XAppStatusIcon *icon, GtkMenu *menu);
GtkWidget      *xapp_status_icon_get_primary_menu   (XAppStatusIcon *icon);
void            xapp_status_icon_set_secondary_menu (XAppStatusIcon *icon, GtkMenu *menu);
GtkWidget      *xapp_status_icon_get_secondary_menu (XAppStatusIcon *icon);

G_END_DECLS

#endif  /* __XAPP_STATUS_ICON_H__ */

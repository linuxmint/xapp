#ifndef __XAPP_STATUS_ICON_H__
#define __XAPP_STATUS_ICON_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_STATUS_ICON            (xapp_status_icon_get_type ())
#define XAPP_STATUS_ICON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAPP_TYPE_STATUS_ICON, XAppStatusIcon))
#define XAPP_STATUS_ICON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  XAPP_TYPE_STATUS_ICON, XAppStatusIconClass))
#define XAPP_IS_STATUS_ICON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XAPP_TYPE_STATUS_ICON))
#define XAPP_IS_STATUS_ICON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  XAPP_TYPE_STATUS_ICON))
#define XAPP_STATUS_ICON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  XAPP_TYPE_STATUS_ICON, XAppStatusIconClass))

typedef struct _XAppStatusIconPrivate XAppStatusIconPrivate;
typedef struct _XAppStatusIcon XAppStatusIcon;
typedef struct _XAppStatusIconClass XAppStatusIconClass;

struct _XAppStatusIcon
{
    GObject parent_object;

    XAppStatusIconPrivate *priv;
};

struct _XAppStatusIconClass
{
    GObjectClass parent_class;
};

GType xapp_status_icon_get_type         (void);
void  xapp_status_icon_set_name         (XAppStatusIcon *icon, const gchar *name);
void  xapp_status_icon_set_icon_name    (XAppStatusIcon *icon, const gchar *icon_name);
void  xapp_status_icon_set_tooltip_text (XAppStatusIcon *icon, const gchar *tooltip_text);
void  xapp_status_icon_set_label        (XAppStatusIcon *icon, const gchar *label);
void  xapp_status_icon_set_visible      (XAppStatusIcon *icon, const gboolean visible);

G_END_DECLS

#endif  /* __XAPP_STATUS_ICON_H__ */

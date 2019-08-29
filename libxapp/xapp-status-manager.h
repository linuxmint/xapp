#ifndef __XAPP_STATUS_MANAGER_H__
#define __XAPP_STATUS_MANAGER_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_STATUS_MANAGER            (xapp_status_manager_get_type ())
#define XAPP_STATUS_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAPP_TYPE_STATUS_MANAGER, XAppStatusManager))
#define XAPP_STATUS_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  XAPP_TYPE_STATUS_MANAGER, XAppStatusManagerClass))
#define XAPP_IS_STATUS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XAPP_TYPE_STATUS_MANAGER))
#define XAPP_IS_STATUS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  XAPP_TYPE_STATUS_MANAGER))
#define XAPP_STATUS_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  XAPP_TYPE_STATUS_MANAGER, XAppStatusManagerClass))

typedef struct _XAppStatusManagerPrivate XAppStatusManagerPrivate;
typedef struct _XAppStatusManager XAppStatusManager;
typedef struct _XAppStatusManagerClass XAppStatusManagerClass;

struct _XAppStatusManager
{
    GObject parent_object;

    XAppStatusManagerPrivate *priv;
};

struct _XAppStatusManagerClass
{
    GObjectClass parent_class;
};

GType xapp_status_manager_get_type         (void);
void  xapp_status_manager_set_icon_name    (XAppStatusManager *self, const gchar *icon_name);
void  xapp_status_manager_set_tooltip_text (XAppStatusManager *self, const gchar *tooltip_text);
void  xapp_status_manager_set_label        (XAppStatusManager *self, const gchar *label);
void  xapp_status_manager_set_visible      (XAppStatusManager *self, const gboolean visible);

G_END_DECLS

#endif  /* __XAPP_STATUS_MANAGER_H__ */

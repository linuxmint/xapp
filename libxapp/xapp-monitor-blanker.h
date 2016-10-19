#ifndef __XAPP_MONITOR_BLANKER_H__
#define __XAPP_MONITOR_BLANKER_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_MONITOR_BLANKER            (xapp_monitor_blanker_get_type ())
#define XAPP_MONITOR_BLANKER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAPP_TYPE_MONITOR_BLANKER, XAppMonitorBlanker))
#define XAPP_MONITOR_BLANKER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  XAPP_TYPE_MONITOR_BLANKER, XAppMonitorBlankerClass))
#define XAPP_IS_MONITOR_BLANKER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XAPP_TYPE_MONITOR_BLANKER))
#define XAPP_IS_MONITOR_BLANKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  XAPP_TYPE_MONITOR_BLANKER))
#define XAPP_MONITOR_BLANKER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  XAPP_TYPE_MONITOR_BLANKER, XAppMonitorBlankerClass))

typedef struct _XAppMonitorBlankerPrivate XAppMonitorBlankerPrivate;
typedef struct _XAppMonitorBlanker XAppMonitorBlanker;
typedef struct _XAppMonitorBlankerClass XAppMonitorBlankerClass;

struct _XAppMonitorBlanker
{
    GObject parent_object;

    XAppMonitorBlankerPrivate *priv;
};

struct _XAppMonitorBlankerClass
{
    GObjectClass parent_class;
};

GType        xapp_monitor_blanker_get_type             (void);
XAppMonitorBlanker *xapp_monitor_blanker_new                  (void);
void         xapp_monitor_blanker_blank_other_monitors (XAppMonitorBlanker *self,
                                                GtkWindow   *window);
void         xapp_monitor_blanker_unblank_monitors     (XAppMonitorBlanker *self);
gboolean     xapp_monitor_blanker_are_monitors_blanked (XAppMonitorBlanker *self);

G_END_DECLS

#endif  /* __XAPP_MONITOR_BLANKER_H__ */

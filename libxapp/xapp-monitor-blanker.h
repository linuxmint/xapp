#ifndef __XAPP_MONITOR_BLANKER_H__
#define __XAPP_MONITOR_BLANKER_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_MONITOR_BLANKER (xapp_monitor_blanker_get_type ())

G_DECLARE_FINAL_TYPE (XAppMonitorBlanker, xapp_monitor_blanker, XAPP, MONITOR_BLANKER, GObject)

XAppMonitorBlanker *xapp_monitor_blanker_new (void);

void xapp_monitor_blanker_blank_other_monitors (XAppMonitorBlanker *self,
                                                GtkWindow          *window);
void xapp_monitor_blanker_unblank_monitors     (XAppMonitorBlanker *self);

gboolean xapp_monitor_blanker_are_monitors_blanked (XAppMonitorBlanker *self);

G_END_DECLS

#endif  /* __XAPP_MONITOR_BLANKER_H__ */

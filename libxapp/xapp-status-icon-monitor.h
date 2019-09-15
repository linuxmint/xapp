#ifndef __XAPP_STATUS_ICON_MONITOR_H__
#define __XAPP_STATUS_ICON_MONITOR_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_STATUS_ICON_MONITOR   (xapp_status_icon_monitor_get_type ())

G_DECLARE_FINAL_TYPE (XAppStatusIconMonitor, xapp_status_icon_monitor, XAPP, STATUS_ICON_MONITOR, GObject)

XAppStatusIconMonitor *xapp_status_icon_monitor_new        (void);
GList                 *xapp_status_icon_monitor_list_icons (XAppStatusIconMonitor *monitor);

G_END_DECLS

#endif  /* __XAPP_STATUS_ICON_MONITOR_H__ */

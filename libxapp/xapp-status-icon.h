#ifndef __XAPP_STATUS_ICON_H__
#define __XAPP_STATUS_ICON_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_STATUS_ICON            (xapp_status_icon_get_type ())

G_DECLARE_FINAL_TYPE (XAppStatusIcon, xapp_status_icon, XAPP, STATUS_ICON, GObject)

/**
 * XAppStatusIconState:
 * @XAPP_STATUS_ICON_STATE_NATIVE: The #XAppStatusIcon is currently being handled
 * by an #XAppStatusIconMonitor (usually in an applet).
 * @XAPP_STATUS_ICON_STATE_FALLBACK: The #XAppStatusIcon is currently being handled
 * by a legacy system tray implementation (using GtkStatusIcon).
 * @XAPP_STATUS_ICON_STATE_NO_SUPPORT: The #XAppStatusIcon is not currently being handled by any
 * kind of status icon implementation.
 */
typedef enum
{
    XAPP_STATUS_ICON_STATE_NATIVE,
    XAPP_STATUS_ICON_STATE_FALLBACK,
    XAPP_STATUS_ICON_STATE_NO_SUPPORT
} XAppStatusIconState;

/**
 * XAppStatusMonitorFound:
 * @XAPP_STATUS_MONITOR_FOUND_YES: An active XAppStatusIconMonitor is detected.
 * @XAPP_STATUS_MONITOR_FOUND_NO: No active XAppStatusIconMonitors exist.
 * @XAPP_STATUS_MONITOR_FOUND_UNKNOWN: It is unknown whether any XAppStatusIconMonitors
 * exist or not. This is probably due to an error during the query.
 */
typedef enum
{
    XAPP_STATUS_MONITOR_FOUND_YES,
    XAPP_STATUS_MONITOR_FOUND_NO,
    XAPP_STATUS_MONITOR_FOUND_UNKNOWN
} XAppStatusMonitorFound;

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
XAppStatusIconState xapp_status_icon_get_state      (XAppStatusIcon *icon);

/* static */
XAppStatusMonitorFound xapp_status_icon_any_monitors (gint timeout_sec);
G_END_DECLS

#endif  /* __XAPP_STATUS_ICON_H__ */

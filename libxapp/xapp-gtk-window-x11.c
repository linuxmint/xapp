#include <config.h>

#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "xapp-gtk-window-backend.h"

#define ICON_NAME_HINT "_NET_WM_XAPP_ICON_NAME"
#define PROGRESS_HINT  "_NET_WM_XAPP_PROGRESS"
#define PROGRESS_PULSE_HINT  "_NET_WM_XAPP_PROGRESS_PULSE"

static gboolean
ensure_x11 (GdkDisplay **display_out)
{
    GdkDisplay *display = gdk_display_get_default ();

    if (!GDK_IS_X11_DISPLAY (display))
        return FALSE;

    *display_out = display;
    return TRUE;
}

static void
set_window_hint_utf8 (Window       xid,
                      const gchar *atom_name,
                      const gchar *str)
{
    GdkDisplay *display;

    if (!ensure_x11 (&display))
        return;

    gdk_error_trap_push ();

    if (str != NULL)
    {
        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name),
                         gdk_x11_get_xatom_by_name_for_display (display, "UTF8_STRING"), 8,
                         PropModeReplace, (guchar *) str, strlen (str));
    }
    else
    {
        XDeleteProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name));
    }

    gdk_error_trap_pop_ignored ();
}

static void
set_window_hint_cardinal (Window       xid,
                          const gchar *atom_name,
                          gulong       cardinal)
{
    GdkDisplay *display;

    if (!ensure_x11 (&display))
        return;

    gdk_error_trap_push ();

    if (cardinal > 0)
    {
        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name),
                         XA_CARDINAL, 32,
                         PropModeReplace,
                         (guchar *) &cardinal, 1);
    }
    else
    {
        XDeleteProperty (GDK_DISPLAY_XDISPLAY (display),
                         xid,
                         gdk_x11_get_xatom_by_name_for_display (display, atom_name));
    }

    gdk_error_trap_pop_ignored ();
}

static Window
get_window_xid (GtkWindow *window)
{
    GdkWindow *gdk_window;

    gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

    if (gdk_window_get_effective_toplevel (gdk_window) != gdk_window)
    {
        g_warning ("Window is not toplevel");
        return 0;
    }

    return GDK_WINDOW_XID (gdk_window);
}

void
_xapp_x11_update_icon_name (GtkWindow   *window,
                            const gchar *icon_str)
{
    Window xid = get_window_xid (window);

    if (xid == 0)
        return;

    set_window_hint_utf8 (xid, ICON_NAME_HINT, icon_str);
}

void
_xapp_x11_update_progress (GtkWindow *window,
                           guint      progress,
                           gboolean   pulse)
{
    Window xid = get_window_xid (window);

    if (xid == 0)
        return;

    set_window_hint_cardinal (xid, PROGRESS_HINT, (gulong) progress);
    set_window_hint_cardinal (xid, PROGRESS_PULSE_HINT, (gulong) (pulse ? 1 : 0));
}

void
_xapp_x11_set_xid_icon_name (gulong       xid,
                             const gchar *icon_str)
{
    set_window_hint_utf8 (xid, ICON_NAME_HINT, icon_str);
}

void
_xapp_x11_set_xid_progress (gulong xid,
                            gint   progress)
{
    set_window_hint_cardinal (xid, PROGRESS_HINT, (gulong) (CLAMP (progress, 0, 100)));
    set_window_hint_cardinal (xid, PROGRESS_PULSE_HINT, (gulong) 0);
}

void
_xapp_x11_set_xid_progress_pulse (gulong   xid,
                                  gboolean pulse)
{
    set_window_hint_cardinal (xid, PROGRESS_PULSE_HINT, (gulong) (pulse ? 1 : 0));
}

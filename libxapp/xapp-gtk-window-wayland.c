#include <config.h>

#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>
#include <wayland-client.h>

#include "xapp-gtk-window-backend.h"
#include "xapp-shell-client-protocol.h"

#define DISPLAY_DATA_KEY "xapp-wayland-shell-data"
#define SURFACE_DATA_KEY "xapp-wayland-surface"

typedef struct
{
    struct xapp_shell_v1 *shell;
    uint32_t            capabilities;
} XAppWaylandShell;

static void
shell_handle_capabilities (void               *data,
                           struct xapp_shell_v1 *shell,
                           uint32_t            capabilities)
{
    XAppWaylandShell *wd = data;

    wd->capabilities = capabilities;
}

static const struct xapp_shell_v1_listener shell_listener = {
    shell_handle_capabilities,
};

static void
registry_handle_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
    XAppWaylandShell *wd = data;

    if (g_strcmp0 (interface, xapp_shell_v1_interface.name) == 0)
    {
        wd->shell = wl_registry_bind (registry, id, &xapp_shell_v1_interface,
                                      MIN (version, 1u));
        if (wd->shell)
            xapp_shell_v1_add_listener (wd->shell, &shell_listener, wd);
    }
}

static void
registry_handle_global_remove (void               *data,
                               struct wl_registry *registry,
                               uint32_t            id)
{
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove,
};

static void
free_shell_data (gpointer data)
{
    XAppWaylandShell *wd = data;

    if (wd->shell)
        xapp_shell_v1_destroy (wd->shell);

    g_free (wd);
}

/* Bind (once per display) the xapp_shell_v1 global from the registry, using a
 * private event queue so our blocking roundtrips don't dispatch GTK's events. */
static XAppWaylandShell *
ensure_shell (GdkDisplay *display)
{
    XAppWaylandShell *wd;
    struct wl_display *wl_display;
    struct wl_event_queue *queue;
    struct wl_registry *registry;

    if (!GDK_IS_WAYLAND_DISPLAY (display))
        return NULL;

    wd = g_object_get_data (G_OBJECT (display), DISPLAY_DATA_KEY);
    if (wd)
        return wd;

    wd = g_new0 (XAppWaylandShell, 1);

    wl_display = gdk_wayland_display_get_wl_display (GDK_WAYLAND_DISPLAY (display));
    queue = wl_display_create_queue (wl_display);
    registry = wl_display_get_registry (wl_display);
    wl_proxy_set_queue ((struct wl_proxy *) registry, queue);
    wl_registry_add_listener (registry, &registry_listener, wd);

    /* First roundtrip processes the globals (binding the shell, if present). */
    if (wl_display_roundtrip_queue (wl_display, queue) < 0)
        g_warning ("XAppGtkWindow: Wayland roundtrip failed while querying for the xapp-shell global");
    /* Second roundtrip processes the shell's 'capabilities' event. */
    if (wd->shell && wl_display_roundtrip_queue (wl_display, queue) < 0)
        g_warning ("XAppGtkWindow: Wayland roundtrip failed while querying xapp-shell capabilities");

    wl_registry_destroy (registry);

    if (wd->shell == NULL)
        g_warning ("XAppGtkWindow: the compositor does not implement the xapp_shell_v1 "
                   "protocol; per-window icon names and progress are unavailable in this "
                   "Wayland session.");

    /* The shell proxy must outlive the temporary queue, so move it back to the
     * default queue. It receives no further events. */
    if (wd->shell)
        wl_proxy_set_queue ((struct wl_proxy *) wd->shell, NULL);

    wl_event_queue_destroy (queue);

    g_object_set_data_full (G_OBJECT (display), DISPLAY_DATA_KEY,
                            wd, free_shell_data);
    return wd;
}

static struct xapp_surface_v1 *
ensure_xapp_surface (GtkWindow        *window,
                     XAppWaylandShell *wd)
{
    GdkWindow *gdk_window;
    struct wl_surface *wl_surface;
    struct xapp_surface_v1 *xapp_surface;

    xapp_surface = g_object_get_data (G_OBJECT (window), SURFACE_DATA_KEY);
    if (xapp_surface)
        return xapp_surface;

    gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
    if (gdk_window == NULL || !GDK_IS_WAYLAND_WINDOW (gdk_window))
        return NULL;

    wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);
    if (wl_surface == NULL)
        return NULL;

    xapp_surface = xapp_shell_v1_get_xapp_surface (wd->shell, wl_surface);
    g_object_set_data_full (G_OBJECT (window), SURFACE_DATA_KEY,
                            xapp_surface,
                            (GDestroyNotify) xapp_surface_v1_destroy);
    return xapp_surface;
}

static void
flush_display (GdkDisplay *display)
{
    wl_display_flush (gdk_wayland_display_get_wl_display (GDK_WAYLAND_DISPLAY (display)));
}

void
_xapp_wayland_update_icon_name (GtkWindow   *window,
                                const gchar *icon_str)
{
    GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (window));
    XAppWaylandShell *wd;
    struct xapp_surface_v1 *xapp_surface;

    wd = ensure_shell (display);
    if (wd == NULL || wd->shell == NULL ||
        !(wd->capabilities & XAPP_SHELL_V1_CAPABILITY_ICON_NAME))
        return;

    xapp_surface = ensure_xapp_surface (window, wd);
    if (xapp_surface == NULL)
        return;

    xapp_surface_v1_set_icon_name (xapp_surface, icon_str);
    flush_display (display);
}

void
_xapp_wayland_update_progress (GtkWindow *window,
                               guint      progress,
                               gboolean   pulse)
{
    GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (window));
    XAppWaylandShell *wd;
    struct xapp_surface_v1 *xapp_surface;

    wd = ensure_shell (display);
    if (wd == NULL || wd->shell == NULL ||
        !(wd->capabilities & XAPP_SHELL_V1_CAPABILITY_PROGRESS))
        return;

    xapp_surface = ensure_xapp_surface (window, wd);
    if (xapp_surface == NULL)
        return;

    xapp_surface_v1_set_progress (xapp_surface, (int32_t) progress);
    xapp_surface_v1_set_progress_pulse (xapp_surface, pulse ? 1 : 0);
    flush_display (display);
}

void
_xapp_wayland_window_unrealized (GtkWindow *window)
{
    /* Drops the cached xapp_surface_v1 (destroying it via its GDestroyNotify) so
     * a fresh one is created against the new wl_surface on the next realize. */
    g_object_set_data (G_OBJECT (window), SURFACE_DATA_KEY, NULL);
}

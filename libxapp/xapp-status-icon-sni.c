/* xapp-status-icon-sni.c
 *
 * StatusNotifier backend for XAppStatusIcon (org.kde.StatusNotifierItem)
 * Provides support for KDE/GNOME and other desktops using StatusNotifier protocol
 */

#include <config.h>
#include <gtk/gtk.h>
#include <libdbusmenu-gtk/dbusmenu-gtk.h>

#include "xapp-status-icon.h"
#include "xapp-status-icon-private.h"
#include "xapp-status-icon-backend.h"
#include "sn-item-interface.h"

#define DEBUG_FLAG XAPP_DEBUG_STATUS_ICON
#include "xapp-debug.h"

/* Constants */
#define SNI_WATCHER_BUS_NAME "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_OBJECT_PATH "/StatusNotifierWatcher"
#define SNI_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"

/* Global variables from main file */
extern XAppStatusIconState process_icon_state;

/* Forward declarations */
static void register_with_sni_watcher (XAppStatusIcon *icon);
static void on_sni_registration_complete (GObject *source, GAsyncResult *res, gpointer user_data);
static void on_sni_watcher_appeared (GDBusConnection *connection,
                                   const gchar *name,
                                   const gchar *name_owner,
                                   gpointer user_data);
static void on_sni_watcher_vanished (GDBusConnection *connection,
                                   const gchar *name,
                                   gpointer user_data);

/* Method handlers */
static gboolean handle_sni_activate (SnItemInterface *skeleton,
                                    GDBusMethodInvocation *invocation,
                                    gint x, gint y,
                                    XAppStatusIcon *icon);
static gboolean handle_sni_secondary_activate (SnItemInterface *skeleton,
                                              GDBusMethodInvocation *invocation,
                                              gint x, gint y,
                                              XAppStatusIcon *icon);
static gboolean handle_sni_context_menu (SnItemInterface *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        gint x, gint y,
                                        XAppStatusIcon *icon);
static gboolean handle_sni_scroll (SnItemInterface *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  gint delta,
                                  const gchar *direction_str,
                                  XAppStatusIcon *icon);

/* Menu integration */
void sni_backend_export_menu (XAppStatusIcon *icon);

/* SNI Backend Core Implementation */

static void
on_sni_registration_complete (GObject *source, GAsyncResult *res, gpointer user_data)
{
    XAppStatusIcon *icon = XAPP_STATUS_ICON(user_data);
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);
    GError *error = NULL;

    GVariant *result = g_dbus_connection_call_finish (G_DBUS_CONNECTION(source), res, &error);

    if (error != NULL)
    {
        if (error->code != G_IO_ERROR_CANCELLED)
        {
            g_critical ("SNI: Failed to register with StatusNotifierWatcher: %s", error->message);
            priv->sni_registered = FALSE;
        }
        g_error_free (error);
        return;
    }

    g_variant_unref (result);
    priv->sni_registered = TRUE;

    DEBUG("SNI: Successfully registered with StatusNotifierWatcher");

    /* Update state */
    process_icon_state = XAPP_STATUS_ICON_STATE_NATIVE;
    emit_state_changed (icon, process_icon_state);
}

static void
register_with_sni_watcher (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    /* The watcher expects either:
     * - A path starting with "/" (it will use the sender as bus name)
     * - A bus name (it will use "/StatusNotifierItem" as path)
     * We send the path, and the watcher extracts our bus name from the method invocation sender.
     */
    DEBUG("SNI: Registering item with watcher at path: %s", priv->sni_item_path);

    g_dbus_connection_call (priv->connection,
                          SNI_WATCHER_BUS_NAME,
                          SNI_WATCHER_OBJECT_PATH,
                          SNI_WATCHER_INTERFACE,
                          "RegisterStatusNotifierItem",
                          g_variant_new ("(s)", priv->sni_item_path),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          priv->cancellable,
                          on_sni_registration_complete,
                          icon);
}

/* Watcher Monitoring */

static void
start_sni_watcher_monitoring (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (priv->sni_watcher_watch_id != 0)
    {
        return;  /* Already watching */
    }

    DEBUG("SNI: Starting watcher monitoring");

    priv->sni_watcher_watch_id = g_bus_watch_name_on_connection (
        priv->connection,
        SNI_WATCHER_BUS_NAME,
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_sni_watcher_appeared,
        on_sni_watcher_vanished,
        icon,
        NULL);
}

static void
on_sni_watcher_appeared (GDBusConnection *connection,
                       const gchar *name,
                       const gchar *name_owner,
                       gpointer user_data)
{
    XAppStatusIcon *icon = XAPP_STATUS_ICON(user_data);
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("SNI: StatusNotifierWatcher appeared: %s", name_owner);

    /* Only switch if we're currently in fallback mode */
    if (priv->active_backend == XAPP_BACKEND_FALLBACK)
    {
        DEBUG("SNI: Switching from fallback to SNI backend");
        switch_to_backend (icon, XAPP_BACKEND_SNI);
    }
}

static void
on_sni_watcher_vanished (GDBusConnection *connection,
                       const gchar *name,
                       gpointer user_data)
{
    XAppStatusIcon *icon = XAPP_STATUS_ICON(user_data);
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("SNI: StatusNotifierWatcher vanished");

    /* If we were using SNI backend, fall back to GtkStatusIcon */
    if (priv->active_backend == XAPP_BACKEND_SNI)
    {
        DEBUG("SNI: Switching to fallback backend");
        switch_to_backend (icon, XAPP_BACKEND_FALLBACK);
    }
}

/* SNI Method Handlers */

static gboolean
handle_sni_activate (SnItemInterface *skeleton,
                   GDBusMethodInvocation *invocation,
                   gint x, gint y,
                   XAppStatusIcon *icon)
{
    DEBUG("SNI: Activate at %d,%d", x, y);

    emit_button_press (icon, x, y, GDK_BUTTON_PRIMARY, GDK_CURRENT_TIME, GTK_POS_TOP);
    emit_button_release (icon, x, y, GDK_BUTTON_PRIMARY, GDK_CURRENT_TIME, GTK_POS_TOP);

    if (should_send_activate (GDK_BUTTON_PRIMARY, TRUE))
    {
        emit_activate (icon, GDK_BUTTON_PRIMARY, GDK_CURRENT_TIME);
    }

    sn_item_interface_complete_activate (skeleton, invocation);
    return TRUE;
}

static gboolean
handle_sni_secondary_activate (SnItemInterface *skeleton,
                              GDBusMethodInvocation *invocation,
                              gint x, gint y,
                              XAppStatusIcon *icon)
{
    DEBUG("SNI: SecondaryActivate at %d,%d", x, y);

    emit_button_press (icon, x, y, GDK_BUTTON_MIDDLE, GDK_CURRENT_TIME, GTK_POS_TOP);
    emit_button_release (icon, x, y, GDK_BUTTON_MIDDLE, GDK_CURRENT_TIME, GTK_POS_TOP);

    if (should_send_activate (GDK_BUTTON_MIDDLE, TRUE))
    {
        emit_activate (icon, GDK_BUTTON_MIDDLE, GDK_CURRENT_TIME);
    }

    sn_item_interface_complete_secondary_activate (skeleton, invocation);
    return TRUE;
}

static gboolean
handle_sni_context_menu (SnItemInterface *skeleton,
                       GDBusMethodInvocation *invocation,
                       gint x, gint y,
                       XAppStatusIcon *icon)
{
    DEBUG("SNI: ContextMenu at %d,%d", x, y);

    emit_button_press (icon, x, y, GDK_BUTTON_SECONDARY, GDK_CURRENT_TIME, GTK_POS_TOP);
    emit_button_release (icon, x, y, GDK_BUTTON_SECONDARY, GDK_CURRENT_TIME, GTK_POS_TOP);

    if (should_send_activate (GDK_BUTTON_SECONDARY, TRUE))
    {
        emit_activate (icon, GDK_BUTTON_SECONDARY, GDK_CURRENT_TIME);
    }

    sn_item_interface_complete_context_menu (skeleton, invocation);
    return TRUE;
}

static gboolean
handle_sni_scroll (SnItemInterface *skeleton,
                 GDBusMethodInvocation *invocation,
                 gint delta,
                 const gchar *direction_str,
                 XAppStatusIcon *icon)
{
    DEBUG("SNI: Scroll: delta=%d, direction=%s", delta, direction_str);

    XAppScrollDirection direction;

    if (g_strcmp0 (direction_str, "vertical") == 0)
    {
        direction = delta > 0 ? XAPP_SCROLL_DOWN : XAPP_SCROLL_UP;
    }
    else
    {
        direction = delta > 0 ? XAPP_SCROLL_RIGHT : XAPP_SCROLL_LEFT;
    }

    emit_scroll (icon, abs (delta), direction, GDK_CURRENT_TIME);

    sn_item_interface_complete_scroll (skeleton, invocation);
    return TRUE;
}

/* DBusMenu Integration */

void
sni_backend_export_menu (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (!priv->sni_skeleton)
    {
        return;
    }

    DEBUG("SNI: Exporting DBusMenu");

    /* Clean up existing server */
    if (priv->dbusmenu_server)
    {
        g_object_unref (priv->dbusmenu_server);
        priv->dbusmenu_server = NULL;
    }

    /* Determine which menu to export - prefer primary */
    GtkMenu *gtk_menu = NULL;
    if (priv->primary_menu)
    {
        gtk_menu = GTK_MENU(priv->primary_menu);
    }
    else if (priv->secondary_menu)
    {
        gtk_menu = GTK_MENU(priv->secondary_menu);
    }

    if (gtk_menu)
    {
        /* Create new DBusMenu server */
        priv->dbusmenu_server = dbusmenu_server_new (priv->dbusmenu_path);

        /* Convert GtkMenu to DbusmenuMenuitem hierarchy */
        DbusmenuMenuitem *root = dbusmenu_gtk_parse_menu_structure (gtk_menu);
        dbusmenu_server_set_root (priv->dbusmenu_server, root);
        g_object_unref (root);

        /* Update SNI Menu property */
        sn_item_interface_set_menu (priv->sni_skeleton, priv->dbusmenu_path);

        DEBUG("SNI: Menu exported at %s", priv->dbusmenu_path);
    }
    else
    {
        /* No menu available - set to empty path */
        sn_item_interface_set_menu (priv->sni_skeleton, "/");
        DEBUG("SNI: No menu to export");
    }

    sn_item_interface_emit_new_menu (priv->sni_skeleton);
}

gboolean
sni_backend_init (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);
    static gint icon_counter = 0;

    DEBUG("SNI backend init");

    /* Generate unique icon ID and paths */
    gchar *icon_id = g_strdup_printf ("icon_%d", icon_counter++);
    priv->sni_item_path = g_strdup_printf ("/StatusNotifierItem/%s", icon_id);
    priv->dbusmenu_path = g_strdup_printf ("/MenuBar/%s", icon_id);
    g_free (icon_id);

    DEBUG("SNI: Creating interface at path %s", priv->sni_item_path);

    /* Create StatusNotifierItem interface skeleton */
    priv->sni_skeleton = sn_item_interface_skeleton_new ();

    /* Set initial properties */
    sn_item_interface_set_id (priv->sni_skeleton, priv->name);
    sn_item_interface_set_category (priv->sni_skeleton, "ApplicationStatus");
    sn_item_interface_set_status (priv->sni_skeleton, "Active");
    sn_item_interface_set_icon_name (priv->sni_skeleton, "");
    sn_item_interface_set_icon_theme_path (priv->sni_skeleton, "");
    sn_item_interface_set_menu (priv->sni_skeleton, priv->dbusmenu_path);
    sn_item_interface_set_title (priv->sni_skeleton, "");

    /* Set empty pixmap arrays */
    GVariant *empty_pixmap = g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0);
    sn_item_interface_set_icon_pixmap (priv->sni_skeleton, empty_pixmap);
    sn_item_interface_set_overlay_icon_pixmap (priv->sni_skeleton,
        g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0));
    sn_item_interface_set_attention_icon_pixmap (priv->sni_skeleton,
        g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0));

    /* Set empty tooltip */
    GVariant *empty_tooltip = g_variant_new ("(s@a(iiay)ss)",
                                            "",
                                            g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0),
                                            "",
                                            "");
    sn_item_interface_set_tool_tip (priv->sni_skeleton, empty_tooltip);

    /* Set XAyatana label extension (Ubuntu/Ayatana indicators) */
    sn_item_interface_set_xayatana_label (priv->sni_skeleton, "");
    sn_item_interface_set_xayatana_label_guide (priv->sni_skeleton, "");

    /* Connect method handlers */
    g_signal_connect (priv->sni_skeleton, "handle-activate",
                    G_CALLBACK(handle_sni_activate), icon);
    g_signal_connect (priv->sni_skeleton, "handle-secondary-activate",
                    G_CALLBACK(handle_sni_secondary_activate), icon);
    g_signal_connect (priv->sni_skeleton, "handle-context-menu",
                    G_CALLBACK(handle_sni_context_menu), icon);
    g_signal_connect (priv->sni_skeleton, "handle-scroll",
                    G_CALLBACK(handle_sni_scroll), icon);

    /* Export to D-Bus */
    GError *error = NULL;
    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON(priv->sni_skeleton),
                                     priv->connection,
                                     priv->sni_item_path,
                                     &error);

    if (error)
    {
        g_critical ("SNI: Failed to export interface: %s", error->message);
        g_error_free (error);
        g_clear_object (&priv->sni_skeleton);
        g_free (priv->sni_item_path);
        g_free (priv->dbusmenu_path);
        priv->sni_item_path = NULL;
        priv->dbusmenu_path = NULL;
        return FALSE;
    }

    DEBUG("SNI: Interface exported successfully");

    /* Register with StatusNotifierWatcher */
    register_with_sni_watcher (icon);

    /* Export menus if present */
    sni_backend_export_menu (icon);

    /* Start monitoring watcher for dynamic switching */
    start_sni_watcher_monitoring (icon);

    return TRUE;
}

void
sni_backend_cleanup (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    DEBUG("SNI backend cleanup");

    /* Unregistration happens automatically when we disconnect from D-Bus */

    /* Unexport interface */
    if (priv->sni_skeleton)
    {
        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON(priv->sni_skeleton));
        g_clear_object (&priv->sni_skeleton);
    }

    /* Clean up DBusMenu */
    if (priv->dbusmenu_server)
    {
        g_object_unref (priv->dbusmenu_server);
        priv->dbusmenu_server = NULL;
    }

    g_clear_pointer (&priv->sni_item_path, g_free);
    g_clear_pointer (&priv->dbusmenu_path, g_free);

    /* Stop watcher monitoring */
    if (priv->sni_watcher_watch_id != 0)
    {
        g_bus_unwatch_name (priv->sni_watcher_watch_id);
        priv->sni_watcher_watch_id = 0;
    }

    priv->sni_registered = FALSE;
}

/* Helper function to convert GdkPixbuf to SNI pixmap format (ARGB) */
static GVariant *
pixbuf_to_sni_pixmap_array (GdkPixbuf *pixbuf)
{
    gint width = gdk_pixbuf_get_width (pixbuf);
    gint height = gdk_pixbuf_get_height (pixbuf);
    gint rowstride = gdk_pixbuf_get_rowstride (pixbuf);
    gint channels = gdk_pixbuf_get_n_channels (pixbuf);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels (pixbuf);

    /* Allocate ARGB buffer */
    gsize data_size = width * height * 4;
    guchar *argb_data = g_malloc (data_size);

    /* Convert RGBA/RGB to ARGB (network byte order) */
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            guchar *src = pixels + y * rowstride + x * channels;
            guchar *dst = argb_data + (y * width + x) * 4;

            if (has_alpha)
            {
                dst[0] = src[3];  /* A */
                dst[1] = src[0];  /* R */
                dst[2] = src[1];  /* G */
                dst[3] = src[2];  /* B */
            }
            else
            {
                dst[0] = 0xFF;    /* A (opaque) */
                dst[1] = src[0];  /* R */
                dst[2] = src[1];  /* G */
                dst[3] = src[2];  /* B */
            }
        }
    }

    GVariant *byte_array = g_variant_new_from_data (
        G_VARIANT_TYPE("ay"),
        argb_data,
        data_size,
        TRUE,
        g_free,
        argb_data);

    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE("a(iiay)"));
    g_variant_builder_add (&builder, "(ii@ay)", width, height, byte_array);

    return g_variant_builder_end (&builder);
}

void
sni_backend_set_icon_name (XAppStatusIcon *icon, const gchar *icon_name)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (!priv->sni_skeleton)
    {
        return;
    }

    DEBUG("SNI: set_icon_name: %s", icon_name ? icon_name : "(null)");

    if (!icon_name || *icon_name == '\0')
    {
        /* Clear icon */
        sn_item_interface_set_icon_name (priv->sni_skeleton, "");
        sn_item_interface_set_icon_pixmap (priv->sni_skeleton,
            g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0));
        sn_item_interface_emit_new_icon (priv->sni_skeleton);
        return;
    }

    if (g_path_is_absolute (icon_name))
    {
        /* Load file and convert to pixmap array */
        GError *error = NULL;
        gint icon_size = priv->icon_size > 0 ? priv->icon_size : 24;

        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale (
            icon_name,
            icon_size,
            icon_size,
            TRUE,
            &error);

        if (error)
        {
            g_warning ("SNI: Failed to load icon from %s: %s", icon_name, error->message);
            g_error_free (error);
            /* Set empty icon */
            sn_item_interface_set_icon_name (priv->sni_skeleton, "");
            sn_item_interface_set_icon_pixmap (priv->sni_skeleton,
                g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0));
        }
        else
        {
            GVariant *pixmap = pixbuf_to_sni_pixmap_array (pixbuf);
            sn_item_interface_set_icon_pixmap (priv->sni_skeleton, pixmap);
            sn_item_interface_set_icon_name (priv->sni_skeleton, "");
            g_object_unref (pixbuf);
        }
    }
    else
    {
        /* Use theme icon name */
        sn_item_interface_set_icon_name (priv->sni_skeleton, icon_name);
        /* Clear pixmap array */
        sn_item_interface_set_icon_pixmap (priv->sni_skeleton,
            g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0));
    }

    sn_item_interface_emit_new_icon (priv->sni_skeleton);
}

void
sni_backend_set_tooltip (XAppStatusIcon *icon, const gchar *tooltip)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (!priv->sni_skeleton)
    {
        return;
    }

    DEBUG("SNI: set_tooltip: %s", tooltip ? tooltip : "(null)");

    /* ToolTip format: (sa (iiay)ss)
     * - icon name (string)
     * - icon pixmap array (empty)
     * - title (string) - we use this for the tooltip text
     * - body (string) - empty
     */
    GVariant *empty_pixmap = g_variant_new_array (G_VARIANT_TYPE("(iiay)"), NULL, 0);

    GVariant *tooltip_struct = g_variant_new (
        "(s@a(iiay)ss)",
        "",                              /* icon name */
        empty_pixmap,                    /* icon pixmap */
        tooltip ? tooltip : "",          /* title */
        ""                               /* body (empty) */
    );

    sn_item_interface_set_tool_tip (priv->sni_skeleton, tooltip_struct);
    sn_item_interface_emit_new_tool_tip (priv->sni_skeleton);
}

void
sni_backend_set_visible (XAppStatusIcon *icon, gboolean visible)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (!priv->sni_skeleton)
    {
        return;
    }

    DEBUG("SNI: set_visible: %s", visible ? "TRUE" : "FALSE");

    /* SNI uses Status property: "Active", "Passive", or "NeedsAttention" */
    const gchar *status = visible ? "Active" : "Passive";
    sn_item_interface_set_status (priv->sni_skeleton, status);
    sn_item_interface_emit_new_status (priv->sni_skeleton, status);
}

void
sni_backend_set_label (XAppStatusIcon *icon, const gchar *label)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (!priv->sni_skeleton)
    {
        return;
    }

    DEBUG("SNI: set_label: %s", label ? label : "(null)");

    const gchar *label_str = label ? label : "";

    /* SNI uses Title property for label text */
    sn_item_interface_set_title (priv->sni_skeleton, label_str);
    sn_item_interface_emit_new_title (priv->sni_skeleton);

    /* Also set XAyatana label extension for Ubuntu/Ayatana indicators */
    sn_item_interface_set_xayatana_label (priv->sni_skeleton, label_str);
    sn_item_interface_emit_xayatana_new_label (priv->sni_skeleton, label_str, "");
}

void
sni_backend_sync (XAppStatusIcon *icon)
{
    XAppStatusIconPrivate *priv = XAPP_STATUS_ICON_GET_PRIVATE(icon);

    if (!priv->sni_skeleton)
    {
        return;
    }

    DEBUG("SNI: Syncing all properties");

    /* Sync all properties to the SNI interface */
    sni_backend_set_icon_name (icon, priv->icon_name);
    sni_backend_set_tooltip (icon, priv->tooltip_text);
    sni_backend_set_visible (icon, priv->visible);
    sni_backend_set_label (icon, priv->label);
    sni_backend_export_menu (icon);
}

/* Backend operations structure */
XAppBackend sni_backend_ops = {
    .type = XAPP_BACKEND_SNI,
    .init = sni_backend_init,
    .cleanup = sni_backend_cleanup,
    .sync = sni_backend_sync,
    .set_icon_name = sni_backend_set_icon_name,
    .set_tooltip = sni_backend_set_tooltip,
    .set_visible = sni_backend_set_visible,
    .set_label = sni_backend_set_label,
};


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <cairo-gobject.h>
#include <libxapp/xapp-status-icon.h>
#include <libdbusmenu-gtk/menu.h>

#include "sn-item-interface.h"
#include "sn-item.h"

#define FALLBACK_ICON_SIZE 24

typedef enum
{
    STATUS_PASSIVE,
    STATUS_ACTIVE,
    STATUS_NEEDS_ATTENTION
} Status;

struct _SnItem
{
    GObject parent_instance;

    GDBusProxy *sn_item_proxy; // SnItemProxy
    GDBusProxy *prop_proxy; // dbus properties (we can't trust SnItemProxy)

    GtkWidget *menu;
    XAppStatusIcon *status_icon;

    Status status;
    gchar *last_png_path;
    gchar *png_path;

    gint current_icon_id;
    gchar *sortable_name;

    gboolean is_ai;
};

G_DEFINE_TYPE (SnItem, sn_item, G_TYPE_OBJECT)

static void update_menu (SnItem *item);
static void update_status (SnItem *item);
static void update_tooltip (SnItem *item);
static void update_icon (SnItem *item);

static gboolean
should_activate (SnItem *item)
{
    gboolean should;

    gchar **whitelist = g_settings_get_strv (xapp_settings,
                                             WHITELIST_KEY);

    should = g_strv_contains ((const gchar * const *) whitelist, item->sortable_name);
    g_strfreev (whitelist);

    return should;
}

static void
sn_item_init (SnItem *self)
{
}

static void
sn_item_dispose (GObject *object)
{
    SnItem *item = SN_ITEM (object);
    g_debug ("SnItem dispose (%p)", object);

    if (item->png_path != NULL)
    {
        g_unlink (item->png_path);
        g_free (item->png_path);
        item->png_path = NULL;
    }

    if (item->last_png_path != NULL)
    {
        g_unlink (item->last_png_path);
        g_free (item->last_png_path);
        item->last_png_path = NULL;
    }

    g_clear_pointer (&item->sortable_name, g_free);
    g_clear_object (&item->status_icon);
    g_clear_object (&item->menu);
    g_clear_object (&item->prop_proxy);
    g_clear_object (&item->sn_item_proxy);

    G_OBJECT_CLASS (sn_item_parent_class)->dispose (object);
}

static void
sn_item_finalize (GObject *object)
{
    g_debug ("SnItem finalize (%p)", object);

    G_OBJECT_CLASS (sn_item_parent_class)->finalize (object);
}

static void
sn_item_class_init (SnItemClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = sn_item_dispose;
    gobject_class->finalize = sn_item_finalize;

}

static guint
lookup_ui_scale (void)
{
    GdkScreen *screen;
    GValue value = G_VALUE_INIT;
    guint scale = 1;

    g_value_init (&value, G_TYPE_UINT);

    screen = gdk_screen_get_default ();

    if (gdk_screen_get_setting (screen, "gdk-window-scaling-factor", &value))
    {
        scale = g_value_get_uint (&value);
    }

    return scale;
}

static gint
get_icon_id (SnItem *item)
{
    item->current_icon_id = (!item->current_icon_id);

    return item->current_icon_id;
}

static gint
get_icon_size (SnItem *item)
{
    gint size = 0;

    size = xapp_status_icon_get_icon_size (item->status_icon);

    if (size > 0)
    {
        return size;
    }

    return FALLBACK_ICON_SIZE;
}

static GVariant *
get_property (SnItem      *item,
              const gchar *prop_name)
{
    GVariant *res, *var;
    GError *error = NULL;

    res = g_dbus_proxy_call_sync (item->prop_proxy,
                                  "Get",
                                  g_variant_new ("(ss)",
                                                 g_dbus_proxy_get_interface_name (item->sn_item_proxy),
                                                 prop_name),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  5 * 1000,
                                  NULL,
                                  &error);

    if (error != NULL)
    {
        g_error_free (error);
        return NULL;
    }

    g_variant_get (res, "(v)", &var);
    g_variant_unref (res);

    return var;
}

static GVariant *
get_pixmap_property (SnItem               *item,
                     const gchar          *name)
{
    GVariant *var = NULL;

    var = get_property (item, name);

    if (var == NULL)
    {
        return NULL;
    }

    return var;
}

static gchar *
get_string_property (SnItem               *item,
                     const gchar          *name)
{
    GVariant *var = NULL;
    gchar *result = NULL;

    var = get_property (item, name);

    if (var == NULL)
    {
        return NULL;
    }

    result = g_variant_dup_string (var, NULL);
    g_variant_unref (var);

    if (g_strcmp0 (result, "") == 0)
    {
        g_clear_pointer (&result, g_free);
    }

    return result;
}

static cairo_surface_t *
surface_from_pixmap_data (gint          width,
                          gint          height,
                          GBytes       *bytes)
{
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    gint rowstride, i;
    gsize size;
    gconstpointer data;
    guchar *copy;
    guchar alpha;

    data = g_bytes_get_data (bytes, &size);
    copy = g_memdup ((guchar *) data, size);

    surface = NULL;
    rowstride = width * 4;
    i = 0;

    while (i < 4 * width * height)
    {
        alpha       = copy[i    ];
        copy[i    ] = copy[i + 1];
        copy[i + 1] = copy[i + 2];
        copy[i + 2] = copy[i + 3];
        copy[i + 3] = alpha;
        i += 4;
    }

    pixbuf = gdk_pixbuf_new_from_data (copy,
                                       GDK_COLORSPACE_RGB,
                                       TRUE, 8,
                                       width, height,
                                       rowstride,
                                       (GdkPixbufDestroyNotify) g_free,
                                       NULL);

    if (pixbuf)
    {
        guint scale = lookup_ui_scale ();

        surface = gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL);
        g_object_unref (pixbuf);

        return surface;
    }
}

static gboolean
process_pixmaps (SnItem    *item,
                 GVariant  *pixmaps,
                 gchar    **image_path)
{
    GVariantIter iter;
    cairo_surface_t *surface;
    gint width, height;
    gint largest_width, largest_height;
    GVariant *byte_array_var;
    GBytes *best_image_bytes = NULL;

    largest_width = largest_height = 0;

    g_variant_iter_init (&iter, pixmaps);

    while (g_variant_iter_loop (&iter, "(ii@ay)", &width, &height, &byte_array_var))
    {
        if (width > 0 & height > 0 &&
            ((width * height) > (largest_width * largest_height)))
        {
            gsize data_size = g_variant_get_size (byte_array_var);

            if (data_size == width * height * 4)
            {
                g_clear_pointer (&best_image_bytes, g_bytes_unref);

                largest_width = width;
                largest_height = height;
                best_image_bytes = g_variant_get_data_as_bytes (byte_array_var);
            }
        }
    }

    if (best_image_bytes == NULL)
    {
        g_warning ("No valid pixmaps found.");
        return FALSE;
    }

    surface = surface_from_pixmap_data (largest_width, largest_height, best_image_bytes);

    if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy (surface);
        return FALSE;
    }

    item->last_png_path = item->png_path;

    gchar *filename = g_strdup_printf ("xapp-tmp-%p-%d.png", item, get_icon_id (item));
    gchar *save_filename = g_build_path ("/", g_get_tmp_dir (), filename, NULL);
    g_free (filename);

    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    status = cairo_surface_write_to_png (surface, save_filename);

    if (status != CAIRO_STATUS_SUCCESS)
    {
        g_warning ("Failed to save png of status icon");
        g_free (image_path);
        cairo_surface_destroy (surface);
        return FALSE;
    }

    *image_path = save_filename;
    cairo_surface_destroy (surface);

    return TRUE;
}

static void
set_icon_from_pixmap (SnItem *item)
{
    GVariant *pixmaps;
    gchar *image_path;

    if (item->status == STATUS_ACTIVE)
    {
        pixmaps = get_pixmap_property (item, "IconPixmap");
    }
    else
    if (item->status == STATUS_NEEDS_ATTENTION)
    {
        pixmaps = get_pixmap_property (item, "AttentionIconPixmap");

        if (!pixmaps)
        {
            pixmaps = get_pixmap_property (item, "IconPixmap");
        }
    }

    if (!pixmaps)
    {
        xapp_status_icon_set_icon_name (item->status_icon, "image-missing");
        g_warning ("No pixmaps to use");
        return;
    }

    if (process_pixmaps (item, pixmaps, &image_path))
    {
        xapp_status_icon_set_icon_name (item->status_icon, image_path);
        g_free (image_path);
    }

    g_variant_unref (pixmaps);
}

static gchar *
get_icon_filename_from_theme (SnItem *item,
                              const gchar *theme_path,
                              const gchar *icon_name)
{
    GtkIconInfo *info;
    gchar *filename;
    const gchar *array[2];

    array[0] = icon_name;
    array[1] = NULL;

    // We have a theme path, but try the system theme first
    GtkIconTheme *theme = gtk_icon_theme_get_default ();

    info = gtk_icon_theme_choose_icon_for_scale (theme,
                                                 array,
                                                 get_icon_size (item),
                                                 lookup_ui_scale (),
                                                 GTK_ICON_LOOKUP_FORCE_SVG | GTK_ICON_LOOKUP_FORCE_SYMBOLIC);

    if (info == NULL)
    {
        // Make a temp theme based off of the provided path
        GtkIconTheme *theme = gtk_icon_theme_new ();

        gtk_icon_theme_prepend_search_path (theme, theme_path);

        info = gtk_icon_theme_choose_icon_for_scale (theme,
                                                     array,
                                                     get_icon_size (item),
                                                     lookup_ui_scale (),
                                                     GTK_ICON_LOOKUP_FORCE_SVG | GTK_ICON_LOOKUP_FORCE_SYMBOLIC);

        g_object_unref (theme);
    }

    if (info == NULL)
    {
        return NULL;
    }

    filename = g_strdup (gtk_icon_info_get_filename(info));
    g_object_unref (info);

    return filename;
}

static void
process_icon_name (SnItem *item,
                   const gchar *icon_theme_path,
                   const gchar *icon_name)
{
    if (g_path_is_absolute (icon_name) || !icon_theme_path)
    {
        xapp_status_icon_set_icon_name (item->status_icon, icon_name);
    }
    else
    {
        gchar *filename = get_icon_filename_from_theme (item, icon_theme_path, icon_name);

        if (filename != NULL)
        {
            xapp_status_icon_set_icon_name (item->status_icon, filename);
            g_free (filename);
        }
        else
        {
            xapp_status_icon_set_icon_name (item->status_icon, "image-missing");
        }
    }
}

static void
set_icon_name_or_path (SnItem *item,
                       const gchar *icon_theme_path,
                       const gchar *icon_name,
                       const gchar *att_icon_name,
                       const gchar *olay_icon_name)
{
    const gchar *name_to_use = NULL;

    if (item->status == STATUS_ACTIVE)
    {
        if (icon_name)
        {
            name_to_use = icon_name;
        }
    }
    else
    if (item->status == STATUS_NEEDS_ATTENTION)
    {
        if (att_icon_name)
        {
            name_to_use = att_icon_name;
        }
        else
        if (icon_name)
        {
            name_to_use = icon_name;
        }
    }

    if (name_to_use == NULL)
    {
        name_to_use = "image-missing";
    }

    process_icon_name (item, icon_theme_path, name_to_use);
}

static void
update_icon (SnItem *item)
{
    gchar *icon_theme_path;
    gchar *icon_name, *att_icon_name, *olay_icon_name;

    icon_theme_path = get_string_property (item, "IconThemePath");
    icon_name = get_string_property (item, "IconName");
    att_icon_name = get_string_property (item, "AttentionIconName");
    olay_icon_name = get_string_property (item, "OverlayIconName");

    if (icon_name || att_icon_name || olay_icon_name)
    {
        // g_printerr ("icon name '%s' '%s' '%s'\n", icon_name, att_icon_name, olay_icon_name);
        set_icon_name_or_path (item,
                               icon_theme_path,
                               icon_name,
                               att_icon_name,
                               olay_icon_name);
    }
    else
    {
        set_icon_from_pixmap (item);
    }

    g_free (icon_theme_path);
    g_free (icon_name);
    g_free (att_icon_name);
    g_free (olay_icon_name);
}

static void
update_menu (SnItem *item)
{
    gchar *menu_path;

    g_clear_object (&item->menu);

    xapp_status_icon_set_primary_menu (item->status_icon, NULL);
    xapp_status_icon_set_secondary_menu (item->status_icon, NULL);

    menu_path = get_string_property (item, "Menu");

    if (menu_path == NULL)
    {
        return;
    }

    item->menu = GTK_WIDGET (dbusmenu_gtkmenu_new ((gchar *) g_dbus_proxy_get_name (item->sn_item_proxy), menu_path));
    g_object_ref_sink (item->menu);

    if (item->is_ai && !should_activate (item))
    {
        xapp_status_icon_set_primary_menu (item->status_icon, GTK_MENU (item->menu));
    }

    xapp_status_icon_set_secondary_menu (item->status_icon, GTK_MENU (item->menu));

    g_free (menu_path);
}

static gchar *
capitalize (const gchar *string)
{
    gchar *utf8;
    gunichar first;
    gchar *remaining;
    gchar *ret;

    utf8 = g_utf8_make_valid (string, -1);

    first = g_utf8_get_char (utf8);
    first = g_unichar_toupper (first);

    remaining = g_utf8_substring (utf8, 1, g_utf8_strlen (utf8, -1));

    ret = g_strdup_printf ("%s%s", (gchar *) &first, remaining);

    g_free (utf8);
    g_free (remaining);

    return ret;
}

static void
update_tooltip (SnItem *item)
{
    g_autoptr(GVariant) tt_var;

    if (item->is_ai)
    {
        gchar *text;

        text = get_string_property (item, "XAyatanaLabel");

        if (text)
        {
            xapp_status_icon_set_tooltip_text (item->status_icon, text);
            g_debug ("Tooltip text from XAyatanaLabel: %s", text);

            g_free (text);
            return;
        }
    }

    tt_var = get_property (item, "ToolTip");

    if (tt_var)
    {
        const gchar *type_str;
        type_str = g_variant_get_type_string (tt_var);

        if (g_strcmp0 (type_str, "(sa(iiay)ss)") == 0)
        {
            const gchar *tooltip_title, *tooltip_body;

            g_variant_get (tt_var, "(sa(iiay)&s&s)", NULL, NULL, &tooltip_title, &tooltip_body);

            if (g_strcmp0 (tooltip_title, "") != 0)
            {

                if (g_strcmp0 (tooltip_body, "") != 0)
                {
                    gchar *text;
                    text = g_strdup_printf ("%s\n%s", tooltip_title, tooltip_body);

                    xapp_status_icon_set_tooltip_text (item->status_icon, text);
                    g_debug ("Tooltip text from ToolTip: %s", text);

                    g_free (text);
                }
                else
                {
                    g_debug ("Tooltip text from ToolTip: %s", tooltip_title);
                    xapp_status_icon_set_tooltip_text (item->status_icon, tooltip_title);
                }

                return;
            }
        }
    }

    gchar *title_string;
    title_string = get_string_property (item, "Title");

    if (title_string != NULL)
    {
        gchar *capped_string;

        capped_string = capitalize (title_string);
        xapp_status_icon_set_tooltip_text (item->status_icon, capped_string);
        g_debug ("Tooltip text from Title: %s", capped_string);

        g_free (title_string);
        g_free (capped_string);
        return;
    }

    xapp_status_icon_set_tooltip_text (item->status_icon, "");
}

static void
update_status (SnItem *item)
{
    Status old_status;
    gchar *status;

    old_status = item->status;

    status = get_string_property (item, "Status");

    if (g_strcmp0 (status, "Passive") == 0)
    {
        item->status = STATUS_PASSIVE;
        xapp_status_icon_set_visible (item->status_icon, FALSE);
    }
    else if (g_strcmp0 (status, "NeedsAttention") == 0)
    {
        item->status = STATUS_NEEDS_ATTENTION;
        xapp_status_icon_set_visible (item->status_icon, TRUE);
    }
    else
    {
        item->status = STATUS_ACTIVE;
        xapp_status_icon_set_visible (item->status_icon, TRUE);
    }

    g_free (status);

    if (old_status != item->status)
    {
        update_icon (item);
    }
}

static void
sn_signal_received (GDBusProxy  *sn_item_proxy,
                    const gchar *sender_name,
                    const gchar *signal_name,
                    GVariant    *parameters,
                    gpointer     user_data)
{
    SnItem *item = SN_ITEM (user_data);

    if (item->prop_proxy == NULL)
    {
        return;
    }

    if (g_strcmp0 (signal_name, "NewIcon") == 0 ||
        g_strcmp0 (signal_name, "NewAttentionIcon") == 0 ||
        g_strcmp0 (signal_name, "NewOverlayIcon") == 0)
    {
        update_icon (item);
    }
    else
    if (g_strcmp0 (signal_name, "NewStatus") == 0)
    {
        update_status (item); // This will update_icon(item) also.
    }
    else
    if (g_strcmp0 (signal_name, "NewMenu") == 0)
    {
        update_menu (item);
    }
    else
    if (g_strcmp0 (signal_name, "XAyatanaNewLabel") ||
        g_strcmp0 (signal_name, "NewToolTip") ||
        g_strcmp0 (signal_name, "NewTitle"))
    {
        update_tooltip (item);
    }
}

static void
xapp_icon_activated (XAppStatusIcon *status_icon,
                     guint           button,
                     guint           _time,
                     gpointer        user_data)
{
}

static void
xapp_icon_button_press (XAppStatusIcon *status_icon,
                        gint            x,
                        gint            y,
                        guint           button,
                        guint           _time,
                        gint            panel_position,
                        gpointer        user_data)
{
    SnItem *item = SN_ITEM (user_data);

    if (button == GDK_BUTTON_PRIMARY)
    {
        if (item->is_ai)
        {
            if (should_activate (item))
            {
                sn_item_interface_call_secondary_activate (SN_ITEM_INTERFACE (item->sn_item_proxy), x, y, NULL, NULL, NULL);
                return;
            }
        } else
        {
            sn_item_interface_call_activate (SN_ITEM_INTERFACE (item->sn_item_proxy), x, y, NULL, NULL, NULL);
        }
    }
    else
    if (button == GDK_BUTTON_MIDDLE)
    {
        sn_item_interface_call_secondary_activate (SN_ITEM_INTERFACE (item->sn_item_proxy), x, y, NULL, NULL, NULL);
    }
}

static void
xapp_icon_button_release (XAppStatusIcon *status_icon,
                          gint            x,
                          gint            y,
                          guint           button,
                          guint           _time,
                          gint            panel_position,
                          gpointer        user_data)
{
    SnItem *item = SN_ITEM (user_data);

    if (button == GDK_BUTTON_SECONDARY && item->menu == NULL)
    {
        sn_item_interface_call_context_menu (SN_ITEM_INTERFACE (item->sn_item_proxy), x, y, NULL, NULL, NULL);
    }
}

static void
xapp_icon_scroll (XAppStatusIcon      *status_icon,
                  gint                 delta,
                  XAppScrollDirection  dir,
                  guint                _time,
                  gpointer             user_data)
{
    SnItem *item = SN_ITEM (user_data);

    switch (dir)
    {
        case XAPP_SCROLL_LEFT:
        case XAPP_SCROLL_RIGHT:
            sn_item_interface_call_scroll (SN_ITEM_INTERFACE (item->sn_item_proxy), delta, "horizontal", NULL, NULL, NULL);
            break;
        case XAPP_SCROLL_UP:
        case XAPP_SCROLL_DOWN:
            sn_item_interface_call_scroll (SN_ITEM_INTERFACE (item->sn_item_proxy), delta, "vertical", NULL, NULL, NULL);
            break;
    }
}

static void
xapp_icon_state_changed (XAppStatusIcon      *status_icon,
                         XAppStatusIconState  new_state,
                         gpointer             user_data)
{
    SnItem *item = SN_ITEM (user_data);

    if (new_state == XAPP_STATUS_ICON_STATE_NO_SUPPORT)
    {
        return;
    }

    update_icon (item);
    update_status (item);
    update_icon (item);
    update_tooltip (item);
}

static void
assign_sortable_name (SnItem         *item,
                      XAppStatusIcon *status_icon)
{
    gchar *sortable_name;

    sortable_name = sn_item_interface_dup_id (SN_ITEM_INTERFACE (item->sn_item_proxy));

    if (sortable_name == NULL)
    {
        sortable_name = get_string_property (item, "Title");
    }

    g_debug ("Sort name for %s is '%s'", g_dbus_proxy_get_name (G_DBUS_PROXY (item->sn_item_proxy)), sortable_name);
    xapp_status_icon_set_name (status_icon, sortable_name);

    item->sortable_name = sortable_name;
}

static void
property_proxy_acquired (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    SnItem *item = SN_ITEM (user_data);
    GError *error = NULL;

    item->prop_proxy = g_dbus_proxy_new_finish (res, &error);

    if (error != NULL)
    {
        g_printerr ("Could not get prop proxy: %s\n", error->message);
        g_error_free (error);
        return;
    }

    g_signal_connect (item->sn_item_proxy,
                      "g-signal",
                      G_CALLBACK (sn_signal_received),
                      item);

    item->status_icon = xapp_status_icon_new ();

    g_signal_connect (item->status_icon, "activate", G_CALLBACK (xapp_icon_activated), item);
    g_signal_connect (item->status_icon, "button-press-event", G_CALLBACK (xapp_icon_button_press), item);
    g_signal_connect (item->status_icon, "button-release-event", G_CALLBACK (xapp_icon_button_release), item);
    g_signal_connect (item->status_icon, "scroll-event", G_CALLBACK (xapp_icon_scroll), item);
    g_signal_connect (item->status_icon, "state-changed", G_CALLBACK (xapp_icon_state_changed), item);

    assign_sortable_name (item, item->status_icon);

    update_status (item);
    update_menu (item);
    update_tooltip (item);
    update_icon (item);
}

static void
initialize_item (SnItem *item)
{
    g_dbus_proxy_new (g_dbus_proxy_get_connection (item->sn_item_proxy),
                      G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                      NULL,
                      g_dbus_proxy_get_name (item->sn_item_proxy),
                      g_dbus_proxy_get_object_path (item->sn_item_proxy),
                      "org.freedesktop.DBus.Properties",
                      NULL,
                      property_proxy_acquired,
                      item);
}

SnItem *
sn_item_new (GDBusProxy *sn_item_proxy,
             gboolean    is_ai)
{
    SnItem *item = g_object_new (sn_item_get_type (), NULL);

    item->sn_item_proxy = sn_item_proxy;
    item->is_ai = is_ai;

    initialize_item (item);
    return item;
}

void
sn_item_update_menus (SnItem *item)
{
    update_menu (item);
}
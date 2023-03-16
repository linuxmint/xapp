
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
#include <libxapp/xapp-util.h>
#include <libdbusmenu-gtk/menu.h>

#include "sn-item-interface.h"
#include "sn-item.h"

#define DEBUG_FLAG XAPP_DEBUG_SN_WATCHER
#include <libxapp/xapp-debug.h>

#define FALLBACK_ICON_SIZE 24

typedef enum
{
    STATUS_PASSIVE,
    STATUS_ACTIVE,
    STATUS_NEEDS_ATTENTION
} Status;

typedef struct
{
    gchar               *id;
    gchar               *title;
    gchar               *status;
    gchar               *tooltip_heading;
    gchar               *tooltip_body;
    gchar               *menu_path;

    gchar               *icon_theme_path;
    gchar               *icon_name;
    gchar               *attention_icon_name;
    gchar               *overlay_icon_name;

    gchar               *icon_md5;
    gchar               *attention_icon_md5;
    gchar               *overlay_icon_md5;
    cairo_surface_t     *icon_surface;
    cairo_surface_t     *attention_icon_surface;
    cairo_surface_t     *overlay_icon_surface;

    gboolean             update_status;
    gboolean             update_tooltip;
    gboolean             update_menu;
    gboolean             update_icon;
    gboolean             update_id;
} SnItemPropertiesResult;

struct _SnItem
{
    GObject parent_instance;

    GDBusProxy *sn_item_proxy; // SnItemProxy
    GDBusProxy *prop_proxy; // dbus properties (we can't trust SnItemProxy)

    XAppStatusIcon *status_icon;

    SnItemPropertiesResult *current_props;
    GCancellable *cancellable;

    Status status;

    gchar *png_paths[2];

    gint current_icon_id;

    guint update_properties_timeout;
    gchar *sortable_name;

    gboolean should_activate;
    gboolean should_replace_tooltip;
    gboolean is_ai;

    GtkWidget *menu;
};

G_DEFINE_TYPE (SnItem, sn_item, G_TYPE_OBJECT)

static void update_menu (SnItem *item, SnItemPropertiesResult *new_props);
static void update_status (SnItem *item, SnItemPropertiesResult *new_props);
static void update_tooltip (SnItem *item, SnItemPropertiesResult *new_props);
static void update_icon (SnItem *item, SnItemPropertiesResult *new_props);
static void assign_sortable_name (SnItem *item, const gchar *title);

static void
props_free (SnItemPropertiesResult *props)
{
    if (props == NULL)
    {
        return;
    }

    g_free (props->id);
    g_free (props->title);
    g_free (props->status);
    g_free (props->tooltip_heading);
    g_free (props->tooltip_body);
    g_free (props->menu_path);
    g_free (props->icon_theme_path);
    g_free (props->icon_name);
    g_free (props->attention_icon_name);
    g_free (props->overlay_icon_name);

    g_free (props->icon_md5);
    g_free (props->attention_icon_md5);
    g_free (props->overlay_icon_md5);

    cairo_surface_destroy (props->icon_surface);
    cairo_surface_destroy (props->attention_icon_surface);
    cairo_surface_destroy (props->overlay_icon_surface);

    g_slice_free (SnItemPropertiesResult, props);
}

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

static gboolean
should_replace_tooltip (SnItem *item)
{
    gboolean should;

    gchar **ids = g_settings_get_strv (xapp_settings,
                                       REPLACE_TOOLTIP_KEY);

    should = g_strv_contains ((const gchar * const *) ids, item->sortable_name);
    g_strfreev (ids);

    return should;
}

static void
update_conditionals (SnItem *item)
{
    item->should_replace_tooltip = should_replace_tooltip (item);
    item->should_activate = should_activate (item);
}

static void
sn_item_init (SnItem *self)
{
}

static void
sn_item_dispose (GObject *object)
{
    SnItem *item = SN_ITEM (object);
    DEBUG ("SnItem dispose (%p)", object);

    g_clear_handle_id (&item->update_properties_timeout, g_source_remove);

    g_clear_pointer (&item->sortable_name, g_free);
    g_clear_object (&item->status_icon);
    g_clear_object (&item->prop_proxy);
    g_clear_object (&item->sn_item_proxy);
    g_clear_object (&item->cancellable);

    props_free (item->current_props);

    G_OBJECT_CLASS (sn_item_parent_class)->dispose (object);
}

static void
sn_item_finalize (GObject *object)
{
    DEBUG ("SnItem finalize (%p)", object);
    SnItem *item = SN_ITEM (object);

    g_unlink (item->png_paths[0]);
    g_free (item->png_paths[0]);
    g_unlink (item->png_paths[1]);
    g_free (item->png_paths[1]);

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

static gchar *
get_temp_file (SnItem *item)
{
    gchar *filename;
    gchar *full_path;

    filename = g_strdup_printf ("xapp-tmp-%p-%d.png", item, get_icon_id (item));
    full_path = g_build_filename (xapp_get_tmp_dir (), filename, NULL);
    g_free (filename);

    return full_path;
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

static cairo_surface_t *
surface_from_pixmap_data (gint          width,
                          gint          height,
                          guchar       *data)
{
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    gint rowstride, i;
    guchar alpha;

    surface = NULL;
    rowstride = width * 4;
    i = 0;

    while (i < 4 * width * height)
    {
        alpha       = data[i    ];
        data[i    ] = data[i + 1];
        data[i + 1] = data[i + 2];
        data[i + 2] = data[i + 3];
        data[i + 3] = alpha;
        i += 4;
    }

    pixbuf = gdk_pixbuf_new_from_data (data,
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

    return NULL;
}

static cairo_surface_t *
get_icon_surface (SnItem    *item,
                  GVariant  *pixmaps,
                  gchar    **md5)
{
    GVariantIter *iter;
    cairo_surface_t *surface;
    gint width, height;
    gint largest_width, largest_height;
    gsize largest_data_size;
    GVariant *byte_array_var;
    gconstpointer data;
    guchar *best_image_array = NULL;

    largest_width = largest_height = 0;

    *md5 = NULL;

    g_variant_get (pixmaps, "a(iiay)", &iter);

    if (iter == NULL)
    {
        return NULL;
    }

    while (g_variant_iter_loop (iter, "(ii@ay)", &width, &height, &byte_array_var))
    {
        if (width > 0 && height > 0 && byte_array_var != NULL &&
            ((width * height) > (largest_width * largest_height)))
        {
            gsize data_size = g_variant_get_size (byte_array_var);

            if (data_size == width * height * 4)
            {
                data = g_variant_get_data (byte_array_var);

                if (data != NULL)
                {
                    if (best_image_array != NULL)
                    {
                        g_free (best_image_array);
                    }

                    best_image_array = g_memdup (data, data_size);

                    largest_data_size = data_size;
                    largest_width = width;
                    largest_height = height;
                }
            }
        }
    }

    g_variant_iter_free (iter);

    if (best_image_array == NULL)
    {
        return NULL;
    }

    surface = surface_from_pixmap_data (largest_width, largest_height, best_image_array);
    *md5 = g_compute_checksum_for_data (G_CHECKSUM_MD5, best_image_array, largest_data_size);

    if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy (surface);
        return NULL;
    }

    return surface;
}

static void
set_icon_from_pixmap (SnItem *item, SnItemPropertiesResult *new_props)
{
    cairo_surface_t *surface;

    DEBUG ("Trying to use icon pixmap for %s",
             item->sortable_name);

    surface = NULL;

    if (item->status == STATUS_ACTIVE)
    {
        if (new_props->icon_surface)
        {
            surface = new_props->icon_surface;
        }
    }
    else
    if (item->status == STATUS_NEEDS_ATTENTION)
    {
        if (new_props->attention_icon_surface)
        {
            surface = new_props->attention_icon_surface;
        }
        else
        if (new_props->icon_surface)
        {
            surface = new_props->icon_surface;
        }
    }

    if (surface != NULL)
    {
        const gchar *current_png_path = item->png_paths[get_icon_id (item)];

        cairo_status_t status = CAIRO_STATUS_SUCCESS;
        status = cairo_surface_write_to_png (surface, current_png_path);

        DEBUG ("Saving tmp image file for '%s' to '%s'", item->sortable_name, current_png_path);

        if (status != CAIRO_STATUS_SUCCESS)
        {
            g_warning ("Failed to save png of status icon");
        }

        xapp_status_icon_set_icon_name (item->status_icon, current_png_path);
        return;
    }

    DEBUG ("No pixmaps to use");
    xapp_status_icon_set_icon_name (item->status_icon, "image-missing");
}

static gchar *
get_name_or_path_from_theme (SnItem      *item,
                             const gchar *theme_path,
                             const gchar *icon_name)
{
    GtkIconInfo *info;
    gchar *filename;
    const gchar *array[2];
    gint host_icon_size;

    array[0] = icon_name;
    array[1] = NULL;

    // We may have a theme path, but try the system theme first
    GtkIconTheme *theme = gtk_icon_theme_get_default ();
    host_icon_size = get_icon_size (item);

    info = gtk_icon_theme_choose_icon_for_scale (theme,
                                                 array,
                                                 host_icon_size,
                                                 lookup_ui_scale (),
                                                 GTK_ICON_LOOKUP_FORCE_SVG | GTK_ICON_LOOKUP_FORCE_SYMBOLIC);

    if (info != NULL)
    {
        // If the icon is found in the system theme, we can just pass along the icon name
        // as is, this way symbolics work properly.
        g_object_unref (info);
        return g_strdup (icon_name);
    }

    if (theme_path != NULL)
    {
        // Make a temp theme based off of the provided path
        GtkIconTheme *theme = gtk_icon_theme_new ();

        gtk_icon_theme_prepend_search_path (theme, theme_path);

        info = gtk_icon_theme_choose_icon_for_scale (theme,
                                                     array,
                                                     host_icon_size,
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

static gboolean
set_icon_name (SnItem      *item,
               const gchar *icon_theme_path,
               const gchar *icon_name)
{
    DEBUG ("Checking for icon name for %s - theme path: '%s', icon name: '%s'",
             item->sortable_name,
             icon_theme_path,
             icon_name);

    if (icon_name == NULL)
    {
        return FALSE;
    }

    if (g_path_is_absolute (icon_name))
    {
        xapp_status_icon_set_icon_name (item->status_icon, icon_name);
        return TRUE;
    }
    else
    {
        gchar *used_name = get_name_or_path_from_theme (item, icon_theme_path, icon_name);

        if (used_name != NULL)
        {
            xapp_status_icon_set_icon_name (item->status_icon, used_name);
            g_free (used_name);
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
set_icon_name_or_path (SnItem                 *item,
                       SnItemPropertiesResult *new_props)
{
    const gchar *name_to_use = NULL;

    // Set an icon here, even if we're passive (hidden) - eventually only the
    // status property might change, but we wouldn't have an icon then (unless
    // the app sets the icon at the same time).
    if (item->status == STATUS_ACTIVE || item->status == STATUS_PASSIVE)
    {
        if (new_props->icon_name)
        {
            name_to_use = new_props->icon_name;
        }
    }
    else
    if (item->status == STATUS_NEEDS_ATTENTION)
    {
        if (new_props->attention_icon_name)
        {
            name_to_use = new_props->attention_icon_name;
        }
        else
        if (new_props->icon_name)
        {
            name_to_use = new_props->icon_name;
        }
    }

    return set_icon_name (item, new_props->icon_theme_path, name_to_use);
}

static void
update_icon (SnItem *item, SnItemPropertiesResult *new_props)
{
    if (!set_icon_name_or_path (item, new_props))
    {
        set_icon_from_pixmap (item, new_props);
    }
}

static void
update_menu (SnItem *item, SnItemPropertiesResult *new_props)
{
    DEBUG ("Possible new menu for '%s' - current path: '%s', new: '%s'",
           item->sortable_name, item->current_props->menu_path, new_props->menu_path);

    xapp_status_icon_set_primary_menu (item->status_icon, NULL);
    xapp_status_icon_set_secondary_menu (item->status_icon, NULL);
    g_clear_object (&item->menu);

    if (new_props->menu_path == NULL)
    {
        return;
    }

    if (g_strcmp0 (new_props->menu_path, "/NO_DBUSMENU") == 0)
    {
        DEBUG ("No menu set for '%s' (/NO_DBUSMENU)", item->sortable_name);
        return;
    }

    item->menu = GTK_WIDGET (dbusmenu_gtkmenu_new ((gchar *) g_dbus_proxy_get_name (item->sn_item_proxy),
                                                   new_props->menu_path));
    g_object_ref_sink (item->menu);

    if (item->is_ai && !item->should_activate)
    {
        xapp_status_icon_set_primary_menu (item->status_icon, GTK_MENU (item->menu));
    }

    xapp_status_icon_set_secondary_menu (item->status_icon, GTK_MENU (item->menu));
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
update_tooltip (SnItem *item, SnItemPropertiesResult *new_props)
{
    if (new_props->title)
    {
        assign_sortable_name (item, new_props->title);
    }

    if (!item->should_replace_tooltip)
    {
        if (new_props->tooltip_heading != NULL)
        {
            if (new_props->tooltip_body != NULL)
            {
                gchar *text;
                text = g_strdup_printf ("%s\n%s",
                                        new_props->tooltip_heading,
                                        new_props->tooltip_body);

                xapp_status_icon_set_tooltip_text (item->status_icon, text);
                DEBUG ("Tooltip text for '%s' from ToolTip: %s",
                         item->sortable_name,
                         text);

                g_free (text);
            }
            else
            {
                DEBUG ("Tooltip text for '%s' from ToolTip: %s",
                         item->sortable_name,
                         new_props->tooltip_heading);

                xapp_status_icon_set_tooltip_text (item->status_icon, new_props->tooltip_heading);
            }

            return;
        }
    }

    if (new_props->title != NULL)
    {
        gchar *capped_string;

        capped_string = capitalize (new_props->title);
        xapp_status_icon_set_tooltip_text (item->status_icon, capped_string);

        DEBUG ("Tooltip text for '%s' from Title: %s",
                 item->sortable_name,
                 capped_string);

        g_free (capped_string);
        return;
    }

    xapp_status_icon_set_tooltip_text (item->status_icon, "");
}

static void
update_status (SnItem *item, SnItemPropertiesResult *new_props)
{
    if (g_strcmp0 (new_props->status, "Passive") == 0)
    {
        item->status = STATUS_PASSIVE;
        xapp_status_icon_set_visible (item->status_icon, FALSE);
    }
    else
    if (g_strcmp0 (new_props->status, "NeedsAttention") == 0)
    {
        item->status = STATUS_NEEDS_ATTENTION;
        xapp_status_icon_set_visible (item->status_icon, TRUE);
    }
    else
    {
        item->status = STATUS_ACTIVE;
        xapp_status_icon_set_visible (item->status_icon, TRUE);
    }

    DEBUG ("Status for '%s' is now '%s'", item->sortable_name, new_props->status);
}

static gchar *
null_or_string_from_string (const gchar *str)
{
    if (str != NULL && strlen(str) > 0)
    {
        return g_strdup (str);
    }
    else
    {
        return NULL;
    }
}

static gchar *
null_or_string_from_variant (GVariant *variant)
{
    const gchar *str;

    str = g_variant_get_string (variant, NULL);

    return null_or_string_from_string (str);
}

static void
get_all_properties_callback (GObject      *source_object,
                             GAsyncResult *res,
                             gpointer      user_data)
{
    SnItem *item = SN_ITEM (user_data);
    SnItemPropertiesResult *new_props;
    GError       *error = NULL;
    GVariant     *properties;
    GVariantIter *iter = NULL;
    const gchar  *name;
    GVariant     *value;
    properties = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

    if (error != NULL)
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
            !g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD))
        {
            g_critical ("Could not get properties for %s: %s\n",
                        g_dbus_proxy_get_name (item->sn_item_proxy),
                        error->message);
        }

        g_error_free (error);
        return;
    }

    if (properties == NULL)
    {
        return;
    }

    new_props = g_slice_new0 (SnItemPropertiesResult);

    g_variant_get (properties, "(a{sv})", &iter);

    while (g_variant_iter_loop (iter, "{&sv}", &name, &value))
    {
        if (g_strcmp0 (name, "Title") == 0)
        {
            new_props->title = null_or_string_from_variant (value);

            if (g_strcmp0 (new_props->title, item->current_props->title) != 0)
            {
                new_props->update_tooltip = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "Status") == 0)
        {
            new_props->status = null_or_string_from_variant (value);

            if (g_strcmp0 (new_props->status, item->current_props->status) != 0)
            {
                new_props->update_status = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "ToolTip") == 0)
        {
            const gchar *ts;

            ts = g_variant_get_type_string (value);

            if (g_strcmp0 (ts, "(sa(iiay)ss)") == 0)
            {
                gchar *heading, *body;

                g_variant_get (value, "(sa(iiay)ss)", NULL, NULL,
                               &heading,
                               &body);

                new_props->tooltip_heading = null_or_string_from_string (heading);
                new_props->tooltip_body = null_or_string_from_string (body);

                g_free (heading);
                g_free (body);
            }
            else
            if (g_strcmp0 (ts, "s") == 0)
            {
                new_props->tooltip_body = null_or_string_from_variant (value);
            }

            if (g_strcmp0 (new_props->tooltip_heading, item->current_props->tooltip_heading) != 0 ||
                g_strcmp0 (new_props->tooltip_body, item->current_props->tooltip_body) != 0)
            {
                new_props->update_tooltip = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "Menu") == 0)
        {
            new_props->menu_path = null_or_string_from_variant (value);

            if (g_strcmp0 (new_props->menu_path, item->current_props->menu_path) != 0)
            {
                new_props->update_menu = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "IconThemePath") == 0)
        {
            new_props->icon_theme_path = null_or_string_from_variant (value);

            if (g_strcmp0 (new_props->icon_theme_path, item->current_props->icon_theme_path) != 0)
            {
                new_props->update_icon = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "IconName") == 0)
        {
            new_props->icon_name = null_or_string_from_variant (value);
            if (g_strcmp0 (new_props->icon_name, item->current_props->icon_name) != 0)
            {
                new_props->update_icon = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "IconPixmap") == 0)
        {
            new_props->icon_surface = get_icon_surface (item, value, &new_props->icon_md5);

            if (g_strcmp0 (new_props->icon_md5, item->current_props->icon_md5) != 0)
            {
                new_props->update_icon = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "AttentionIconName") == 0)
        {
            new_props->attention_icon_name = null_or_string_from_variant (value);

            if (g_strcmp0 (new_props->attention_icon_name, item->current_props->attention_icon_name) != 0)
            {
                new_props->update_icon = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "AttentionIconPixmap") == 0)
        {
            new_props->attention_icon_surface = get_icon_surface (item, value, &new_props->attention_icon_md5);

            if (g_strcmp0 (new_props->attention_icon_md5, item->current_props->attention_icon_md5) != 0)
            {
                new_props->update_icon = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "OverlayIconName") == 0)
        {
            new_props->overlay_icon_name = null_or_string_from_variant (value);

            if (g_strcmp0 (new_props->overlay_icon_name, item->current_props->overlay_icon_name) != 0)
            {
                new_props->update_icon = TRUE;
            }
        }
        else
        if (g_strcmp0 (name, "OverlayIconPixmap") == 0)
        {
            new_props->overlay_icon_surface = get_icon_surface (item, value, &new_props->overlay_icon_md5);

            if (g_strcmp0 (new_props->overlay_icon_md5, item->current_props->overlay_icon_md5) != 0)
            {
                new_props->update_icon = TRUE;
            }
        }
        if (g_strcmp0 (name, "Id") == 0)
        {
            new_props->id = null_or_string_from_variant (value);
            if (g_strcmp0 (new_props->id, item->current_props->id) != 0)
            {
                new_props->update_id = TRUE;
            }
        }
    }

    g_variant_iter_free (iter);
    g_variant_unref (properties);

    if (new_props->update_status)
    {
        update_status (item, new_props);
    }

    if (new_props->update_tooltip)
    {
        update_tooltip (item, new_props);
    }

    if (new_props->update_menu)
    {
        update_menu (item, new_props);
    }

    if (new_props->update_icon || new_props->update_status)
    {
        update_icon (item, new_props);
    }
    if ((new_props->update_id || new_props->update_status) && !new_props->update_tooltip)
    {
        assign_sortable_name (item, new_props->id);
    }

    props_free (item->current_props);
    item->current_props = new_props;
}

static gboolean
update_all_properties (gpointer data)
{
    SnItem *item = SN_ITEM (data);

    g_dbus_proxy_call (item->prop_proxy,
                       "GetAll",
                       g_variant_new ("(s)",
                                      g_dbus_proxy_get_interface_name (item->sn_item_proxy)),
                       G_DBUS_CALL_FLAGS_NONE,
                       5 * 1000,
                       item->cancellable,
                       get_all_properties_callback,
                       item);

    item->update_properties_timeout = 0;

    return G_SOURCE_REMOVE;
}

static void
queue_update_properties (SnItem *item, gboolean force)
{
    if (item->update_properties_timeout > 0)
    {
        g_source_remove (item->update_properties_timeout);
    }

    if (force)
    {
        props_free (item->current_props);
        item->current_props = g_slice_new0 (SnItemPropertiesResult);
    }

    item->update_properties_timeout = g_timeout_add (10, G_SOURCE_FUNC (update_all_properties), item);
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

    DEBUG ("Signal received from StatusNotifierItem: %s", signal_name);

    if (g_strcmp0 (signal_name, "NewIcon") == 0 ||
        g_strcmp0 (signal_name, "Id") == 0 ||
        g_strcmp0 (signal_name, "NewAttentionIcon") == 0 ||
        g_strcmp0 (signal_name, "NewOverlayIcon") == 0 ||
        g_strcmp0 (signal_name, "NewToolTip") == 0 ||
        g_strcmp0 (signal_name, "NewTitle") == 0 ||
        g_strcmp0 (signal_name, "NewStatus") == 0 ||
        g_strcmp0 (signal_name, "NewMenu") == 0)
    {
        queue_update_properties (item, FALSE);
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
            if (item->should_activate)
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

    queue_update_properties (item, TRUE);
}

static void
assign_sortable_name (SnItem         *item,
                      const gchar    *id)
{
    gchar *init_name, *normalized;
    gchar *sortable_name, *old_sortable_name;

    if (id != NULL)
    {
        init_name = g_strdup (id);
    }
    else
    {
        init_name = g_strdup (g_dbus_proxy_get_name (G_DBUS_PROXY (item->sn_item_proxy)));
    }

    normalized = g_utf8_normalize (init_name,
                                   -1,
                                   G_NORMALIZE_DEFAULT);

    sortable_name = g_utf8_strdown (normalized, -1);

    g_free (init_name);
    g_free (normalized);

    if (g_strcmp0 (sortable_name, item->sortable_name) == 0)
    {
        g_free (sortable_name);
        return;
    }

    DEBUG ("Sort name for '%s' is '%s'",
             g_dbus_proxy_get_name (G_DBUS_PROXY (item->sn_item_proxy)),
             sortable_name);

    xapp_status_icon_set_name (item->status_icon, sortable_name);

    old_sortable_name = item->sortable_name;
    item->sortable_name = sortable_name;
    g_free (old_sortable_name);

    update_conditionals (item);
}

static void
property_proxy_acquired (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    SnItem *item = SN_ITEM (user_data);
    GError *error = NULL;
    gchar *json = NULL;

    item->prop_proxy = g_dbus_proxy_new_finish (res, &error);

    if (error != NULL)
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            g_critical ("Could not get property proxy for %s: %s\n",
                        g_dbus_proxy_get_name (item->sn_item_proxy),
                        error->message);
            g_error_free (error);
            return;
        }
    }

    g_signal_connect (item->sn_item_proxy,
                      "g-signal",
                      G_CALLBACK (sn_signal_received),
                      item);

    item->status_icon = xapp_status_icon_new ();

    json = g_strdup_printf ("{ \"highlight-both-menus\": %s }", item->is_ai ? "true" : "false");
    xapp_status_icon_set_metadata (item->status_icon, json);
    g_free (json);

    g_signal_connect (item->status_icon, "activate", G_CALLBACK (xapp_icon_activated), item);
    g_signal_connect (item->status_icon, "button-press-event", G_CALLBACK (xapp_icon_button_press), item);
    g_signal_connect (item->status_icon, "button-release-event", G_CALLBACK (xapp_icon_button_release), item);
    g_signal_connect (item->status_icon, "scroll-event", G_CALLBACK (xapp_icon_scroll), item);
    g_signal_connect (item->status_icon, "state-changed", G_CALLBACK (xapp_icon_state_changed), item);

    assign_sortable_name (item, NULL);
    update_conditionals (item);

    queue_update_properties (item, TRUE);
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
                      item->cancellable,
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
    item->cancellable = g_cancellable_new ();

    item->png_paths[0] = get_temp_file (item);
    item->png_paths[1] = get_temp_file (item);

    initialize_item (item);
    return item;
}


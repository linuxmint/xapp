
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <cairo.h>

#include <libgnomekbd/gkbd-configuration.h>

#include "xapp-kbd-layout-controller.h"

enum
{
  PROP_0,

  PROP_ENABLED,
  PROP_USE_CAPS,
};

enum
{
  KBD_LAYOUT_CHANGED,
  KBD_CONFIG_CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

struct _XAppKbdLayoutControllerPrivate
{
    GkbdConfiguration *config;

    gint num_groups;
    gchar *flag_dir;
    gchar *temp_flag_theme_dir;

    gchar *icon_names[4];
    gchar *text_store[4];

    gulong changed_id;
    gulong group_changed_id;
    guint idle_changed_id;

    gboolean enabled;
};

G_DEFINE_TYPE (XAppKbdLayoutController, xapp_kbd_layout_controller, G_TYPE_OBJECT);

static void
initialize_flag_dir (XAppKbdLayoutController *controller)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;
    gint i;

    const char * const * data_dirs;

    data_dirs = g_get_system_data_dirs ();

    for (i = 0; data_dirs[i] != NULL; i++)
    {
        gchar *try_path = g_build_filename (data_dirs[i], "xapps", "flags", NULL);

        if (g_file_test (try_path, G_FILE_TEST_EXISTS))
        {
            priv->flag_dir = g_strdup (try_path);
            break;
        }

        g_free (try_path);
    }
}

static void
initialize_icon_theme (XAppKbdLayoutController *controller)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    const gchar *cache_dir = g_get_user_cache_dir ();

    gchar *path = g_build_filename (cache_dir, "xapp-kbd-layout-controller", NULL);

    g_mkdir_with_parents (path, 0700);

    priv->temp_flag_theme_dir = path;

    gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (), path);
}

static void
clear_stores (XAppKbdLayoutController *controller)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    gint i;

    for (i = 0; i < 4; i++)
    {
        g_clear_pointer (&priv->text_store[i], g_free);
        g_clear_pointer (&priv->icon_names[i], g_free);
    }
}

typedef struct
{
    gchar *group;
    gint id;
} GroupData;

static void
group_data_free (GroupData *data)
{
    g_clear_pointer (&data->group, g_free);
    data->id = 0;

    g_slice_free (GroupData, data);
}

static gchar *
create_text (XAppKbdLayoutController *controller,
             const gchar             *name,
             gint                     id)
{
    if (g_utf8_validate (name, -1, NULL))
    {
        gchar utf8[20];
        gchar *utf8_cased = NULL;

        g_utf8_strncpy (utf8, name, 2);

        utf8_cased = g_utf8_strdown (utf8, -1);

        GString *string = g_string_new (utf8_cased);

        g_free (utf8_cased);

        if (id > 0)
        {
            string = g_string_append_unichar (string, 0x2080 + id);
        }

        return g_string_free (string, FALSE);
    }

    return NULL;
}

static GdkPixbuf *
add_notation (GdkPixbuf *original, gint id)
{
    gint width, height;
    cairo_surface_t *surface;
    cairo_t *cr;

    width = gdk_pixbuf_get_width (original);
    height = gdk_pixbuf_get_height (original);

    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
    
    if (surface == NULL)
    {
        return original;
    }

    cr = cairo_create (surface);

    gdk_cairo_set_source_pixbuf (cr, original, 0, 0);
    cairo_paint (cr);

    gint rx, ry, rw, rh;

    rx = rw = width / 2;
    ry = rh = height / 2;

    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, .5);
    cairo_rectangle (cr, rx, ry, rw, rh);
    cairo_fill (cr);

    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, .8);
    cairo_rectangle (cr, rx - 1, ry - 1, rw, rh);
    cairo_fill (cr);

    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);

    gchar *num_string = g_strdup_printf ("%d", id);
    cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, height / 2);

    cairo_text_extents_t ext;
    cairo_text_extents (cr, num_string, &ext);

    cairo_move_to (cr, rx + (rw / 2) - (ext.width / 2) - 1, ry + (rh / 2) + (ext.height / 2) - 1);

    cairo_show_text (cr, num_string);
    g_free (num_string);

    GdkPixbuf *ret = gdk_pixbuf_get_from_surface (surface, 0, 0, width, height);

    cairo_surface_destroy (surface);
    cairo_destroy (cr);

    if (ret == NULL)
    {
        return original;
    }

    g_object_unref (original);

    return ret;
}

static gchar *
create_pixbuf (XAppKbdLayoutController *controller,
               guint                    group,
               const gchar             *name,
               gint                     id)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;
    GdkPixbuf *pixbuf = NULL;

    GdkPixbuf *flag_pixbuf;

    gchar *filename = g_strdup_printf ("%s.png", name);
    gchar *full_path = g_build_filename (priv->flag_dir, filename, NULL);

    flag_pixbuf = gdk_pixbuf_new_from_file (full_path, NULL);

    g_free (filename);
    g_free (full_path);

    if (flag_pixbuf != NULL)
    {
        if (id == 0)
        {
            pixbuf = flag_pixbuf;
        }
        else
        {
            pixbuf = add_notation (flag_pixbuf, id);
        }
    }

    if (flag_pixbuf != NULL)
    {
        gchar *save_name = g_strdup_printf ("xapp-kbd-layout-%d.png", group);

        gchar *path = g_build_filename (priv->temp_flag_theme_dir, save_name, NULL);
        g_remove (path);

        gdk_pixbuf_save (pixbuf,
                         path,
                         "png",
                         NULL,
                         NULL);

        g_object_unref (pixbuf);

        g_free (save_name);
        g_free (path);
    }
    else
    {
        return NULL;
    }

    return g_strdup_printf ("xapp-kbd-layout-%d", group);
}

static void
load_stores (XAppKbdLayoutController *controller)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    gchar **group_names = gkbd_configuration_get_group_names (priv->config);
    priv->num_groups = g_strv_length (group_names);

    /* We do nothing if there's only one keyboard layout enabled */
    if (priv->num_groups == 1)
    {
        priv->enabled = FALSE;
        return;
    }

    priv->enabled = TRUE;

    /* Make a list of [name, id] tuples, where name is the group/flag name,
     * and id is either 0, or, if a flag name is duplicated, a 1, 2, 3, etc...
     */
    gint i, j, id;
    GPtrArray *list = g_ptr_array_new_with_free_func ((GDestroyNotify) group_data_free);

    for (i = 0; i < priv->num_groups; i++)
    {
        GroupData *data = g_slice_new0 (GroupData);

        gchar *name = gkbd_configuration_get_group_name (priv->config, i);
        id = 0;

        for (j = 0; j < list->len; j++)
        {
            GroupData *iter = g_ptr_array_index (list, j);

            if (g_strcmp0 (name, iter->group) == 0)
            {
                id++;
                iter->id = id;
            }
        }

        if (id > 0)
        {
            id++;
        }

        data->group = name;
        data->id = id;

        g_ptr_array_add (list, data);
    }

    for (i = 0; i < list->len; i++)
    {
        GroupData *data = g_ptr_array_index (list, i);

        priv->icon_names[i] = create_pixbuf (controller, i, data->group, data->id);
        priv->text_store[i] = create_text (controller, data->group, data->id);
    }

    gtk_icon_theme_rescan_if_needed (gtk_icon_theme_get_default ());

    g_ptr_array_unref (list);
}

static gboolean
idle_config_changed (XAppKbdLayoutController *controller)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    clear_stores (controller);
    load_stores (controller);

    if (gkbd_configuration_get_current_group (priv->config) >= priv->num_groups)
    {
        xapp_kbd_layout_controller_set_current_group (controller, 0);
    }

    g_signal_emit (controller, signals[KBD_CONFIG_CHANGED], 0);

    priv->idle_changed_id = 0;
    return FALSE;
}

static void
on_configuration_changed (GkbdConfiguration       *config,
                          XAppKbdLayoutController *controller)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    if (priv->idle_changed_id != 0)
    {
        g_source_remove (priv->idle_changed_id);
        priv->idle_changed_id = 0;
    }

    priv->idle_changed_id = g_idle_add ((GSourceFunc) idle_config_changed, controller);
}


static void
on_configuration_group_changed (GkbdConfiguration       *config,
                                gint                     group,
                                XAppKbdLayoutController *controller)
{
    g_signal_emit (controller, signals[KBD_LAYOUT_CHANGED], 0, (guint) group);
}

static void
xapp_kbd_layout_controller_init (XAppKbdLayoutController *controller)
{
    controller->priv = G_TYPE_INSTANCE_GET_PRIVATE (controller, XAPP_TYPE_KBD_LAYOUT_CONTROLLER, XAppKbdLayoutControllerPrivate);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    priv->config = gkbd_configuration_get ();
    priv->enabled = FALSE;
    priv->flag_dir = NULL;
    priv->temp_flag_theme_dir = NULL;
    priv->idle_changed_id = 0;
}

static void
xapp_kbd_layout_controller_constructed (GObject *object)
{
    G_OBJECT_CLASS (xapp_kbd_layout_controller_parent_class)->constructed (object);

    XAppKbdLayoutController *controller = XAPP_KBD_LAYOUT_CONTROLLER (object);
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    initialize_flag_dir (controller);

    initialize_icon_theme (controller);

    gkbd_configuration_start_listen (priv->config);

    priv->changed_id = g_signal_connect_object (priv->config,
                                                "changed",
                                                G_CALLBACK (on_configuration_changed),
                                                controller, 0);

    priv->group_changed_id = g_signal_connect_object (priv->config,
                                                      "group-changed",
                                                      G_CALLBACK (on_configuration_group_changed),
                                                      controller, 0);
    clear_stores (controller);
    load_stores (controller);
}

static void
xapp_kbd_layout_controller_get_property (GObject    *gobject,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
    XAppKbdLayoutController *controller = XAPP_KBD_LAYOUT_CONTROLLER (gobject);
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    switch (prop_id)
    {
        case PROP_ENABLED:
            g_value_set_boolean (value, priv->enabled);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
            break;
    }
}

static void
xapp_kbd_layout_controller_dispose (GObject *object)
{
    XAppKbdLayoutController *controller = XAPP_KBD_LAYOUT_CONTROLLER (object);
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    gkbd_configuration_stop_listen (priv->config);

    clear_stores (controller);

    if (priv->changed_id > 0)
    {
        g_signal_handler_disconnect (priv->config, priv->changed_id);
        priv->changed_id = 0;
    }

    if (priv->group_changed_id > 0)
    {
        g_signal_handler_disconnect (priv->config, priv->group_changed_id);
        priv->group_changed_id = 0;
    }

    if (priv->idle_changed_id != 0)
    {
        g_source_remove (priv->idle_changed_id);
        priv->idle_changed_id = 0;
    }

    G_OBJECT_CLASS (xapp_kbd_layout_controller_parent_class)->dispose (object);
}

static void
xapp_kbd_layout_controller_finalize (GObject *object)
{
    XAppKbdLayoutController *controller = XAPP_KBD_LAYOUT_CONTROLLER (object);
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    g_clear_object (&priv->config);
    g_clear_pointer (&priv->flag_dir, g_free);
    g_clear_pointer (&priv->temp_flag_theme_dir, g_free);

    G_OBJECT_CLASS (xapp_kbd_layout_controller_parent_class)->finalize (object);
}

static void
xapp_kbd_layout_controller_class_init (XAppKbdLayoutControllerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = xapp_kbd_layout_controller_dispose;
    gobject_class->finalize = xapp_kbd_layout_controller_finalize;
    gobject_class->get_property = xapp_kbd_layout_controller_get_property;
    gobject_class->constructed = xapp_kbd_layout_controller_constructed;

    g_type_class_add_private (gobject_class, sizeof (XAppKbdLayoutControllerPrivate));

    g_object_class_install_property (gobject_class, PROP_ENABLED,
                                     g_param_spec_boolean ("enabled",
                                                           "Enabled",
                                                           "Whether we're enabled (more than one keyboard layout is installed)",
                                                           FALSE,
                                                           G_PARAM_READABLE)
                                    );

    signals[KBD_LAYOUT_CHANGED] = g_signal_new ("layout-changed",
                                                G_TYPE_FROM_CLASS (gobject_class),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL, NULL,
                                                g_cclosure_marshal_VOID__UINT,
                                                G_TYPE_NONE,
                                                1, G_TYPE_UINT);

    signals[KBD_CONFIG_CHANGED] = g_signal_new ("config-changed",
                                                G_TYPE_FROM_CLASS (gobject_class),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL, NULL,
                                                g_cclosure_marshal_VOID__VOID,
                                                G_TYPE_NONE,
                                                0, G_TYPE_NONE);
}

XAppKbdLayoutController *
xapp_kbd_layout_controller_new (void)
{
    return g_object_new (XAPP_TYPE_KBD_LAYOUT_CONTROLLER, NULL);
}

gboolean
xapp_kbd_layout_controller_get_enabled (XAppKbdLayoutController *controller)
{
    return controller->priv->enabled;
}

guint
xapp_kbd_layout_controller_get_current_group (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, 0);

    return gkbd_configuration_get_current_group (controller->priv->config);
}

void
xapp_kbd_layout_controller_set_current_group (XAppKbdLayoutController *controller,
                                              guint                    group)
{
    g_return_if_fail (controller->priv->enabled);
    g_return_if_fail (group <= controller->priv->num_groups);

    guint current = gkbd_configuration_get_current_group (controller->priv->config);

    if (current != group)
    {
        gkbd_configuration_lock_group (controller->priv->config, group);
    }
}

void
xapp_kbd_layout_controller_next_group (XAppKbdLayoutController *controller)
{
    g_return_if_fail (controller->priv->enabled);

    gkbd_configuration_lock_next_group (controller->priv->config);
}

void
xapp_kbd_layout_controller_previous_group (XAppKbdLayoutController *controller)
{
    g_return_if_fail (controller->priv->enabled);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    gint current = gkbd_configuration_get_current_group (priv->config);

    if (current > 0)
    {
        current--;
    }
    else
    {
        current = priv->num_groups - 1;
    }

    gkbd_configuration_lock_group (controller->priv->config, current);
}

/**
 * xapp_kbd_layout_controller_get_current_name:
 *
 * Returns the full name of the current keyboard layout.
 *
 * Returns: (transfer full): the newly created string or NULL
 * if something went wrong.
 */
gchar *
xapp_kbd_layout_controller_get_current_name (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    return gkbd_configuration_get_current_tooltip (controller->priv->config);
}

/**
 * xapp_kbd_layout_controller_get_all_names:
 *
 * Returns an array of all full layout names
 *
 * Returns: (transfer none) (array zero-terminated=1): array of names
 */
gchar **
xapp_kbd_layout_controller_get_all_names (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    return gkbd_configuration_get_group_names (controller->priv->config);
}

/**
 * xapp_kbd_layout_controller_get_current_icon_name:
 *
 * Returns the icon name to use for the current layout
 *
 * Returns: (transfer full): a new string with the icon name.
 */
gchar *
xapp_kbd_layout_controller_get_current_icon_name (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    guint current = gkbd_configuration_get_current_group (priv->config);

    return g_strdup (priv->icon_names[current]);
}


/**
 * xapp_kbd_layout_controller_get_icon_name_for_group:
 *
 * Returns the icon name to use for the specified layout.
 *
 * Returns: (transfer full): a new string with the icon name.
 */
gchar *
xapp_kbd_layout_controller_get_icon_name_for_group (XAppKbdLayoutController *controller, guint group)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);
    g_return_val_if_fail (group <= controller->priv->num_groups, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    return g_strdup (priv->icon_names[group]);
}

/**
 * xapp_kbd_layout_controller_get_current_short_name:
 *
 * Returns the short name (and subscript, if any) of the current layout
 *
 * Returns: (transfer full): a new string or NULL.
 */
gchar *
xapp_kbd_layout_controller_get_short_name (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    guint current = gkbd_configuration_get_current_group (priv->config);

    return g_strdup (priv->text_store[current]);
}

/**
 * xapp_kbd_layout_controller_get_short_name_for_group:
 *
 * Returns the short name and subscript of the specified group.
 *
 * Returns: (transfer full): a new string or NULL.
 */
gchar *
xapp_kbd_layout_controller_get_short_name_for_group (XAppKbdLayoutController *controller,
                                                     guint group)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    g_return_val_if_fail (group < controller->priv->num_groups, NULL);

    return g_strdup (priv->text_store[group]);
}

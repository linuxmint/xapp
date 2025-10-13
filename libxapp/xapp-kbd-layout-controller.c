
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <libgnomekbd/gkbd-configuration.h>

#include "xapp-kbd-layout-controller.h"

/**
 * SECTION:xapp-kbd-layout-controller
 * @Short_description: Keyboard layout selection UI element provider.
 * @Title: XAppKbdLayoutController
 *
 * A GObject wrapper for Gkbd that provides additional UI element
 * support for keyboard layout flags and abbreviations, as well as
 * Wfacilities to distinguish regional and hardware-based variants
 * which might otherwise appear identical in a layout list.
 */

enum
{
  PROP_0,

  PROP_ENABLED
};

enum
{
  KBD_LAYOUT_CHANGED,
  KBD_CONFIG_CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

typedef struct
{
    gchar *group_name;
    gchar *variant_name;

    gchar *group_label;
    gint   group_dupe_id;

    gchar *variant_label;
    gint   variant_dupe_id;
} GroupData;

#define GROUP_DATA(ptr, idx) ((GroupData *) g_ptr_array_index (ptr, idx))

static void
group_data_free (GroupData *data)
{
    g_clear_pointer (&data->group_name, g_free);
    g_clear_pointer (&data->group_label, g_free);
    g_clear_pointer (&data->variant_label, g_free);
    data->group_dupe_id = 0;
    data->variant_dupe_id = 0;

    g_slice_free (GroupData, data);
}

struct _XAppKbdLayoutControllerPrivate
{
    GkbdConfiguration *config;

    guint num_groups;

    GPtrArray *group_data;

    gulong changed_id;
    gulong group_changed_id;
    guint idle_changed_id;

    gboolean enabled;
};

G_DEFINE_TYPE_WITH_PRIVATE (XAppKbdLayoutController, xapp_kbd_layout_controller, G_TYPE_OBJECT);

static void
clear_stores (XAppKbdLayoutController *controller)
{
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    g_clear_pointer (&priv->group_data, g_ptr_array_unref);
}

static gchar *
create_label (XAppKbdLayoutController *controller,
              const gchar             *name,
              gint                     dupe_id)
{
    if (g_utf8_validate (name, -1, NULL))
    {
        gchar utf8[20];
        gchar *utf8_cased = NULL;

        g_utf8_strncpy (utf8, name, 3);

        utf8_cased = g_utf8_strdown (utf8, -1);

        GString *string = g_string_new (utf8_cased);

        g_free (utf8_cased);

        if (dupe_id > 0)
        {
            string = g_string_append_unichar (string, 0x2080 + dupe_id);
        }

        return g_string_free (string, FALSE);
    }

    return NULL;
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

    /* Populate the GroupData pointer array */

    gint group_dupe_id, variant_dupe_id;
    guint i, j;

    GPtrArray *list = g_ptr_array_new_with_free_func ((GDestroyNotify) group_data_free);

    for (i = 0; i < priv->num_groups; i++)
    {
        GroupData *data = g_slice_new0 (GroupData);

        /* Iterate through group names, figure out subscript values for duplicates */

        gchar *group_name = gkbd_configuration_get_group_name (priv->config, i);
        group_dupe_id = 0;

        for (j = 0; j < list->len; j++)
        {
            GroupData *iter = g_ptr_array_index (list, j);

            if (g_strcmp0 (group_name, iter->group_name) == 0)
            {
                group_dupe_id++;
                iter->group_dupe_id = group_dupe_id;
            }
        }

        if (group_dupe_id > 0)
        {
            group_dupe_id++;
        }

        data->group_name = group_name;
        data->group_dupe_id = group_dupe_id;

        /* Iterate through layout/variant names, figure out subscript values for duplicates */

        gchar *variant_name = gkbd_configuration_extract_layout_name (priv->config, i);
        variant_dupe_id = 0;

        for (j = 0; j < list->len; j++)
        {
            GroupData *iter = g_ptr_array_index (list, j);

            if (g_strcmp0 (variant_name, iter->variant_name) == 0)
            {
                variant_dupe_id++;
                iter->variant_dupe_id = variant_dupe_id;
            }
        }

        if (variant_dupe_id > 0)
        {
            variant_dupe_id++;
        }

        data->variant_name = variant_name;
        data->variant_dupe_id = variant_dupe_id;

        /* Finally, add the GroupData slice to the array */
        g_ptr_array_add (list, data);
    }

    for (i = 0; i < priv->num_groups; i++)
    {
        GroupData *data = g_ptr_array_index (list, i);

        /* Now generate labels for group names and for variant names.  This is done
         * in its own loop after the initial run, because previous dupe ids could have
         * changed later in the loop.
         */

        data->group_label =  create_label (controller, data->group_name, data->group_dupe_id);
        data->variant_label =  create_label (controller, data->variant_name, data->variant_dupe_id);
    }

    priv->group_data = list;
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
    controller->priv = xapp_kbd_layout_controller_get_instance_private (controller);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    priv->config = gkbd_configuration_get ();
    priv->enabled = FALSE;
    priv->idle_changed_id = 0;

    priv->group_data = NULL;
}

static void
xapp_kbd_layout_controller_constructed (GObject *object)
{
    G_OBJECT_CLASS (xapp_kbd_layout_controller_parent_class)->constructed (object);

    XAppKbdLayoutController *controller = XAPP_KBD_LAYOUT_CONTROLLER (object);
    XAppKbdLayoutControllerPrivate *priv = controller->priv;

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

/**
 * xapp_kbd_layout_controller_new
 *
 * Creates a new XAppKbdLayoutController instance.
 *
 * Returns: (transfer full): a new #XAppKbdLayoutController instance
 */
XAppKbdLayoutController *
xapp_kbd_layout_controller_new (void)
{
    return g_object_new (XAPP_TYPE_KBD_LAYOUT_CONTROLLER, NULL);
}

/**
 * xapp_kbd_layout_controller_get_enabled:
 * @controller: the #XAppKbdLayoutController
 *
 * Returns whether or not the layout controller is enabled
 */
gboolean
xapp_kbd_layout_controller_get_enabled (XAppKbdLayoutController *controller)
{
    return controller->priv->enabled;
}

/**
 * xapp_kbd_layout_controller_get_current_group:
 * @controller: the #XAppKbdLayoutController
 *
 * Selects the previous group in the group list.
 */
guint
xapp_kbd_layout_controller_get_current_group (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, 0);

    return gkbd_configuration_get_current_group (controller->priv->config);
}

/**
 * xapp_kbd_layout_controller_set_current_group
 * @controller: the #XAppKbdLayoutController
 * @group: the group number to make active
 *
 * Selects the given group number as active.
 */
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

/**
 * xapp_kbd_layout_controller_next_group
 * @controller: the #XAppKbdLayoutController
 *
 * Selects the next group in the group list.
 */
void
xapp_kbd_layout_controller_next_group (XAppKbdLayoutController *controller)
{
    g_return_if_fail (controller->priv->enabled);

    gkbd_configuration_lock_next_group (controller->priv->config);
}

/**
 * xapp_kbd_layout_controller_previous_group
 * @controller: the #XAppKbdLayoutController
 *
 * Selects the previous group in the group list.
 */
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
 * @controller: the #XAppKbdLayoutController
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
 * @controller: the #XAppKbdLayoutController
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
 * @controller: the #XAppKbdLayoutController
 *
 * Returns the icon file name (no path or extension) to use for the current layout
 *
 * Returns: (transfer full): a new string with the icon name.
 */
gchar *
xapp_kbd_layout_controller_get_current_icon_name (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    guint current = gkbd_configuration_get_current_group (priv->config);

    return g_strdup (GROUP_DATA (priv->group_data, current)->group_name);
}

/**
 * xapp_kbd_layout_controller_get_icon_name_for_group:
 * @controller: the #XAppKbdLayoutController
 * @group: a group number
 *
 * Returns the icon file name (no path or extension) to use for the specified layout.
 *
 * Returns: (transfer full): a new string with the icon name.
 */
gchar *
xapp_kbd_layout_controller_get_icon_name_for_group (XAppKbdLayoutController *controller,
                                                    guint                    group)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);
    g_return_val_if_fail (group <= controller->priv->num_groups, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    return g_strdup (GROUP_DATA (priv->group_data, group)->group_name);
}

/**
 * xapp_kbd_layout_controller_get_current_flag_id:
 * @controller: the #XAppKbdLayoutController
 *
 * Returns the duplicate id for the current layout
 *
 * Returns: the id
 */
gint
xapp_kbd_layout_controller_get_current_flag_id (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, 0);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    guint current = gkbd_configuration_get_current_group (priv->config);

    return GROUP_DATA (priv->group_data, current)->group_dupe_id;
}


/**
 * xapp_kbd_layout_controller_flag_id_for_group:
 * @controller: the #XAppKbdLayoutController
 * @group: a group number
 *
 * Returns the duplicate id for the specified layout
 *
 * Returns: the id
 */
gint
xapp_kbd_layout_controller_get_flag_id_for_group (XAppKbdLayoutController *controller,
                                                  guint                    group)
{
    g_return_val_if_fail (controller->priv->enabled, 0);
    g_return_val_if_fail (group < controller->priv->num_groups, 0);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    return GROUP_DATA (priv->group_data, group)->group_dupe_id;
}

/**
 * xapp_kbd_layout_controller_get_current_short_group_label:
 * @controller: the #XAppKbdLayoutController
 *
 * Returns the short group label (and subscript, if any) of the current layout
 *
 * Returns: (transfer full): a new string or NULL.
 */
gchar *
xapp_kbd_layout_controller_get_current_short_group_label (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    guint current = gkbd_configuration_get_current_group (priv->config);

    return g_strdup (GROUP_DATA (priv->group_data, current)->group_label);
}

/**
 * xapp_kbd_layout_controller_get_short_group_label_for_group:
 * @controller: the #XAppKbdLayoutController
 * @group: a group number
 *
 * Returns the short group label and subscript of the specified layout.
 *
 * Returns: (transfer full): a new string or NULL.
 */
gchar *
xapp_kbd_layout_controller_get_short_group_label_for_group (XAppKbdLayoutController *controller,
                                                            guint                    group)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);
    g_return_val_if_fail (group < controller->priv->num_groups, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;


    return g_strdup (GROUP_DATA (priv->group_data, group)->group_label);
}

/**
 * xapp_kbd_layout_controller_get_current_variant_label:
 * @controller: the #XAppKbdLayoutController
 *
 * Returns the variant label (and subscript, if any) of the current layout
 *
 * Returns: (transfer full): a new string or NULL.
 */
gchar *
xapp_kbd_layout_controller_get_current_variant_label (XAppKbdLayoutController *controller)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    guint current = gkbd_configuration_get_current_group (priv->config);

    return g_strdup (GROUP_DATA (priv->group_data, current)->variant_label);
}

/**
 * xapp_kbd_layout_controller_get_variant_label_for_group:
 * @controller: the #XAppKbdLayoutController
 * @group: a group number
 *
 * Returns the variant label and subscript of the specified layout.
 *
 * Returns: (transfer full): a new string or NULL.
 */
gchar *
xapp_kbd_layout_controller_get_variant_label_for_group (XAppKbdLayoutController *controller,
                                                        guint                    group)
{
    g_return_val_if_fail (controller->priv->enabled, NULL);

    XAppKbdLayoutControllerPrivate *priv = controller->priv;

    g_return_val_if_fail (group < controller->priv->num_groups, NULL);

    return g_strdup (GROUP_DATA (priv->group_data, group)->variant_label);
}

/**
 * xapp_kbd_layout_controller_render_cairo_subscript:
 * @cr: a #cairo_t
 * @x: the x position of the drawing area
 * @y: the y position of the drawing area
 * @width: the width of the drawing area
 * @height: the height of the drawing area
 * @subscript: the number to render
 *
 * Renders a subscript number in the given work area.  This should
 * be called from within a "draw" or "paint" widget/actor function,
 * where a valid cairo_t is provided to draw with.
 */
void
xapp_kbd_layout_controller_render_cairo_subscript (cairo_t  *cr,
                                                   gdouble   x,
                                                   gdouble   y,
                                                   gdouble   width,
                                                   gdouble   height,
                                                   gint      subscript)
{
    if (subscript == 0)
    {
        return;
    }

    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, .5);
    cairo_rectangle (cr, x, y, width, height);
    cairo_fill (cr);

    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, .8);
    cairo_rectangle (cr, x + 1, y + 1, width - 2, height - 2);
    cairo_fill (cr);

    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);

    gchar *num_string = g_strdup_printf ("%d", subscript);
    cairo_select_font_face (cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, height - 2.0);

    cairo_text_extents_t ext;
    cairo_text_extents (cr, num_string, &ext);

    cairo_move_to (cr,
                   (x + (width / 2.0) - (ext.width / 2.0)),
                   (y + (height / 2.0) + (ext.height / 2.0)));

    cairo_show_text (cr, num_string);
    g_free (num_string);
}


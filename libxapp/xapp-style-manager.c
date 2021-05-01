#include "xapp-style-manager.h"
#include "xapp-util.h"

/**
 * SECTION:xapp-style-manager
 * @Short_description: Convenience class for adding on the fly styling to gtk widgets.
 * @Title: XAppStyleManager
 *
 * #XAppStyleManager is a convenience class designed to make it easier to programmatically add style information to a widget.
 */
struct _XAppStyleManager
{
    GObject parent_instance;

    GHashTable      *properties;
    GtkWidget       *widget;
    GtkCssProvider  *provider;
    gchar           *class_name;
};

G_DEFINE_TYPE (XAppStyleManager, xapp_style_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDGET,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static gint next_id = 0;

static void
process_property_list_update (XAppStyleManager *style_manager)
{
    GString *css_text = g_string_new ("");
    GHashTableIter iter;
    gpointer key, value;
    gchar *raw_text;

    g_string_append (css_text, ".");
    g_string_append (css_text, style_manager->class_name);
    g_string_append (css_text, "{");

    g_hash_table_iter_init (&iter, style_manager->properties);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        g_string_append (css_text, (const gchar*) key);
        g_string_append (css_text, ":");
        g_string_append (css_text, (const gchar*) value);
        g_string_append (css_text, ";");
    }
    g_string_append (css_text, "}");

    raw_text = g_string_free (css_text, FALSE);
    gtk_css_provider_load_from_data (style_manager->provider, raw_text, -1, NULL);
    g_free (raw_text);
}

static void
xapp_style_manager_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    XAppStyleManager *style_manager = XAPP_STYLE_MANAGER (object);

    switch (prop_id)
    {
        case PROP_WIDGET:
            xapp_style_manager_set_widget (style_manager, g_value_get_object (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_style_manager_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    XAppStyleManager *style_manager = XAPP_STYLE_MANAGER (object);

    switch (prop_id)
    {
        case PROP_WIDGET:
            g_value_set_object (value, xapp_style_manager_get_widget (style_manager));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_style_manager_init (XAppStyleManager *style_manager)
{
    style_manager->provider = gtk_css_provider_new ();
    style_manager->class_name = g_strdup_printf ("xapp-%d", next_id);
    next_id++;

    style_manager->properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    style_manager->widget = NULL;
}

static void
xapp_style_manager_dispose (GObject *object)
{
    XAppStyleManager *style_manager = XAPP_STYLE_MANAGER (object);

    xapp_style_manager_set_widget (style_manager, NULL);
    g_hash_table_unref (style_manager->properties);
    g_object_unref (style_manager->provider);
    g_free (style_manager->class_name);

    G_OBJECT_CLASS (xapp_style_manager_parent_class)->dispose (object);
}

static void
xapp_style_manager_class_init (XAppStyleManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = xapp_style_manager_dispose;
    object_class->set_property = xapp_style_manager_set_property;
    object_class->get_property = xapp_style_manager_get_property;

    /**
     * XAppStyleManager:widget:
     *
     * The widget to be styled.
     */
    obj_properties[PROP_WIDGET] =
        g_param_spec_object ("widget",
                             "Widget",
                             "The widget to be styled.",
                             GTK_TYPE_WIDGET,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

/**
 * xapp_style_manager_new:
 *
 * Creates a new #XAppStyleManager instance
 *
 * Returns: (transfer full): a new #XAppStyleManager. Use g_object_unref when finished.
 *
 * Since: 2.2
 */
XAppStyleManager *
xapp_style_manager_new (void)
{
    XAppStyleManager *style_manager;
    style_manager = g_object_new (XAPP_TYPE_STYLE_MANAGER, NULL);

    return style_manager;
}

/**
 * xapp_style_manager_get_widget:
 * @style_manager: a #XAppStyleManager
 *
 * Gets the #GtkWidget the style manager currently applies styles to.
 *
 * Returns: (transfer none): the #GtkWidget previously set on the style manager, or %NULL.
 *
 * Since: 2.2
 */
GtkWidget *
xapp_style_manager_get_widget (XAppStyleManager *style_manager)
{
    return style_manager->widget;
}

/**
 * xapp_style_manager_set_widget:
 * @style_manager: a #XAppStyleManager
 * @widget: the #GtkWidget that the style manager will apply styles to, or
 * %NULL to unset the current widget and remove all styles currently set by
 * this #XAppStyleManager instance.
 *
 * Sets the #GtkWidget the style manager will apply styles to.
 *
 * Since: 2.2
 */
void
xapp_style_manager_set_widget (XAppStyleManager *style_manager,
                               GtkWidget        *widget)
{
    GtkStyleContext *context;

    if (style_manager->widget)
    {
        context = gtk_widget_get_style_context (style_manager->widget);
        gtk_style_context_remove_provider (context, GTK_STYLE_PROVIDER (style_manager->provider));
        gtk_style_context_remove_class (context, style_manager->class_name);
    }

    if (!widget)
    {
        style_manager->widget = NULL;
        return;
    }

    style_manager->widget = widget;
    context = gtk_widget_get_style_context (widget);

    gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (style_manager->provider), 700);
    gtk_style_context_add_class (context, style_manager->class_name);
}

/**
 * xapp_style_manager_set_style_property:
 * @style_manager: a #XAppStyleManager
 * @name: the property name
 * @value: the value to set the property to
 *
 * Adds the given style property to the widget. If the property has already been set, the value will be replaced.
 *
 * Since: 2.2
 */
void
xapp_style_manager_set_style_property (XAppStyleManager *style_manager,
                                       const gchar      *name,
                                       const gchar      *value)
{
    g_hash_table_insert (style_manager->properties, g_strdup (name), g_strdup (value));

    process_property_list_update (style_manager);
}

/**
 * xapp_style_manager_remove_style_property:
 * @style_manager: a #XAppStyleManager
 * @name: the property name
 *
 * Removes the given style property from the widget if set.
 *
 * Since: 2.2
 */
void
xapp_style_manager_remove_style_property (XAppStyleManager *style_manager,
                                          const gchar      *name)
{
    if (!g_hash_table_lookup (style_manager->properties, name))
        return;

    g_hash_table_remove (style_manager->properties, name);

    process_property_list_update (style_manager);
}

/**
 * xapp_style_manager_set_from_pango_font_string:
 * @style_manager: a #XAppStyleManager
 * @desc_string: a pango font description string
 *
 * Sets the css font property on the widget based on the supplied pango font description string.
 *
 * Since: 2.2
 */
void
xapp_style_manager_set_from_pango_font_string (XAppStyleManager *style_manager,
                                                     const gchar      *desc_string)
{
    gchar *css_string = xapp_pango_font_string_to_css (desc_string);

    xapp_style_manager_set_style_property (style_manager, "font", css_string);
    g_free (css_string);
}

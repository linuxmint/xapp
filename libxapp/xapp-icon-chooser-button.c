#include <config.h>
#include "xapp-icon-chooser-button.h"
#include <glib/gi18n-lib.h>

#define XAPP_BUTTON_ICON_SIZE_DEFAULT GTK_ICON_SIZE_DIALOG

/**
 * SECTION:xapp-icon-chooser-button
 * @Short_description: A button for selecting an icon
 * @Title: XAppIconChooserButton
 *
 * The XAppIconChooserButton creates a button so that
 * the user can select an icon. When the button is clicked
 * it will open an XAppIconChooserDialog. The currently
 * selected icon will be displayed as the button image.
 */

typedef struct
{
    GtkWidget               *image;
    XAppIconChooserDialog   *dialog;
    GtkIconSize              icon_size;
    gchar                   *icon_string;
    gchar                   *category_string;
} XAppIconChooserButtonPrivate;

struct _XAppIconChooserButton
{
    GtkButton parent_instance;
};

enum
{
    PROP_0,
    PROP_ICON_SIZE,
    PROP_ICON,
    PROP_CATEGORY,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (XAppIconChooserButton, xapp_icon_chooser_button, GTK_TYPE_BUTTON)

static void
ensure_dialog (XAppIconChooserButton *button)
{
    XAppIconChooserButtonPrivate *priv;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    if (priv->dialog != NULL)
    {
        return;
    }

    priv->dialog = xapp_icon_chooser_dialog_new ();
}

static void
on_clicked (GtkButton *button)
{
    XAppIconChooserButtonPrivate *priv;
    GtkResponseType               response;
    GtkWidget *toplevel;

    toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    priv = xapp_icon_chooser_button_get_instance_private (XAPP_ICON_CHOOSER_BUTTON (button));

    ensure_dialog (XAPP_ICON_CHOOSER_BUTTON (button));

    gtk_window_set_transient_for (GTK_WINDOW (priv->dialog), GTK_WINDOW (toplevel));
    gtk_window_set_modal (GTK_WINDOW (priv->dialog), gtk_window_get_modal (GTK_WINDOW (toplevel)));

    if (priv->category_string)
    {
        response = xapp_icon_chooser_dialog_run_with_category (priv->dialog, priv->category_string);
    }
    else if (priv->icon_string)
    {
        response = xapp_icon_chooser_dialog_run_with_icon (priv->dialog, priv->icon_string);
    }
    else
    {
        response = xapp_icon_chooser_dialog_run (priv->dialog);
    }

    if (response == GTK_RESPONSE_OK)
    {
        gchar *icon;

        icon = xapp_icon_chooser_dialog_get_icon_string (priv->dialog);
        xapp_icon_chooser_button_set_icon (XAPP_ICON_CHOOSER_BUTTON (button), icon);
    }
}

void
xapp_icon_chooser_button_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    XAppIconChooserButton        *button;
    XAppIconChooserButtonPrivate *priv;

    button = XAPP_ICON_CHOOSER_BUTTON (object);
    priv = xapp_icon_chooser_button_get_instance_private (button);

    switch (prop_id)
    {
        case PROP_ICON_SIZE:
            g_value_set_enum (value, priv->icon_size);
            break;
        case PROP_ICON:
            g_value_set_string (value, priv->icon_string);
            break;
        case PROP_CATEGORY:
            g_value_set_string (value, priv->category_string);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

void
xapp_icon_chooser_button_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    XAppIconChooserButton        *button;

    button = XAPP_ICON_CHOOSER_BUTTON (object);

    switch (prop_id)
    {
        case PROP_ICON_SIZE:
            xapp_icon_chooser_button_set_icon_size (button, g_value_get_enum (value));
            break;
        case PROP_ICON:
            xapp_icon_chooser_button_set_icon (button, g_value_get_string (value));
            break;
        case PROP_CATEGORY:
            xapp_icon_chooser_button_set_default_category (button, g_value_get_string (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

void
xapp_icon_chooser_button_dispose (GObject *object)
{
    XAppIconChooserButtonPrivate *priv;

    priv = xapp_icon_chooser_button_get_instance_private (XAPP_ICON_CHOOSER_BUTTON (object));

    g_clear_pointer (&priv->icon_string, g_free);
    g_clear_pointer (&priv->category_string, g_free);

    g_clear_object (&priv->dialog);

    G_OBJECT_CLASS (xapp_icon_chooser_button_parent_class)->dispose (object);
}

static void
xapp_icon_chooser_button_init (XAppIconChooserButton *button)
{
    XAppIconChooserButtonPrivate *priv;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    priv->image = gtk_image_new_from_icon_name ("unknown", XAPP_BUTTON_ICON_SIZE_DEFAULT);
    gtk_button_set_image (GTK_BUTTON (button), priv->image);

    gtk_widget_set_hexpand (GTK_WIDGET (button), FALSE);
    gtk_widget_set_vexpand (GTK_WIDGET (button), FALSE);
    gtk_widget_set_halign (GTK_WIDGET (button), GTK_ALIGN_CENTER);
    gtk_widget_set_valign (GTK_WIDGET (button), GTK_ALIGN_CENTER);

    xapp_icon_chooser_button_set_icon_size (button, -1);

    priv->dialog = NULL;
}

static void
xapp_icon_chooser_button_class_init (XAppIconChooserButtonClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkButtonClass *button_class = GTK_BUTTON_CLASS (klass);

    object_class->get_property = xapp_icon_chooser_button_get_property;
    object_class->set_property = xapp_icon_chooser_button_set_property;
    object_class->dispose = xapp_icon_chooser_button_dispose;

    button_class->clicked = on_clicked;

    /**
     * XAppIconChooserButton:icon-size:
     *
     * The size to use when displaying the icon.
     */
    obj_properties[PROP_ICON_SIZE] =
        g_param_spec_enum ("icon-size",
                           _("Icon size"),
                           _("The preferred icon size."),
                           GTK_TYPE_ICON_SIZE,
                           GTK_ICON_SIZE_DND,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * XAppIconChooserButton:icon:
     *
     * The preferred size to use when looking up icons. This only works with icon names.
     * Additionally, there is no guarantee that a selected icon name will exist in a
     * particular size.
     */
    obj_properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             _("Icon"),
                             _("The string representing the icon."),
                             "",
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * XAppIconChooserButton:category:
     *
     * The category selected by default.
     */
    obj_properties[PROP_CATEGORY] =
        g_param_spec_string ("category",
                             _("Category"),
                             _("The default category."),
                             "",
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

/**
 * xapp_icon_chooser_button_new:
 *
 * Creates a new #XAppIconChooserButton and sets its icon to @icon.
 *
 * Returns: a newly created #XAppIconChooserButton
 */
XAppIconChooserButton *
xapp_icon_chooser_button_new (void)
{
    return g_object_new (XAPP_TYPE_ICON_CHOOSER_BUTTON, NULL);
}

/**
 * xapp_icon_chooser_button_new_with_size:
 * @icon_size: the size of icon to use in the button, or NULL to use the default value.
 *
 * Creates a new #XAppIconChooserButton, and sets the sizes of the button image and the icons in
 * the dialog. Note that xapp_icon_chooser_button_new_with_size (NULL, NULL) is the same as calling
 * xapp_icon_chooser_button_new ().
 *
 * Returns: a newly created #XAppIconChooserButton
 */
XAppIconChooserButton *
xapp_icon_chooser_button_new_with_size (GtkIconSize   icon_size)
{
    XAppIconChooserButton *button;

    button = g_object_new (XAPP_TYPE_ICON_CHOOSER_BUTTON, NULL);

    xapp_icon_chooser_button_set_icon_size (button, icon_size);

    return button;
}

/**
 * xapp_icon_chooser_button_set_icon_size:
 * @button: a #XAppIconChooserButton
 * @icon_size: the size of icon to use in the button, or -1 to use the default value.
 *
 * Sets the icon size used in the button.
 */
void
xapp_icon_chooser_button_set_icon_size (XAppIconChooserButton *button,
                                        GtkIconSize            icon_size)
{
    XAppIconChooserButtonPrivate *priv;
    gint width, height;
    gchar *icon;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    if (icon_size == -1)
    {
        priv->icon_size = XAPP_BUTTON_ICON_SIZE_DEFAULT;
    }
    else
    {
        priv->icon_size = icon_size;
    }

    gtk_icon_size_lookup (priv->icon_size, &width, &height);
    gtk_image_set_pixel_size (GTK_IMAGE (priv->image), width);

    // We need to make sure the icon gets resized if it's a file path. Since
    // this means regenerating the pixbuf anyway, it's easier to just call
    // xapp_icon_chooser_button_set_icon, but we need to dup the string so
    // it doesn't get freed before it gets used.
    icon = g_strdup(priv->icon_string);
    xapp_icon_chooser_button_set_icon (button, icon);
    g_free (icon);

    g_object_notify (G_OBJECT (button), "icon-size");
}

/**
 * xapp_icon_chooser_button_get_icon:
 * @button: a #XAppIconChooserButton
 *
 * Gets the icon from the #XAppIconChooserButton.
 *
 * returns: a string representing the icon. This may be an icon name or a file path.
 */
const gchar*
xapp_icon_chooser_button_get_icon (XAppIconChooserButton *button)
{
    XAppIconChooserButtonPrivate *priv;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    return priv->icon_string;
}

/**
 * xapp_icon_chooser_button_set_icon:
 * @button: a #XAppIconChooserButton
 * @icon: (nullable): a string representing the icon to be set. This may be an icon name or a file path.
 *
 * Sets the icon on the #XAppIconChooserButton.
 */
void
xapp_icon_chooser_button_set_icon (XAppIconChooserButton *button,
                                   const gchar           *icon)
{
    XAppIconChooserButtonPrivate *priv;
    const gchar                  *icon_string;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    if (priv->icon_string != NULL)
    {
        g_free (priv->icon_string);
    }

    if (icon == NULL)
    {
        priv->icon_string = NULL;
        icon_string = "unknown";
    }
    else
    {
        priv->icon_string = g_strdup (icon);
        icon_string = icon;
    }

    if (g_strrstr (icon_string, "/"))
    {
        GdkPixbuf *pixbuf;
        gint width, height;

        gtk_icon_size_lookup (priv->icon_size, &width, &height);

        pixbuf = gdk_pixbuf_new_from_file_at_size (icon_string, width, height, NULL);

        gtk_image_set_from_pixbuf (GTK_IMAGE (priv->image), pixbuf);
    }
    else
    {
        gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), icon_string, priv->icon_size);
    }

    g_object_notify (G_OBJECT (button), "icon");
}

/**
 * xapp_icon_chooser_button_set_default_category:
 * @button: a #XAppIconChooserButton
 * @category: (nullable): a string representing the category selected by default.
 *
 * Sets the icon on the #XAppIconChooserButton.
 */
void
xapp_icon_chooser_button_set_default_category (XAppIconChooserButton *button,
                                               const gchar           *category)
{
    XAppIconChooserButtonPrivate *priv;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    if (priv->category_string != NULL)
    {
        g_free (priv->category_string);
    }

    priv->category_string = g_strdup (category);
}

/**
 * xapp_icon_chooser_button_get_dialog:
 * @button: a #XAppIconChooserButton
 *
 * Gets a reference to the icon chooser dialog for the #XAppIconChooserButton.
 * This is useful for setting properties on the dialog.
 *
 * Returns: (transfer none): the #XAppIconChooserDialog
 */
XAppIconChooserDialog *
xapp_icon_chooser_button_get_dialog (XAppIconChooserButton *button)
{
    XAppIconChooserButtonPrivate *priv;

    ensure_dialog (button);

    priv = xapp_icon_chooser_button_get_instance_private (button);

    return priv->dialog;
}

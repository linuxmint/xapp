#include <config.h>
#include "xapp-icon-chooser-button.h"
#include <glib/gi18n-lib.h>

#define XAPP_BUTTON_ICON_SIZE_DEFAULT GTK_ICON_SIZE_DIALOG
#define XAPP_DIALOG_ICON_SIZE_DEFAULT XAPP_ICON_SIZE_32

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
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (XAppIconChooserButton, xapp_icon_chooser_button, GTK_TYPE_BUTTON)

static void
on_clicked (GtkButton *button)
{
    XAppIconChooserButtonPrivate *priv;
    GtkResponseType               response;

    priv = xapp_icon_chooser_button_get_instance_private (XAPP_ICON_CHOOSER_BUTTON (button));

    gtk_window_set_transient_for (GTK_WINDOW (priv->dialog), GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))));

    if (priv->icon_string == NULL)
    {
        response = xapp_icon_chooser_dialog_run (priv->dialog);
    }
    else
    {
        response = xapp_icon_chooser_dialog_run_with_icon (priv->dialog, priv->icon_string);
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
    XAppIconChooserButtonPrivate *priv;

    button = XAPP_ICON_CHOOSER_BUTTON (object);
    priv = xapp_icon_chooser_button_get_instance_private (button);

    switch (prop_id)
    {
        case PROP_ICON_SIZE:
            xapp_icon_chooser_button_set_icon_size (button, g_value_get_enum (value));
            break;
        case PROP_ICON:
            priv->icon_string = g_strdup (g_value_get_string (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_icon_chooser_button_init (XAppIconChooserButton *button)
{
    XAppIconChooserButtonPrivate *priv;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    priv->image = gtk_image_new_from_icon_name ("unkown", XAPP_BUTTON_ICON_SIZE_DEFAULT);
    gtk_button_set_image (GTK_BUTTON (button), priv->image);

    gtk_widget_set_hexpand (GTK_WIDGET (button), FALSE);
    gtk_widget_set_vexpand (GTK_WIDGET (button), FALSE);
    gtk_widget_set_halign (GTK_WIDGET (button), GTK_ALIGN_CENTER);
    gtk_widget_set_valign (GTK_WIDGET (button), GTK_ALIGN_CENTER);

    xapp_icon_chooser_button_set_icon_size (button, -1);

    priv->dialog = xapp_icon_chooser_dialog_new ();
}

static void
xapp_icon_chooser_button_class_init (XAppIconChooserButtonClass *klass)
{
    GtkBindingSet *binding_set;
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkButtonClass *button_class = GTK_BUTTON_CLASS (klass);

    object_class->get_property = xapp_icon_chooser_button_get_property;
    object_class->set_property = xapp_icon_chooser_button_set_property;

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

    priv = xapp_icon_chooser_button_get_instance_private (button);

    if (icon_size == -1)
    {
        priv->icon_size = XAPP_BUTTON_ICON_SIZE_DEFAULT;
    }
    else
    {
        priv->icon_size = icon_size;
    }

    switch (priv->icon_size)
    {
        case GTK_ICON_SIZE_MENU:
        case GTK_ICON_SIZE_SMALL_TOOLBAR:
        case GTK_ICON_SIZE_BUTTON:
            gtk_image_set_pixel_size (GTK_IMAGE (priv->image), 16);
            break;
        case GTK_ICON_SIZE_LARGE_TOOLBAR:
            gtk_image_set_pixel_size (GTK_IMAGE (priv->image), 24);
            break;
        case GTK_ICON_SIZE_DND:
            gtk_image_set_pixel_size (GTK_IMAGE (priv->image), 32);
            break;
        case GTK_ICON_SIZE_DIALOG:
            gtk_image_set_pixel_size (GTK_IMAGE (priv->image), 48);
            break;
        default:
            g_warning ("%d is not a valid icon size", priv->icon_size);
            break;
    }

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
                                   gchar                 *icon)
{
    XAppIconChooserButtonPrivate *priv;
    const gchar                  *icon_string;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    priv->icon_string = icon;

    if (icon == NULL)
    {
        icon_string = "unkown";
    }
    else
    {
        icon_string = icon;
    }

    if (g_strrstr (icon_string, "/"))
    {
        gtk_image_set_from_file (GTK_IMAGE (priv->image), icon_string);
    }
    else
    {
        gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), icon_string, priv->icon_size);
    }

    g_object_notify (G_OBJECT (button), "icon");
}

/**
 * xapp_icon_chooser_button_get_dialog:
 * @button: a #XAppIconChooserButton
 *
 * Gets a reference to the icon chooser dialog for the #XAppIconChooserButton.
 * This is useful for setting properties on the dialog.
 *
 * Returns: the XAppIconChooserDialog
 */
XAppIconChooserDialog *
xapp_icon_chooser_button_get_dialog (XAppIconChooserButton *button)
{
    XAppIconChooserButtonPrivate *priv;

    priv = xapp_icon_chooser_button_get_instance_private (button);

    return priv->dialog;
}

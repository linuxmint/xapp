/* Based on gtkstackicon.c */

#include "statusicon.h"

struct _StatusIcon
{
    GtkToggleButton parent_instance;

    gint size;  /* Size of the panel to calculate from */
    GtkPositionType orientation; /* Orientation of the panel */
    const gchar *name;
    const gchar *process_name;

    XAppStatusIconInterface *proxy; /* The proxy for a remote XAppStatusIcon */

    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;
};

G_DEFINE_TYPE (StatusIcon, status_icon, GTK_TYPE_TOGGLE_BUTTON)

static void
update_image (StatusIcon *icon)
{
    g_return_if_fail (STATUS_IS_ICON (icon));

    GIcon *gicon;
    const gchar *icon_name;
    gint size;

    size = icon->size;

    if (size % 2 != 0)
    {
        size--;
    }

    gtk_image_set_pixel_size (GTK_IMAGE (icon->image), size);

    icon_name = xapp_status_icon_interface_get_icon_name (XAPP_STATUS_ICON_INTERFACE (icon->proxy));
    gicon = NULL;

    if (!icon_name)
    {
        return;
    }

    if (g_file_test (icon_name, G_FILE_TEST_EXISTS))
    {
        GFile *icon_file = g_file_new_for_path (icon_name);
        gicon = G_ICON (g_file_icon_new (icon_file));

        g_object_unref (icon_file);
    }
    else
    {
        GtkIconTheme *theme = gtk_icon_theme_get_default ();

        if (gtk_icon_theme_has_icon (theme, icon_name))
        {
            gicon = G_ICON (g_themed_icon_new (icon_name));

        }
    }

    if (gicon)
    {
        gtk_image_set_from_gicon (GTK_IMAGE (icon->image), G_ICON (gicon), GTK_ICON_SIZE_MENU);

        g_object_unref (gicon);
    }
    else
    {
        gtk_image_set_from_icon_name (GTK_IMAGE (icon->image), "image-missing", GTK_ICON_SIZE_MENU);
    }
}

static void
calculate_proxy_args (StatusIcon *icon,
                      gint       *x,
                      gint       *y)
{
    GdkWindow *window;
    GtkAllocation allocation;
    gint final_x, final_y, wx, wy;

    final_x = 0;
    final_y = 0;

    window = gtk_widget_get_window (GTK_WIDGET (icon));

    gdk_window_get_origin (window, &wx, &wy);
    gtk_widget_get_allocation (GTK_WIDGET (icon), &allocation);

    switch (icon->orientation)
    {
        case GTK_POS_TOP:
            final_x = wx + allocation.x;
            final_y = wy + allocation.y + INDICATOR_BOX_BORDER_COMP;
            break;
        case GTK_POS_BOTTOM:
            final_x = wx + allocation.x;
            final_y = wy + allocation.y - INDICATOR_BOX_BORDER_COMP;
            break;
        case GTK_POS_LEFT:
            final_x = wx + allocation.x + allocation.width + INDICATOR_BOX_BORDER_COMP;
            final_y = wy + allocation.y;
            break;
        case GTK_POS_RIGHT:
            final_x = wx + allocation.x - INDICATOR_BOX_BORDER_COMP;
            final_y = wy + allocation.y;
            break;
        default:
            break;
    }

    *x = final_x;
    *y = final_y;
}

static void
update_orientation (StatusIcon *icon)
{
    switch (icon->orientation)
    {
        case GTK_POS_TOP:
        case GTK_POS_BOTTOM:
            gtk_orientable_set_orientation (GTK_ORIENTABLE (icon->box), GTK_ORIENTATION_HORIZONTAL);
            if (strlen (gtk_label_get_label (GTK_LABEL (icon->label))) > 0)
            {
                gtk_widget_set_visible (icon->label, TRUE);
                gtk_widget_set_margin_start (icon->label, VISIBLE_LABEL_MARGIN);
            }
            break;
        case GTK_POS_LEFT:
        case GTK_POS_RIGHT:
            gtk_orientable_set_orientation (GTK_ORIENTABLE (icon->box), GTK_ORIENTATION_VERTICAL);
            gtk_widget_set_visible (icon->label, FALSE);
            gtk_widget_set_margin_start (icon->label, 0);
            break;
    }
}

static gboolean
do_idle_untoggle (StatusIcon *icon)
{
    g_return_val_if_fail (STATUS_IS_ICON (icon), G_SOURCE_REMOVE);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (icon), FALSE);
}

static gboolean
on_button_press_event (GtkWidget *widget,
                       GdkEvent  *event,
                       gpointer   user_data)
{
    StatusIcon *icon = STATUS_ICON (widget);
    gint x, y;

    x = 0;
    y = 0;

    calculate_proxy_args (icon, &x, &y);

    xapp_status_icon_interface_call_button_press (icon->proxy,
                                                  x, y,
                                                  event->button.button,
                                                  event->button.time,
                                                  icon->orientation,
                                                  NULL,
                                                  NULL,
                                                  NULL);

    if (event->button.button = GDK_BUTTON_PRIMARY)
    {
        return GDK_EVENT_PROPAGATE;
    }

    return GDK_EVENT_STOP;
}

static gboolean
on_button_release_event (GtkWidget *widget,
                         GdkEvent  *event,
                         gpointer   user_data)
{
    StatusIcon *icon = STATUS_ICON (widget);
    gint x, y;

    x = 0;
    y = 0;

    calculate_proxy_args (icon, &x, &y);

    xapp_status_icon_interface_call_button_release (icon->proxy,
                                                    x, y,
                                                    event->button.button,
                                                    event->button.time,
                                                    icon->orientation,
                                                    NULL,
                                                    NULL,
                                                    NULL);

    g_idle_add ((GSourceFunc) do_idle_untoggle, icon);

    return GDK_EVENT_PROPAGATE;
}

static void
status_icon_init (StatusIcon *icon)
{
    GtkStyleContext *context;
    GtkCssProvider  *provider;
    icon->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_container_add (GTK_CONTAINER (icon), icon->box);

    icon->image = gtk_image_new ();

    icon->label = gtk_label_new (NULL);
    gtk_widget_set_no_show_all (icon->label, TRUE);

    gtk_box_pack_start (GTK_BOX (icon->box), icon->image, TRUE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (icon->box), icon->label, FALSE, FALSE, 0);

    gtk_widget_set_can_default (GTK_WIDGET (icon), FALSE);
    gtk_widget_set_can_focus (GTK_WIDGET (icon), FALSE);
    gtk_button_set_relief (GTK_BUTTON (icon), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click (GTK_WIDGET (icon), FALSE);

    gtk_widget_set_name (GTK_WIDGET (icon), "xfce-panel-toggle-button");
    /* Make sure themes like Adwaita, which set excessive padding, don't cause the
       launcher buttons to overlap when panels have a fairly normal size */
    context = gtk_widget_get_style_context (GTK_WIDGET (icon));
    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (provider, ".xfce4-panel button { padding: 1px; }", -1, NULL);
    gtk_style_context_add_provider (context,
                                    GTK_STYLE_PROVIDER (provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
status_icon_dispose (GObject *object)
{
    StatusIcon *icon = STATUS_ICON (object);

    g_clear_object (&icon->proxy);

    G_OBJECT_CLASS (status_icon_parent_class)->dispose (object);
}

static void
status_icon_finalize (GObject *object)
{
    G_OBJECT_CLASS (status_icon_parent_class)->finalize (object);
}

static void
status_icon_class_init (StatusIconClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = status_icon_dispose;
    object_class->finalize = status_icon_finalize;
}

static void
bind_props_and_signals (StatusIcon *icon)
{
    guint flags = G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE;

    g_object_bind_property (icon->proxy, "label", icon->label, "label", flags);
    g_object_bind_property (icon->proxy, "tooltip-text", GTK_BUTTON (icon), "tooltip-text", flags);
    g_object_bind_property (icon->proxy, "visible", GTK_BUTTON (icon), "visible", flags);

    g_signal_connect_swapped (icon->proxy, "notify::icon-name", G_CALLBACK (update_image), icon);
    g_signal_connect (GTK_WIDGET (icon), "button-press-event", G_CALLBACK (on_button_press_event), NULL);
    g_signal_connect (GTK_WIDGET (icon), "button-release-event", G_CALLBACK (on_button_release_event), NULL);
}

StatusIcon *
status_icon_new (XAppStatusIconInterface *proxy)
{
    StatusIcon *icon = g_object_new (STATUS_TYPE_ICON, NULL);
    icon->proxy = g_object_ref (proxy);

    bind_props_and_signals (icon);

    update_orientation (icon);
    update_image (icon);

    gtk_widget_show_all (GTK_WIDGET (icon));

    return icon;
}

void
status_icon_set_size (StatusIcon *icon, gint size)
{
    if (icon->size == size)
    {
        return;
    }

    icon->size = size;

    update_image (icon);
}

void
status_icon_set_orientation (StatusIcon *icon, GtkPositionType orientation)
{
    if (icon->orientation == orientation)
    {
        return;
    }

    icon->orientation = orientation;

    update_orientation (icon);
}
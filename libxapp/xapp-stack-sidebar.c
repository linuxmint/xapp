/* Based on gtkstacksidebar.c */

#include "xapp-stack-sidebar.h"

/**
 * SECTION:xapp-stack-sidebar
 * @Title: XAppStackSidebar
 * @Short_description: An automatic sidebar widget
 *
 * A XAppStackSidebar allows you to quickly and easily provide a
 * consistent "sidebar" object for your user interface
 *
 * In order to use a XAppStackSidebar, you simply use a GtkStack to
 * organize your UI flow, and add the sidebar to your sidebar area. You
 * can use xapp_stack_sidebar_set_stack() to connect the #XAppStackSidebar
 * to the #GtkStack. The #XAppStackSidebar is an extended version of the
 * the #GtkStackSidebar that allows showing an icon in addition to the text.
 *
 * # CSS nodes
 *
 * XAppStackSidebar has a single CSS node with the name stacksidebar and
 * style class .sidebar
 *
 * When circumstances require it, XAppStackSidebar adds the
 * .needs-attention style class to the widgets representing the stack
 * pages.
 */


struct _XAppStackSidebar
{
    GtkBin parent_instance;

    GtkListBox *list;
    GtkStack *stack;
    GHashTable *rows;
    gboolean in_child_changed;
};

enum
{
    PROP_0,
    PROP_STACK,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (XAppStackSidebar, xapp_stack_sidebar, GTK_TYPE_BIN)

static void
xapp_stack_sidebar_set_property (GObject    *object,
                                 guint       prop_id,
                                 const       GValue *value,
                                 GParamSpec *pspec)
{
    switch (prop_id)
    {
        case PROP_STACK:
            xapp_stack_sidebar_set_stack (XAPP_STACK_SIDEBAR (object), g_value_get_object (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_stack_sidebar_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    XAppStackSidebar *sidebar = XAPP_STACK_SIDEBAR (object);

    switch (prop_id)
    {
        case PROP_STACK:
            g_value_set_object (value, sidebar->stack);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static gint
sort_list (GtkListBoxRow *row1,
           GtkListBoxRow *row2,
           gpointer       userdata)
{
    XAppStackSidebar *sidebar = XAPP_STACK_SIDEBAR (userdata);
    GtkWidget *item;
    GtkWidget *widget;
    gint left = 0;
    gint right = 0;


    if (row1)
    {
        item = gtk_bin_get_child (GTK_BIN (row1));
        widget = g_object_get_data (G_OBJECT (item), "stack-child");
        gtk_container_child_get (GTK_CONTAINER (sidebar->stack), widget,
                                 "position", &left,
                                 NULL);
    }

    if (row2)
    {
        item = gtk_bin_get_child (GTK_BIN (row2));
        widget = g_object_get_data (G_OBJECT (item), "stack-child");
        gtk_container_child_get (GTK_CONTAINER (sidebar->stack), widget,
                                 "position", &right,
                                 NULL);
    }

    if (left < right)
    {
        return  -1;
    }

    if (left == right)
    {
        return 0;
    }

    return 1;
}

static void
xapp_stack_sidebar_row_selected (GtkListBox    *box,
                                 GtkListBoxRow *row,
                                 gpointer       user_data)
{
    XAppStackSidebar *sidebar = XAPP_STACK_SIDEBAR (user_data);
    GtkWidget *item;
    GtkWidget *widget;

    if (sidebar->in_child_changed)
    {
        return;
    }

    if (!row)
    {
        return;
    }

    item = gtk_bin_get_child (GTK_BIN (row));
    widget = g_object_get_data (G_OBJECT (item), "stack-child");
    gtk_stack_set_visible_child (sidebar->stack, widget);
}

static void
xapp_stack_sidebar_init (XAppStackSidebar *sidebar)
{
    GtkStyleContext *style;
    GtkWidget *sw;

    sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);

    gtk_container_add (GTK_CONTAINER (sidebar), sw);

    sidebar->list = GTK_LIST_BOX (gtk_list_box_new ());

    gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (sidebar->list));

    gtk_list_box_set_sort_func (sidebar->list, sort_list, sidebar, NULL);

    g_signal_connect (sidebar->list, "row-selected",
                      G_CALLBACK (xapp_stack_sidebar_row_selected), sidebar);

    style = gtk_widget_get_style_context (GTK_WIDGET (sidebar));
    gtk_style_context_add_class (style, "sidebar");

    gtk_widget_show_all (GTK_WIDGET (sidebar));

    sidebar->rows = g_hash_table_new (NULL, NULL);
}

static void
update_row (XAppStackSidebar *sidebar,
            GtkWidget        *widget,
            GtkWidget        *row)
{
    GList *children;
    GList *list;
    GtkWidget *item;
    gchar *title;
    gchar *icon_name;
    gboolean needs_attention;
    GtkStyleContext *context;

    gtk_container_child_get (GTK_CONTAINER (sidebar->stack),
                             widget,
                             "title", &title,
                             "icon-name", &icon_name,
                             "needs-attention", &needs_attention,
                             NULL);

    item = gtk_bin_get_child (GTK_BIN (row));

    children = gtk_container_get_children (GTK_CONTAINER (item));
    for (list = children; list != NULL; list = list->next)
    {
        GtkWidget *child = list->data;

        if (GTK_IS_LABEL (child))
        {
            gtk_label_set_text (GTK_LABEL (child), title);
        }
        else if (GTK_IS_IMAGE (child))
        {
            gtk_image_set_from_icon_name (GTK_IMAGE (child), icon_name, GTK_ICON_SIZE_MENU);
        }
    }

    gtk_widget_set_visible (row, gtk_widget_get_visible (widget) && (title != NULL || icon_name != NULL));

    context = gtk_widget_get_style_context (row);
    if (needs_attention)
    {
        gtk_style_context_add_class (context, GTK_STYLE_CLASS_NEEDS_ATTENTION);
    }
    else
    {
        gtk_style_context_remove_class (context, GTK_STYLE_CLASS_NEEDS_ATTENTION);
    }

    g_free (title);
    g_free (icon_name);
    g_list_free (children);
}

static void
on_position_updated (GtkWidget        *widget,
                     GParamSpec       *pspec,
                     XAppStackSidebar *sidebar)
{
    gtk_list_box_invalidate_sort (sidebar->list);
}

static void
on_child_updated (GtkWidget        *widget,
                  GParamSpec       *pspec,
                  XAppStackSidebar *sidebar)
{
    GtkWidget *row;

    row = g_hash_table_lookup (sidebar->rows, widget);
    update_row (sidebar, widget, row);
}

static void
add_child (GtkWidget        *widget,
           XAppStackSidebar *sidebar)
{
    GtkWidget *item;
    GtkWidget *label;
    GtkWidget *icon;
    GtkWidget *row;

    /* Check we don't actually already know about this widget */
    if (g_hash_table_lookup (sidebar->rows, widget))
    {
        return;
    }

    /* Make a pretty item when we add children */
    item = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start (item, 6);
    gtk_widget_set_margin_end (item, 6);

    icon = gtk_image_new ();
    gtk_box_pack_start (GTK_BOX (item), icon, FALSE, FALSE, 0);

    label = gtk_label_new ("");
    gtk_box_pack_start (GTK_BOX (item), label, FALSE, FALSE, 0);

    row = gtk_list_box_row_new ();
    gtk_container_add (GTK_CONTAINER (row), item);
    gtk_widget_show_all (item);

    update_row (sidebar, widget, row);

    /* Hook up events */
    g_signal_connect (widget, "child-notify::title",
                      G_CALLBACK (on_child_updated), sidebar);
    g_signal_connect (widget, "child-notify::icon-name",
                      G_CALLBACK (on_child_updated), sidebar);
    g_signal_connect (widget, "child-notify::needs-attention",
                      G_CALLBACK (on_child_updated), sidebar);
    g_signal_connect (widget, "notify::visible",
                      G_CALLBACK (on_child_updated), sidebar);
    g_signal_connect (widget, "child-notify::position",
                      G_CALLBACK (on_position_updated), sidebar);

    g_object_set_data (G_OBJECT (item), "stack-child", widget);
    g_hash_table_insert (sidebar->rows, widget, row);
    gtk_container_add (GTK_CONTAINER (sidebar->list), row);
}

static void
remove_child (GtkWidget        *widget,
              XAppStackSidebar *sidebar)
{
    GtkWidget *row;

    row = g_hash_table_lookup (sidebar->rows, widget);
    if (!row)
    {
        return;
    }

    g_signal_handlers_disconnect_by_func (widget, on_child_updated, sidebar);
    g_signal_handlers_disconnect_by_func (widget, on_position_updated, sidebar);

    gtk_container_remove (GTK_CONTAINER (sidebar->list), row);
    g_hash_table_remove (sidebar->rows, widget);
}

static void
populate_sidebar (XAppStackSidebar *sidebar)
{
    GtkWidget *widget;
    GtkWidget *row;

    gtk_container_foreach (GTK_CONTAINER (sidebar->stack), (GtkCallback)add_child, sidebar);

    widget = gtk_stack_get_visible_child (sidebar->stack);
    if (widget)
    {
        row = g_hash_table_lookup (sidebar->rows, widget);
        gtk_list_box_select_row (sidebar->list, GTK_LIST_BOX_ROW (row));
    }
}

static void
clear_sidebar (XAppStackSidebar *sidebar)
{
    gtk_container_foreach (GTK_CONTAINER (sidebar->stack), (GtkCallback)remove_child, sidebar);
}

static void
on_child_changed (GtkWidget        *widget,
                  GParamSpec       *pspec,
                  XAppStackSidebar *sidebar)
{
    GtkWidget *child;
    GtkWidget *row;

    child = gtk_stack_get_visible_child (GTK_STACK (widget));
    row = g_hash_table_lookup (sidebar->rows, child);

    if (row != NULL)
    {
        sidebar->in_child_changed = TRUE;
        gtk_list_box_select_row (sidebar->list, GTK_LIST_BOX_ROW (row));
        sidebar->in_child_changed = FALSE;
    }
}

static void
on_stack_child_added (GtkContainer     *container,
                      GtkWidget        *widget,
                      XAppStackSidebar *sidebar)
{
    add_child (widget, sidebar);
}

static void
on_stack_child_removed (GtkContainer     *container,
                        GtkWidget        *widget,
                        XAppStackSidebar *sidebar)
{
    remove_child (widget, sidebar);
}

static void
disconnect_stack_signals (XAppStackSidebar *sidebar)
{
    g_signal_handlers_disconnect_by_func (sidebar->stack, on_stack_child_added, sidebar);
    g_signal_handlers_disconnect_by_func (sidebar->stack, on_stack_child_removed, sidebar);
    g_signal_handlers_disconnect_by_func (sidebar->stack, on_child_changed, sidebar);
    g_signal_handlers_disconnect_by_func (sidebar->stack, disconnect_stack_signals, sidebar);
}

static void
connect_stack_signals (XAppStackSidebar *sidebar)
{
    g_signal_connect_after (sidebar->stack, "add",
                            G_CALLBACK (on_stack_child_added), sidebar);
    g_signal_connect_after (sidebar->stack, "remove",
                            G_CALLBACK (on_stack_child_removed), sidebar);
    g_signal_connect (sidebar->stack, "notify::visible-child",
                      G_CALLBACK (on_child_changed), sidebar);
    g_signal_connect_swapped (sidebar->stack, "destroy",
                              G_CALLBACK (disconnect_stack_signals), sidebar);
}

static void
xapp_stack_sidebar_dispose (GObject *object)
{
    XAppStackSidebar *sidebar = XAPP_STACK_SIDEBAR (object);

    xapp_stack_sidebar_set_stack (sidebar, NULL);

    G_OBJECT_CLASS (xapp_stack_sidebar_parent_class)->dispose (object);
}

static void
xapp_stack_sidebar_finalize (GObject *object)
{
    XAppStackSidebar *sidebar = XAPP_STACK_SIDEBAR (object);

    g_hash_table_destroy (sidebar->rows);

    G_OBJECT_CLASS (xapp_stack_sidebar_parent_class)->finalize (object);
}

static void
xapp_stack_sidebar_class_init (XAppStackSidebarClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = xapp_stack_sidebar_dispose;
    object_class->finalize = xapp_stack_sidebar_finalize;
    object_class->set_property = xapp_stack_sidebar_set_property;
    object_class->get_property = xapp_stack_sidebar_get_property;

    obj_properties[PROP_STACK] =
        g_param_spec_object ("stack",
                             "Stack",
                             "Associated stack for this XAppStackSidebar",
                             GTK_TYPE_STACK,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);

    gtk_widget_class_set_css_name (widget_class, "stacksidebar");
}

/**
 * xapp_stack_sidebar_new:
 *
 * Creates a new sidebar.
 *
 * Returns: the new #XAppStackSidebar
 */

XAppStackSidebar *
xapp_stack_sidebar_new (void)
{
    return g_object_new (XAPP_TYPE_STACK_SIDEBAR, NULL);
}

/**
 * xapp_stack_sidebar_set_stack:
 * @sidebar: a #XAppStackSidebar
 * @stack: a #GtkStack
 *
 * Set the #GtkStack associated with this #XAppStackSidebar.
 *
 * The sidebar widget will automatically update according to the order
 * (packing) and items within the given #GtkStack.
 */

void
xapp_stack_sidebar_set_stack (XAppStackSidebar *sidebar,
                              GtkStack         *stack)
{
    g_return_if_fail (XAPP_IS_STACK_SIDEBAR (sidebar));
    g_return_if_fail (GTK_IS_STACK (stack) || stack == NULL);

    if (sidebar->stack == stack)
    {
        return;
    }

    if (sidebar->stack)
    {
        disconnect_stack_signals (sidebar);
        clear_sidebar (sidebar);
        g_clear_object (&sidebar->stack);
    }

    if (stack)
    {
        sidebar->stack = g_object_ref (stack);
        populate_sidebar (sidebar);
        connect_stack_signals (sidebar);
    }

    gtk_widget_queue_resize (GTK_WIDGET (sidebar));

    g_object_notify (G_OBJECT (sidebar), "stack");
}

/**
 * xapp_stack_sidebar_get_stack:
 * @sidebar: a #XAppStackSidebar
 *
 * Retrieves the stack.
 * See xapp_stack_sidebar_set_stack().
 *
 * Returns: (nullable) (transfer none): the associated #GtkStack or
 *     %NULL if none has been set explicitly
 */

GtkStack *
xapp_stack_sidebar_get_stack (XAppStackSidebar *sidebar)
{
    g_return_val_if_fail (XAPP_IS_STACK_SIDEBAR (sidebar), NULL);

    return GTK_STACK (sidebar->stack);
}

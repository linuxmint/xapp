#include <gdk/gdk.h>
#include "xapp-preferences-window.h"
#include "xapp-stack-sidebar.h"

/**
 * SECTION:xapp-preferences-window
 * @Short_description: A base preferences window
 * @Title: XAppPreferencesWindow
 *
 * The XAppPreferencesWindow sets up a simple dialog
 * window with a GtkStack, GtkSidebarSwitcher, and
 * GtkActionBar. The stack switcher and action bar only
 * show when needed.
 */

typedef struct
{
    GtkWidget        *stack;
    XAppStackSidebar *side_switcher;
    GtkWidget        *button_area;

    gint              num_pages;
} XAppPreferencesWindowPrivate;

enum
{
    CLOSE,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

G_DEFINE_TYPE_WITH_PRIVATE (XAppPreferencesWindow, xapp_preferences_window, GTK_TYPE_WINDOW)

static void
xapp_preferences_window_init (XAppPreferencesWindow *window)
{
    XAppPreferencesWindowPrivate *priv = xapp_preferences_window_get_instance_private (window);
    GtkWidget *main_box;
    GtkWidget *secondary_box;
    GtkStyleContext *style_context;

    gtk_window_set_default_size (GTK_WINDOW (window), 600, 400);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
    gtk_window_set_type_hint (GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_container_set_border_width (GTK_CONTAINER (window), 5);

    main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width (GTK_CONTAINER (main_box), 5);
    gtk_container_add (GTK_CONTAINER (window), main_box);

    secondary_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_set_border_width (GTK_CONTAINER (secondary_box), 5);
    gtk_box_pack_start (GTK_BOX (main_box), secondary_box, TRUE, TRUE, 0);

    style_context = gtk_widget_get_style_context (secondary_box);
    gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_FRAME);

    priv->side_switcher = xapp_stack_sidebar_new ();
    gtk_widget_set_size_request (GTK_WIDGET (priv->side_switcher), 100, -1);
    gtk_box_pack_start (GTK_BOX (secondary_box), GTK_WIDGET (priv->side_switcher), FALSE, FALSE, 0);
    gtk_widget_set_no_show_all (GTK_WIDGET (priv->side_switcher), TRUE);

    // XAppStackSidebar calls show_all on itself during its init, we need to hide
    // it here so our single/multiple page mechanism here works properly.
    gtk_widget_hide (GTK_WIDGET (priv->side_switcher));

    priv->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (priv->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_box_pack_start (GTK_BOX (secondary_box), priv->stack, TRUE, TRUE, 0);
    xapp_stack_sidebar_set_stack (priv->side_switcher, GTK_STACK (priv->stack));

    style_context = gtk_widget_get_style_context (priv->stack);
    gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_VIEW);

    priv->button_area = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_container_set_border_width (GTK_CONTAINER (priv->button_area), 5);
    gtk_box_pack_start (GTK_BOX (main_box), priv->button_area, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all (priv->button_area, TRUE);

    /* Keep track of the number of pages so we can hide the stack switcher with < 2 */
    priv->num_pages = 0;
}

static void
xapp_preferences_window_close (XAppPreferencesWindow *window)
{
    gtk_window_close (GTK_WINDOW (window));
}

static void
xapp_preferences_window_class_init (XAppPreferencesWindowClass *klass)
{
    GtkBindingSet *binding_set;

    klass->close = xapp_preferences_window_close;

    signals[CLOSE] =
        g_signal_new ("close",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (XAppPreferencesWindowClass, close),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    binding_set = gtk_binding_set_by_class (klass);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Escape, 0, "close", 0);
}

/**
 * xapp_preferences_window_new:
 *
 * Creates a new #XAppPreferencesWindow.
 *
 * Returns: a newly created #XAppPreferencesWindow
 */
XAppPreferencesWindow *
xapp_preferences_window_new (void)
{
    return g_object_new (XAPP_TYPE_PREFERENCES_WINDOW, NULL);
}


/**
 * xapp_preferences_window_add_page:
 * @window: a #XAppPreferencesWindow
 * @widget: a #GtkWidget to add
 * @name: the name for the page
 * @title: a human-readable title for the page
 *
 * Adds a page to the window. The page is identified by name. The
 * title will be used in the sidebar so should be short. The sidebar
 * will show automatically once at least two pages are added.
 */
void
xapp_preferences_window_add_page (XAppPreferencesWindow *window,
                                  GtkWidget             *widget,
                                  const gchar           *name,
                                  const gchar           *title)
{
    XAppPreferencesWindowPrivate *priv = xapp_preferences_window_get_instance_private (window);

    g_return_if_fail (XAPP_IS_PREFERENCES_WINDOW (window));

    gtk_stack_add_titled (GTK_STACK (priv->stack), widget, name, title);

    priv->num_pages++;

    if (priv->num_pages > 1)
    {
        gtk_widget_set_no_show_all (GTK_WIDGET (priv->side_switcher), FALSE);
    }
}

/**
 * xapp_preferences_window_add_button:
 * @window: a #XAppPreferencesWindow
 * @button: a #GtkWidget to add
 * @pack_type: a #GtkPackType to use
 *
 * Adds a button to the bottom action bar of the window. Where
 * the button is place will be determined by the #GtkPackType. The
 * action bar will show automatically once at least one button is
 * added.
 */
void
xapp_preferences_window_add_button (XAppPreferencesWindow *window,
                                    GtkWidget             *button,
                                    GtkPackType            pack_type)
{
    XAppPreferencesWindowPrivate *priv = xapp_preferences_window_get_instance_private (window);
    GtkStyleContext *style_context;

    g_return_if_fail (XAPP_IS_PREFERENCES_WINDOW (window));
    g_return_if_fail (GTK_IS_WIDGET (button));

    gtk_container_add (GTK_CONTAINER (priv->button_area), button);

    if (pack_type == GTK_PACK_END)
    {
        gtk_button_box_set_child_secondary (GTK_BUTTON_BOX (priv->button_area), button, TRUE);
    }
    else if (pack_type != GTK_PACK_START)
    {
        return;
    }

    style_context = gtk_widget_get_style_context (button);
    gtk_style_context_add_class (style_context, "text-button");

    gtk_widget_set_no_show_all (priv->button_area, FALSE);
}

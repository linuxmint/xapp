#include <config.h>
#include <gdk/gdk.h>
#include "xapp-icon-chooser-dialog.h"
#include "xapp-stack-sidebar.h"
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

/**
 * SECTION:xapp-icon-chooser-dialog
 * @Short_description: A dialog for selecting an icon
 * @Title: XAppIconChooserDialog
 *
 * The XAppIconChooserDialog creates a dialog so that
 * the user can select an icon. It provides the ability
 * to browse by category, search by icon name, or select
 * from a specific file.
 */

typedef struct
{
    GtkResponseType  response;
    XAppIconSize     icon_size;
    GCancellable    *cancellable;
    GtkListStore    *category_list;
    GtkListStore    *icon_store;
    GHashTable      *categories;
    GtkWidget       *search_bar;
    GtkWidget       *icon_view;
    GtkWidget       *list_box;
    GtkWidget       *select_button;
    GtkWidget       *browse_button;
    gchar           *icon_string;
    gboolean         allow_paths;
} XAppIconChooserDialogPrivate;

struct _XAppIconChooserDialog
{
    GtkWindow parent_instance;
};

typedef struct
{
    GtkListStore    *model;
    const gchar     *short_name;
    const gchar     *long_name;
} IconInfoLoadCallbackInfo;

typedef struct
{
    gchar        *name;
    GList        *contexts;
    GList        *icons;
    GtkListStore *model;
    gboolean      is_loaded;
} IconCategoryInfo;

typedef struct
{
    const gchar  *name;
    const gchar  *contexts[5];
} IconCategoryDefinition;

static IconCategoryDefinition categories[] = {
    //  Category name       context names
    {
        N_("Actions"),       { "Actions", NULL }
    },
    {
        N_("Applications"),  { "Applications", "Apps", NULL }
    },
    {
        N_("Categories"),    { "Categories", NULL }
    },
    {
        N_("Devices"),       { "Devices", NULL }
    },
    {
        N_("Emblems"),       { "Emblems", NULL }
    },
    {
        N_("Emoji"),         { "Emotes", NULL }
    },
    {
        N_("Mime types"),    { "MimeTypes", "Mimetypes", NULL }
    },
    {
        N_("Places"),        { "Places", NULL }
    },
    {
        N_("Status"),        { "Status", "Notifications", NULL }
    },
    {
        N_("Other"),         { "Panel", NULL }
    }
};

enum
{
    CLOSE,
    SELECT,
    LAST_SIGNAL
};

enum
{
    PROP_0,
    PROP_ICON_SIZE,
    PROP_ALLOW_PATHS,
    N_PROPERTIES
};

enum
{
    COLUMN_DISPLAY_NAME,
    COLUMN_FULL_NAME,
    COLUMN_PIXBUF,
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static guint signals[LAST_SIGNAL] = {0, };

G_DEFINE_TYPE_WITH_PRIVATE (XAppIconChooserDialog, xapp_icon_chooser_dialog, GTK_TYPE_WINDOW)

static void on_category_selected (GtkListBox            *list_box,
                                 XAppIconChooserDialog *dialog);

static void on_search (GtkSearchEntry        *entry,
                       XAppIconChooserDialog *dialog);

static void on_icon_view_selection_changed (GtkIconView *icon_view,
                                            gpointer     user_data);

static void on_icon_store_icons_added (GtkTreeModel *tree_model,
                                       GtkTreePath  *path,
                                       GtkTreeIter  *iter,
                                       gpointer      user_data);

static void on_browse_button_clicked (GtkButton *button,
                                      gpointer   user_data);

static void on_select_button_clicked (GtkButton *button,
                                      gpointer   user_data);

static void on_cancel_button_clicked (GtkButton *button,
                                      gpointer   user_data);

static gboolean on_search_bar_key_pressed (GtkWidget *widget,
                                           GdkEvent  *event,
                                           gpointer   user_data);

static gboolean on_delete_event (GtkWidget   *widget,
                                 GdkEventAny *event);

static gboolean on_select_event (XAppIconChooserDialog *dialog,
                                 GdkEventAny           *event);

static void on_icon_view_item_activated (GtkIconView *iconview,
                                         GtkTreePath *path,
                                         gpointer     user_data);

static void load_categories (XAppIconChooserDialog *dialog);

static gint list_box_sort (GtkListBoxRow *row1,
                           GtkListBoxRow *row2,
                           gpointer       user_data);

void
xapp_icon_chooser_dialog_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    XAppIconChooserDialog *dialog;
    XAppIconChooserDialogPrivate *priv;

    dialog = XAPP_ICON_CHOOSER_DIALOG (object);
    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    switch (prop_id)
    {
        case PROP_ICON_SIZE:
            g_value_set_enum (value, priv->icon_size);
            break;
        case PROP_ALLOW_PATHS:
            g_value_set_boolean (value, priv->allow_paths);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

void
xapp_icon_chooser_dialog_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    XAppIconChooserDialog        *dialog;
    XAppIconChooserDialogPrivate *priv;

    dialog = XAPP_ICON_CHOOSER_DIALOG (object);
    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    switch (prop_id)
    {
        case PROP_ICON_SIZE:
            priv->icon_size = g_value_get_enum (value);
            break;
        case PROP_ALLOW_PATHS:
            priv->allow_paths = g_value_get_boolean (value);
            if (priv->allow_paths)
            {
                gtk_widget_show (priv->browse_button);
                gtk_widget_set_no_show_all (priv->browse_button, FALSE);
            }
            else
            {
                gtk_widget_hide (priv->browse_button);
                gtk_widget_set_no_show_all (priv->browse_button, TRUE);
            }
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_icon_chooser_dialog_init (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;
    GtkWidget                    *main_box;
    GtkWidget                    *secondary_box;
    GtkWidget                    *toolbar;
    GtkToolItem                  *tool_item;
    GtkWidget                    *toolbar_box;
    GtkWidget                    *right_box;
    GtkCellRenderer              *renderer;
    GtkTreeViewColumn            *column;
    GtkStyleContext              *style;
    GtkSizeGroup                 *button_size_group;
    GtkWidget                    *cancel_button;
    GtkWidget                    *button_area;
    GtkWidget                    *selection;
    GtkWidget                    *scrolled_window;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    priv->icon_size = XAPP_ICON_SIZE_32;
    priv->categories = g_hash_table_new (NULL, NULL);
    priv->response = GTK_RESPONSE_NONE;
    priv->icon_string = "";
    priv->cancellable = NULL;

    priv->icon_store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->icon_store), COLUMN_DISPLAY_NAME,
                                          GTK_SORT_ASCENDING);
    g_signal_connect (priv->icon_store, "row-inserted",
                      G_CALLBACK (on_icon_store_icons_added), dialog);

    gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 400);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), TRUE);
    gtk_window_set_type_hint (GTK_WINDOW (dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

    main_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (dialog), main_box);

    // toolbar
    toolbar = gtk_toolbar_new ();
    gtk_box_pack_start (GTK_BOX (main_box), toolbar, FALSE, FALSE, 0);
    style = gtk_widget_get_style_context (toolbar);
    gtk_style_context_add_class (style, "primary-toolbar");

    tool_item = gtk_tool_item_new ();
    gtk_toolbar_insert (GTK_TOOLBAR (toolbar), tool_item, 0);
    gtk_tool_item_set_expand (tool_item, TRUE);

    toolbar_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add (GTK_CONTAINER (tool_item), toolbar_box);
    style = gtk_widget_get_style_context (GTK_WIDGET (toolbar_box));
    gtk_style_context_add_class (style, "linked");

    priv->search_bar = gtk_search_entry_new ();
    gtk_box_pack_start (GTK_BOX (toolbar_box), priv->search_bar, TRUE, TRUE, 0);
    gtk_entry_set_placeholder_text (GTK_ENTRY (priv->search_bar), _("Search"));

    g_signal_connect (priv->search_bar, "search-changed",
                      G_CALLBACK (on_search), dialog);
    g_signal_connect (priv->search_bar, "key-press-event",
                      G_CALLBACK (on_search_bar_key_pressed), dialog);

    priv->browse_button = gtk_button_new_with_label (_("Browse"));
    gtk_box_pack_start (GTK_BOX (toolbar_box), priv->browse_button, FALSE, FALSE, 0);

    g_signal_connect (priv->browse_button, "clicked",
                      G_CALLBACK (on_browse_button_clicked), dialog);

    secondary_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (main_box), secondary_box, TRUE, TRUE, 0);

    // context list
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start (GTK_BOX (secondary_box), scrolled_window, FALSE, FALSE, 0);

    priv->list_box = gtk_list_box_new ();
    gtk_container_add(GTK_CONTAINER (scrolled_window), GTK_WIDGET (priv->list_box));
    gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->list_box), list_box_sort, NULL, NULL);
    g_signal_connect (priv->list_box, "selected-rows-changed",
                      G_CALLBACK (on_category_selected), dialog);

    style = gtk_widget_get_style_context (GTK_WIDGET (scrolled_window));
    gtk_style_context_add_class (style, "sidebar");

    right_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start (GTK_BOX (secondary_box), right_box, TRUE, TRUE, 0);

    // icon view
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_box_pack_start (GTK_BOX (right_box), scrolled_window, TRUE, TRUE, 0);

    priv->icon_view = gtk_icon_view_new ();
    gtk_container_add(GTK_CONTAINER (scrolled_window), GTK_WIDGET (priv->icon_view));

    gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (priv->icon_view), COLUMN_PIXBUF);
    gtk_icon_view_set_text_column (GTK_ICON_VIEW (priv->icon_view), COLUMN_DISPLAY_NAME);
    gtk_icon_view_set_tooltip_column (GTK_ICON_VIEW (priv->icon_view), COLUMN_FULL_NAME);

    g_signal_connect (priv->icon_view, "selection-changed",
                      G_CALLBACK (on_icon_view_selection_changed), dialog);
    g_signal_connect (priv->icon_view, "item-activated",
                      G_CALLBACK (on_icon_view_item_activated), dialog);

    // buttons
    button_area = gtk_action_bar_new ();
    gtk_box_pack_start (GTK_BOX (main_box), button_area, FALSE, FALSE, 0);

    button_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

    priv->select_button = gtk_button_new_with_label (_("Select"));
    style = gtk_widget_get_style_context (GTK_WIDGET (priv->select_button));
    gtk_style_context_add_class (style, "text-button");
    gtk_size_group_add_widget (button_size_group, priv->select_button);
    gtk_action_bar_pack_end (GTK_ACTION_BAR (button_area), priv->select_button);

    g_signal_connect (priv->select_button, "clicked",
                      G_CALLBACK (on_select_button_clicked), dialog);

    cancel_button = gtk_button_new_with_label (_("Cancel"));
    style = gtk_widget_get_style_context (GTK_WIDGET (cancel_button));
    gtk_style_context_add_class (style, "text-button");
    gtk_size_group_add_widget (button_size_group, cancel_button);
    gtk_action_bar_pack_end (GTK_ACTION_BAR (button_area), cancel_button);

    g_signal_connect (cancel_button, "clicked",
                      G_CALLBACK (on_cancel_button_clicked), dialog);

    load_categories (dialog);
}

static void
xapp_icon_chooser_dialog_class_init (XAppIconChooserDialogClass *klass)
{
    GtkBindingSet *binding_set;
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->get_property = xapp_icon_chooser_dialog_get_property;
    object_class->set_property = xapp_icon_chooser_dialog_set_property;

    widget_class->delete_event = on_delete_event;

    /**
     * XAppIconChooserDialog:icon-size:
     *
     * The preferred size to use when looking up icons. This only works with icon names.
     * Additionally, there is no guarantee that a selected icon name will exist in a
     * particular size.
     */
    obj_properties[PROP_ICON_SIZE] =
        g_param_spec_enum ("icon-size",
                           _("Icon size"),
                           _("The preferred icon size."),
                           XAPP_TYPE_ICON_SIZE,
                           XAPP_ICON_SIZE_32,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

    /**
     * XAppIconChooserDialog:allow-paths:
     *
     * Whether to allow paths to be searched and selected or only icon names.
     */
    obj_properties[PROP_ALLOW_PATHS] =
        g_param_spec_boolean ("allow-paths",
                              _("Allow Paths"),
                              _("Whether to allow paths."),
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);

    // keybinding signals
    signals[CLOSE] =
        g_signal_new ("close",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (GtkWidgetClass, delete_event),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SELECT] =
        g_signal_new ("select",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    binding_set = gtk_binding_set_by_class (klass);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Escape, 0, "close", 0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_Return, 0, "select", 0);
    gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Enter, 0, "select", 0);

    gtk_widget_class_set_css_name (widget_class, "stacksidebar");
}

/**
 * xapp_icon_chooser_dialog_new:
 *
 * Creates a new #XAppIconChooserDialog.
 *
 * Returns: a newly created #XAppIconChooserDialog
 */
XAppIconChooserDialog *
xapp_icon_chooser_dialog_new (void)
{
    return g_object_new (XAPP_TYPE_ICON_CHOOSER_DIALOG, NULL);
}

/**
 * xapp_icon_chooser_dialog_run:
 * @dialog: a #XAppIconChooserDialog
 *
 * Shows the dialog and enters a separate main loop until an icon is chosen or the action is canceled.
 *
 * xapp_icon_chooser_dialog_run (), xapp_icon_chooser_dialog_run_with_icon(), and
 * xapp_icon_chooser_dialog_run_with_category () may all be called multiple times. This is useful for
 * applications which use this dialog multiple times, as it may improve performance for subsequent
 * calls.
 *
 * Returns: GTK_RESPONSE_OK if the user selected an icon, or GTK_RESPONSE_CANCEL otherwise
 */
gint
xapp_icon_chooser_dialog_run (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    gtk_widget_show_all (GTK_WIDGET (dialog));
    gtk_widget_grab_focus (priv->search_bar);

    gtk_main ();

    return priv->response;
}

/**
 * xapp_icon_chooser_dialog_run_with_icon:
 * @dialog: a #XAppIconChooserDialog
 * @icon: a string representing the icon that should be selected
 *
 * Like xapp_icon_chooser_dialog_run but selects the icon specified by @icon. This can be either an
 * icon name or a path. Passing an icon string or path that doesn't exist is accepted, but it may show
 * multiple results, or none at all. This behavior is useful if, for example, you wish to have the
 * user select an image file from a particular directory.
 *
 * If the property allow_paths is FALSE, setting a path will yield no results when the dialog is opened.
 *
 * xapp_icon_chooser_dialog_run (), xapp_icon_chooser_dialog_run_with_icon(), and
 * xapp_icon_chooser_dialog_run_with_category () may all be called multiple times. This is useful for
 * applications which use this dialog multiple times, as it may improve performance for subsequent
 * calls.
 *
 * Returns: GTK_RESPONSE_OK if the user selected an icon, or GTK_RESPONSE_CANCEL otherwise
 */
gint
xapp_icon_chooser_dialog_run_with_icon (XAppIconChooserDialog *dialog,
                                        gchar                 *icon)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    gtk_widget_show_all (GTK_WIDGET (dialog));
    gtk_entry_set_text (GTK_ENTRY (priv->search_bar), icon);
    gtk_widget_grab_focus (priv->search_bar);

    gtk_main ();

    return priv->response;
}

/**
 * xapp_icon_chooser_dialog_run_with_category:
 * @dialog: a #XAppIconChooserDialog
 *
 * Like xapp_icon_chooser_dialog_run but selects a particular category specified by @category.
 * This is used when there is a particular category of icon that is more appropriate than the
 * others. If the category does not exist, the first category in the list will be selected. To
 * get a list of possible categories, use gtk_icon_theme_list_contexts ().
 *
 * xapp_icon_chooser_dialog_run (), xapp_icon_chooser_dialog_run_with_icon(), and
 * xapp_icon_chooser_dialog_run_with_category () may all be called multiple times. This is useful for
 * applications which use this dialog multiple times, as it may improve performance for subsequent
 * calls.
 *
 * Returns: GTK_RESPONSE_OK if the user selected an icon, or GTK_RESPONSE_CANCEL otherwise
 */
gint
xapp_icon_chooser_dialog_run_with_category (XAppIconChooserDialog *dialog,
                                            gchar                 *category)
{
    XAppIconChooserDialogPrivate *priv;
    GList                        *children;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    gtk_widget_show_all (GTK_WIDGET (dialog));
    gtk_widget_grab_focus (priv->search_bar);

    children = gtk_container_get_children (GTK_CONTAINER (priv->list_box));
    for ( ; children; children = children->next)
    {
        GtkWidget    *row;
        GtkWidget    *child;
        const gchar  *context;

        row = children->data;
        child = gtk_bin_get_child (GTK_BIN (row));
        context = gtk_label_get_text (GTK_LABEL (child));
        if (g_strcmp0 (context, category) == 0)
        {
            gtk_list_box_select_row (GTK_LIST_BOX (priv->list_box), GTK_LIST_BOX_ROW (row));
            break;
        }
    }

    gtk_main ();

    return priv->response;
}

/**
 * xapp_icon_chooser_dialog_get_icon_string:
 * @dialog: a #XAppIconChooserDialog
 *
 * Gets the currently selected icon from the dialog. If allow-paths is TRUE, this function may return
 * either an icon name or a path depending on what the user selects. Otherwise it will only return an
 * icon name.
 *
 * Returns: the string representation of the currently selected icon or NULL
 * if no icon is selected.
 */
gchar *
xapp_icon_chooser_dialog_get_icon_string (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);
    return priv->icon_string;
}

gboolean
xapp_icon_chooser_dialog_close (XAppIconChooserDialog *dialog,
                                gboolean               cancelled)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    priv->response = (cancelled) ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK;
    gtk_widget_hide (GTK_WIDGET (dialog));

    gtk_main_quit ();
}

static gint
list_box_sort (GtkListBoxRow *row1,
               GtkListBoxRow *row2,
               gpointer       user_data)
{
    GtkWidget   *item;
    const gchar *label1;
    const gchar *label2;

    item = gtk_bin_get_child (GTK_BIN (row1));
    label1 = gtk_label_get_text (GTK_LABEL (item));

    item = gtk_bin_get_child (GTK_BIN (row2));
    label2 = gtk_label_get_text (GTK_LABEL (item));

    return g_strcmp0 (label1, label2);
}

static gint
search_model_sort (GtkTreeModel *model,
                   GtkTreeIter  *a,
                   GtkTreeIter  *b,
                   gpointer      user_data)
{
    gchar    *a_value;
    gchar    *b_value;
    gchar    *search_str;
    gboolean  a_starts_with;
    gboolean  b_starts_with;

    search_str = user_data;

    gtk_tree_model_get (model, a, COLUMN_DISPLAY_NAME, &a_value, -1);
    gtk_tree_model_get (model, b, COLUMN_DISPLAY_NAME, &b_value, -1);

    if (g_strcmp0 (a_value, search_str) == 0)
    {
        return -1;
    }

    if (g_strcmp0 (b_value, search_str) == 0)
    {
        return 1;
    }

    a_starts_with = g_str_has_prefix (a_value, search_str);
    b_starts_with = g_str_has_prefix (b_value, search_str);

    if (a_starts_with && !b_starts_with)
    {
        return -1;
    }

    if (!a_starts_with && b_starts_with)
    {
        return 1;
    }

    return g_strcmp0 (a_value, b_value);
}

static gint
category_model_sort (GtkTreeModel *model,
                     GtkTreeIter  *a,
                     GtkTreeIter  *b,
                     gpointer      user_data)
{
    gchar *a_value;
    gchar *b_value;

    gtk_tree_model_get (model, a, COLUMN_DISPLAY_NAME, &a_value, -1);
    gtk_tree_model_get (model, b, COLUMN_DISPLAY_NAME, &b_value, -1);

    return g_strcmp0 (a_value, b_value);
}

static void
load_categories (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;
    GtkListBoxRow                *row;
    GtkIconTheme                 *theme;
    gint                          i;
    gint                          j;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    theme = gtk_icon_theme_get_default ();

    for (i = 0; i < G_N_ELEMENTS (categories); i++)
    {
        IconCategoryDefinition  *category;
        IconCategoryInfo        *category_info;
        GtkWidget               *row;
        GtkWidget               *label;

        category = &categories[i];

        category_info = g_new (IconCategoryInfo, 1);

        category_info->name = _(category->name);
        category_info->is_loaded = FALSE;
        category_info->contexts = NULL;
        category_info->icons = NULL;

        for (j = 0; category->contexts[j] != NULL; j++)
        {
            GList *context_icons;

            category_info->contexts = g_list_prepend (category_info->contexts, g_strdup (category->contexts[j]));
            context_icons = gtk_icon_theme_list_icons (theme, category->contexts[j]);
            category_info->icons = g_list_concat (category_info->icons, context_icons);
        }

        if (g_list_length (category_info->icons) == 0)
        {
            g_list_free (category_info->contexts);
            g_free (category_info);

            continue;
        }

        category_info->model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);
        g_signal_connect (category_info->model, "row-inserted",
                          G_CALLBACK (on_icon_store_icons_added), dialog);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (category_info->model), COLUMN_DISPLAY_NAME,
                                         category_model_sort, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (category_info->model), COLUMN_DISPLAY_NAME,
                                              GTK_SORT_ASCENDING);

        row = gtk_list_box_row_new ();
        label = gtk_label_new (category_info->name);
        gtk_label_set_xalign (GTK_LABEL (label), 0.0);
        gtk_widget_set_margin_start (GTK_WIDGET (label), 6);
        gtk_widget_set_margin_end (GTK_WIDGET (label), 6);

        gtk_container_add (GTK_CONTAINER (row), label);
        gtk_container_add (GTK_CONTAINER (priv->list_box), row);

        g_hash_table_insert (priv->categories, row, category_info);
    }

    row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (priv->list_box), 0);
    gtk_list_box_select_row (GTK_LIST_BOX (priv->list_box), row);
}

static void
finish_pixbuf_load_from_file (GObject      *stream,
                              GAsyncResult *res,
                              gpointer     *user_data)
{
    IconInfoLoadCallbackInfo *callback_info;
    GdkPixbuf                *pixbuf;
    GError                   *error = NULL;
    GtkTreeIter               iter;

    callback_info = (IconInfoLoadCallbackInfo*) user_data;

    pixbuf = gdk_pixbuf_new_from_stream_finish (res, &error);

    g_input_stream_close (G_INPUT_STREAM (stream), NULL, NULL);

    if (error == NULL)
    {
        gtk_list_store_append (callback_info->model, &iter);
        gtk_list_store_set (callback_info->model, &iter,
                            COLUMN_DISPLAY_NAME, callback_info->short_name,
                            COLUMN_FULL_NAME, callback_info->long_name,
                            COLUMN_PIXBUF, pixbuf,
                            -1);
    }
    else if (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED)
    {
        g_message ("%s\n", error->message);
    }

    g_free (callback_info);
    g_clear_error (&error);
    g_object_unref (pixbuf);
    g_object_unref (stream);
}

static void
finish_pixbuf_load_from_name (GObject      *info,
                              GAsyncResult *res,
                              gpointer     *user_data)
{
    IconInfoLoadCallbackInfo *callback_info;
    GdkPixbuf                *pixbuf;
    GError                   *error = NULL;
    GtkTreeIter               iter;

    callback_info = (IconInfoLoadCallbackInfo*) user_data;

    pixbuf = gtk_icon_info_load_icon_finish (GTK_ICON_INFO (info), res, &error);

    if (error == NULL)
    {
        gtk_list_store_append (callback_info->model, &iter);
        gtk_list_store_set (callback_info->model, &iter,
                            COLUMN_DISPLAY_NAME, callback_info->short_name,
                            COLUMN_FULL_NAME, callback_info->long_name,
                            COLUMN_PIXBUF, pixbuf,
                            -1);

        g_object_unref (pixbuf);
    }
    else if (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED)
    {
        g_message ("%s\n", error->message);
    }

    g_free (callback_info);
    g_clear_error (&error);
    g_object_unref (info);
}

static void
load_icons_for_category (GtkListStore *model,
                         GList        *icons,
                         guint         icon_size)
{
    GtkIconTheme *theme;

    theme = gtk_icon_theme_get_default ();

    for ( ; icons; icons = icons->next)
    {
        GtkIconInfo              *info;
        IconInfoLoadCallbackInfo *callback_info;

        callback_info = g_new (IconInfoLoadCallbackInfo, 1);
        callback_info->model = model;
        callback_info->short_name = icons->data;
        callback_info->long_name = icons->data;
        info = gtk_icon_theme_lookup_icon (theme, callback_info->short_name, icon_size, GTK_ICON_LOOKUP_FORCE_SIZE);
        gtk_icon_info_load_icon_async (info, NULL, (GAsyncReadyCallback) (finish_pixbuf_load_from_name), callback_info);
    }
}

static void
on_category_selected (GtkListBox            *list_box,
                      XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;
    GList                        *selection;
    GtkWidget                    *selected;
    IconCategoryInfo             *category_info;
    GList                        *contexts;
    GtkTreePath                  *new_path;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    if (priv->cancellable != NULL)
    {
        g_cancellable_cancel (priv->cancellable);
        g_object_unref (priv->cancellable);
        priv->cancellable = NULL;
    }

    gtk_entry_set_text (GTK_ENTRY (priv->search_bar), "");
    selection = gtk_list_box_get_selected_rows (GTK_LIST_BOX (priv->list_box));

    if (!selection)
    {
        GtkListBoxRow *row;

        row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (priv->list_box), 0);
        gtk_list_box_select_row (GTK_LIST_BOX (priv->list_box), row);

        return;
    }

    selected = selection->data;
    category_info = g_hash_table_lookup (priv->categories, selected);
    if (!category_info->is_loaded)
    {
        load_icons_for_category (category_info->model, category_info->icons, priv->icon_size);
        category_info->is_loaded = TRUE;
    }

    gtk_icon_view_set_model (GTK_ICON_VIEW (priv->icon_view), GTK_TREE_MODEL (category_info->model));

    new_path = gtk_tree_path_new_first ();
    gtk_icon_view_select_path (GTK_ICON_VIEW (priv->icon_view), new_path);

    g_list_free (selection);
}

static void
search_path (const gchar  *path_string,
             GtkListStore *icon_store,
             GCancellable *cancellable)
{
    const gchar      *search_str = "";
    GFile            *dir;
    GFileEnumerator  *children;
    GFileInfo        *child_info;

    if (g_file_test (path_string, G_FILE_TEST_IS_DIR))
    {
        dir = g_file_new_for_path (path_string);
    }
    else
    {
        GFile *file;

        file = g_file_new_for_path (path_string);
        dir = g_file_get_parent (file);
        search_str = g_file_get_basename (file);
        g_object_unref (file);
    }

    if (!g_file_query_exists (dir, NULL) ||
        g_file_query_file_type (dir, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY)
    {
        g_object_unref (dir);
        return;
    }

    children = g_file_enumerate_children (dir, "*", G_FILE_QUERY_INFO_NONE, NULL, NULL);
    child_info = g_file_enumerator_next_file (children, NULL, NULL);

    while (child_info != NULL)
    {
        const gchar  *child_name;
        const gchar  *child_path;
        GFile        *child;
        GError       *error = NULL;

        child_name = g_file_info_get_name (child_info);
        child = g_file_enumerator_get_child (children, child_info);
        child_path = g_file_get_path (child);

        if (g_str_has_prefix (child_name, search_str) &&
            g_file_query_file_type (child, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY &&
            g_str_has_prefix (g_content_type_guess (child_name, NULL, 0, NULL), "image") &&
            g_access (child_path, R_OK) == 0)
        {
            GFileInputStream         *stream;
            IconInfoLoadCallbackInfo *callback_info;

            stream = g_file_read (child, NULL, &error);
            if (stream == NULL)
            {
                g_message ("%s\n", error->message);
                continue;
            }

            callback_info = g_new (IconInfoLoadCallbackInfo, 1);
            callback_info->model = icon_store;
            callback_info->short_name = child_name;
            callback_info->long_name = child_path;
            gdk_pixbuf_new_from_stream_async (G_INPUT_STREAM (stream), cancellable, (GAsyncReadyCallback) (finish_pixbuf_load_from_file), callback_info);

            g_object_unref (child);
        }

        child_info = g_file_enumerator_next_file (children, NULL, NULL);

        g_clear_error (&error);
    }

    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (icon_store), COLUMN_DISPLAY_NAME, search_model_sort,
                                     (gpointer) search_str, NULL);

    g_file_enumerator_close (children, NULL, NULL);
    g_object_unref (children);
    g_object_unref (dir);
}

static void
search_icon_name (const gchar  *name_string,
                  GtkListStore *icon_store,
                  GCancellable *cancellable,
                  guint         icon_size)
{
    GtkIconTheme             *theme;
    GList                    *icons;
    GtkIconInfo              *info;
    IconInfoLoadCallbackInfo *callback_info;

    theme = gtk_icon_theme_get_default ();

    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (icon_store), COLUMN_DISPLAY_NAME, search_model_sort,
                                     (gpointer) name_string, NULL);

    icons = gtk_icon_theme_list_icons (theme, NULL);

    for ( ; icons; icons = icons->next)
    {
        if (g_strrstr (icons->data, name_string)) {
            callback_info = g_new (IconInfoLoadCallbackInfo, 1);
            callback_info->model = icon_store;
            callback_info->short_name = icons->data;
            callback_info->long_name = icons->data;
            info = gtk_icon_theme_lookup_icon (theme, callback_info->short_name, icon_size, GTK_ICON_LOOKUP_FORCE_SIZE);
            gtk_icon_info_load_icon_async (info, cancellable, (GAsyncReadyCallback) (finish_pixbuf_load_from_name), callback_info);
        }
    }
    g_list_free_full (icons, g_free);
}

static void
on_search (GtkSearchEntry        *entry,
           XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;
    const gchar                  *search_text;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    if (priv->cancellable != NULL)
    {
        g_cancellable_cancel (priv->cancellable);
        g_object_unref (priv->cancellable);
    }
    priv->cancellable = g_cancellable_new ();

    search_text = gtk_entry_get_text (GTK_ENTRY (entry));

    if (g_strcmp0 (search_text, "") == 0)
    {
        on_category_selected (GTK_LIST_BOX (priv->list_box), dialog);
    }
    else
    {
        gtk_list_store_clear (GTK_LIST_STORE (priv->icon_store));
        gtk_icon_view_set_model (GTK_ICON_VIEW (priv->icon_view), GTK_TREE_MODEL (priv->icon_store));
        if (g_strrstr (search_text, "/"))
        {
            if (priv->allow_paths)
            {
                search_path (search_text, priv->icon_store, priv->cancellable);
            }
        }
        else
        {
            search_icon_name (search_text, priv->icon_store, priv->cancellable, priv->icon_size);
        }
    }
}

static void
on_icon_view_selection_changed (GtkIconView *icon_view,
                                gpointer     user_data)
{
    XAppIconChooserDialogPrivate *priv;
    GList                        *selected_items;
    gchar                        *icon_string = "";

    priv = xapp_icon_chooser_dialog_get_instance_private (user_data);

    selected_items = gtk_icon_view_get_selected_items (icon_view);
    if (selected_items == NULL)
    {
        gtk_widget_set_sensitive (GTK_WIDGET (priv->select_button), FALSE);
    }
    else
    {
        GtkTreePath  *tree_path;
        GtkTreeModel *model;
        GtkTreeIter   iter;

        gtk_widget_set_sensitive (GTK_WIDGET (priv->select_button), TRUE);

        tree_path = selected_items->data;
        model = gtk_icon_view_get_model (icon_view);
        gtk_tree_model_get_iter (model, &iter, tree_path);
        gtk_tree_model_get (model, &iter, COLUMN_FULL_NAME, &icon_string, -1);
    }

    priv->icon_string = icon_string;

    g_list_free_full (selected_items, (GDestroyNotify) gtk_tree_path_free);
}

static void
on_icon_store_icons_added (GtkTreeModel *tree_model,
                           GtkTreePath  *path,
                           GtkTreeIter  *iter,
                           gpointer      user_data)
{
    XAppIconChooserDialogPrivate *priv;
    GtkTreePath                  *new_path;

    priv = xapp_icon_chooser_dialog_get_instance_private (user_data);

    if (tree_model != gtk_icon_view_get_model (GTK_ICON_VIEW (priv->icon_view))) {
        return;
    }

    new_path = gtk_tree_path_new_first ();
    gtk_icon_view_select_path (GTK_ICON_VIEW (priv->icon_view), new_path);
}

static void
on_browse_button_clicked (GtkButton *button,
                          gpointer   user_data)
{
    XAppIconChooserDialog        *dialog = user_data;
    XAppIconChooserDialogPrivate *priv;
    GtkWidget                    *file_dialog;
    const gchar                  *search_text;
    GtkFileFilter                *file_filter;
    gint                          response;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    file_dialog = gtk_file_chooser_dialog_new (_("Select image file"),
                                               GTK_WINDOW (dialog),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               _("Cancel"),
                                               GTK_RESPONSE_CANCEL,
                                               _("Open"),
                                               GTK_RESPONSE_ACCEPT,
                                               NULL);

    search_text = gtk_entry_get_text (GTK_ENTRY (priv->search_bar));
    if (g_strrstr (search_text, "/"))
    {
        gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (file_dialog), search_text);
    }
    else
    {
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_dialog), "/usr/share/icons/");
    }

    file_filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (file_filter, _("Image"));
    gtk_file_filter_add_pixbuf_formats (file_filter);
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), file_filter);

    response = gtk_dialog_run (GTK_DIALOG (file_dialog));
    if (response == GTK_RESPONSE_ACCEPT)
    {
        gchar *filename;

        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
        gtk_entry_set_text (GTK_ENTRY (priv->search_bar), filename);
        g_free (filename);
    }

    gtk_widget_destroy (file_dialog);
}

static void
on_select_button_clicked (GtkButton *button,
                          gpointer   user_data)
{
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), FALSE);
}

static void
on_cancel_button_clicked (GtkButton *button,
                          gpointer   user_data)
{
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), TRUE);
}

static gboolean
on_delete_event (GtkWidget   *widget,
                 GdkEventAny *event)
{
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (widget), TRUE);
}

static gboolean
on_select_event (XAppIconChooserDialog *dialog,
                 GdkEventAny           *event)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    if (g_strcmp0 (priv->icon_string, "") != 0)
    {
        xapp_icon_chooser_dialog_close (dialog, FALSE);
    }
}

static void
on_icon_view_item_activated (GtkIconView *iconview,
                             GtkTreePath *path,
                             gpointer     user_data)
{
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), FALSE);
}

static gboolean
on_search_bar_key_pressed (GtkWidget *widget,
                           GdkEvent  *event,
                           gpointer   user_data)
{
    XAppIconChooserDialogPrivate *priv;
    guint                         keyval;

    priv = xapp_icon_chooser_dialog_get_instance_private (user_data);

    gdk_event_get_keyval (event, &keyval);
    switch (keyval)
    {
        case GDK_KEY_Escape:
            xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), TRUE);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (g_strcmp0 (priv->icon_string, "") != 0)
            {
                xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), FALSE);
            }
            return TRUE;
    }

    return FALSE;
}

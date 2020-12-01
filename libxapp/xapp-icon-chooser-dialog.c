#include <config.h>
#include <gdk/gdk.h>
#include <math.h>
#include <cairo-gobject.h>

#include "xapp-enums.h"
#include "xapp-icon-chooser-dialog.h"
#include "xapp-stack-sidebar.h"
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#define DEBUG_REFS 0
#define DEBUG_ICON_THEME 0

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
    const gchar  *name; /* This is a translation which doesn't get freed */
    GList        *icons;
    GList        *iter;
    GtkListStore *model;
} IconCategoryInfo;

typedef struct
{
    GtkResponseType  response;
    XAppIconSize     icon_size;
    GtkListStore    *search_icon_store;
    GFileEnumerator *search_file_enumerator;
    GCancellable    *cancellable;
    GList           *full_icon_list;
    GList           *search_iter;
    GHashTable      *categories;
    GHashTable      *surfaces_by_name;
    GtkWidget       *search_bar;
    GtkWidget       *icon_view;
    GtkWidget       *list_box;
    GtkWidget       *default_button;
    GtkWidget       *select_button;
    GtkWidget       *browse_button;
    GtkWidget       *action_area;
    GtkWidget       *loading_bar;
    GtkCellArea     *ca_box;
    gchar           *icon_string;
    gchar           *current_text;
    gulong           search_changed_id;
    gboolean         allow_paths;
    gchar           *default_icon;
    IconCategoryInfo *current_category;
} XAppIconChooserDialogPrivate;

struct _XAppIconChooserDialog
{
    XAppGtkWindow parent_instance;
};

typedef struct
{
    XAppIconChooserDialog *dialog;
    GtkListStore          *model;
    IconCategoryInfo      *category_info;
    GCancellable          *cancellable;
    cairo_surface_t       *surface;
    const gchar           *name;
    gboolean               chunk_end;
} NamedIconInfoLoadCallbackInfo;

typedef struct
{
    XAppIconChooserDialog *dialog;
    GtkListStore          *model;
    GCancellable          *cancellable;
    GFileEnumerator       *enumerator;
    gchar                 *short_name;
    gchar                 *long_name;
    gboolean               chunk_end;
} FileIconInfoLoadCallbackInfo;

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
    PROP_DEFAULT_ICON,
    N_PROPERTIES
};

enum
{
    COLUMN_DISPLAY_NAME,
    COLUMN_FULL_NAME,
    COLUMN_SURFACE,
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static guint signals[LAST_SIGNAL] = {0, };

G_DEFINE_TYPE_WITH_PRIVATE (XAppIconChooserDialog, xapp_icon_chooser_dialog, XAPP_TYPE_GTK_WINDOW)

static void on_category_selected (GtkListBox            *list_box,
                                 XAppIconChooserDialog *dialog);

static void on_search_text_changed (GtkSearchEntry        *entry,
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

static void on_default_button_clicked (GtkButton *button,
                                           gpointer   user_data);

static gboolean on_search_bar_key_pressed (GtkWidget *widget,
                                           GdkEvent  *event,
                                           gpointer   user_data);

static gboolean on_delete_event (GtkWidget   *widget,
                                 GdkEventAny *event);

static void on_icon_view_item_activated (GtkIconView *iconview,
                                         GtkTreePath *path,
                                         gpointer     user_data);

static void load_categories (XAppIconChooserDialog *dialog);

static void load_icons_for_category (XAppIconChooserDialog *dialog,
                                     IconCategoryInfo      *category_info,
                                     guint                  icon_size);

static void search_path (XAppIconChooserDialog *dialog,
                         const gchar           *path_string,
                         GtkListStore          *icon_store);

static void search_icon_name (XAppIconChooserDialog *dialog,
                              const gchar           *name_string,
                              GtkListStore          *icon_store);

static gint list_box_sort (GtkListBoxRow *row1,
                           GtkListBoxRow *row2,
                           gpointer       user_data);

static gint search_model_sort (GtkTreeModel *model,
                               GtkTreeIter  *a,
                               GtkTreeIter  *b,
                               gpointer      user_data);

static void
free_category_info (IconCategoryInfo *category_info)
{
    g_list_free_full (category_info->icons, g_free);

    g_clear_object (&category_info->model);

    g_free (category_info);
}

static void
free_file_info (FileIconInfoLoadCallbackInfo *file_info)
{
    g_object_unref (file_info->cancellable);

    g_object_unref (file_info->enumerator);

    g_free (file_info->short_name);
    g_free (file_info->long_name);

    g_free (file_info);
}

static void
free_named_info (NamedIconInfoLoadCallbackInfo *named_info)
{
    g_object_unref (named_info->cancellable);

    g_clear_pointer (&named_info->surface, cairo_surface_destroy);

    g_free (named_info);
}

#if DEBUG_REFS
static void
on_cancellable_finalize (gpointer  data,
                         GObject  *object)
{
    g_printerr ("Cancellable Finalize: %p\n", object);
}

static void
on_enumerator_finalize (gpointer  data,
                         GObject  *object)
{
    g_printerr ("Enumerator Finalize: %p\n", object);
}
#endif

static void
on_enumerator_toggle_ref_called (gpointer data,
                                 GObject *object,
                                 gboolean is_last_ref)
{
    XAppIconChooserDialog *dialog;
    XAppIconChooserDialogPrivate *priv;

    dialog = XAPP_ICON_CHOOSER_DIALOG (data);
    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    if (is_last_ref)
    {
        g_object_remove_toggle_ref (object,
                                    (GToggleNotify) on_enumerator_toggle_ref_called,
                                    dialog);

        priv->search_file_enumerator = NULL;
    }
}

static void
clear_search_state (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    g_cancellable_cancel (priv->cancellable);
    g_clear_object (&priv->cancellable);

    g_clear_object (&priv->search_file_enumerator);

    gtk_widget_hide (priv->loading_bar);
    priv->search_iter = NULL;
}

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
        case PROP_DEFAULT_ICON:
            g_value_set_string (value, xapp_icon_chooser_dialog_get_default_icon(dialog));
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
        case PROP_DEFAULT_ICON:
            xapp_icon_chooser_dialog_set_default_icon (dialog, g_value_get_string (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
xapp_icon_chooser_dialog_dispose (GObject *object)
{
    XAppIconChooserDialog        *dialog;
    XAppIconChooserDialogPrivate *priv;

    dialog = XAPP_ICON_CHOOSER_DIALOG (object);
    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    if (priv->categories != NULL)
    {
        g_hash_table_destroy (priv->categories);
        priv->categories = NULL;
    }

    if (priv->surfaces_by_name != NULL)
    {
        g_hash_table_destroy (priv->surfaces_by_name);
        priv->surfaces_by_name = NULL;
    }

    g_clear_pointer (&priv->icon_string, g_free);
    g_clear_pointer (&priv->default_icon, g_free);
    g_clear_pointer (&priv->current_text, g_free);
    g_clear_object (&priv->cancellable);

    G_OBJECT_CLASS (xapp_icon_chooser_dialog_parent_class)->dispose (object);
}

static void
xapp_icon_chooser_dialog_init (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;
    GtkWidget                    *main_box;
    GtkWidget                    *secondary_box;
    GtkWidget                    *toolbar;
    GtkWidget                    *overlay;
    GtkWidget                    *spinner;
    GtkWidget                    *spinner_label;
    GtkWidget                    *loading_bar_box;
    GtkToolItem                  *tool_item;
    GtkWidget                    *toolbar_box;
    GtkWidget                    *right_box;
    GtkStyleContext              *style;
    GtkSizeGroup                 *button_size_group;
    GtkWidget                    *cancel_button;
    GtkWidget                    *button_area;
    GtkWidget                    *scrolled_window;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    priv->icon_size = XAPP_ICON_SIZE_32;
    priv->categories = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) free_category_info);

    /* surfaces_by_name will save surfaces generated by icon name, so they can avoid creating
     * them again when they're reloaded (like re-selecting a previously selected category) */
    priv->surfaces_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) cairo_surface_destroy);

    priv->response = GTK_RESPONSE_NONE;
    priv->icon_string = NULL;
    priv->current_text = NULL;
    priv->cancellable = NULL;
    priv->allow_paths = TRUE;
    priv->full_icon_list = NULL;

    priv->search_icon_store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, CAIRO_GOBJECT_TYPE_SURFACE);

    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (priv->search_icon_store),
                                     COLUMN_DISPLAY_NAME,
                                     search_model_sort,
                                     priv,
                                     NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->search_icon_store),
                                          COLUMN_DISPLAY_NAME,
                                          GTK_SORT_ASCENDING);

    g_signal_connect (priv->search_icon_store, "row-inserted",
                      G_CALLBACK (on_icon_store_icons_added), dialog);

    gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 450);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), TRUE);
    gtk_window_set_type_hint (GTK_WINDOW (dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_title (GTK_WINDOW (dialog), _("Choose an icon"));

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

    priv->search_changed_id = g_signal_connect (priv->search_bar, "search-changed",
                                                G_CALLBACK (on_search_text_changed), dialog);
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

    overlay = gtk_overlay_new ();
    gtk_box_pack_start (GTK_BOX (right_box), overlay, TRUE, TRUE, 0);

    // icon view
    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_overlay_add_overlay (GTK_OVERLAY (overlay), scrolled_window);
    gtk_widget_set_halign (scrolled_window, GTK_ALIGN_FILL);
    gtk_widget_set_valign (scrolled_window, GTK_ALIGN_FILL);

    priv->loading_bar = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (priv->loading_bar), GTK_SHADOW_NONE);

    gtk_style_context_add_class (gtk_widget_get_style_context (priv->loading_bar),
                                 "background");

    gtk_overlay_add_overlay (GTK_OVERLAY (overlay), priv->loading_bar);
    gtk_widget_set_halign (priv->loading_bar, GTK_ALIGN_START);
    gtk_widget_set_valign (priv->loading_bar, GTK_ALIGN_END);
    gtk_widget_set_no_show_all (priv->loading_bar, TRUE);

    loading_bar_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add (GTK_CONTAINER (priv->loading_bar), loading_bar_box);
    g_object_set (loading_bar_box,
                  "margin", 4,
                  NULL);

    spinner = gtk_spinner_new ();
    gtk_spinner_start (GTK_SPINNER (spinner));
    gtk_box_pack_start (GTK_BOX (loading_bar_box), spinner, FALSE, FALSE, 4);

    spinner_label = gtk_label_new (_("Loading..."));
    gtk_box_pack_start (GTK_BOX (loading_bar_box), spinner_label, FALSE, FALSE, 4);

    gtk_widget_show_all (loading_bar_box);

    GtkCellArea *ca_box = gtk_cell_area_box_new ();
    gtk_orientable_set_orientation (GTK_ORIENTABLE (ca_box), GTK_ORIENTATION_VERTICAL);

    GtkCellRenderer *r;

    r = gtk_cell_renderer_pixbuf_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (ca_box), r, FALSE);
    gtk_cell_area_attribute_connect (GTK_CELL_AREA (ca_box), r,
                                     "surface",
                                     COLUMN_SURFACE);

    r = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (ca_box), r, FALSE);
    gtk_cell_area_attribute_connect (GTK_CELL_AREA (ca_box), r,
                                     "text",
                                     COLUMN_DISPLAY_NAME);
    g_object_set (G_OBJECT (r),
                  "alignment", PANGO_ALIGN_CENTER,
                  "xalign", 0.5,
                  "width-chars", 20,
                  "wrap-mode", PANGO_WRAP_WORD_CHAR,
                  "wrap-width", 0,
                  NULL);

    priv->ca_box = ca_box;
    priv->icon_view = gtk_icon_view_new_with_area (GTK_CELL_AREA (ca_box));
    gtk_container_add(GTK_CONTAINER (scrolled_window), GTK_WIDGET (priv->icon_view));

    gtk_icon_view_set_tooltip_column (GTK_ICON_VIEW (priv->icon_view), COLUMN_FULL_NAME);

    g_signal_connect (priv->icon_view, "selection-changed",
                      G_CALLBACK (on_icon_view_selection_changed), dialog);
    g_signal_connect (priv->icon_view, "item-activated",
                      G_CALLBACK (on_icon_view_item_activated), dialog);

    // buttons
    button_area = gtk_action_bar_new ();
    priv->action_area = button_area;
    gtk_box_pack_start (GTK_BOX (main_box), button_area, FALSE, FALSE, 0);

    button_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

    priv->default_button = gtk_button_new_with_label (_("Default"));
    gtk_widget_set_no_show_all (priv->default_button, TRUE);
    style = gtk_widget_get_style_context (GTK_WIDGET (priv->default_button));
    gtk_style_context_add_class (style, "text-button");
    gtk_size_group_add_widget (button_size_group, priv->default_button);
    gtk_action_bar_pack_start (GTK_ACTION_BAR (button_area), priv->default_button);

    g_signal_connect (priv->default_button, "clicked",
                      G_CALLBACK (on_default_button_clicked), dialog);

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
    object_class->dispose = xapp_icon_chooser_dialog_dispose;

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

    /**
     * XAppIconChooserDialog:default-icon:
     *
     * The icon to use by default.
     */
    obj_properties[PROP_DEFAULT_ICON] =
        g_param_spec_string ("default-icon",
                             _("Default Icon"),
                             _("The icon to use by default"),
                             NULL,
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
 * xapp_icon_chooser_dialog_get_default_icon:
 *
 * Returns the default icon (if set).
 *
 * Returns: (transfer full): the default icon, or NULL if none is set
 */
gchar *
xapp_icon_chooser_dialog_get_default_icon (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    return g_strdup(priv->default_icon);
}

/**
 * xapp_icon_chooser_dialog_set_default_icon:
 * @icon: the default icon, or NULL to unset
 *
 * Sets the default icon. If @icon is not NULL, a button will be shown that
 * will reset the dialog to it's default value.
 */
void
xapp_icon_chooser_dialog_set_default_icon (XAppIconChooserDialog *dialog,
                                           const gchar           *icon)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    priv->default_icon = g_strdup (icon);
    if (icon == NULL)
    {
        gtk_widget_hide (priv->default_button);
    }
    else
    {
        gtk_widget_show (priv->default_button);
    }
}

/**
 * xapp_icon_chooser_dialog_add_custom_category:
 * @dialog: a #XAppIconChooserDialog
 * @name: the name of the category as it will be displayed in the category list
 * @icons: (transfer full) (element-type utf8): a list of icon names to add to the new category
 *
 * Adds a custom category to the dialog.
 */
void
xapp_icon_chooser_dialog_add_custom_category   (XAppIconChooserDialog *dialog,
                                                const gchar           *name,
                                                GList                 *icons)
{
    XAppIconChooserDialogPrivate *priv;
    IconCategoryInfo        *category_info;
    GtkWidget               *row;
    GtkWidget               *label;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    category_info = g_new0 (IconCategoryInfo, 1);
    category_info->name = name;
    category_info->icons = icons;

    category_info->model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, CAIRO_GOBJECT_TYPE_SURFACE);
    g_signal_connect (category_info->model, "row-inserted",
                      G_CALLBACK (on_icon_store_icons_added), dialog);

    category_info->icons = g_list_sort (category_info->icons, (GCompareFunc) g_utf8_collate);

    row = gtk_list_box_row_new ();
    label = gtk_label_new (category_info->name);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_widget_set_margin_start (GTK_WIDGET (label), 6);
    gtk_widget_set_margin_end (GTK_WIDGET (label), 6);

    gtk_container_add (GTK_CONTAINER (row), label);
    gtk_container_add (GTK_CONTAINER (priv->list_box), row);

    g_hash_table_insert (priv->categories, row, category_info);
}

/**
 * xapp_icon_chooser_dialog_get_icon_string:
 * @dialog: a #XAppIconChooserDialog
 *
 * Gets the currently selected icon from the dialog. If allow-paths is TRUE, this function may return
 * either an icon name or a path depending on what the user selects. Otherwise it will only return an
 * icon name.
 *
 * Returns: (transfer full): the string representation of the currently selected icon or NULL
 * if no icon is selected.
 */
gchar *
xapp_icon_chooser_dialog_get_icon_string (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    return g_strdup (priv->icon_string);
}

static void
xapp_icon_chooser_dialog_close (XAppIconChooserDialog *dialog,
                                GtkResponseType        response)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    priv->response = response;
    gtk_widget_hide (GTK_WIDGET (dialog));

    gtk_main_quit ();
}

static void
on_custom_button_clicked (GtkButton *button,
                          gpointer   user_data)
{
    GtkResponseType response_id;

    response_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "response-id"));

    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), response_id);
}

/**
 * xapp_icon_chooser_dialog_add_button:
 * @dialog: an #XAppIconChooserDialog
 * @button: a #GtkButton to add
 * @packing: the #GtkPackType to specify start or end packing to the action bar
 * @response_id: the dialog response id to return when this button is clicked.
 *
 * Allows a button to be added to the #GtkActionBar of the dialog with a custom
 * response id.
 */
void
xapp_icon_chooser_dialog_add_button (XAppIconChooserDialog *dialog,
                                     GtkWidget             *button,
                                     GtkPackType            packing,
                                     GtkResponseType        response_id)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    g_signal_connect (button,
                      "clicked",
                      G_CALLBACK (on_custom_button_clicked),
                      dialog);

    /* This saves having to use a custom container for callback data. */
    g_object_set_data (G_OBJECT (button),
                       "response-id", GINT_TO_POINTER (response_id));

    if (packing == GTK_PACK_START)
    {
        gtk_action_bar_pack_start (GTK_ACTION_BAR (priv->action_area), button);
    }
    else
    {
        gtk_action_bar_pack_end (GTK_ACTION_BAR (priv->action_area), button);
    }
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
    gchar       *a_value;
    gchar       *b_value;
    gchar       *search_str;
    gboolean     a_starts_with;
    gboolean     b_starts_with;
    gint ret;

    XAppIconChooserDialogPrivate *priv = (XAppIconChooserDialogPrivate *) user_data;

    search_str = priv->current_text;

    gtk_tree_model_get (model, a, COLUMN_DISPLAY_NAME, &a_value, -1);
    gtk_tree_model_get (model, b, COLUMN_DISPLAY_NAME, &b_value, -1);

    ret = g_strcmp0 (a_value, b_value);

    if (search_str == NULL)
    {
    }
    else
    if (g_strcmp0 (a_value, search_str) == 0)
    {
        ret = -1;
    }
    else
    if (g_strcmp0 (b_value, search_str) == 0)
    {
        ret = 1;
    }
    else
    {
        a_starts_with = g_str_has_prefix (a_value, search_str);
        b_starts_with = g_str_has_prefix (b_value, search_str);

        if (a_starts_with && !b_starts_with)
        {
            ret = -1;
        }

        if (!a_starts_with && b_starts_with)
        {
            ret = 1;
        }
    }

    g_free (a_value);
    g_free (b_value);

    return ret;
}

static void
load_categories (XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;
    GtkListBoxRow                *row;
    GtkIconTheme                 *theme;
    GList                        *contexts, *l;
    gint                          i;
    gint                          j;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    theme = gtk_icon_theme_get_default ();
    contexts = gtk_icon_theme_list_contexts (theme);

    for (i = 0; i < G_N_ELEMENTS (categories); i++)
    {
        IconCategoryDefinition  *category;
        IconCategoryInfo        *category_info;
        GtkWidget               *row;
        GtkWidget               *label;
        GList                   *context_icons;

        category = &categories[i];

        category_info = g_new0 (IconCategoryInfo, 1);

        category_info->name = _(category->name);

        for (j = 0; category->contexts[j] != NULL; j++)
        {
            GList *match;

            context_icons = gtk_icon_theme_list_icons (theme, category->contexts[j]);
            category_info->icons = g_list_concat (category_info->icons, context_icons);

            match = g_list_find_custom (contexts, category->contexts[j], (GCompareFunc) g_strcmp0);

            if (match)
            {
                contexts = g_list_remove_link (contexts, match);
                g_free (match->data);
                g_list_free (match);
            }
        }

        /* Any contexts not consumed by categories should be added to the 'other' category */
        if (i == (G_N_ELEMENTS (categories) - 1) && g_list_length (contexts) > 0)
        {
            for (l = contexts; l != NULL; l = l->next)
            {

#if DEBUG_ICON_THEME
                g_message ("Adding unused category to Other category: '%s'", (gchar *) l->data);
#endif
                context_icons = gtk_icon_theme_list_icons (theme, (gchar *) l->data);

                category_info->icons = g_list_concat (category_info->icons, context_icons);
            }
        }

        if (g_list_length (category_info->icons) == 0)
        {
            free_category_info (category_info);

            continue;
        }

        /* Add the list of icons for this category into our master search list */
        priv->full_icon_list = g_list_concat (priv->full_icon_list,
                                              g_list_copy (category_info->icons));

        category_info->model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, CAIRO_GOBJECT_TYPE_SURFACE);
        g_signal_connect (category_info->model, "row-inserted",
                          G_CALLBACK (on_icon_store_icons_added), dialog);

        category_info->icons = g_list_sort (category_info->icons, (GCompareFunc) g_utf8_collate);

        row = gtk_list_box_row_new ();
        label = gtk_label_new (category_info->name);
        gtk_label_set_xalign (GTK_LABEL (label), 0.0);
        gtk_widget_set_margin_start (GTK_WIDGET (label), 6);
        gtk_widget_set_margin_end (GTK_WIDGET (label), 6);

        gtk_container_add (GTK_CONTAINER (row), label);
        gtk_container_add (GTK_CONTAINER (priv->list_box), row);

        g_hash_table_insert (priv->categories, row, category_info);
    }

    g_list_free_full (contexts, g_free);

    priv->full_icon_list = g_list_sort (priv->full_icon_list, (GCompareFunc) g_utf8_collate);

    row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (priv->list_box), 0);
    gtk_list_box_select_row (GTK_LIST_BOX (priv->list_box), row);
}

static gboolean
load_next_file_search_chunk (gpointer user_data)
{
    XAppIconChooserDialogPrivate *priv;
    FileIconInfoLoadCallbackInfo *callback_info;

    callback_info = (FileIconInfoLoadCallbackInfo*) user_data;
    priv = xapp_icon_chooser_dialog_get_instance_private (callback_info->dialog);

    if (g_cancellable_is_cancelled (callback_info->cancellable))
    {
        free_file_info (callback_info);

        return G_SOURCE_REMOVE;
    }

    search_path (callback_info->dialog,
                 priv->current_text,
                 priv->search_icon_store);

    free_file_info (callback_info);

    return G_SOURCE_REMOVE;
}

static gboolean
load_next_category_chunk (gpointer user_data)
{
    XAppIconChooserDialogPrivate *priv;
    NamedIconInfoLoadCallbackInfo *callback_info;

    callback_info = (NamedIconInfoLoadCallbackInfo*) user_data;
    priv = xapp_icon_chooser_dialog_get_instance_private (callback_info->dialog);

    if (g_cancellable_is_cancelled (callback_info->cancellable))
    {
        free_named_info (callback_info);

        return G_SOURCE_REMOVE;
    }

    load_icons_for_category (callback_info->dialog,
                             callback_info->category_info,
                             priv->icon_size);

    free_named_info (callback_info);

    return G_SOURCE_REMOVE;
}

static gboolean
load_next_name_search_chunk (gpointer user_data)
{
    XAppIconChooserDialogPrivate *priv;
    NamedIconInfoLoadCallbackInfo *callback_info;

    callback_info = (NamedIconInfoLoadCallbackInfo*) user_data;
    priv = xapp_icon_chooser_dialog_get_instance_private (callback_info->dialog);

    if (g_cancellable_is_cancelled (callback_info->cancellable))
    {
        free_named_info (callback_info);

        return G_SOURCE_REMOVE;
    }

    search_icon_name (callback_info->dialog,
                      priv->current_text,
                      priv->search_icon_store);

    free_named_info (callback_info);

    return G_SOURCE_REMOVE;
}

static void
finish_pixbuf_load_from_file (GObject      *stream,
                              GAsyncResult *res,
                              gpointer     *user_data)
{
    FileIconInfoLoadCallbackInfo *callback_info;
    GdkPixbuf                *pixbuf;
    GError                   *error = NULL;

    callback_info = (FileIconInfoLoadCallbackInfo *) user_data;

    pixbuf = gdk_pixbuf_new_from_stream_finish (res, &error);

    g_input_stream_close (G_INPUT_STREAM (stream), NULL, NULL);
    g_object_unref (stream);

    if (g_cancellable_is_cancelled (callback_info->cancellable))
    {
        g_clear_object (&pixbuf);
        free_file_info (callback_info);

        return;
    }

    if (pixbuf == NULL)
    {
        if (error && (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED))
        {
            g_message ("%s\n", error->message);
        }

        g_clear_error (&error);
    }

    if (pixbuf)
    {
        GtkTreeIter iter;
        cairo_surface_t *surface;

        surface = gdk_cairo_surface_create_from_pixbuf (pixbuf,
                                                        0,
                                                        gtk_widget_get_window (GTK_WIDGET (callback_info->dialog)));

        gtk_list_store_append (callback_info->model, &iter);
        gtk_list_store_set (callback_info->model, &iter,
                            COLUMN_DISPLAY_NAME, callback_info->short_name,
                            COLUMN_FULL_NAME, callback_info->long_name,
                            COLUMN_SURFACE, surface,
                            -1);

        cairo_surface_destroy (surface);
        g_object_unref (pixbuf);
    }

    if (callback_info->chunk_end)
    {
        g_idle_add ((GSourceFunc) load_next_file_search_chunk, callback_info);
    }
    else
    {
        free_file_info (callback_info);
    }

}

static void
add_named_entry (NamedIconInfoLoadCallbackInfo *callback_info,
                 cairo_surface_t               *surface)
{
    GtkTreeIter iter;

    gtk_list_store_append (callback_info->model, &iter);
    gtk_list_store_set (callback_info->model, &iter,
                        COLUMN_DISPLAY_NAME, callback_info->name,
                        COLUMN_FULL_NAME, callback_info->name,
                        COLUMN_SURFACE, surface,
                        -1);
}

static gboolean
add_named_entry_with_existing_surface (gpointer user_data)
{
    NamedIconInfoLoadCallbackInfo *callback_info;

    callback_info = (NamedIconInfoLoadCallbackInfo*) user_data;

    if (g_cancellable_is_cancelled (callback_info->cancellable))
    {
        free_named_info (callback_info);

        return G_SOURCE_REMOVE;
    }

    /* Category results have a category_info attached. */
    if (callback_info->category_info)
    {
        add_named_entry (callback_info, callback_info->surface);

        if (callback_info->chunk_end)
        {
            g_idle_add ((GSourceFunc) load_next_category_chunk, callback_info);

            return G_SOURCE_REMOVE;
        }
    }
    /* Otherwise, it's a search result set */
    else
    {
        add_named_entry (callback_info, callback_info->surface);

        if (callback_info->chunk_end)
        {
            g_idle_add ((GSourceFunc) load_next_name_search_chunk, callback_info);

            return G_SOURCE_REMOVE;
        }
    }

    free_named_info (callback_info);

    return G_SOURCE_REMOVE;
}

static void
finish_pixbuf_load_from_name (GObject      *info,
                              GAsyncResult *res,
                              gpointer     *user_data)
{
    XAppIconChooserDialogPrivate *priv;
    NamedIconInfoLoadCallbackInfo *callback_info;
    GdkPixbuf                *pixbuf;
    cairo_surface_t          *surface;
    GError                   *error = NULL;

    callback_info = (NamedIconInfoLoadCallbackInfo*) user_data;
    priv = xapp_icon_chooser_dialog_get_instance_private (callback_info->dialog);

    pixbuf = gtk_icon_info_load_symbolic_for_context_finish (GTK_ICON_INFO (info), res, NULL, &error);
    g_object_unref (info);

    if (g_cancellable_is_cancelled (callback_info->cancellable))
    {
        g_clear_object (&pixbuf);

        free_named_info (callback_info);

        return;
    }

    if (pixbuf == NULL)
    {
        if (error && (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CANCELLED))
        {
            g_message ("%s\n", error->message);
        }

        free_named_info (callback_info);

        g_clear_error (&error);

        return;
    }

    surface = gdk_cairo_surface_create_from_pixbuf (pixbuf,
                                                    0,
                                                    gtk_widget_get_window (GTK_WIDGET (callback_info->dialog)));
    g_object_unref (pixbuf);

    /* Hash table 'takes' reference, we don't have to free surface.
       callback_info->name is already owned by priv->full_icon_list so
       it needs to be copied */
    g_hash_table_insert (priv->surfaces_by_name,
                         g_strdup (callback_info->name),
                         (gpointer) surface);

    /* If there's a category_info, this is a category selection. */
    if (callback_info->category_info)
    {
        add_named_entry (callback_info, surface);

        if (callback_info->chunk_end)
        {
            g_idle_add ((GSourceFunc) load_next_category_chunk, callback_info);

            return;
        }
    }
    /* Otherwise, it's a search result set */
    else
    {
        add_named_entry (callback_info, surface);

        if (callback_info->chunk_end)
        {
            g_idle_add ((GSourceFunc) load_next_name_search_chunk, callback_info);

            return;
        }
    }

    free_named_info (callback_info);
}

#define CATEGORY_CHUNK_SIZE 500

static void
load_icons_for_category (XAppIconChooserDialog *dialog,
                         IconCategoryInfo      *category_info,
                         guint                  icon_size)
{
    XAppIconChooserDialogPrivate *priv;
    GtkIconTheme *theme;
    gint chunk_count;
    gint scale;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);
    theme = gtk_icon_theme_get_default ();

    scale = gtk_widget_get_scale_factor (GTK_WIDGET (dialog));

    for (chunk_count = 0; chunk_count < CATEGORY_CHUNK_SIZE; chunk_count++)
    {
        if (category_info->iter == NULL)
        {
            category_info->iter = category_info->icons;
        }
        else
        {
            category_info->iter = category_info->iter->next;
        }

        if (category_info->iter)
        {
            NamedIconInfoLoadCallbackInfo *callback_info;
            const gchar *name = category_info->iter->data;
            cairo_surface_t *surface;

            callback_info = g_new0 (NamedIconInfoLoadCallbackInfo, 1);
            callback_info->dialog = dialog;
            callback_info->category_info = category_info;
            callback_info->model = category_info->model;
            callback_info->cancellable = g_object_ref (priv->cancellable);
            callback_info->name = name;
            callback_info->chunk_end = (chunk_count == CATEGORY_CHUNK_SIZE - 1);

            surface = g_hash_table_lookup (priv->surfaces_by_name, name);

            if (surface != NULL)
            {
                callback_info->surface = cairo_surface_reference (surface);
                g_idle_add ((GSourceFunc) add_named_entry_with_existing_surface, callback_info);
            }
            else
            {
                GtkIconInfo *info;
                GtkStyleContext *context;

                info = gtk_icon_theme_lookup_icon_for_scale (theme, name, icon_size, scale, GTK_ICON_LOOKUP_FORCE_SIZE);
                if (info == NULL)
                {
                    // this shouldn't be the case most of the time, but if a custom category is defined it's possible
                    // the icon doesn't exist. In that case it's best to just skip over it since trying to load it will
                    // lead to a segfault.
                    continue;
                }

                context = gtk_widget_get_style_context (priv->icon_view);
                gtk_icon_info_load_symbolic_for_context_async (info, context, NULL, (GAsyncReadyCallback) (finish_pixbuf_load_from_name), callback_info);
            }
        }
        else
        {
            gtk_widget_hide (priv->loading_bar);
            break; // Quit the count early, we're out of data
        }
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

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    clear_search_state (dialog);

    selection = gtk_list_box_get_selected_rows (GTK_LIST_BOX (priv->list_box));

    if (!selection)
    {
        return;
    }

    gtk_widget_show (priv->loading_bar);

    g_signal_handler_block (priv->search_bar, priv->search_changed_id);
    gtk_entry_set_text (GTK_ENTRY (priv->search_bar), "");
    g_signal_handler_unblock (priv->search_bar, priv->search_changed_id);

    selected = selection->data;
    category_info = g_hash_table_lookup (priv->categories, selected);

    priv->cancellable = g_cancellable_new ();

#if DEBUG_REFS
    g_object_weak_ref (G_OBJECT (priv->cancellable), (GWeakNotify) on_cancellable_finalize, priv);
#endif

    priv->current_category = category_info;

    gtk_list_store_clear (GTK_LIST_STORE (category_info->model));
    gtk_icon_view_set_model (GTK_ICON_VIEW (priv->icon_view), GTK_TREE_MODEL (category_info->model));

    load_icons_for_category (dialog,
                             category_info,
                             priv->icon_size);

    gtk_adjustment_set_value (gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (priv->icon_view)), 0.0);

    g_list_free (selection);
}

#define SEARCH_CHUNK_SIZE 2

static void
search_path (XAppIconChooserDialog *dialog,
             const gchar           *path_string,
             GtkListStore          *icon_store)
{
    XAppIconChooserDialogPrivate *priv;
    gchar            *search_str = NULL;
    GFile            *dir;
    GFileInfo        *child_info = NULL;;
    gint chunk_count;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

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
        g_free (search_str);
        g_object_unref (dir);
        return;
    }

    if (!priv->search_file_enumerator)
    {
        priv->search_file_enumerator  = g_file_enumerate_children (dir,
                                                                   G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                                                   G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE ","
                                                                   G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                                                                   G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                                   G_FILE_QUERY_INFO_NONE, NULL, NULL);

        g_object_add_toggle_ref (G_OBJECT (priv->search_file_enumerator),
                                 (GToggleNotify) on_enumerator_toggle_ref_called,
                                 dialog);

#if DEBUG_REFS
        g_object_weak_ref (G_OBJECT (priv->search_file_enumerator), (GWeakNotify) on_enumerator_finalize, dialog);
#endif
    }

    chunk_count = 0;

    while (chunk_count < SEARCH_CHUNK_SIZE)
    {
        const gchar  *child_name;
        gchar        *child_path;
        GFile        *child;
        GError       *error = NULL;

        g_clear_object (&child_info);
        child_info = g_file_enumerator_next_file (priv->search_file_enumerator, NULL, NULL);

        if (!child_info)
        {
            break;
        }

        child_name = g_file_info_get_name (child_info);
        child = g_file_enumerator_get_child (priv->search_file_enumerator, child_info);
        child_path = g_file_get_path (child);

        if (search_str == NULL || g_str_has_prefix (child_name, search_str))
        {
            priv->current_category = NULL;

            gchar *content_type;
            gboolean uncertain;

            content_type = g_content_type_guess (child_name, NULL, 0, &uncertain);

            if (content_type && g_str_has_prefix (content_type, "image") && !uncertain)
            {
                GFileInputStream         *stream;

                stream = g_file_read (child, NULL, &error);

                if (stream != NULL)
                {
                    FileIconInfoLoadCallbackInfo *callback_info;
                    gint scale;

                    callback_info = g_new0 (FileIconInfoLoadCallbackInfo, 1);
                    callback_info->dialog = dialog;
                    callback_info->model = icon_store;
                    callback_info->cancellable = g_object_ref (priv->cancellable);
                    callback_info->enumerator = g_object_ref (priv->search_file_enumerator);
                    callback_info->short_name = g_strdup (child_name);
                    callback_info->long_name = g_strdup (child_path);
                    callback_info->chunk_end = (chunk_count == SEARCH_CHUNK_SIZE - 1);


                    scale = gtk_widget_get_scale_factor (GTK_WIDGET (dialog));

                    gdk_pixbuf_new_from_stream_at_scale_async (G_INPUT_STREAM (stream),
                                                               -1,
                                                               priv->icon_size * scale,
                                                               TRUE,
                                                               NULL,
                                                               (GAsyncReadyCallback) finish_pixbuf_load_from_file,
                                                               callback_info);

                    chunk_count ++;
                }
                else
                {
                    if (error)
                    {
                        g_message ("%s\n", error->message);
                        g_error_free (error);
                    }
                }
            }

            g_free (content_type);
        }

        g_free (child_path);
        g_object_unref (child);
    }

    if (!child_info)
    {
        if (priv->search_file_enumerator)
        {
            g_object_unref (priv->search_file_enumerator);
        }

        gtk_widget_hide (priv->loading_bar);
    }
    else
    {
        g_clear_object (&child_info);
    }

    g_free (search_str);
    g_object_unref (dir);
}

static void
search_icon_name (XAppIconChooserDialog *dialog,
                  const gchar           *name_string,
                  GtkListStore          *icon_store)
{
    XAppIconChooserDialogPrivate *priv;
    GtkIconTheme                 *theme;
    GList                        *icons;
    gint chunk_count;
    gint scale;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    theme = gtk_icon_theme_get_default ();
    scale = gtk_widget_get_scale_factor (GTK_WIDGET (dialog));

    icons = priv->full_icon_list;

    chunk_count = 0;

    while (chunk_count < SEARCH_CHUNK_SIZE)
    {
        if (priv->search_iter == NULL)
        {
            priv->search_iter = icons;
        }
        else
        {
            priv->search_iter = priv->search_iter->next;
        }

        if (priv->search_iter)
        {
            priv->current_category = NULL;

            if (g_strrstr (priv->search_iter->data, name_string))
            {
                NamedIconInfoLoadCallbackInfo *callback_info;
                cairo_surface_t *surface;
                const gchar *name = priv->search_iter->data;

                callback_info = g_new0 (NamedIconInfoLoadCallbackInfo, 1);
                callback_info->dialog = dialog;
                callback_info->model = priv->search_icon_store;
                callback_info->cancellable = g_object_ref (priv->cancellable);
                callback_info->category_info = NULL;
                callback_info->name = name;
                callback_info->chunk_end = (chunk_count == SEARCH_CHUNK_SIZE - 1);

                surface = g_hash_table_lookup (priv->surfaces_by_name, name);

                if (surface != NULL)
                {
                    callback_info->surface = cairo_surface_reference (surface);
                    g_idle_add ((GSourceFunc) add_named_entry_with_existing_surface, callback_info);
                }
                else
                {
                    GtkIconInfo *info;
                    GtkStyleContext *context;

                    info = gtk_icon_theme_lookup_icon_for_scale (theme, name, priv->icon_size, scale, GTK_ICON_LOOKUP_FORCE_SIZE);
                    context = gtk_widget_get_style_context (priv->icon_view);
                    gtk_icon_info_load_symbolic_for_context_async (info, context, NULL, (GAsyncReadyCallback) (finish_pixbuf_load_from_name), callback_info);
                }

                chunk_count++;
            }
        }
        else
        {
            gtk_widget_hide (priv->loading_bar);

            break; // Quit the count early, we're out of data
        }
    }
}

static void
on_search_text_changed (GtkSearchEntry        *entry,
                        XAppIconChooserDialog *dialog)
{
    XAppIconChooserDialogPrivate *priv;
    const gchar                  *search_text;

    priv = xapp_icon_chooser_dialog_get_instance_private (dialog);

    /* The search cancellable is carried in search callback data.  If the
     * text changes, we cancel it here, our load callbacks will check the
     * state, and react appropriately (like not adding results to the model). */
    clear_search_state (dialog);

    gtk_list_box_select_row (GTK_LIST_BOX (priv->list_box), NULL);

    priv->cancellable = g_cancellable_new ();

#if DEBUG_REFS
    g_object_weak_ref (G_OBJECT (priv->cancellable), (GWeakNotify) on_cancellable_finalize, dialog);
#endif

    search_text = gtk_entry_get_text (GTK_ENTRY (entry));

    if (g_strcmp0 (search_text, "") == 0)
    {
        g_clear_pointer (&priv->current_text, g_free);
        g_clear_pointer (&priv->icon_string, g_free);

        gtk_widget_hide (priv->loading_bar);

        gtk_list_store_clear (GTK_LIST_STORE (priv->search_icon_store));
    }
    else
    if (strlen (search_text) < 2)
    {
        return;
    }
    else
    {
        g_free (priv->current_text);
        priv->current_text = g_strdup (search_text);

        gtk_widget_show (priv->loading_bar);

        gtk_list_store_clear (GTK_LIST_STORE (priv->search_icon_store));
        gtk_icon_view_set_model (GTK_ICON_VIEW (priv->icon_view), GTK_TREE_MODEL (priv->search_icon_store));
        if (g_strrstr (search_text, "/"))
        {
            if (priv->allow_paths)
            {
                search_path (dialog, search_text, priv->search_icon_store);
            }
        }
        else
        {
            search_icon_name (dialog, search_text, priv->search_icon_store);
        }
    }
}

static void
on_icon_view_selection_changed (GtkIconView *icon_view,
                                gpointer     user_data)
{
    XAppIconChooserDialogPrivate *priv;
    GList                        *selected_items;
    gchar                        *icon_string = NULL;

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

    if (priv->icon_string != NULL)
    {
        g_free (priv->icon_string);
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

    gtk_tree_path_free (new_path);
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
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), GTK_RESPONSE_OK);
}

static void
on_cancel_button_clicked (GtkButton *button,
                          gpointer   user_data)
{
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), GTK_RESPONSE_CANCEL);
}

static gboolean
on_delete_event (GtkWidget   *widget,
                 GdkEventAny *event)
{
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (widget), GTK_RESPONSE_CANCEL);

    return TRUE;
}

static void
on_icon_view_item_activated (GtkIconView *iconview,
                             GtkTreePath *path,
                             gpointer     user_data)
{
    xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), GTK_RESPONSE_OK);
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
            xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), GTK_RESPONSE_CANCEL);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (priv->icon_string != NULL)
            {
                xapp_icon_chooser_dialog_close (XAPP_ICON_CHOOSER_DIALOG (user_data), GTK_RESPONSE_OK);
            }
            return TRUE;
    }

    return FALSE;
}

static void
on_default_button_clicked (GtkButton *button,
                           gpointer   user_data)
{
    XAppIconChooserDialogPrivate *priv;

    priv = xapp_icon_chooser_dialog_get_instance_private (user_data);

    gtk_entry_set_text (GTK_ENTRY (priv->search_bar), priv->default_icon);
}

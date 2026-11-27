#include <config.h>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "xapp-favorites.h"
#include "favorite-vfs-file.h"

#define DEBUG_FLAG XAPP_DEBUG_MODULE
#include "xapp-debug.h"

#define ICON_OVERRIDE_VAR "XAPP_FORCE_GTKWINDOW_ICON"

/* xapp-gtk3-module:
 *
 * - Initializes the favorites:// vfs and adds a Favorites shortcut
 *   to any gtk3 sidebars (like in file dialogs).
 *
 * - Overrides the window icon for any GtkWindows when an environment
 *   variable is set with an icon path or name.
 *
 * - In Cinnamon on Wayland, applies Cinnamon's dialogs-use-header setting
 *   (dialog buttons at the bottom by default), as XSettings do on X11.
 */

#define CINNAMON_XSETTINGS_SCHEMA "org.cinnamon.settings-daemon.plugins.xsettings"
#define DIALOGS_USE_HEADER_KEY "dialogs-use-header"

void gtk_module_init (gint *argc, gchar ***argv[]);
static void (* original_sidebar_constructed) (GObject *object);
static void (* original_window_realize) (GtkWidget *widget);
static void (* original_window_unrealize) (GtkWidget *widget);

static void
on_sidebar_realized (GtkWidget *widget, gpointer data)
{
    GtkPlacesSidebar *sidebar = GTK_PLACES_SIDEBAR (widget);
    GSettings *fav_settings;
    gchar **list;

    // This is better than initializing favorites to count.
    // That way if there aren't any favorites, fav_settings
    // will go away. XAppFavorites is a singleton.
    fav_settings = g_settings_new ("org.x.apps.favorites");
    list = g_settings_get_strv (fav_settings, "list");

    if (g_strv_length (list) > 0)
    {
        GFile *favorites = g_file_new_for_uri ("favorites:///");
        gtk_places_sidebar_add_shortcut (sidebar, favorites);
        g_object_unref (favorites);
    }

    g_strfreev (list);
    g_object_unref (fav_settings);
    g_signal_handlers_disconnect_by_func (widget, on_sidebar_realized, NULL);
}

static void
xapp_sidebar_constructed (GObject *object)
{
    (* original_sidebar_constructed) (object);
    g_signal_connect (object, "realize", G_CALLBACK (on_sidebar_realized), NULL);
}

static void
window_icon_changed (GtkWindow *window)
{
    const gchar *forced_icon_str;
    gpointer anti_recursion_ptr;

    forced_icon_str = g_object_get_data (G_OBJECT (window), "xapp-forced-window-icon");
    anti_recursion_ptr = g_object_get_data (G_OBJECT (window), "xapp-forced-icon-last-icon-ptr");

    if (anti_recursion_ptr && anti_recursion_ptr == gtk_window_get_icon (window))
    {
        DEBUG ("Window icon notify received, but anti-recurse pointer hasn't changed, returning.");
        return;
    }

    if (forced_icon_str != NULL)
    {
        gboolean clear_pixbuf = FALSE;

        DEBUG ("Window icon changed, forcing back to '%s'", forced_icon_str);

        g_signal_handlers_block_by_func (G_OBJECT (window), window_icon_changed, window);

        if (g_path_is_absolute (forced_icon_str))
        {
            gtk_window_set_icon_name (window, NULL);
            gtk_window_set_icon_from_file (window, forced_icon_str, NULL);
        }
        else
        {
            gtk_window_set_icon (window, NULL);
            gtk_window_set_icon_name (window, forced_icon_str);
            clear_pixbuf = TRUE;
        }

        g_object_set_data_full (G_OBJECT (window),
                                "xapp-forced-icon-last-icon-ptr",
                                clear_pixbuf ? NULL : g_object_ref (gtk_window_get_icon (window)),
                                g_object_unref);

        g_signal_handlers_unblock_by_func (G_OBJECT (window), window_icon_changed, window);
    }
}

static void
overridden_window_realize (GtkWidget *widget)
{
    (* original_window_realize) (widget);

    if (g_object_get_data (G_OBJECT (widget), "xapp-module-window-seen"))
    {
        return;
    }

    g_object_set_data (G_OBJECT (widget), "xapp-module-window-seen", GINT_TO_POINTER (1));

    DEBUG ("Realize overridden window (%p).", widget);

    const gchar *env_icon = g_getenv (ICON_OVERRIDE_VAR);

    if (env_icon != NULL)
    {
        g_object_set_data_full (G_OBJECT (widget), "xapp-forced-window-icon", g_strdup (env_icon), g_free);
        window_icon_changed (GTK_WINDOW (widget));

        g_signal_connect_swapped (widget, "notify::icon", G_CALLBACK (window_icon_changed), widget);
        g_signal_connect_swapped (widget, "notify::icon-name", G_CALLBACK (window_icon_changed), widget);
    }
}

static void
overridden_window_unrealize (GtkWidget *widget)
{
    (* original_window_unrealize) (widget);

    DEBUG ("Unrealize overridden window (%p).", widget);

    g_signal_handlers_disconnect_by_func (widget, window_icon_changed, widget);
}

static void
apply_window_icon_override (void)
{
    static gboolean applied = 0;

    // I don't think these guards are necessary. This should only run once, but better off safe.
    if (!applied)
    {
        DEBUG ("XAPP_FORCE_GTKWINDOW_ICON found in environment, overriding the window icon with its contents");

        applied = TRUE;

        GtkWidgetClass *widget_class;
        widget_class = g_type_class_ref (GTK_TYPE_WINDOW);

        original_window_realize = widget_class->realize;
        widget_class->realize = overridden_window_realize;
        original_window_unrealize = widget_class->unrealize;
        widget_class->unrealize = overridden_window_unrealize;
    }
}

static void
apply_sidebar_favorites_override (void)
{
    static gboolean applied = 0;

    if (!applied)
    {
        DEBUG ("Adding a Favorites shortcut to GtkPlacesSideBars");

        applied = TRUE;

        GObjectClass *object_class;
        object_class = g_type_class_ref (GTK_TYPE_PLACES_SIDEBAR);

        original_sidebar_constructed = object_class->constructed;
        object_class->constructed = xapp_sidebar_constructed;
    }
}

#ifdef GDK_WINDOWING_WAYLAND
/* Dialog header bars: on X11 cinnamon-settings-daemon gives applications
 * Cinnamon's dialogs-use-header setting through XSettings. GTK's Wayland
 * backend has no XSettings and supplies gtk-dialogs-use-header itself, as
 * TRUE, over settings.ini; only a value the application sets outranks it, so
 * the module sets Cinnamon's value in each application. */
static GSettings *xsettings_settings = NULL;

static void
apply_dialogs_use_header (GtkSettings *settings)
{
    gboolean wanted, current;

    wanted = g_settings_get_boolean (xsettings_settings, DIALOGS_USE_HEADER_KEY);
    g_object_get (settings, "gtk-dialogs-use-header", &current, NULL);

    if (current != wanted)
    {
        DEBUG ("Setting gtk-dialogs-use-header to %d", wanted);
        g_object_set (settings, "gtk-dialogs-use-header", wanted, NULL);
    }
}

static void
on_dialogs_use_header_changed (GObject    *object,
                               GParamSpec *pspec,
                               gpointer    user_data)
{
    apply_dialogs_use_header (GTK_SETTINGS (user_data));
}

static void
on_xsettings_changed (GSettings   *gsettings,
                      const gchar *key,
                      gpointer     user_data)
{
    apply_dialogs_use_header (GTK_SETTINGS (user_data));
}

static void
apply_dialogs_override_for_display (GdkDisplay *display)
{
    GtkSettings *settings;

    if (xsettings_settings != NULL || !GDK_IS_WAYLAND_DISPLAY (display))
    {
        return;
    }

    settings = gtk_settings_get_for_screen (gdk_display_get_default_screen (display));
    if (settings == NULL)
    {
        return;
    }

    xsettings_settings = g_settings_new (CINNAMON_XSETTINGS_SCHEMA);
    g_signal_connect_object (xsettings_settings, "changed::" DIALOGS_USE_HEADER_KEY,
                             G_CALLBACK (on_xsettings_changed), settings, 0);
    /* The backend can supply its value again later (when GTK reads its
     * settings while starting): put Cinnamon's back */
    g_signal_connect (settings, "notify::gtk-dialogs-use-header",
                      G_CALLBACK (on_dialogs_use_header_changed), settings);

    apply_dialogs_use_header (settings);
}

static void
on_display_opened (GdkDisplayManager *manager,
                   GdkDisplay        *display,
                   gpointer           user_data)
{
    apply_dialogs_override_for_display (display);
}

static void
apply_dialogs_override (void)
{
    GSettingsSchemaSource *source;
    GSettingsSchema *schema;
    GdkDisplayManager *manager;
    GdkDisplay *display;
    const gchar *desktop;

    /* Only in Cinnamon: other desktops have their own defaults */
    desktop = g_getenv ("XDG_CURRENT_DESKTOP");
    if (desktop == NULL || !g_regex_match_simple ("(^|:)X-Cinnamon(:|$)", desktop, 0, 0))
    {
        return;
    }

    source = g_settings_schema_source_get_default ();
    schema = source ? g_settings_schema_source_lookup (source, CINNAMON_XSETTINGS_SCHEMA, TRUE) : NULL;
    if (schema == NULL)
    {
        return;
    }
    if (!g_settings_schema_has_key (schema, DIALOGS_USE_HEADER_KEY))
    {
        g_settings_schema_unref (schema);
        return;
    }
    g_settings_schema_unref (schema);

    /* Modules from GTK3_MODULES load before GTK opens its display */
    manager = gdk_display_manager_get ();
    display = gdk_display_manager_get_default_display (manager);
    if (display != NULL)
    {
        apply_dialogs_override_for_display (display);
    }
    else
    {
        g_signal_connect (manager, "display-opened",
                          G_CALLBACK (on_display_opened), NULL);
    }
}
#endif

G_MODULE_EXPORT void gtk_module_init (gint *argc, gchar ***argv[]) {
    DEBUG ("Initializing XApp GtkModule");
    // This won't instantiate XAppFavorites but will register the uri so
    // it can be used by apps (like pix which doesn't use the favorites api,
    // but just adds favorites:/// to its sidebar.)
    init_favorite_vfs ();
    apply_sidebar_favorites_override ();

    if (g_getenv (ICON_OVERRIDE_VAR) != NULL)
    {
        apply_window_icon_override ();
    }

#ifdef GDK_WINDOWING_WAYLAND
    apply_dialogs_override ();
#endif
}

G_MODULE_EXPORT gchar* g_module_check_init (GModule *module);

G_MODULE_EXPORT gchar* g_module_check_init (GModule *module) {
        g_module_make_resident(module);
        return NULL;
}

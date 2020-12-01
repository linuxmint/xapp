#include <config.h>
#include <gtk/gtk.h>

#include "xapp-favorites.h"
#include "favorite-vfs-file.h"

/* Gtk module justification:
 *
 * The sole purpose of this module currently is to add a 'Favorites'
 * shortcut to GtkFileChooser dialogs.
 *
 * In gtk_module_init, the 'favorites' uri scheme is added to the default
 * vfs. Ordinarily, non-file:// schemes aren't supported in these dialogs
 * unless their 'local-only' property is set to FALSE. Since favorites
 * are shortcuts to locally-available files, we lie to the chooser setup
 * by returning "/" instead of NULL when g_file_get_path ("favorites:///")
 * is called.
 */

void gtk_module_init (gint *argc, gchar ***argv[]);
static void (* original_sidebar_constructed) (GObject *object);

static void
xapp_sidebar_constructed (GObject *object)
{
    GtkPlacesSidebar *sidebar = GTK_PLACES_SIDEBAR (object);

    (* original_sidebar_constructed) (object);

    GFile *favorites = g_file_new_for_uri ("favorites:///");
    gtk_places_sidebar_add_shortcut (sidebar, favorites);
    g_object_unref (favorites);
}

G_MODULE_EXPORT void gtk_module_init (gint *argc, gchar ***argv[]) {
    GObjectClass *object_class;

    // This won't instantiate XAppFavorites but will register the uri so
    // it can be used by apps (like pix which doesn't use the favorites api,
    // but just adds favorites:/// to its sidebar.)
    init_favorite_vfs ();

    object_class = g_type_class_ref (GTK_TYPE_PLACES_SIDEBAR);

    original_sidebar_constructed = object_class->constructed;
    object_class->constructed = xapp_sidebar_constructed;
}

G_MODULE_EXPORT gchar* g_module_check_init (GModule *module);

G_MODULE_EXPORT gchar* g_module_check_init (GModule *module) {
        g_module_make_resident(module);
        return NULL;
}

/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
  This file is part of libcanberra.

  Copyright 2008 Lennart Poettering

  libcanberra is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 2.1 of the
  License, or (at your option) any later version.

  libcanberra is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with libcanberra. If not, see
  <http://www.gnu.org/licenses/>.
***/

#include <config.h>
#include <gtk/gtk.h>

#include "xapp-favorites.h"

/* Gtk module justification:
 *
 * The sole purpose of this module currently is to add a 'Favorites'
 * shortcut to GtkFileChooser dialogs.
 *
 * In gtk_module_init, the XAppFavorites singleton is initialized, and
 * the 'favorites' uri scheme is added to the default vfs. Ordinarily
 * non-file:// schemes aren't supported in these dialogs unless their
 * 'local-only' property is set to FALSE. Since favorites are shortcuts
 * to locally-available files, we lie to the chooser setup by returning
 * "/" instead of NULL when g_file_get_path ("favorites:///") is called.
 */


/* Make sure GCC doesn't warn us about a missing prototype for this
 * exported function */
void gtk_module_init (gint *argc, gchar ***argv[]);

static gboolean
selection_changed_cb (GSignalInvocationHint *ihint,
            guint                  n_param_values,
            const GValue          *param_values,
            gpointer               data)
{
    GtkFileChooser *chooser = GTK_FILE_CHOOSER (g_value_get_object (&param_values[0]));
    GSList *list, *i;
    gboolean already_applied = FALSE;
    list = gtk_file_chooser_list_shortcut_folder_uris (chooser);

    for (i = list; i != NULL; i = i->next)
    {
        if (g_strcmp0 ((gchar *) i->data, "favorites:///") == 0)
        {
            already_applied = TRUE;
            break;
        }
    }

    g_slist_free_full (list, g_free);

    if (!already_applied)
    {
        gtk_file_chooser_add_shortcut_folder_uri (chooser, "favorites:///", NULL);
    }

    return TRUE;
}

static void
add_chooser_hook (GType type)
{
    GTypeClass *type_class;
    guint sigid;

    type_class = g_type_class_ref (type);

    sigid = g_signal_lookup ("selection-changed", type);
    g_signal_add_emission_hook (sigid, 0, selection_changed_cb, NULL, NULL);

    g_type_class_unref (type_class);
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv[]) {
    xapp_favorites_get_default ();
    add_chooser_hook (GTK_TYPE_FILE_CHOOSER_WIDGET);
    add_chooser_hook (GTK_TYPE_FILE_CHOOSER_DIALOG);
    add_chooser_hook (GTK_TYPE_FILE_CHOOSER_BUTTON);
}

G_MODULE_EXPORT gchar* g_module_check_init(GModule *module);

G_MODULE_EXPORT gchar* g_module_check_init(GModule *module) {
        g_module_make_resident(module);
        return NULL;
}

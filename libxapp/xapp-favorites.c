
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>

#include "xapp-favorites.h"
#include "favorite-vfs-file.h"

#define DEBUG_FLAG XAPP_DEBUG_FAVORITES
#include "xapp-debug.h"

#define FAVORITES_SCHEMA "org.x.apps.favorites"
#define FAVORITES_KEY "list"
#define SETTINGS_DELIMITER "::"
#define MAX_DISPLAY_URI_LENGTH 20

G_DEFINE_BOXED_TYPE (XAppFavoriteInfo, xapp_favorite_info, xapp_favorite_info_copy, xapp_favorite_info_free);
/**
 * SECTION:xapp-favorites
 * @Short_description: Keeps track of favorite files.
 * @Title: XAppFavorites
 *
 * The XAppFavorites class allows applications display frequently-used files and
 * provide a safe mechanism for launching them.
 *
 * A list of #XAppFavoriteInfos can be retrieved in full, or only for specific mimetypes.
 *
 * A favorites uri scheme is also available if the xapp gtk3 module is loaded and this also makes the
 * uri available as a shortcut in file dialogs.
 *
 * XAppFavorites are new for 2.0
 */

/**
 * xapp_favorite_info_copy:
 * @info: The #XAppFavoriteInfo to duplicate.
 *
 * Makes an exact copy of an existing #XAppFavoriteInfo.
 *
 * Returns: (transfer full): a new #XAppFavoriteInfo.  Free using #xapp_favorite_info_free.
 *
 * Since 2.0
 */
XAppFavorites *global_favorites;

XAppFavoriteInfo *
xapp_favorite_info_copy (const XAppFavoriteInfo *info)
{
    // DEBUG ("XAppFavoriteInfo: copy");
    g_return_val_if_fail (info != NULL, NULL);

    XAppFavoriteInfo *_info = g_slice_dup (XAppFavoriteInfo, info);
    _info->uri = g_strdup (info->uri);
    _info->display_name = g_strdup (info->display_name);
    _info->cached_mimetype = g_strdup (info->cached_mimetype);

    return _info;
}

/**
 * xapp_favorite_info_free:
 * @info: The #XAppFavoriteInfo to free.
 *
 * Destroys the #XAppFavoriteInfo.
 *
 * Since 2.0
 */
void
xapp_favorite_info_free (XAppFavoriteInfo *info)
{
    DEBUG ("XAppFavoriteInfo free (%s)", info->uri);
    g_return_if_fail (info != NULL);

    g_free (info->uri);
    g_free (info->display_name);
    g_free (info->cached_mimetype);
    g_slice_free (XAppFavoriteInfo, info);
}

typedef struct
{
    GHashTable *infos;
    GHashTable *menus;

    GSettings *settings;

    gulong settings_listener_id;
    guint changed_timer_id;
} XAppFavoritesPrivate;

struct _XAppFavorites
{
    GObject parent_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE (XAppFavorites, xapp_favorites, G_TYPE_OBJECT)

enum
{
    CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

static void finish_add_favorite (XAppFavorites *favorites,
                                 const gchar   *uri,
                                 const gchar   *mimetype,
                                 gboolean       from_saved);

static gboolean
changed_callback (gpointer data)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (data), G_SOURCE_REMOVE);
    XAppFavorites *favorites = XAPP_FAVORITES (data);

    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    DEBUG ("XAppFavorites: list updated, emitting changed signal");

    priv->changed_timer_id = 0;
    g_signal_emit (favorites, signals[CHANGED], 0);

    return G_SOURCE_REMOVE;
}

static void
queue_changed (XAppFavorites *favorites)
{
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);

    if (priv->changed_timer_id > 0)
    {
        g_source_remove (priv->changed_timer_id);
    }

    priv->changed_timer_id = g_idle_add ((GSourceFunc) changed_callback, favorites);
}

static void
sync_metadata_callback (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
    // Disabled
    return;

//     GFile *file;
//     GError *error;

//     file = G_FILE (source);
//     error = NULL;

//     if (!g_file_set_attributes_finish (file,
//                                        res,
//                                        NULL,
//                                        &error))
//     {
//         if (error != NULL)
//         {
//             if (error->code != G_IO_ERROR_NOT_FOUND)
//             {
//                 g_warning ("Could not update file metadata for favorite file '%s': %s", g_file_get_uri (file), error->message);
//             }

//             g_error_free (error);
//         }
//     }
//     else
//     {
//         if (g_file_is_native (file))
//         {
//             // I can't think of any other way to touch a file so a file monitor might notice
//             // the attribute change. It shouldn't be too much trouble since most times add/remove
//             // will be done in the file manager (where the update can be triggered internally).

//             gchar *local_path = g_file_get_path (file);
//             g_utime (local_path, NULL);
//             g_free (local_path);
//         }
//     }
}

static void
sync_file_metadata (XAppFavorites *favorites,
                    const gchar   *uri,
                    gboolean       is_favorite)
{
    /* Disabled - this is less than optimal, and is implemented instead in
     * nemo, currently. This could be changed later to help support other browsers.
     * Also, this only works with local files. */
    return;

    /* borrowed from nemo-vfs-file.c */
    GFileInfo *info;
    GFile *file;

    DEBUG ("Sync metadata: %s - Favorite? %d", uri, is_favorite);

    info = g_file_info_new ();

    if (is_favorite) {
        g_file_info_set_attribute_string (info, FAVORITE_METADATA_KEY, META_TRUE);
    } else {
        /* Unset the key */
        g_file_info_set_attribute (info, FAVORITE_METADATA_KEY, G_FILE_ATTRIBUTE_TYPE_INVALID, NULL);
    }

    file = g_file_new_for_uri (uri);

    g_file_set_attributes_async (file,
                                 info,
                                 0,
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 sync_metadata_callback,
                                 favorites);

    g_object_unref (file);
    g_object_unref (info);
}

static void
store_favorites (XAppFavorites *favorites)
{
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    GList *iter, *keys;
    GPtrArray *array;
    gchar **new_settings;

    array = g_ptr_array_new ();

    keys = g_hash_table_get_keys (priv->infos);

    for (iter = keys; iter != NULL; iter = iter->next)
    {
        XAppFavoriteInfo *info = (XAppFavoriteInfo *) g_hash_table_lookup (priv->infos, iter->data);
        gchar *entry;

        entry = g_strjoin (SETTINGS_DELIMITER,
                           info->uri,
                           info->cached_mimetype,
                           NULL);

        g_ptr_array_add (array, entry);
    }

    g_ptr_array_add (array, NULL);

    g_list_free (keys);

    new_settings = (gchar **) g_ptr_array_free (array, FALSE);

    g_signal_handler_block (priv->settings, priv->settings_listener_id);
    g_settings_set_strv (priv->settings, FAVORITES_KEY, (const gchar* const*) new_settings);
    g_signal_handler_unblock (priv->settings, priv->settings_listener_id);

    DEBUG ("XAppFavorites: store_favorites: favorites saved");

    g_strfreev (new_settings);
}

static void
load_favorites (XAppFavorites *favorites,
                gboolean       signal_changed)
{
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    gchar **raw_list;
    gint i;

    if (priv->infos != NULL)
    {
        g_hash_table_destroy (priv->infos);
    }

    priv->infos = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify) xapp_favorite_info_free);

    raw_list = g_settings_get_strv (priv->settings, FAVORITES_KEY);

    if (!raw_list)
    {
        // no favorites
        return;
    }

    for (i = 0; i < g_strv_length (raw_list); i++)
    {
        gchar **entry = g_strsplit (raw_list[i], SETTINGS_DELIMITER, 2);

        finish_add_favorite (favorites,
                             entry[0],  // uri
                             entry[1],  // cached_mimetype
                             TRUE);

        g_strfreev (entry);
    }

    g_strfreev (raw_list);

    DEBUG ("XAppFavorites: load_favorite: favorites loaded (%d)", i);

    if (signal_changed)
    {
        queue_changed (favorites);
    }
}

static void
rename_favorite (XAppFavorites *favorites,
                 const gchar   *old_uri,
                 const gchar   *new_uri)
{
    XAppFavoriteInfo *info;
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    gchar *final_new_uri = NULL;

    if (g_str_has_prefix (old_uri, ROOT_URI))
    {
        // Renaming occurred inside of favorites:/// we need to identify by
        // display name.

        const gchar *old_display_name = old_uri + strlen (ROOT_URI);
        const gchar *new_display_name = new_uri + strlen (ROOT_URI);

        info = xapp_favorites_find_by_display_name (favorites, old_display_name);

        if (info)
        {
            GFile *real_file, *parent, *renamed_file;

            real_file = g_file_new_for_uri (info->uri);
            parent = g_file_get_parent (real_file);

            renamed_file = g_file_get_child_for_display_name (parent,
                                                              new_display_name,
                                                              NULL);

            if (renamed_file != NULL)
            {
                final_new_uri = g_file_get_uri (renamed_file);
            }

            g_object_unref (real_file);
            g_object_unref (parent);
            g_clear_object (&renamed_file);
        }
    }
    else
    {
        info = g_hash_table_lookup (priv->infos, old_uri);
        final_new_uri = g_strdup (new_uri);
    }

    if (info != NULL && final_new_uri != NULL)
    {
        gchar *mimetype = g_strdup (info->cached_mimetype);

        sync_file_metadata (favorites, info->uri, FALSE);

        g_hash_table_remove (priv->infos,
                             (gconstpointer) info->uri);

        finish_add_favorite (favorites,
                             final_new_uri,
                             mimetype,
                             FALSE);

        sync_file_metadata (favorites, final_new_uri, TRUE);

        g_free (mimetype);
    }

    g_free (final_new_uri);
}

static void
remove_favorite (XAppFavorites *favorites,
                 const gchar   *uri)
{
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    gchar *real_uri;

    if (g_str_has_prefix (uri, "favorites"))
    {
        GFile *file = g_file_new_for_uri (uri);
        real_uri = favorite_vfs_file_get_real_uri (file);

        g_object_unref (file);
    }
    else
    {
        real_uri = g_strdup (uri);
    }

    g_return_if_fail (real_uri != NULL);

    DEBUG ("XAppFavorites: remove favorite: %s", real_uri);

    // It may be orphaned for some reason.. even if it's not in gsettings, still try
    // to remove the favorite attribute.
    sync_file_metadata (favorites, real_uri, FALSE);

    if (!g_hash_table_remove (priv->infos, real_uri))
    {
        DEBUG ("XAppFavorites: remove_favorite: could not find favorite for uri '%s'", real_uri);
        g_free (real_uri);
        return;
    }

    g_free (real_uri);

    store_favorites (favorites);
    queue_changed (favorites);
}

static void
deduplicate_display_names (XAppFavorites *favorites,
                           GHashTable    *infos)
{
    GList *fav_uris, *ptr;
    GHashTable *lists_of_keys_by_basename = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                   g_free, NULL);
    GHashTableIter iter;

    fav_uris = g_hash_table_get_keys (infos);

    for (ptr = fav_uris; ptr != NULL; ptr = ptr->next)
    {
        GList *uris;
        const gchar *uri = (gchar *) ptr->data;
        gchar *original_display_name = g_path_get_basename (uri);

        if (g_hash_table_contains (lists_of_keys_by_basename, original_display_name))
        {
            uris = g_hash_table_lookup (lists_of_keys_by_basename, original_display_name);

            // this could be prepend, but then the value in the table would have to be replaced
            uris = g_list_append ((GList *) uris, g_strdup (uri));
        }
        else
        {
            uris = g_list_prepend (NULL, g_strdup (uri));
            g_hash_table_insert (lists_of_keys_by_basename,
                                 g_strdup (original_display_name),
                                 uris);
        }

        g_free (original_display_name);
    }

    g_list_free (fav_uris);

    gpointer key, value;

    g_hash_table_iter_init (&iter, lists_of_keys_by_basename);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        GList *same_names_list, *uri_ptr;
        gchar *common_display_name = NULL;

        if (((GList *) value)->next == NULL)
        {
            // Single member of current common name list;
            g_list_free_full ((GList *) value, g_free);
            continue;
        }
        // Now we know we have a list of uris that would have identical display names
        // Add a part of the uri after each to distinguish them.
        common_display_name = g_uri_unescape_string ((const gchar *) key, NULL);
        same_names_list = (GList *) value;

        for (uri_ptr = same_names_list; uri_ptr != NULL; uri_ptr = uri_ptr->next)
        {
            XAppFavoriteInfo *info;
            GFile *uri_file, *home_file, *parent_file;
            GString *new_display_string;
            const gchar *current_uri;

            current_uri = (const gchar *) uri_ptr->data;

            uri_file = g_file_new_for_uri (current_uri);
            parent_file = g_file_get_parent (uri_file);
            home_file = g_file_new_for_path (g_get_home_dir());

            new_display_string = g_string_new (common_display_name);
            g_string_append (new_display_string, "  (");

            // How much effort should we put into duplicate naming? Keeping it
            // simple like this won't work all the time.
            gchar *parent_basename = g_file_get_basename (parent_file);
            g_string_append (new_display_string, parent_basename);
            g_free (parent_basename);

            // TODO: ellipsized deduplication paths?

            // if (g_file_has_prefix (parent_file, home_file))
            // {
            //     gchar *home_rpath = g_file_get_relative_path (home_file, parent_file);
            //     gchar *home_basename = g_file_get_basename (home_file);

            //     if (strlen (home_rpath) < MAX_DISPLAY_URI_LENGTH)
            //     {
            //         g_string_append (new_display_string, home_basename);
            //         g_string_append (new_display_string, "/");
            //         g_string_append (new_display_string, home_rpath);
            //     }
            //     else
            //     {
            //         gchar *parent_basename = g_file_get_basename (parent_file);

            //         g_string_append (new_display_string, home_basename);
            //         g_string_append (new_display_string, "/.../");
            //         g_string_append (new_display_string, parent_basename);

            //         g_free (parent_basename);
            //     }

            //     g_free (home_rpath);
            //     g_free (home_basename);
            // }
            // else
            // {
            //     GString *tmp_string = g_string_new (NULL);

            //     if (g_file_is_native (parent_file))
            //     {
            //         g_string_append (tmp_string, g_file_peek_path (parent_file));
            //     }
            //     else
            //     {
            //         g_string_append (tmp_string, current_uri);
            //     }

            //     if (tmp_string->len > MAX_DISPLAY_URI_LENGTH)
            //     {
            //         gint diff;
            //         gint replace_pos;

            //         diff = tmp_string->len - MAX_DISPLAY_URI_LENGTH;
            //         replace_pos = (tmp_string->len / 2) - (diff / 2) - 2;

            //         g_string_erase (tmp_string,
            //                         replace_pos,
            //                         diff);
            //         g_string_insert (tmp_string,
            //                          replace_pos,
            //                          "...");
            //     }

            //     g_string_append (new_display_string, tmp_string->str);
            //     g_string_free (tmp_string, TRUE);
            // }

            g_object_unref (uri_file);
            g_object_unref (home_file);
            g_object_unref (parent_file);

            g_string_append (new_display_string, ")");

            // Look up the info from our master table
            info = g_hash_table_lookup (infos, current_uri);
            g_free (info->display_name);

            info->display_name = g_string_free (new_display_string, FALSE);
        }

        g_free (common_display_name);
        g_list_free_full (same_names_list, g_free);
    }

    // We freed the individual lists just above, only the keys will need
    // freed here.
    g_hash_table_destroy (lists_of_keys_by_basename);
}

static void
on_display_name_received (GObject      *source,
                          GAsyncResult *res,
                          gpointer      user_data)
{
    GFile *file;
    GFileInfo *file_info;
    GError *error;
    gchar *display_name;
    g_autofree gchar *uri = NULL;

    file = G_FILE (source);
    error = NULL;

    uri = g_file_get_uri (file);
    file_info = g_file_query_info_finish (file, res, &error);

    if (error)
    {
        DEBUG ("XAppFavorites: problem trying to get real display name for uri '%s': %s",
               uri, error->message);
        g_error_free (error);
        return;
    }

    g_return_if_fail (XAPP_IS_FAVORITES (user_data));

    XAppFavorites *favorites = XAPP_FAVORITES (user_data);
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);

    display_name = NULL;

    if (file_info)
    {
        XAppFavoriteInfo *info = g_hash_table_lookup (priv->infos,  uri);
        const gchar *real_display_name = g_file_info_get_display_name (file_info);

        if (info != NULL && g_strcmp0 (info->display_name, real_display_name) != 0)
        {
            gchar *old_name = info->display_name;
            info->display_name = g_strdup (real_display_name);
            g_free (old_name);

            deduplicate_display_names (favorites, priv->infos);
            queue_changed (favorites);
        }
    }

    g_free (display_name);
    g_clear_object (&file_info);
}

static void
finish_add_favorite (XAppFavorites *favorites,
                     const gchar   *uri,
                     const gchar   *cached_mimetype,
                     gboolean       from_saved)
{
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    XAppFavoriteInfo *info;
    gchar *unescaped_uri;

    // Check if it's there again, in case it was added while we were getting mimetype.
    if (g_hash_table_contains (priv->infos, uri))
    {
        DEBUG ("XAppFavorites: favorite for '%s' exists, ignoring", uri);
        return;
    }

    info = g_slice_new0 (XAppFavoriteInfo);
    info->uri = g_strdup (uri);

    unescaped_uri = g_uri_unescape_string (uri, NULL);
    info->display_name = g_path_get_basename (unescaped_uri);
    g_free (unescaped_uri);

    info->cached_mimetype = g_strdup (cached_mimetype);

    g_hash_table_insert (priv->infos, (gpointer) g_strdup (uri), (gpointer) info);

    DEBUG ("XAppFavorites: added favorite: %s", uri);

    deduplicate_display_names (favorites, priv->infos);

    GFile *gfile = g_file_new_for_uri (uri);
    g_file_query_info_async (gfile,
                             G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_LOW,
                             NULL,
                             on_display_name_received,
                             favorites);
    g_object_unref (gfile);

    if (from_saved)
    {
        return;
    }

    store_favorites (favorites);
    queue_changed (favorites);
}

static void
on_content_type_info_received (GObject      *source,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    XAppFavorites *favorites = XAPP_FAVORITES (user_data);
    GFile *file;
    GFileInfo *file_info;
    GError *error;
    gchar *cached_mimetype, *uri;

    file = G_FILE (source);
    uri = g_file_get_uri (file);
    error = NULL;
    cached_mimetype = NULL;

    file_info = g_file_query_info_finish (file, res, &error);

    if (error)
    {
        DEBUG ("XAppFavorites: problem trying to figure out content type for uri '%s': %s",
                 uri, error->message);
        g_error_free (error);
    }

    if (file_info)
    {
        cached_mimetype = g_strdup (g_file_info_get_attribute_string (file_info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));

        if (cached_mimetype == NULL)
        {
            cached_mimetype = g_strdup ("application/unknown");
        }

        finish_add_favorite (favorites,
                             uri,
                             cached_mimetype,
                             FALSE);

        sync_file_metadata (favorites, uri, TRUE);
    }

    g_free (uri);
    g_free (cached_mimetype);
    g_clear_object (&file_info);
    g_object_unref (file);
}

static void
add_favorite (XAppFavorites *favorites,
              const gchar   *uri)
{
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    GFile *file;

    if (g_hash_table_contains (priv->infos, uri))
    {
        DEBUG ("XAppFavorites: favorite for '%s' exists, ignoring", uri);
        return;
    }

    file = g_file_new_for_uri (uri);

    g_file_query_info_async (file,
                             G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_LOW,
                             NULL,
                             on_content_type_info_received,
                             favorites);
}

static void
on_settings_list_changed (GSettings *settings,
                          gchar     *key,
                          gpointer   user_data)
{
    XAppFavorites *favorites = XAPP_FAVORITES (user_data);

    load_favorites (favorites, TRUE);
}

static void
xapp_favorites_init (XAppFavorites *favorites)
{
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);

    DEBUG ("XAppFavorites: init:");

    priv->settings = g_settings_new (FAVORITES_SCHEMA);
    priv->settings_listener_id = g_signal_connect (priv->settings,
                                                   "changed::" FAVORITES_KEY,
                                                   G_CALLBACK (on_settings_list_changed),
                                                   favorites);

    load_favorites (favorites, FALSE);
}

static void
xapp_favorites_dispose (GObject *object)
{
    XAppFavorites *favorites = XAPP_FAVORITES (object);
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);

    DEBUG ("XAppFavorites dispose (%p)", object);

    g_clear_object (&priv->settings);
    g_clear_pointer (&priv->infos, g_hash_table_destroy);

    G_OBJECT_CLASS (xapp_favorites_parent_class)->dispose (object);
}

static void
xapp_favorites_finalize (GObject *object)
{
    DEBUG ("XAppFavorites finalize (%p)", object);

    G_OBJECT_CLASS (xapp_favorites_parent_class)->finalize (object);
}

static void
xapp_favorites_class_init (XAppFavoritesClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = xapp_favorites_dispose;
    gobject_class->finalize = xapp_favorites_finalize;

    /**
     * XAppFavorites::changed:

     * Notifies when the favorites list has changed.
     */
    signals [CHANGED] =
        g_signal_new ("changed",
                      XAPP_TYPE_FAVORITES,
                      G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

/**
 * xapp_favorites_get_default:
 *
 * Returns the #XAppFavorites instance.
 *
 * Returns: (transfer none): the XAppFavorites instance for the process. Do not free.
 *
 * Since: 2.0
 */
XAppFavorites *
xapp_favorites_get_default (void)
{
    if (global_favorites == NULL)
    {
        init_favorite_vfs ();
        global_favorites = g_object_new (XAPP_TYPE_FAVORITES, NULL);
    }

    return global_favorites;
}

typedef struct {
    GList *items;
    const gchar **mimetypes;
} MatchData;

void
match_mimetypes (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
    MatchData *data = (MatchData *) user_data;
    const XAppFavoriteInfo *info = (XAppFavoriteInfo *) value;

    if (data->mimetypes == NULL)
    {
        data->items = g_list_prepend (data->items, xapp_favorite_info_copy (info));
        return;
    }

    gint i;

    for (i = 0; i < g_strv_length ((gchar **) data->mimetypes); i++)
    {
        if (g_content_type_is_mime_type (info->cached_mimetype, data->mimetypes[i]))
        {
            data->items = g_list_prepend (data->items, xapp_favorite_info_copy (info));
            return;
        }
    }
}

/**
 * xapp_favorites_get_favorites:
 * @favorites: The #XAppFavorites
 * @mimetypes: (nullable) (array zero-terminated=1): The mimetypes to filter by for results
 *
 * Gets a list of all favorites.  If mimetype is not %NULL, the list will
 * contain only favorites with that mimetype.
 *
 * Returns: (element-type XAppFavoriteInfo) (transfer full): a list of #XAppFavoriteInfos.
            Free the list with #g_list_free, free elements with #xapp_favorite_info_free.
 *
 * Since: 2.0
 */
GList *
xapp_favorites_get_favorites (XAppFavorites       *favorites,
                              const gchar * const *mimetypes)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (favorites), NULL);
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    GList *ret = NULL;
    MatchData data;

    data.items = NULL;
    data.mimetypes = (const gchar **) mimetypes;
    g_hash_table_foreach (priv->infos,
                          (GHFunc) match_mimetypes,
                          &data);

    ret = g_list_reverse (data.items);

    gchar *typestring = mimetypes ? g_strjoinv (", ", (gchar **) mimetypes) : NULL;
    DEBUG ("XAppFavorites: get_favorites returning list for mimetype '%s' (%d items)",
             typestring, g_list_length (ret));
    g_free (typestring);

    return ret;
}

/**
 * xapp_favorites_get_n_favorites:
 * @favorites: The #XAppFavorites
 *
 * Returns: The number of favorite files

 * Since: 2.0
 */
gint
xapp_favorites_get_n_favorites (XAppFavorites *favorites)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (favorites), 0);
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    gint n;

    n = g_hash_table_size (priv->infos);

    DEBUG ("XAppFavorites: get_n_favorites returning number of items: %d.", n);

    return n;
}

static gboolean
lookup_display_name (gpointer key,
                     gpointer value,
                     gpointer user_data)
{
    XAppFavoriteInfo *info = (XAppFavoriteInfo *) value;

    if (g_strcmp0 (info->display_name, (const gchar *) user_data) == 0)
    {
        return TRUE;
    }

    return FALSE;
}

/**
 * xapp_favorites_find_by_display_name:
 * @favorites: The #XAppFavorites
 * @display_name: (not nullable): The display name to lookup info for.
 *
 * Looks for an XAppFavoriteInfo that corresponds to @display_name.
 *
 * Returns: (transfer none): an XAppFavoriteInfo or NULL if one was not found. This is owned
 *          by the favorites manager and should not be freed.
 *
 * Since: 2.0
 */
XAppFavoriteInfo *
xapp_favorites_find_by_display_name (XAppFavorites *favorites,
                                     const gchar   *display_name)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (favorites), NULL);
    g_return_val_if_fail (display_name != NULL, NULL);

    XAppFavoriteInfo *info;
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);

    info = g_hash_table_find (priv->infos,
                              (GHRFunc) lookup_display_name,
                              (gpointer) display_name);

    if (info != NULL)
    {
        return info;
    }

    return NULL;
}

/**
 * xapp_favorites_find_by_uri:
 * @favorites: The #XAppFavorites
 * @uri: (not nullable): The uri to lookup info for.
 *
 * Looks for an XAppFavoriteInfo that corresponds to @uri.
 *
 * Returns: (transfer none): an XAppFavoriteInfo or NULL if one was not found. This is owned
 *          by the favorites manager and should not be freed.
 *
 * Since: 2.0
 */
XAppFavoriteInfo *
xapp_favorites_find_by_uri (XAppFavorites *favorites,
                            const gchar   *uri)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (favorites), NULL);
    g_return_val_if_fail (uri != NULL, NULL);

    XAppFavoriteInfo *info;
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);

    info = g_hash_table_lookup (priv->infos, uri);

    if (info != NULL)
    {
        return (XAppFavoriteInfo *) info;
    }

    return NULL;
}

/**
 * xapp_favorites_add:
 * @favorites: The #XAppFavorites
 * @uri: The uri the favorite is for
 *
 * Adds a new favorite.  If the uri already exists, this does nothing.
 *
 * Since: 2.0
 */
void
xapp_favorites_add (XAppFavorites *favorites,
                    const gchar   *uri)
{
    g_return_if_fail (XAPP_IS_FAVORITES (favorites));
    g_return_if_fail (uri != NULL);

    add_favorite (favorites, uri);
}

/**
 * xapp_favorites_remove:
 * @favorites: The #XAppFavorites
 * @uri: The uri for the favorite being removed
 *
 * Removes a favorite from the list.
 *
 * Since: 2.0
 */
void
xapp_favorites_remove (XAppFavorites *favorites,
                       const gchar   *uri)
{
    g_return_if_fail (XAPP_IS_FAVORITES (favorites));
    g_return_if_fail (uri != NULL);

    remove_favorite (favorites, uri);
}

static void
launch_uri_callback (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
    gchar *uri = (gchar *) user_data;
    GError *error;

    error = NULL;

    if (!g_app_info_launch_default_for_uri_finish (res, &error))
    {
        if (error)
        {
            DEBUG ("XAppFavorites: launch: error opening uri '%s': %s", uri, error->message);
            g_error_free (error);
        }
    }

    g_free (uri);
}

/**
 * xapp_favorites_launch:
 * @favorites: The #XAppFavorites
 * @uri: The uri for the favorite to launch
 * @timestamp: The timestamp from an event or 0
 *
 * Opens a favorite in its default app.
 *
 * Since: 2.0
 */
void
xapp_favorites_launch (XAppFavorites *favorites,
                       const gchar   *uri,
                       guint32        timestamp)
{
    GdkDisplay *display;
    GdkAppLaunchContext *launch_context;

    display = gdk_display_get_default ();
    launch_context = gdk_display_get_app_launch_context (display);
    gdk_app_launch_context_set_timestamp (launch_context, timestamp);

    g_app_info_launch_default_for_uri_async (uri,
                                             G_APP_LAUNCH_CONTEXT (launch_context),
                                             NULL,
                                             launch_uri_callback,
                                             g_strdup (uri));

    g_object_unref (launch_context);
}

/**
 * xapp_favorites_rename:
 * @old_uri: the old favorite's uri.
 * @new_uri: The new uri.
 *
 * Removes old_uri and adds new_uri. This is mainly for file managers to use as
 * a convenience instead of add/remove, and guarantees the result, without having to
 * worry about multiple dbus calls (gsettings).
 *
 * Since: 2.0
 */
void
xapp_favorites_rename (XAppFavorites *favorites,
                       const gchar   *old_uri,
                       const gchar   *new_uri)
{
    g_return_if_fail (XAPP_IS_FAVORITES (favorites));
    g_return_if_fail (old_uri != NULL && new_uri != NULL);

    rename_favorite (favorites, old_uri, new_uri);
}

typedef struct {
    XAppFavorites *favorites;
    guint update_id;

    GDestroyNotify destroy_func;
    gpointer user_data;
} DestroyData;

typedef struct {
    XAppFavorites *favorites;
    XAppFavoritesItemSelectedCallback callback;
    gchar *uri;
    gpointer user_data;
} ItemCallbackData;

static void
cb_data_destroy_notify (gpointer callback_data,
                        GObject *object)
{
    DestroyData *dd = (DestroyData *) callback_data;

    if (dd->update_id > 0)
    {
        g_signal_handler_disconnect (dd->favorites, dd->update_id);
    }

    dd->destroy_func (dd->user_data);

    g_slice_free (DestroyData, dd);
}

static void
free_item_callback_data (gpointer  callback_data,
                         GClosure *closure)
{
    ItemCallbackData *data = (ItemCallbackData *) callback_data;
    g_free (data->uri);
    g_slice_free (ItemCallbackData, data);
}

static void
item_activated (GObject *item,
                gpointer user_data)
{
    ItemCallbackData *data = (ItemCallbackData *) user_data;

    data->callback (data->favorites,
                    data->uri,
                    data->user_data);
}

static void
remove_menu_item (GtkWidget *item,
                  gpointer   user_data)
{
    gtk_container_remove (GTK_CONTAINER (user_data), item);
}

static void
populate_menu (XAppFavorites *favorites,
               GtkMenu       *menu)
{
    GList *fav_list, *ptr;
    GtkWidget *item;
    XAppFavoritesItemSelectedCallback callback;
    gpointer user_data;
    const gchar **mimetypes;

    gtk_container_foreach (GTK_CONTAINER (menu), (GtkCallback) remove_menu_item, menu);

    mimetypes = (const gchar **) g_object_get_data (G_OBJECT (menu), "mimetypes");
    callback = g_object_get_data (G_OBJECT (menu), "activate-cb");
    user_data = g_object_get_data (G_OBJECT (menu), "user-data");

    fav_list = xapp_favorites_get_favorites (favorites, mimetypes);

    if (fav_list == NULL)
    {
        return;
    }

    for (ptr = fav_list; ptr != NULL; ptr = ptr->next)
    {
        XAppFavoriteInfo *info = (XAppFavoriteInfo *) ptr->data;
        ItemCallbackData *data;

        if (mimetypes != NULL)
        {
            item = gtk_menu_item_new_with_label (info->display_name);
        }
        else
        {
            GtkWidget *image;
            GIcon *icon;

            icon = g_content_type_get_symbolic_icon (info->cached_mimetype);
            image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);
            g_object_unref (icon);

            item = gtk_image_menu_item_new_with_label (info->display_name);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
        }

        data = g_slice_new0 (ItemCallbackData);
        data->favorites = favorites;
        data->uri = g_strdup (info->uri);
        data->callback = callback;
        data->user_data = user_data;

        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

        g_signal_connect_data (item,
                               "activate", G_CALLBACK (item_activated),
                               data, (GClosureNotify) free_item_callback_data, 0);
    }

    g_list_free_full (fav_list, (GDestroyNotify) xapp_favorite_info_free);

    gtk_widget_show_all (GTK_WIDGET (menu));
}

static void
refresh_menu_items (XAppFavorites *favorites,
                    gpointer       user_data)
{
    g_return_if_fail (XAPP_IS_FAVORITES (favorites));
    g_return_if_fail (GTK_IS_MENU (user_data));

    GtkMenu *menu = GTK_MENU (user_data);

    populate_menu (favorites, menu);
}

/**
 * xapp_favorites_create_menu:
 * @favorites: The #XAppFavorites instance.
 * @mimetypes: (nullable): The mimetypes to filter for, or NULL to include all favorites.
 * @callback: (scope notified): (closure user_data): The callback to use when a menu item has been selected.
 * @user_data: (closure): The data to pass to the callback
 * @func: Destroy function for user_data
 *
 * Generates a GtkMenu widget populated with favorites. The callback will be called when
 * a menu item has been activated, and will include the uri of the respective item.
 *
 * Returns: (transfer full): a new #GtkMenu populated with a list of favorites, or NULL
            if there are no favorites.
 *
 * Since: 2.0
 */
GtkWidget *
xapp_favorites_create_menu (XAppFavorites                      *favorites,
                            const gchar                       **mimetypes,
                            XAppFavoritesItemSelectedCallback   callback,
                            gpointer                            user_data,
                            GDestroyNotify                      func)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (favorites), NULL);
    GtkWidget *menu;

    menu = gtk_menu_new ();

    g_object_set_data_full (G_OBJECT (menu),
                            "mimetype", g_strdupv ((gchar **) mimetypes),
                            (GDestroyNotify) g_strfreev);

    g_object_set_data (G_OBJECT (menu),
                       "activate-cb", callback);

    g_object_set_data (G_OBJECT (menu),
                       "user-data", user_data);

    populate_menu (favorites, GTK_MENU (menu));

    DestroyData *dd = g_slice_new0 (DestroyData);
    dd->destroy_func = func;
    dd->user_data = user_data;
    dd->favorites = favorites;
    dd->update_id = g_signal_connect (favorites,
                                      "changed",
                                      G_CALLBACK (refresh_menu_items),
                                      menu);

    g_object_weak_ref (G_OBJECT (menu), (GWeakNotify) cb_data_destroy_notify, dd);

    return menu;
}

static GList *
populate_action_list (XAppFavorites  *favorites,
                      const gchar   **mimetypes)
{
    GList *fav_list, *ptr;
    GList *actions;
    GtkAction *action;
    gint i;

    fav_list = xapp_favorites_get_favorites (favorites, mimetypes);

    if (fav_list == NULL)
    {
        return NULL;
    }

    actions = NULL;

    for (ptr = fav_list, i = 0; ptr != NULL; ptr = ptr->next, i++)
    {
        XAppFavoriteInfo *info = (XAppFavoriteInfo *) ptr->data;

        if (mimetypes != NULL)
        {
            action = g_object_new (GTK_TYPE_ACTION,
                                   "name", info->uri,
                                   "label", info->display_name,
                                   NULL);
        }
        else
        {
            GIcon *icon;
            icon = g_content_type_get_symbolic_icon (info->cached_mimetype);

            action = g_object_new (GTK_TYPE_ACTION,
                                   "name", info->uri,
                                   "label", info->display_name,
                                   "gicon", icon,
                                   NULL);

            g_free (icon);
        }

        actions = g_list_prepend (actions, action);
    }

    actions = g_list_reverse (actions);

    return actions;
}

/**
 * xapp_favorites_create_actions:
 * @favorites: The #XAppFavorites instance.
 * @mimetypes: (nullable): The mimetypes to filter for, or NULL to include all favorites.
 *
 * Generates a list of favorite GtkActions.
 *
 * Returns: (element-type Gtk.Action) (transfer full): a new #GtkActionGroup populated with a list of favorites, or NULL
            if there are no favorites.

 * Since: 2.0
 */
GList *
xapp_favorites_create_actions (XAppFavorites  *favorites,
                               const gchar   **mimetypes)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (favorites), NULL);
    GList *actions;

    actions = populate_action_list (favorites,
                                    mimetypes);

    return actions;
}

/* Used by favorite_vfs_file */
GList *
_xapp_favorites_get_display_names (XAppFavorites *favorites)
{
    g_return_val_if_fail (XAPP_IS_FAVORITES (favorites), NULL);
    XAppFavoritesPrivate *priv = xapp_favorites_get_instance_private (favorites);
    GHashTableIter iter;
    GList *ret;
    gpointer key, value;

    ret = NULL;
    g_hash_table_iter_init (&iter, priv->infos);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        XAppFavoriteInfo *info = (XAppFavoriteInfo *) value;
        ret = g_list_prepend (ret, info->display_name);
    }

    ret = g_list_reverse (ret);
    return ret;
}


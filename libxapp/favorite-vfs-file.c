#include <config.h>
#include <glib/gi18n-lib.h>

#include "favorite-vfs-file.h"
#include "favorite-vfs-file-enumerator.h"
#include "favorite-vfs-file-monitor.h"

#define DEBUG_FLAG XAPP_DEBUG_FAVORITE_VFS
#include "xapp-debug.h"

#define FAVORITES_SCHEMA "org.x.apps.favorites"
#define FAVORITE_DCONF_METADATA_KEY "root-metadata"

static GSettings *settings = NULL;

typedef struct
{
    gchar *uri; // favorites://foo

    XAppFavoriteInfo *info;
} FavoriteVfsFilePrivate;

struct _FavoriteVfsFile
{
    GObject parent_instance;
    FavoriteVfsFilePrivate *priv;
};

GList       *_xapp_favorites_get_display_names      (XAppFavorites *favorites);
static void  favorite_vfs_file_gfile_iface_init (GFileIface *iface);

gchar *
path_to_fav_uri (const gchar *path)
{
    g_return_val_if_fail (path != NULL, NULL);

    return g_strconcat (ROOT_URI, path, NULL);
}

gchar *
fav_uri_to_display_name (const gchar *uri)
{
    g_return_val_if_fail (uri != NULL, NULL);
    g_return_val_if_fail (g_str_has_prefix (uri, ROOT_URI), NULL);

    const gchar *ptr;

    ptr = uri + strlen (ROOT_URI);

    if (ptr[0] == '/')
    {
        ptr++;
    }

    return g_strdup (ptr);
}

G_DEFINE_TYPE_EXTENDED (FavoriteVfsFile,
                        favorite_vfs_file,
                        G_TYPE_OBJECT,
                        0,
                        G_ADD_PRIVATE (FavoriteVfsFile)
                        G_IMPLEMENT_INTERFACE (G_TYPE_FILE, favorite_vfs_file_gfile_iface_init))

static gboolean
is_root_file (FavoriteVfsFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (file);

    return g_strcmp0 (priv->uri, ROOT_URI) == 0;
}

static GFile *
file_dup (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    return favorite_vfs_file_new_for_uri (priv->uri);
}

static guint
file_hash (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    return g_str_hash (priv->uri);
}

static gboolean
file_equal (GFile *file1,
            GFile *file2)
{
    FavoriteVfsFilePrivate *priv1 = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file1));
    FavoriteVfsFilePrivate *priv2 = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file2));

    return g_strcmp0 (priv1->uri, priv2->uri) == 0;
}

static gboolean
file_is_native (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        gboolean is_really_native;

        real_file = g_file_new_for_uri (priv->info->uri);
        is_really_native = g_file_is_native (real_file);

        g_object_unref (real_file);
        return is_really_native;
    }

    return FALSE;
}

static gboolean
file_has_uri_scheme (GFile      *file,
                     const gchar *uri_scheme)
{
    return g_strcmp0 (uri_scheme, URI_SCHEME) == 0;
}

static gchar *
file_get_uri_scheme (GFile *file)
{
    return g_strdup (URI_SCHEME);
}

static gchar *
file_get_basename (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info == NULL)
    {
        return g_strdup ("/");
    }

    return g_strdup (priv->info->display_name);
}

static gchar *
file_get_path (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (file_is_native (file))
    {
        GFile *real_file;
        gchar *ret;

        real_file = g_file_new_for_uri (priv->info->uri);

        // file can't be native without an info, so we don't need to check for null here
        ret = g_file_get_path (real_file);
        g_object_unref (real_file);

        return ret;
    }

    // GtkFileChooser checks for a path (and not null) before allowing a shortcut to
    // be added using gtk_file_chooser_add_shortcut_folder_uri(). Even though this / doesn't
    // make much sense for favorites:/// it allows it to be added to the file chooser without
    // being forced to override its 'local-only' property.
    if (is_root_file (FAVORITE_VFS_FILE (file)))
    {
        return g_strdup ("/");
    }

    return NULL;
}

static gchar *
file_get_uri (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    return g_strdup (priv->uri);
}

static gchar *
file_get_parse_name (GFile *file)
{
    return file_get_uri (file);
}

static GFile *
file_get_parent (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    // We're only ever one level deep.
    if (priv->info != NULL)
    {
        return g_file_new_for_uri (ROOT_URI);
    }

    return NULL;
}

static gboolean
file_prefix_matches (GFile *parent,
                     GFile *descendant)
{
    g_autofree gchar *puri = NULL;
    g_autofree gchar *duri = NULL;
    gchar *ptr = NULL;

    puri = g_file_get_uri (parent);
    duri = g_file_get_uri (descendant);

    ptr = g_strstr_len (puri, -1, duri);

    if ((ptr == puri) && ptr[strlen (duri) + 1] == '/')
    {
        return TRUE;
    }

    return FALSE;
}

static gchar *
file_get_relative_path (GFile *parent,
                        GFile *descendant)
{
    g_autofree gchar *puri = NULL;
    g_autofree gchar *duri = NULL;
    g_autofree gchar *rpath = NULL;
    gchar *ptr = NULL;

    puri = g_file_get_uri (parent);
    duri = g_file_get_uri (descendant);

    ptr = g_strstr_len (puri, -1, duri);

    if ((ptr == puri) && ptr[strlen (duri) + 1] == '/')
    {
        rpath = g_strdup (puri + strlen (duri) + 1);

        return rpath;
    }

    return NULL;
}

static GFile *
file_resolve_relative_path (GFile       *file,
                            const gchar *relative_path)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));
    GFile *relative_file;
    gchar *uri;

    if (g_path_is_absolute (relative_path))
    {
        return g_file_new_for_path (relative_path);
    }

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        real_file = g_file_new_for_uri (priv->info->uri);

        relative_file = g_file_resolve_relative_path (real_file,
                                                      relative_path);

        g_object_unref (real_file);
        return relative_file;
    }

    if (g_strcmp0 (relative_path, ".") == 0)
    {
        relative_file = file_dup (file);

        return relative_file;
    }

    uri = path_to_fav_uri (relative_path);

    relative_file = g_file_new_for_uri (uri);
    g_free (uri);

    return relative_file;
}

static GFile *
file_get_child_for_display_name (GFile       *file,
                                 const char  *display_name,
                                 GError     **error)
{
    return g_file_get_child (file, display_name);
}

static GFile *
file_set_display_name (GFile         *file,
                       const gchar   *display_name,
                       GCancellable  *cancellable,
                       GError       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        GFile *ret;

        real_file = g_file_new_for_uri (priv->info->uri);
        ret = g_file_set_display_name (real_file,
                                       display_name,
                                       cancellable,
                                       error);
        g_object_unref (real_file);

        return ret;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                         "Can't rename file");

    return NULL;
}

static GFileEnumerator *
file_enumerate_children(GFile               *file,
                        const char          *attributes,
                        GFileQueryInfoFlags  flags,
                        GCancellable        *cancellable,
                        GError             **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));
    GFileEnumerator *enumerator;

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        real_file = g_file_new_for_uri (priv->info->uri);

        enumerator = g_file_enumerate_children (real_file,
                                                attributes,
                                                flags,
                                                cancellable,
                                                error);

        g_object_unref (real_file);
        return enumerator;
    }

    GList *uris;

    uris = _xapp_favorites_get_display_names (xapp_favorites_get_default ());
    enumerator = favorite_vfs_file_enumerator_new (file,
                                                   attributes,
                                                   flags,
                                                   uris);

    g_list_free (uris);

    return enumerator;
}

static GFileInfo *
file_query_info (GFile               *file,
                 const char          *attributes,
                 GFileQueryInfoFlags  flags,
                 GCancellable        *cancellable,
                 GError             **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));
    GFileInfo *info;
    GIcon *icon;

    info = NULL;

    if (priv->info != NULL)
    {
        if (!priv->info->uri)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found");
            return NULL;
        }

        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        info = g_file_query_info (real_file, attributes, flags, cancellable, error);

        if (info != NULL)
        {
            gchar *local_path;

            g_file_info_set_display_name (info, priv->info->display_name);
            g_file_info_set_name (info, priv->info->display_name);
            g_file_info_set_is_symlink (info, TRUE);

            local_path = g_file_get_path (real_file);

            if (local_path != NULL)
            {
                g_file_info_set_symlink_target (info, local_path);
                g_free (local_path);
            }
            else
            {
                g_file_info_set_symlink_target (info, priv->info->uri);
            }

            // Recent sets this also. If it's set, this uri is used to display the "location"
            // for the file (the directory in which real file resides).
            g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI, priv->info->uri);

            g_file_info_set_attribute_string (info, FAVORITE_AVAILABLE_METADATA_KEY, META_TRUE);
        }
        else
        {
            // This file is still in our favorites list but doesn't exist (currently).
            g_clear_error (error);
            gchar *content_type;

            info = g_file_info_new ();

            g_file_info_set_display_name (info, priv->info->display_name);
            g_file_info_set_name (info, priv->info->display_name);
            g_file_info_set_file_type (info, G_FILE_TYPE_SYMBOLIC_LINK);
            g_file_info_set_is_symlink (info, TRUE);
            g_file_info_set_symlink_target (info, priv->info->uri);
            g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_TARGET_URI, priv->info->uri);

            /* Prevent showing a 'thumbnailing' icon */
            g_file_info_set_attribute_boolean (info, G_FILE_ATTRIBUTE_THUMBNAILING_FAILED, TRUE);

            /* This will keep the sort position the same for missing or unmounted files */
            g_file_info_set_attribute_string (info, FAVORITE_METADATA_KEY, META_TRUE);
            g_file_info_set_attribute_string (info, FAVORITE_AVAILABLE_METADATA_KEY, META_FALSE);

            content_type = g_content_type_from_mime_type (priv->info->cached_mimetype);

            icon = g_content_type_get_icon (content_type);
            g_file_info_set_icon (info, icon);
            g_object_unref (icon);

            icon = g_content_type_get_symbolic_icon (content_type);
            g_file_info_set_symbolic_icon (info, icon);
            g_object_unref (icon);

            g_free (content_type);
        }

        g_object_unref (real_file);

        return info;
    }

    if (is_root_file (FAVORITE_VFS_FILE (file)))
    {
        GFileAttributeMatcher *matcher = g_file_attribute_matcher_new (attributes);

        info = g_file_info_new ();

        if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_STANDARD_NAME))
            g_file_info_set_name (info, "/");

        if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME))
            g_file_info_set_display_name (info, _("Favorites"));

        if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_STANDARD_TYPE))
            g_file_info_set_file_type (info, G_FILE_TYPE_DIRECTORY);

        if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_STANDARD_ICON))
        {
            icon = g_themed_icon_new ("xapp-user-favorites");
            g_file_info_set_icon (info, icon);

            g_object_unref (icon);
        }

        if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON))
        {
            icon = g_themed_icon_new ("xapp-user-favorites-symbolic");
            g_file_info_set_symbolic_icon (info, icon);
            g_object_unref (icon);
        }

        if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_GVFS_BACKEND))
            g_file_info_set_attribute_string (info, G_FILE_ATTRIBUTE_GVFS_BACKEND, "favorites");

        if (g_file_attribute_matcher_matches (matcher, FAVORITE_AVAILABLE_METADATA_KEY))
            g_file_info_set_attribute_string (info, FAVORITE_AVAILABLE_METADATA_KEY, META_TRUE);

        if (g_file_attribute_matcher_enumerate_namespace (matcher, "metadata"))
        {
            gchar **entries = g_settings_get_strv (settings, FAVORITE_DCONF_METADATA_KEY);

            if (entries != NULL)
            {
                gint i;

                for (i = 0; entries[i] != NULL; i++)
                {
                    gchar **t_n_v;

                    t_n_v = g_strsplit (entries[i], "==", 3);

                    if (g_strv_length (t_n_v) == 3)
                    {
                        if (g_strcmp0 (t_n_v[0], "string") == 0)
                        {
                            g_file_info_set_attribute_string (info, t_n_v[1], t_n_v[2]);
                        }
                        else
                        if (g_strcmp0 (t_n_v[0], "strv") == 0)
                        {
                            gchar **members = g_strsplit (t_n_v[2], "|", -1);

                            g_file_info_set_attribute_stringv (info, t_n_v[1], members);

                            g_strfreev (members);
                        }
                    }

                    g_strfreev (t_n_v);
                }
            }

            g_strfreev (entries);
        }

        g_file_attribute_matcher_unref (matcher);
    }
    else
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't retrieve info for favorite file");
    }

    return info;
}

GFileInfo *
file_query_filesystem_info (GFile         *file,
                            const char    *attributes,
                            GCancellable  *cancellable,
                            GError       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));
    GFileInfo *info;
    GFileAttributeMatcher *matcher;

    matcher = g_file_attribute_matcher_new (attributes);

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFileInfo *info;
        GFile *real_file;
        real_file = g_file_new_for_uri (priv->info->uri);

        info = g_file_query_filesystem_info (real_file,
                                             attributes,
                                             cancellable,
                                             error);

        if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY))
        {
            g_file_info_set_attribute_boolean (info,
                                               G_FILE_ATTRIBUTE_FILESYSTEM_READONLY, TRUE);
        }

        g_object_unref (real_file);
        return info;
    }

    info = g_file_info_new ();

    if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE))
    {
        g_file_info_set_attribute_string (info,
                                          G_FILE_ATTRIBUTE_FILESYSTEM_TYPE, "favorites");
    }

    if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY))
    {
        g_file_info_set_attribute_boolean (info,
                                           G_FILE_ATTRIBUTE_FILESYSTEM_READONLY, TRUE);
    }

    if (g_file_attribute_matcher_matches (matcher, G_FILE_ATTRIBUTE_FILESYSTEM_USE_PREVIEW))
    {
        g_file_info_set_attribute_uint32 (info,
                                          G_FILE_ATTRIBUTE_FILESYSTEM_USE_PREVIEW,
                                          G_FILESYSTEM_PREVIEW_TYPE_IF_LOCAL);
    }

    g_file_attribute_matcher_unref (matcher);

    return info;
}

GMount *
file_find_enclosing_mount (GFile         *file,
                           GCancellable  *cancellable,
                           GError       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GMount *mount;
        GFile *real_file;
        real_file = g_file_new_for_uri (priv->info->uri);

        mount = g_file_find_enclosing_mount (real_file,
                                             cancellable,
                                             error);

        g_object_unref (real_file);
        return mount;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                         "Can't find favorite file enclosing mount");

    return NULL;
}

GFileAttributeInfoList *
file_query_settable_attributes (GFile         *file,
                                GCancellable  *cancellable,
                                GError       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFileAttributeInfoList *list;
        GFile *real_file;
        real_file = g_file_new_for_uri (priv->info->uri);

        list = g_file_query_settable_attributes (real_file,
                                                 cancellable,
                                                 error);

        g_object_unref (real_file);
        return list;
    }

    return g_file_attribute_info_list_new ();
}

GFileAttributeInfoList *
file_query_writable_namespaces (GFile         *file,
                                GCancellable  *cancellable,
                                GError       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));
    GFileAttributeInfoList *list;

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        real_file = g_file_new_for_uri (priv->info->uri);

        list = g_file_query_writable_namespaces (real_file,
                                                 cancellable,
                                                 error);

        g_object_unref (real_file);
        return list;
    }

    list = g_file_attribute_info_list_new ();

    g_file_attribute_info_list_add (list,
                                    "metadata",
                                    G_FILE_ATTRIBUTE_TYPE_STRING,
                                    G_FILE_ATTRIBUTE_INFO_NONE);

    return list;
}

static void
remove_root_metadata (const gchar *attr_name)
{
    GPtrArray *new_array;
    gchar **old_metadata, **new_metadata;
    gint i;

    old_metadata = g_settings_get_strv (settings, FAVORITE_DCONF_METADATA_KEY);

    if (old_metadata == NULL)
    {
        return;
    }

    new_array = g_ptr_array_new ();

    for (i = 0; old_metadata[i] != NULL; i++)
    {
        gchar **t_n_v;

        t_n_v = g_strsplit (old_metadata[i], "==", 3);

        if (g_strcmp0 (t_n_v[1], attr_name) != 0)
        {
            g_ptr_array_add (new_array, g_strdup (old_metadata[i]));
        }

        g_strfreev (t_n_v);
    }

    g_ptr_array_add (new_array, NULL);
    g_strfreev (old_metadata);

    new_metadata = (gchar **) g_ptr_array_free (new_array, FALSE);

    g_settings_set_strv (settings, FAVORITE_DCONF_METADATA_KEY, (const gchar * const *) new_metadata);
    g_strfreev (new_metadata);
}

static void
set_or_update_root_metadata (const gchar        *attr_name,
                             const gpointer      value,
                             GFileAttributeType  type)
{
    GPtrArray *new_array;
    gchar **old_metadata, **new_metadata;
    gint i;
    gchar *entry;
    gboolean exists;

    old_metadata = g_settings_get_strv (settings, FAVORITE_DCONF_METADATA_KEY);

    if (old_metadata == NULL)
    {
        return;
    }

    switch (type)
    {
        case G_FILE_ATTRIBUTE_TYPE_STRING:
        {
            entry = g_strdup_printf ("string==%s==%s", attr_name, (gchar *) value);
            break;
        }
        case G_FILE_ATTRIBUTE_TYPE_STRINGV:
        {
            gchar *val_strv = g_strjoinv ("|", (gchar **) value);
            entry = g_strdup_printf ("strv==%s==%s", attr_name, val_strv);
            g_free (val_strv);
            break;
        }
        default:
            g_warn_if_reached ();
            g_strfreev (old_metadata);
            return;
    }

    exists = FALSE;
    new_array = g_ptr_array_new ();

    for (i = 0; old_metadata[i] != NULL; i++)
    {
        gchar **t_n_v;

        t_n_v = g_strsplit (old_metadata[i], "==", 3);

        if (g_strcmp0 (t_n_v[1], attr_name) == 0)
        {
            g_ptr_array_add (new_array, entry);
            exists = TRUE;
        }
        else
        {
            g_ptr_array_add (new_array, g_strdup (old_metadata[i]));
        }

        g_strfreev (t_n_v);
    }

    if (!exists)
    {
        g_ptr_array_add (new_array, entry);
    }

    g_ptr_array_add (new_array, NULL);
    g_strfreev (old_metadata);

    new_metadata = (gchar **) g_ptr_array_free (new_array, FALSE);

    g_settings_set_strv (settings, FAVORITE_DCONF_METADATA_KEY, (const gchar * const *) new_metadata);
    g_strfreev (new_metadata);
}

gboolean
file_set_attribute (GFile                *file,
                    const gchar          *attribute,
                    GFileAttributeType    type,
                    gpointer              value_p,
                    GFileQueryInfoFlags   flags,
                    GCancellable         *cancellable,
                    GError              **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));
    gboolean ret;

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        gboolean ret;
        real_file = g_file_new_for_uri (priv->info->uri);

        ret = g_file_set_attribute (real_file,
                                    attribute,
                                    type,
                                    value_p,
                                    flags,
                                    cancellable,
                                    error);

        g_object_unref (real_file);
        return ret;
    }

    ret = FALSE;

    if (!is_root_file (FAVORITE_VFS_FILE (file)))
    {
        g_set_error (error, G_IO_ERROR,
                     G_IO_ERROR_NOT_SUPPORTED,
                     "Can't set attributes for %s - only the root (favorites:///) is supported.", priv->uri);
    }
    else
    {
        if (g_str_has_prefix (attribute, "metadata"))
        {
            if (type == G_FILE_ATTRIBUTE_TYPE_INVALID || value_p == NULL || ((char *) value_p)[0] == '\0')
            {
                // unset metadata
                remove_root_metadata (attribute);
                ret = TRUE;
            }
            else
            {
                if (type == G_FILE_ATTRIBUTE_TYPE_STRING || type == G_FILE_ATTRIBUTE_TYPE_STRINGV)
                {
                    set_or_update_root_metadata (attribute, (gchar *) value_p, type);
                    ret = TRUE;
                }
                else
                {
                    g_set_error (error, G_IO_ERROR,
                                 G_IO_ERROR_NOT_SUPPORTED,
                                 "Can't set attribute '%s' for favorites:/// file "
                                 "(only string-type metadata are allowed).", attribute);
                }
            }
        }
        else
        {
            g_set_error (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                         "Can't set attribute '%s' for favorites:/// file "
                         "(only 'metadata' namespace is allowed).", attribute);
        }
    }

    return ret;
}

static gboolean
file_set_attributes_from_info (GFile                *file,
                               GFileInfo            *info,
                               GFileQueryInfoFlags   flags,
                               GCancellable         *cancellable,
                               GError              **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));
    gboolean res;

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        gboolean ret = FALSE;
        real_file = g_file_new_for_uri (priv->info->uri);

        ret = g_file_set_attributes_from_info (real_file, info, flags, cancellable, error);

        g_object_unref (real_file);
        return ret;
    }

    res = TRUE;

    if (g_file_info_has_namespace (info, "metadata"))
    {
        GFileAttributeType type;
        gchar **attributes;
        gpointer value_p;
        gint i;

        attributes = g_file_info_list_attributes (info, "metadata");

        for (i = 0; attributes[i] != NULL; i++)
        {
            if (g_file_info_get_attribute_data (info, attributes[i], &type, &value_p, NULL))
            {
                if (!file_set_attribute (file,
                                         attributes[i],
                                         type,
                                         value_p,
                                         flags,
                                         cancellable,
                                         error))
                {
                    g_file_info_set_attribute_status (info, attributes[i], G_FILE_ATTRIBUTE_STATUS_ERROR_SETTING);
                    error = NULL; // from gvfs gdaemonvfs.c - ignore subsequent errors iterating thru attribute list.
                    res = FALSE;
                }
                else
                {
                    g_file_info_set_attribute_status (info, attributes[i], G_FILE_ATTRIBUTE_STATUS_SET);
                }
            }
        }

        g_strfreev (attributes);
    }

    return res;
}

static GFileInputStream *
file_read_fn (GFile         *file,
              GCancellable  *cancellable,
              GError       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        GFileInputStream *stream;

        stream = g_file_read (real_file, cancellable, error);

        g_object_unref (real_file);
        return stream;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return NULL;
}

GFileOutputStream *
file_append_to (GFile             *file,
                GFileCreateFlags   flags,
                GCancellable      *cancellable,
                GError           **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        GFileOutputStream *stream;

        stream = g_file_append_to (real_file,
                                   flags,
                                   cancellable,
                                   error);

        g_object_unref (real_file);
        return stream;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));
    return NULL;
}

static GFileOutputStream *
file_replace (GFile             *file,
              const char        *etag,
              gboolean           make_backup,
              GFileCreateFlags   flags,
              GCancellable      *cancellable,
              GError           **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        GFileOutputStream *stream;

        stream = g_file_replace (real_file,
                                 etag,
                                 make_backup,
                                 flags,
                                 cancellable,
                                 error);

        g_object_unref (real_file);
        return stream;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return NULL;
}

static GFileIOStream *
file_open_readwrite (GFile                      *file,
                     GCancellable               *cancellable,
                     GError                    **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFileIOStream *res;
        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        res = g_file_open_readwrite (real_file,
                                     cancellable,
                                     error);

        g_object_unref (real_file);
        return res;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return NULL;
}

static GFileIOStream *
file_replace_readwrite (GFile                      *file,
                        const char                 *etag,
                        gboolean                    make_backup,
                        GFileCreateFlags            flags,
                        GCancellable               *cancellable,
                        GError                    **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFileIOStream *res;
        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        res = g_file_replace_readwrite (real_file,
                                        etag,
                                        make_backup,
                                        flags,
                                        cancellable,
                                        error);

        g_object_unref (real_file);
        return res;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return NULL;
}

static gboolean
file_delete (GFile         *file,
             GCancellable  *cancellable,
             GError       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (!is_root_file (FAVORITE_VFS_FILE (file)))
    {
        if (priv->info != NULL && priv->info->uri != NULL)
        {
            xapp_favorites_remove (xapp_favorites_get_default (), priv->info->uri);
        }
        else
        {
            xapp_favorites_remove (xapp_favorites_get_default (), priv->uri);
        }

        return TRUE;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return FALSE;
}

static gboolean
file_trash (GFile         *file,
            GCancellable  *cancellable,
            GError       **error)
{
    return file_delete (file, cancellable, error);
}

gboolean
file_copy (GFile                  *source,
           GFile                  *destination,
           GFileCopyFlags          flags,
           GCancellable           *cancellable,
           GFileProgressCallback   progress_callback,
           gpointer                progress_callback_data,
           GError                **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (source));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        gboolean res;
        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        res = g_file_copy (real_file,
                           destination,
                           flags,
                           cancellable,
                           progress_callback,
                           progress_callback_data,
                           error);

        g_object_unref (real_file);
        return res;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return FALSE;
}

static GFileMonitor *
file_monitor_dir (GFile              *file,
                  GFileMonitorFlags   flags,
                  GCancellable       *cancellable,
                  GError            **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        GFileMonitor *monitor;

        real_file = g_file_new_for_uri (priv->info->uri);
        monitor = g_file_monitor_directory (real_file,
                                            flags,
                                            cancellable,
                                            error);
        g_object_unref (real_file);

        return monitor;
    }
    else
    if (is_root_file (FAVORITE_VFS_FILE (file)))
    {
        return favorite_vfs_file_monitor_new ();
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return NULL;
}

static GFileMonitor *
file_monitor_file (GFile              *file,
                   GFileMonitorFlags   flags,
                   GCancellable       *cancellable,
                   GError            **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        GFile *real_file;
        GFileMonitor *monitor;

        real_file = g_file_new_for_uri (priv->info->uri);
        monitor = g_file_monitor_file (real_file,
                                       flags,
                                       cancellable,
                                       error);
        g_object_unref (real_file);

        return monitor;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return NULL;
}

gboolean
file_measure_disk_usage (GFile                         *file,
                         GFileMeasureFlags              flags,
                         GCancellable                  *cancellable,
                         GFileMeasureProgressCallback   progress_callback,
                         gpointer                       progress_data,
                         guint64                       *disk_usage,
                         guint64                       *num_dirs,
                         guint64                       *num_files,
                         GError                       **error)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        gboolean res;
        GFile *real_file = g_file_new_for_uri (priv->info->uri);

        res = g_file_measure_disk_usage (real_file,
                                         flags,
                                         cancellable,
                                         progress_callback,
                                         progress_data,
                                         disk_usage,
                                         num_dirs,
                                         num_files,
                                         error);

        g_object_unref (real_file);
        return res;
    }

    g_set_error_literal (error, G_IO_ERROR,
                         G_IO_ERROR_NOT_SUPPORTED,
                        _("Operation not supported"));

    return FALSE;
}

static void favorite_vfs_file_gfile_iface_init (GFileIface *iface)
{
    iface->dup = file_dup;
    iface->hash = file_hash;
    iface->equal = file_equal;
    iface->is_native = file_is_native;
    iface->has_uri_scheme = file_has_uri_scheme;
    iface->get_uri_scheme = file_get_uri_scheme;
    iface->get_basename = file_get_basename;
    iface->get_path = file_get_path;
    iface->get_uri = file_get_uri;
    iface->get_parse_name = file_get_parse_name;
    iface->get_parent = file_get_parent;
    iface->prefix_matches = file_prefix_matches;
    iface->get_relative_path = file_get_relative_path;
    iface->resolve_relative_path = file_resolve_relative_path;
    iface->get_child_for_display_name = file_get_child_for_display_name;
    iface->set_display_name = file_set_display_name;
    iface->enumerate_children = file_enumerate_children;
    iface->query_info = file_query_info;
    iface->query_filesystem_info = file_query_filesystem_info;
    iface->find_enclosing_mount = file_find_enclosing_mount;
    iface->query_settable_attributes = file_query_settable_attributes;
    iface->query_writable_namespaces = file_query_writable_namespaces;
    iface->set_attribute = file_set_attribute;
    iface->set_attributes_from_info = file_set_attributes_from_info;
    iface->read_fn = file_read_fn;
    iface->append_to = file_append_to;
    // iface->create = file_create; ### Don't support
    iface->replace = file_replace;
    iface->open_readwrite = file_open_readwrite;
    // iface->create_readwrite = file_create_readwrite; ### Don't support
    iface->replace_readwrite = file_replace_readwrite;
    iface->delete_file = file_delete;
    iface->trash = file_trash;
    // iface->make_directory = file_make_directory; ### Don't support
    // iface->make_symbolic_link = file_make_symbolic_link; ### Don't support
    iface->copy = file_copy;
    iface->monitor_dir = file_monitor_dir;
    iface->monitor_file = file_monitor_file;
    iface->measure_disk_usage = file_measure_disk_usage;
    iface->supports_thread_contexts = TRUE;
}

static void favorite_vfs_file_dispose (GObject *object)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (object));

    if (priv->info != NULL)
    {
        xapp_favorite_info_free (priv->info);
        priv->info = NULL;
    }

    G_OBJECT_CLASS (favorite_vfs_file_parent_class)->dispose (object);
}

static void favorite_vfs_file_finalize (GObject *object)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (object));

    g_clear_pointer (&priv->uri, g_free);

    G_OBJECT_CLASS (favorite_vfs_file_parent_class)->finalize (object);
}

static void
ensure_metadata_store (FavoriteVfsFile *file)
{
    if (is_root_file (file))
    {
        if (settings == NULL)
        {
            settings = g_settings_new (FAVORITES_SCHEMA);
            g_object_add_weak_pointer (G_OBJECT (settings), (gpointer) &settings);
        }
        else
        {
            g_object_ref (settings);
        }
    }
}

static void favorite_vfs_file_class_init (FavoriteVfsFileClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = favorite_vfs_file_dispose;
    gobject_class->finalize = favorite_vfs_file_finalize;
}

static void favorite_vfs_file_init (FavoriteVfsFile *self)
{
}

GFile *_favorite_vfs_file_new_for_info (XAppFavoriteInfo *info)
{
    FavoriteVfsFile *new_file;

    new_file = g_object_new (FAVORITE_TYPE_VFS_FILE, NULL);

    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (new_file));

    priv->uri = path_to_fav_uri (info->display_name);
    priv->info = xapp_favorite_info_copy (info);
    ensure_metadata_store (new_file);

    return G_FILE (new_file);
}

gchar *favorite_vfs_file_get_real_uri (GFile *file)
{
    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (file));

    if (priv->info != NULL && priv->info->uri != NULL)
    {
        return g_strdup (priv->info->uri);
    }

    return NULL;
}

GFile *favorite_vfs_file_new_for_uri (const char *uri)
{
    FavoriteVfsFile *new_file;
    g_autofree gchar *basename = NULL;

    new_file = g_object_new (FAVORITE_TYPE_VFS_FILE, NULL);

    DEBUG ("FavoriteVfsFile new for uri: %s", uri);

    FavoriteVfsFilePrivate *priv = favorite_vfs_file_get_instance_private (FAVORITE_VFS_FILE (new_file));

    priv->uri = g_strdup (uri);
    ensure_metadata_store (new_file);

    if (g_strcmp0 (uri, ROOT_URI) == 0)
    {
        priv->info = NULL;
    }
    else
    {
        gchar *display_name;

        display_name = fav_uri_to_display_name (uri);
        XAppFavoriteInfo *info = xapp_favorites_find_by_display_name (xapp_favorites_get_default (),
                                                                      display_name);

        if (info != NULL)
        {
            priv->info = xapp_favorite_info_copy (info);
        }
        else
        {
            info = g_slice_new0 (XAppFavoriteInfo);
            info->uri = g_strdup (NULL);
            info->display_name = g_strdup (display_name);
            info->cached_mimetype = NULL;

            priv->info = info;
        }

        g_free (display_name);
    }

    return G_FILE (new_file);
}

GFile *favorite_vfs_file_new (void)
{
    return favorite_vfs_file_new_for_uri (ROOT_URI);
}

static GFile *
favorite_vfs_lookup (GVfs       *vfs,
                     const char *identifier,
                     gpointer    user_data)
{
    if (g_str_has_prefix (identifier, ROOT_URI))
    {
        return favorite_vfs_file_new_for_uri (identifier);
    }

    return NULL;
}

void
init_favorite_vfs (void)
{
    static gsize once_init_value = 0;

    if (g_once_init_enter (&once_init_value))
    {
        GVfs *vfs;
        vfs = g_vfs_get_default ();

        g_vfs_register_uri_scheme (vfs, "favorites",
                                   favorite_vfs_lookup, NULL, NULL,
                                   favorite_vfs_lookup, NULL, NULL);

        g_once_init_leave (&once_init_value, 1);
    }
}

#include "xapp-favorites.h"
#include "favorite-vfs-file.h"
#include "favorite-vfs-file-monitor.h"

#define DEBUG_FLAG XAPP_DEBUG_FAVORITE_VFS
#include "xapp-debug.h"

typedef struct
{
    gulong changed_handler_id;
    GHashTable *file_monitors;
    GList *infos;

    GVolumeMonitor *mount_mon;
} FavoriteVfsFileMonitorPrivate;

struct _FavoriteVfsFileMonitor
{
    GObject parent_instance;
    FavoriteVfsFileMonitorPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE(FavoriteVfsFileMonitor, \
                           favorite_vfs_file_monitor, \
                           G_TYPE_FILE_MONITOR)

GFile *_favorite_vfs_file_new_for_info (XAppFavoriteInfo *info);

// static void
// rename_favorite (GFile *old_file,
//                  GFile *new_file)
// {
//     gchar *old_file_uri, *new_file_uri;

//     old_file_uri = g_file_get_uri (old_file);
//     new_file_uri = g_file_get_uri (new_file);

//     _xapp_favorites_rename (xapp_favorites_get_default (),
//                            old_file_uri,
//                            new_file_uri);

//     g_free (old_file_uri);
//     g_free (new_file_uri);
// }

static void
favorite_real_file_changed (GFileMonitor     *rfmonitor,
                            GFile            *file,
                            GFile            *other_file,
                            GFileMonitorEvent event_type,
                            gpointer          user_data)
{
    // Disabled
    return;

//     g_return_if_fail (FAVORITE_IS_VFS_FILE_MONITOR (user_data));
//     FavoriteVfsFileMonitor *monitor = FAVORITE_VFS_FILE_MONITOR (user_data);
//     FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private (monitor);

//     DEBUG ("real file changed: %s: %d", g_file_get_uri (file), event_type);

//     switch (event_type)
//     {
//         case G_FILE_MONITOR_EVENT_MOVED_OUT:
//             break;
//             {
//                 gchar *uri = g_file_get_uri (file);

//                 DEBUG ("Deleted: %s\n", uri);

//                 xapp_favorites_remove (xapp_favorites_get_default (), uri);
//                 g_free (uri);
//             }

//             break;
//         case G_FILE_MONITOR_EVENT_RENAMED:
//             break;
//             {
//                 gchar *uri = g_file_get_uri (file);

//                 DEBUG ("Renamed: %s\n", uri);

//                 rename_favorite (file, other_file);
//             }
//             break;
//         case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
//         case G_FILE_MONITOR_EVENT_CHANGED:
//             {
//                 gchar *uri = g_file_get_uri (file);

//                 GList *iter;

//                 for (iter = priv->infos; iter != NULL; iter = iter->next)
//                 {
//                     XAppFavoriteInfo *info = (XAppFavoriteInfo *) iter->data;

//                     if (g_strcmp0 (uri, info->uri) == 0)
//                     {
//                         GFile *fav_file;
//                         gchar *uri;

//                         uri = path_to_fav_uri (info->display_name);
//                         fav_file = g_file_new_for_uri (uri);

//                         DEBUG ("Changed: %s", uri);
//                         g_free (uri);

//                         g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
//                                                    fav_file,
//                                                    NULL,
//                                                    G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED);
//                         g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
//                                                    fav_file,
//                                                    NULL,
//                                                    G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT);
//                         g_object_unref (fav_file);

//                         break;
//                     }
//                 }
//             }
//             break;
//         case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
//         case G_FILE_MONITOR_EVENT_CREATED:
//         // case G_FILE_MONITOR_EVENT_CHANGED:
//         case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
//         case G_FILE_MONITOR_EVENT_UNMOUNTED:
//         case G_FILE_MONITOR_EVENT_MOVED:
//         case G_FILE_MONITOR_EVENT_MOVED_IN:
//         case G_FILE_MONITOR_EVENT_DELETED:
//             break;
//         default:
//             g_warn_if_reached ();
//     }
}

static void
unmonitor_files (FavoriteVfsFileMonitor *monitor)
{
    /* Disabled. See below */
    return;

    FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private (monitor);

    if (priv->file_monitors != NULL)
    {
        g_hash_table_destroy (priv->file_monitors);
        priv->file_monitors = NULL;
    }
}

static void
monitor_files (FavoriteVfsFileMonitor *monitor)
{

    /* Disabled - this isn't necessary right now but could be expanded to help
     * support less integrated apps. */
    return;

    FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private (monitor);
    GList *iter;

    priv->file_monitors = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, (GDestroyNotify) g_object_unref);

    for (iter = priv->infos; iter != NULL; iter = iter->next)
    {
        XAppFavoriteInfo *info = (XAppFavoriteInfo *) iter->data;
        GFileMonitor *real_monitor;
        GFile *real_file;
        GError *error;

        DEBUG ("Monitoring real file: %s\n", info->uri);

        error = NULL;
        real_file = g_file_new_for_uri (info->uri);
        real_monitor = g_file_monitor (real_file,
                                       G_FILE_MONITOR_WATCH_MOVES,
                                       NULL,
                                       &error);
        g_object_unref (real_file);

        if (real_monitor == NULL)
        {
            if (error != NULL)
            {
                g_warning ("Unable to add file monitor for '%s': %s", info->uri, error->message);
                g_error_free (error);
            }

            continue;
        }

        g_hash_table_insert (priv->file_monitors,
                             (gpointer) g_strdup (info->uri),
                             (gpointer) real_monitor);

        g_signal_connect (real_monitor,
                          "changed",
                          G_CALLBACK (favorite_real_file_changed),
                          monitor);
    }
}

static gint
find_info_by_uri (gconstpointer ptr_a,
                  gconstpointer ptr_b)
{
    XAppFavoriteInfo *info = (XAppFavoriteInfo *) ptr_a;
    const gchar *uri = (gchar *) ptr_b;

    // GCompareFunc returns 0 when there's a match.
    return g_strcmp0 (info->uri, uri);
}

static void
favorites_changed (XAppFavorites *favorites,
                   gpointer       user_data)
{
    g_return_if_fail (XAPP_IS_FAVORITES (favorites));
    g_return_if_fail (FAVORITE_IS_VFS_FILE_MONITOR (user_data));

    FavoriteVfsFileMonitor *monitor = FAVORITE_VFS_FILE_MONITOR (user_data);
    FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private (monitor);
    GList *added, *removed;
    GList *iter, *new_infos;

    if (g_file_monitor_is_cancelled (G_FILE_MONITOR (monitor)))
    {
        return;
    }

    added = removed = NULL;

    new_infos = xapp_favorites_get_favorites (favorites, NULL);

    for (iter = priv->infos; iter != NULL; iter = iter->next)
    {
        XAppFavoriteInfo *old_info = (XAppFavoriteInfo *) iter->data;
        GList *res = g_list_find_custom (new_infos,
                                         (gpointer) old_info->uri,
                                         (GCompareFunc) find_info_by_uri);
        if (res == NULL)
        {
            removed = g_list_prepend (removed, old_info);
        }
    }

    for (iter = new_infos; iter != NULL; iter = iter->next)
    {
        XAppFavoriteInfo *new_info = (XAppFavoriteInfo *) iter->data;
        GList *res = g_list_find_custom (priv->infos,
                                         (gpointer) new_info->uri,
                                         (GCompareFunc) find_info_by_uri);
        if (res == NULL)
        {
            added = g_list_prepend (added, new_info);
        }
    }

    for (iter = removed; iter != NULL; iter = iter->next)
    {
        XAppFavoriteInfo *removed_info = (XAppFavoriteInfo *) iter->data;

        GFile *file = _favorite_vfs_file_new_for_info (removed_info);

        g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
                                   file,
                                   NULL,
                                   G_FILE_MONITOR_EVENT_DELETED);
        g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
                                   file,
                                   NULL,
                                   G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT);
        g_object_unref (file);
    }

    for (iter = added; iter != NULL; iter = iter->next)
    {
        XAppFavoriteInfo *added_info = (XAppFavoriteInfo *) iter->data;

        GFile *file = _favorite_vfs_file_new_for_info (added_info);

        g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
                                   file,
                                   NULL,
                                   G_FILE_MONITOR_EVENT_CREATED);
        g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
                                   file,
                                   NULL,
                                   G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT);
        g_object_unref (file);
    }

    g_list_free (added);
    g_list_free (removed);

    GList *tmp = priv->infos;
    priv->infos = new_infos;

    g_list_free_full (tmp, (GDestroyNotify) xapp_favorite_info_free);

    //FIXME: add/remove individually
    unmonitor_files (monitor);
    monitor_files (monitor);
}

static void
mounts_changed (GVolumeMonitor *mount_mon,
                GMount         *mount,
                gpointer        user_data)
{
    g_return_if_fail (FAVORITE_IS_VFS_FILE_MONITOR (user_data));

    FavoriteVfsFileMonitor *monitor = FAVORITE_VFS_FILE_MONITOR (user_data);
    FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private (monitor);

    GFile *root;
    GList *iter, *mount_favorites;

    root = g_mount_get_root (mount);
    mount_favorites = NULL;

    // Find any favorites that are descendent from root.

    for (iter = priv->infos; iter != NULL; iter = iter->next)
    {
        XAppFavoriteInfo *info = (XAppFavoriteInfo *) iter->data;
        GFile *fav_file = g_file_new_for_uri (info->uri);
        gchar *relpath;

        relpath = g_file_get_relative_path (root, fav_file);

        if (relpath != NULL)
        {
            mount_favorites = g_list_prepend (mount_favorites, info);
        }

        g_free (relpath);
        g_object_unref (fav_file);
    }

    if (mount_favorites != NULL)
    {
        for (iter = mount_favorites; iter != NULL; iter = iter->next) {
            XAppFavoriteInfo *info = (XAppFavoriteInfo *) iter->data;
            GFile *fav_file;
            gchar *uri;
            
            uri = path_to_fav_uri (info->display_name);
            fav_file = g_file_new_for_uri (uri);

            g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
                                       fav_file,
                                       NULL,
                                       G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED);
            g_file_monitor_emit_event (G_FILE_MONITOR (monitor),
                                       fav_file,
                                       NULL,
                                       G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT);

            g_free (uri);
            g_object_unref (fav_file);
        }

        g_list_free (mount_favorites);
    }

    g_object_unref (root);

    unmonitor_files (monitor);
    monitor_files (monitor);
}

static gboolean
favorite_vfs_file_monitor_cancel (GFileMonitor* gfilemon)
{
    FavoriteVfsFileMonitor *monitor = FAVORITE_VFS_FILE_MONITOR (gfilemon);
    FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private (monitor);

    if (priv->changed_handler_id > 0)
    {
        g_signal_handler_disconnect (xapp_favorites_get_default (), priv->changed_handler_id);
    }

  return TRUE;
}

static void
favorite_vfs_file_monitor_init (FavoriteVfsFileMonitor *monitor)
{
    FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private (monitor);

    priv->mount_mon = g_volume_monitor_get ();
    g_signal_connect (priv->mount_mon,
                      "mount-added",
                      G_CALLBACK (mounts_changed),
                      monitor);
    g_signal_connect (priv->mount_mon,
                      "mount-removed",
                      G_CALLBACK (mounts_changed),
                      monitor);

    priv->infos = xapp_favorites_get_favorites (xapp_favorites_get_default (), NULL);
    priv->changed_handler_id = g_signal_connect (xapp_favorites_get_default (),
                                                 "changed",
                                                 G_CALLBACK (favorites_changed),
                                                 monitor);

    monitor_files (monitor);
}

static void
favorite_vfs_file_monitor_dispose (GObject *object)
{
    FavoriteVfsFileMonitor *monitor = FAVORITE_VFS_FILE_MONITOR(object);
    FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private(monitor);

    unmonitor_files (monitor);

    g_signal_handlers_disconnect_by_func (priv->mount_mon, mounts_changed, monitor);
    g_clear_object (&priv->mount_mon);

    if (priv->infos != NULL)
    {
        g_list_free_full (priv->infos, (GDestroyNotify) xapp_favorite_info_free);
        priv->infos = NULL;
    }

    G_OBJECT_CLASS (favorite_vfs_file_monitor_parent_class)->dispose (object);
}

static void
favorite_vfs_file_monitor_finalize (GObject *object)
{
    // FavoriteVfsFileMonitor *self = FAVORITE_VFS_FILE_MONITOR(object);
    // FavoriteVfsFileMonitorPrivate *priv = favorite_vfs_file_monitor_get_instance_private(self);

    G_OBJECT_CLASS (favorite_vfs_file_monitor_parent_class)->finalize (object);
}

static void
favorite_vfs_file_monitor_class_init (FavoriteVfsFileMonitorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GFileMonitorClass *monitor_class = G_FILE_MONITOR_CLASS (klass);

    gobject_class->dispose = favorite_vfs_file_monitor_dispose;
    gobject_class->finalize = favorite_vfs_file_monitor_finalize;

    monitor_class->cancel = favorite_vfs_file_monitor_cancel;
}

GFileMonitor *
favorite_vfs_file_monitor_new (void)
{
    return G_FILE_MONITOR (g_object_new (FAVORITE_TYPE_VFS_FILE_MONITOR, NULL));
}


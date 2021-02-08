#include "xapp-favorites.h"
#include "favorite-vfs-file-enumerator.h"
#include "favorite-vfs-file.h"

#define DEBUG_FLAG XAPP_DEBUG_FAVORITE_VFS
#include "xapp-debug.h"

typedef struct
{
    GFile *file;

    GList *uris;
    gchar *attributes;
    GFileQueryInfoFlags flags;

    GList *current_pos;
} FavoriteVfsFileEnumeratorPrivate;

struct _FavoriteVfsFileEnumerator
{
    GObject parent_instance;
    FavoriteVfsFileEnumeratorPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE(FavoriteVfsFileEnumerator,
                           favorite_vfs_file_enumerator,
                           G_TYPE_FILE_ENUMERATOR)

static GFileInfo *
next_file (GFileEnumerator *enumerator,
           GCancellable    *cancellable,
           GError         **error)
{
    FavoriteVfsFileEnumerator *self = FAVORITE_VFS_FILE_ENUMERATOR (enumerator);
    FavoriteVfsFileEnumeratorPrivate *priv = favorite_vfs_file_enumerator_get_instance_private (self);
    GFileInfo *info;

    if (g_cancellable_set_error_if_cancelled (cancellable, error))
    {
        return NULL;
    }

    info = NULL;

    while (priv->current_pos != NULL && info == NULL)
    {
        gchar *uri;

        uri = path_to_fav_uri ((const gchar *) priv->current_pos->data);
        if (!xapp_favorites_find_by_display_name (xapp_favorites_get_default (), (gchar *) priv->current_pos->data))
        {
            if (error)
            {
                *error = g_error_new (G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File not found");
            }

            g_warn_if_reached ();
        }
        else
        {
            GFile *file;

            file = g_file_new_for_uri (uri);
            info = g_file_query_info (file,
                                      priv->attributes,
                                      priv->flags,
                                      cancellable,
                                      error);

            g_object_unref (file);
        }

        g_free (uri);
    }

    if (priv->current_pos)
    {
        priv->current_pos = priv->current_pos->next;
    }

    return info;
}

static void
next_async_op_free (GList *files)
{
    g_list_free_full (files, g_object_unref);
}

static void
next_files_async_thread (GTask        *task,
                         gpointer      source_object,
                         gpointer      task_data,
                         GCancellable *cancellable)
{
    FavoriteVfsFileEnumerator *self = FAVORITE_VFS_FILE_ENUMERATOR (source_object);
    GList *ret;
    GError *error = NULL;
    gint i, n_requested;

    n_requested = GPOINTER_TO_INT (g_task_get_task_data (task));

    ret = NULL;

    for (i = 0; i < n_requested; i++)
    {
        GFileInfo *info = NULL;
        // g_clear_error (&error);

        if (g_cancellable_set_error_if_cancelled (cancellable, &error))
        {
            g_task_return_error (task, error);
            return;
        }
        else
        {
            info = next_file (G_FILE_ENUMERATOR (self), cancellable, &error);
        }

        if (info != NULL)
        {
            ret = g_list_prepend (ret, info);
        }
        else
        {
            if (error)
            {
                g_critical ("ERROR: %s\n", error->message);
            }
            break;
        }
    }

    if (error)
    {
        g_task_return_error (task, error);
    }
    else
    {
        ret = g_list_reverse (ret);
        g_task_return_pointer (task, ret, (GDestroyNotify) next_async_op_free);
    }
}

static void
next_files_async (GFileEnumerator     *enumerator,
                  gint                 num_files,
                  gint                 io_priority,
                  GCancellable        *cancellable,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
    // FavoriteVfsFileEnumerator *self = FAVORITE_VFS_FILE_ENUMERATOR (enumerator);

    GTask *task;
    task = g_task_new (enumerator, cancellable, callback, user_data);
    g_task_set_priority (task, io_priority);
    g_task_set_task_data (task, GINT_TO_POINTER (num_files), NULL);

    g_task_run_in_thread (task, next_files_async_thread);
    g_object_unref (task);
}

static GList *
next_files_finished (GFileEnumerator  *enumerator,
                     GAsyncResult     *result,
                     GError          **error)
{
    g_return_val_if_fail (G_IS_FILE_ENUMERATOR (enumerator), NULL);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

    return (GList *) g_task_propagate_pointer (G_TASK (result), error);
}

static gboolean
close_fn (GFileEnumerator *enumerator,
          GCancellable    *cancellable,
          GError         **error)
{
    // FavoriteVfsFileEnumerator *self = FAVORITE_VFS_FILE_ENUMERATOR (enumerator);

    return TRUE;
}

static void
favorite_vfs_file_enumerator_init (FavoriteVfsFileEnumerator *self)
{
}

static void
favorite_vfs_file_enumerator_dispose (GObject *object)
{
    G_OBJECT_CLASS (favorite_vfs_file_enumerator_parent_class)->dispose (object);
}

static void
favorite_vfs_file_enumerator_finalize (GObject *object)
{
    FavoriteVfsFileEnumerator *self = FAVORITE_VFS_FILE_ENUMERATOR(object);
    FavoriteVfsFileEnumeratorPrivate *priv = favorite_vfs_file_enumerator_get_instance_private(self);

    g_list_free_full (priv->uris, (GDestroyNotify) g_free);
    g_free (priv->attributes);
    g_object_unref (priv->file);

    G_OBJECT_CLASS (favorite_vfs_file_enumerator_parent_class)->finalize (object);
}

static void
favorite_vfs_file_enumerator_class_init (FavoriteVfsFileEnumeratorClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GFileEnumeratorClass *enumerator_class = G_FILE_ENUMERATOR_CLASS (klass);

    gobject_class->dispose = favorite_vfs_file_enumerator_dispose;
    gobject_class->finalize = favorite_vfs_file_enumerator_finalize;

    enumerator_class->next_file = next_file;
    enumerator_class->next_files_async = next_files_async;
    enumerator_class->next_files_finish = next_files_finished;
    enumerator_class->close_fn = close_fn;
}

GFileEnumerator *
favorite_vfs_file_enumerator_new (GFile               *file,
                                  const gchar         *attributes,
                                  GFileQueryInfoFlags  flags,
                                  GList               *uris)
{
    FavoriteVfsFileEnumerator *enumerator = g_object_new (FAVORITE_TYPE_VFS_FILE_ENUMERATOR, NULL);
    FavoriteVfsFileEnumeratorPrivate *priv = favorite_vfs_file_enumerator_get_instance_private(enumerator);

    priv->uris = g_list_copy_deep (uris, (GCopyFunc) g_strdup, NULL);
    priv->current_pos = priv->uris;

    priv->file = g_object_ref (file);
    priv->attributes = g_strdup (attributes);
    priv->flags = flags;

    return G_FILE_ENUMERATOR (enumerator);
}


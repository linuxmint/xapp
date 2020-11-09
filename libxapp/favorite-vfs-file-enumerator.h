#ifndef FAVORITE_VFS_FILE_ENUMERATOR_H
#define FAVORITE_VFS_FILE_ENUMERATOR_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define FAVORITE_TYPE_VFS_FILE_ENUMERATOR favorite_vfs_file_enumerator_get_type()

G_DECLARE_FINAL_TYPE (FavoriteVfsFileEnumerator, favorite_vfs_file_enumerator, \
                      FAVORITE, VFS_FILE_ENUMERATOR, \
                      GFileEnumerator)

GFileEnumerator *
favorite_vfs_file_enumerator_new (GFile               *file,
                                  const gchar         *attributes,
                                  GFileQueryInfoFlags  flags,
                                  GList               *favorites);

G_END_DECLS

#endif // FAVORITE_VFS_FILE_ENUMERATOR_H

#ifndef FAVORITE_VFS_FILE_H
#define FAVORITE_VFS_FILE_H

#include "xapp-favorites.h"
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define FAVORITE_TYPE_VFS_FILE (favorite_vfs_file_get_type ())

G_DECLARE_FINAL_TYPE (FavoriteVfsFile, favorite_vfs_file, \
                      FAVORITE, VFS_FILE, GObject)

// Initializer for favorites:/// - called when the XAppFavorites singleton is created
void init_favorite_vfs (void);

GFile *favorite_vfs_file_new_for_uri (const char *uri);
gchar *favorite_vfs_file_get_real_uri (GFile *file);

#define URI_SCHEME "favorites"
#define ROOT_URI ("favorites:///")

#define FAVORITE_METADATA_KEY "metadata::xapp-favorite"
#define FAVORITE_AVAILABLE_METADATA_KEY "metadata::xapp-favorite-available"

#define META_TRUE "true"
#define META_FALSE "false"

gchar *path_to_fav_uri (const gchar *path);
gchar *fav_uri_to_display_name (const gchar *uri);

G_END_DECLS

#endif // FAVORITE_VFS_FILE_H

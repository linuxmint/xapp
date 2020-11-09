#ifndef __FAVORITE_VFS_FILE_MONITOR_H__
#define __FAVORITE_VFS_FILE_MONITOR_H__

#include <gio/gio.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define FAVORITE_TYPE_VFS_FILE_MONITOR       (favorite_vfs_file_monitor_get_type ())

G_DECLARE_FINAL_TYPE (FavoriteVfsFileMonitor, favorite_vfs_file_monitor,
                      FAVORITE, VFS_FILE_MONITOR, GFileMonitor)

GFileMonitor *favorite_vfs_file_monitor_new (void);

G_END_DECLS

#endif /* __FAVORITE_VFS_FILE_MONITOR_H__ */

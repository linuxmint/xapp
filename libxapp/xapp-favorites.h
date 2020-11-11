#ifndef __XAPP_FAVORITES_H__
#define __XAPP_FAVORITES_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_FAVORITE_INFO (xapp_favorite_info_get_type ())
typedef struct _XAppFavoriteInfo XAppFavoriteInfo;

#define XAPP_TYPE_FAVORITES           (xapp_favorites_get_type ())

G_DECLARE_FINAL_TYPE (XAppFavorites, xapp_favorites, XAPP, FAVORITES, GObject)

XAppFavorites        *xapp_favorites_get_default            (void);
GList                *xapp_favorites_get_favorites          (XAppFavorites *favorites,
                                                             const gchar  **mimetypes);
gint                  xapp_favorites_get_n_favorites        (XAppFavorites *favorites);
XAppFavoriteInfo     *xapp_favorites_find_by_display_name   (XAppFavorites *favorites,
                                                             const gchar   *display_name);
XAppFavoriteInfo     *xapp_favorites_find_by_uri            (XAppFavorites *favorites,
                                                             const gchar   *uri);
void                  xapp_favorites_add                    (XAppFavorites *favorites,
                                                             const gchar   *uri);
void                  xapp_favorites_remove                 (XAppFavorites *favorites,
                                                             const gchar   *uri);
void                  xapp_favorites_launch                 (XAppFavorites *favorites,
                                                             const gchar   *uri,
                                                             guint32        timestamp);
void                  xapp_favorites_rename                 (XAppFavorites *favorites,
                                                             const gchar   *old_uri,
                                                             const gchar   *new_uri);

/**
 * XAppFavoriteInfo:
 * @uri: The uri to the favorite file.
 * @display_name: The name for use when displaying the item. This may not exactly match
 * the filename if there are files with the same name but in different folders.
 * @cached_mimetype: The mimetype calculated for the uri when it was added to favorites.
 *
 * Information related to a single favorite file.
 */
struct _XAppFavoriteInfo
{
    gchar *uri;
    gchar *display_name;
    gchar *cached_mimetype;
};

GType             xapp_favorite_info_get_type (void) G_GNUC_CONST;
XAppFavoriteInfo *xapp_favorite_info_copy     (const XAppFavoriteInfo *info);
void              xapp_favorite_info_free     (XAppFavoriteInfo *info);


/*         XAppFavoritesMenu          */

/**
 * FavoritesItemSelectedCallback
 * @favorites: the #XAppFavorites instance
 * @uri: the uri of the favorite that was selected
 * @user_data: Callback data
 *
 * Callback when an item has been selected from the favorites
 * #GtkMenu.
 */
typedef void (* XAppFavoritesItemSelectedCallback) (XAppFavorites  *favorites,
                                                    const gchar    *uri,
                                                    gpointer        user_data);

GtkWidget       *xapp_favorites_create_menu (XAppFavorites                      *favorites,
                                             const gchar                       **mimetypes,
                                             XAppFavoritesItemSelectedCallback   callback,
                                             gpointer                            user_data,
                                             GDestroyNotify                      func);
GList           *xapp_favorites_create_actions    (XAppFavorites  *favorites,
                                                   const gchar   **mimetypes);

G_END_DECLS

#endif  /* __XAPP_FAVORITES_H__ */

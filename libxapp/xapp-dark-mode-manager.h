#ifndef __XAPP_DARK_MODE_MANAGER_H__
#define __XAPP_DARK_MODE_MANAGER_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_DARK_MODE_MANAGER (xapp_dark_mode_manager_get_type ())
G_DECLARE_FINAL_TYPE (XAppDarkModeManager, xapp_dark_mode_manager, XAPP, DARK_MODE_MANAGER, GObject)

XAppDarkModeManager  *xapp_dark_mode_manager_new (gboolean prefer_dark_mode);

G_END_DECLS

#endif  /* __XAPP_DARK_MODE_MANAGER_H__ */

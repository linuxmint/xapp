#ifndef __SN_ITEM_H__
#define __SN_ITEM_H__

#include <stdio.h>

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SN_TYPE_ITEM (sn_item_get_type ())

G_DECLARE_FINAL_TYPE (SnItem, sn_item, SN, ITEM, GObject)

SnItem *sn_item_new (GDBusProxy *sn_item_proxy, gboolean is_ai);
void    sn_item_update_menus (SnItem *item);

#define STATUS_ICON_SCHEMA "org.x.apps.statusicon"
#define WHITELIST_KEY "left-click-activate-apps"
#define VALID_XDG_DESKTOPS_KEY "status-notifier-enabled-desktops"
#define DEBUG_KEY "sn-watcher-debug"
#define DEBUG_FLAGS_KEY "sn-watcher-debug-flags"
#define REPLACE_TOOLTIP_KEY "sn-watcher-replace-tooltip"
#define ADVERTISE_SNH_KEY "sn-watcher-advertise-host"
extern GSettings *xapp_settings;

G_END_DECLS

#endif  /* __SN_ITEM_H__ */

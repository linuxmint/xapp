/* xapp-status-icon-private.h
 *
 * Private structures and functions shared across XAppStatusIcon implementations
 */

#ifndef __XAPP_STATUS_ICON_PRIVATE_H__
#define __XAPP_STATUS_ICON_PRIVATE_H__

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <libdbusmenu-gtk/dbusmenu-gtk.h>
#include "xapp-status-icon.h"
#include "xapp-status-icon-backend.h"
#include "xapp-statusicon-interface.h"
#include "sn-item-interface.h"

G_BEGIN_DECLS

/* Global state shared across all icons */
extern XAppStatusIconState process_icon_state;

/* Signal indices */
enum
{
    SIGNAL_BUTTON_PRESS,
    SIGNAL_BUTTON_RELEASE,
    SIGNAL_ACTIVATE,
    SIGNAL_STATE_CHANGED,
    SIGNAL_SCROLL,
    SIGNAL_LAST
};

extern guint status_icon_signals[SIGNAL_LAST];

/**
 * XAppStatusIconPrivate:
 *
 * Private structure containing all state for a status icon instance.
 * This is shared across all backend implementations.
 */
typedef struct
{
    /* D-Bus connection (shared) */
    GDBusConnection *connection;
    GCancellable *cancellable;

    /* Backend management */
    XAppBackendType active_backend;
    XAppBackend *backend_ops;
    gboolean sni_attempted;  /* Whether SNI backend was tried */

    /* Native backend state (org.x.StatusIcon) */
    XAppStatusIconInterface *interface_skeleton;
    XAppObjectSkeleton *object_skeleton;

    /* SNI backend state (org.kde.StatusNotifierItem) */
    SnItemInterface *sni_skeleton;
    gchar *sni_item_path;
    gchar *dbusmenu_path;
    DbusmenuServer *dbusmenu_server;
    gboolean sni_registered;
    guint sni_watcher_watch_id;

    /* Fallback backend state (GtkStatusIcon) */
    GtkStatusIcon *gtk_status_icon;

    /* Menus (shared by all backends) */
    GtkWidget *primary_menu;
    GtkWidget *secondary_menu;

    /* Common icon properties */
    gchar *name;
    gchar *icon_name;
    gchar *tooltip_text;
    gchar *label;
    gboolean visible;
    gint icon_size;
    gchar *metadata;

    /* State tracking */
    guint listener_id;
    gint fail_counter;
    gboolean have_button_press;
} XAppStatusIconPrivate;

struct _XAppStatusIcon
{
    GObject parent_instance;
};

/* Structure accessors */
/* The G_DEFINE_TYPE_WITH_PRIVATE macro generates an inline static version of
 * xapp_status_icon_get_instance_private that's only available in xapp-status-icon.c.
 * We provide _xapp_status_icon_get_priv as a non-inline wrapper that other
 * compilation units can link against. */
XAppStatusIconPrivate *_xapp_status_icon_get_priv(XAppStatusIcon *self);

#define XAPP_STATUS_ICON_GET_PRIVATE(obj) \
    (_xapp_status_icon_get_priv((XAppStatusIcon *) (obj)))

/* Common utility functions used across backends */
const gchar *panel_position_to_str(GtkPositionType type);
const gchar *button_to_str(guint button);
const gchar *state_to_str(XAppStatusIconState state);
const gchar *scroll_direction_to_str(XAppScrollDirection direction);

void cancellable_reset(XAppStatusIcon *self);

/* Menu handling */
void popup_menu(XAppStatusIcon *self,
                GtkMenu *menu,
                gint x,
                gint y,
                guint button,
                guint _time,
                gint panel_position);

gboolean should_send_activate(guint button, gboolean have_button_press);
GtkMenu *get_menu_to_use(XAppStatusIcon *self, guint button);

/* Signal emission helpers */
void emit_button_press(XAppStatusIcon *self,
                      gint x, gint y,
                      guint button,
                      guint time,
                      gint panel_position);

void emit_button_release(XAppStatusIcon *self,
                        gint x, gint y,
                        guint button,
                        guint time,
                        gint panel_position);

void emit_activate(XAppStatusIcon *self,
                  guint button,
                  guint time);

void emit_scroll(XAppStatusIcon *self,
                gint delta,
                XAppScrollDirection direction,
                guint time);

void emit_state_changed(XAppStatusIcon *self,
                       XAppStatusIconState state);

/* Backend selection and switching */
void refresh_icon(XAppStatusIcon *self);
void switch_to_backend(XAppStatusIcon *self, XAppBackendType new_backend);

/* Name ownership */
const gchar *get_process_bus_name(XAppStatusIcon *self);

/* SNI backend specific functions */
void sni_backend_export_menu(XAppStatusIcon *self);

G_END_DECLS

#endif /* __XAPP_STATUS_ICON_PRIVATE_H__ */

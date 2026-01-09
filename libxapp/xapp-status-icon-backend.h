/* xapp-status-icon-backend.h
 *
 * Backend abstraction for XAppStatusIcon implementations
 */

#ifndef __XAPP_STATUS_ICON_BACKEND_H__
#define __XAPP_STATUS_ICON_BACKEND_H__

#include <glib-object.h>
#include "xapp-status-icon.h"

G_BEGIN_DECLS

typedef struct _XAppStatusIcon XAppStatusIcon;

/**
 * XAppBackendType:
 * @XAPP_BACKEND_NONE: No backend active
 * @XAPP_BACKEND_NATIVE: Native XAppStatusIconMonitor backend (org.x.StatusIcon)
 * @XAPP_BACKEND_SNI: StatusNotifier backend (org.kde.StatusNotifierItem)
 * @XAPP_BACKEND_FALLBACK: GtkStatusIcon fallback backend (X11 xembed)
 *
 * Types of backends available for status icon display.
 */
typedef enum
{
    XAPP_BACKEND_NONE,
    XAPP_BACKEND_NATIVE,
    XAPP_BACKEND_SNI,
    XAPP_BACKEND_FALLBACK
} XAppBackendType;

/**
 * XAppBackend:
 * @type: The backend type
 * @init: Initialize the backend for an icon instance
 * @cleanup: Clean up backend resources for an icon instance
 * @sync: Sync all properties to the backend
 * @set_icon_name: Update icon name
 * @set_tooltip: Update tooltip text
 * @set_visible: Update visibility state
 * @set_label: Update label text
 *
 * Backend operations structure. Each backend implements these functions
 * to handle icon lifecycle and property updates.
 */
typedef struct
{
    XAppBackendType type;

    /* Lifecycle */
    gboolean (*init)(XAppStatusIcon *icon);
    void (*cleanup)(XAppStatusIcon *icon);
    void (*sync)(XAppStatusIcon *icon);

    /* Property updates */
    void (*set_icon_name)(XAppStatusIcon *icon, const gchar *icon_name);
    void (*set_tooltip)(XAppStatusIcon *icon, const gchar *tooltip);
    void (*set_visible)(XAppStatusIcon *icon, gboolean visible);
    void (*set_label)(XAppStatusIcon *icon, const gchar *label);
} XAppBackend;

/* Backend implementation declarations */
extern XAppBackend native_backend_ops;
extern XAppBackend sni_backend_ops;
extern XAppBackend fallback_backend_ops;

/* Backend initialization functions */
gboolean native_backend_init(XAppStatusIcon *icon);
void native_backend_cleanup(XAppStatusIcon *icon);
void native_backend_sync(XAppStatusIcon *icon);
void native_backend_set_icon_name(XAppStatusIcon *icon, const gchar *icon_name);
void native_backend_set_tooltip(XAppStatusIcon *icon, const gchar *tooltip);
void native_backend_set_visible(XAppStatusIcon *icon, gboolean visible);
void native_backend_set_label(XAppStatusIcon *icon, const gchar *label);

gboolean fallback_backend_init(XAppStatusIcon *icon);
void fallback_backend_cleanup(XAppStatusIcon *icon);
void fallback_backend_sync(XAppStatusIcon *icon);
void fallback_backend_set_icon_name(XAppStatusIcon *icon, const gchar *icon_name);
void fallback_backend_set_tooltip(XAppStatusIcon *icon, const gchar *tooltip);
void fallback_backend_set_visible(XAppStatusIcon *icon, gboolean visible);
void fallback_backend_set_label(XAppStatusIcon *icon, const gchar *label);

gboolean sni_backend_init(XAppStatusIcon *icon);
void sni_backend_cleanup(XAppStatusIcon *icon);
void sni_backend_sync(XAppStatusIcon *icon);
void sni_backend_set_icon_name(XAppStatusIcon *icon, const gchar *icon_name);
void sni_backend_set_tooltip(XAppStatusIcon *icon, const gchar *tooltip);
void sni_backend_set_visible(XAppStatusIcon *icon, gboolean visible);
void sni_backend_set_label(XAppStatusIcon *icon, const gchar *label);

/* Helper functions */
const gchar *backend_type_to_string(XAppBackendType type);
XAppStatusIconState backend_type_to_state(XAppBackendType type);

G_END_DECLS

#endif /* __XAPP_STATUS_ICON_BACKEND_H__ */

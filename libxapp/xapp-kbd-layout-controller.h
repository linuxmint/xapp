#ifndef __XAPP_KBD_LAYOUT_CONTROLLER_H__
#define __XAPP_KBD_LAYOUT_CONTROLLER_H__

#include <stdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <glib-object.h>
#include <cairo.h>

G_BEGIN_DECLS

#define XAPP_TYPE_KBD_LAYOUT_CONTROLLER            (xapp_kbd_layout_controller_get_type ())
#define XAPP_KBD_LAYOUT_CONTROLLER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAPP_TYPE_KBD_LAYOUT_CONTROLLER, XAppKbdLayoutController))
#define XAPP_KBD_LAYOUT_CONTROLLER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  XAPP_TYPE_KBD_LAYOUT_CONTROLLER, XAppKbdLayoutControllerClass))
#define XAPP_IS_KBD_LAYOUT_CONTROLLER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XAPP_TYPE_KBD_LAYOUT_CONTROLLER))
#define XAPP_IS_KBD_LAYOUT_CONTROLLER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  XAPP_TYPE_KBD_LAYOUT_CONTROLLER))
#define XAPP_KBD_LAYOUT_CONTROLLER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  XAPP_TYPE_KBD_LAYOUT_CONTROLLER, XAppKbdLayoutControllerClass))

typedef struct _XAppKbdLayoutControllerPrivate XAppKbdLayoutControllerPrivate;
typedef struct _XAppKbdLayoutController XAppKbdLayoutController;
typedef struct _XAppKbdLayoutControllerClass XAppKbdLayoutControllerClass;

struct _XAppKbdLayoutController
{
    GObject parent_object;

    XAppKbdLayoutControllerPrivate *priv;
};

struct _XAppKbdLayoutControllerClass
{
    GObjectClass parent_class;
};

GType                    xapp_kbd_layout_controller_get_type                        (void);
XAppKbdLayoutController *xapp_kbd_layout_controller_new                             (void);
gboolean                 xapp_kbd_layout_controller_get_enabled                     (XAppKbdLayoutController *controller);
guint                    xapp_kbd_layout_controller_get_current_group               (XAppKbdLayoutController *controller);
void                     xapp_kbd_layout_controller_set_current_group               (XAppKbdLayoutController *controller,
                                                                                     guint                    group);
void                     xapp_kbd_layout_controller_next_group                      (XAppKbdLayoutController *controller);
void                     xapp_kbd_layout_controller_previous_group                  (XAppKbdLayoutController *controller);
gchar                   *xapp_kbd_layout_controller_get_current_name                (XAppKbdLayoutController *controller);
gchar                  **xapp_kbd_layout_controller_get_all_names                   (XAppKbdLayoutController *controller);
        
gchar                   *xapp_kbd_layout_controller_get_current_icon_name           (XAppKbdLayoutController *controller);
gchar                   *xapp_kbd_layout_controller_get_icon_name_for_group         (XAppKbdLayoutController *controller,
                                                                                     guint                    group);
gint                     xapp_kbd_layout_controller_get_current_flag_id             (XAppKbdLayoutController *controller);
gint                     xapp_kbd_layout_controller_get_flag_id_for_group           (XAppKbdLayoutController *controller,
                                                                                     guint                    group);
gchar                   *xapp_kbd_layout_controller_get_current_short_group_label   (XAppKbdLayoutController *controller);
gchar                   *xapp_kbd_layout_controller_get_short_group_label_for_group (XAppKbdLayoutController *controller,
                                                                                     guint                    group);
gchar                   *xapp_kbd_layout_controller_get_current_variant_label       (XAppKbdLayoutController *controller);

gchar                   *xapp_kbd_layout_controller_get_variant_label_for_group     (XAppKbdLayoutController *controller,
                                                                                     guint                    group);

/* Class function */
void                     xapp_kbd_layout_controller_render_cairo_subscript          (cairo_t *cr,
                                                                                     gdouble  x,
                                                                                     gdouble  y,
                                                                                     gdouble  width,
                                                                                     gdouble  height,
                                                                                     gint     subscript);

G_END_DECLS

#endif  /* __XAPP_KBD_LAYOUT_CONTROLLER_H__ */

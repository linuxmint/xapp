#ifndef __XAPP_DISPLAY_H__
#define __XAPP_DISPLAY_H__

#include <stdio.h>
#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_DISPLAY            (xapp_display_get_type ())
#define XAPP_DISPLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAPP_TYPE_DISPLAY, XAppDisplay))
#define XAPP_DISPLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  XAPP_TYPE_DISPLAY, XAppDisplayClass))
#define XAPP_IS_DISPLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XAPP_TYPE_DISPLAY))
#define XAPP_IS_DISPLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  XAPP_TYPE_DISPLAY))
#define XAPP_DISPLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  XAPP_TYPE_DISPLAY, XAppDisplayClass))

typedef struct _XAppDisplayPrivate XAppDisplayPrivate;
typedef struct _XAppDisplay XAppDisplay;
typedef struct _XAppDisplayClass XAppDisplayClass;

struct _XAppDisplay
{
  GObject parent_object;

  XAppDisplayPrivate *priv;
};

struct _XAppDisplayClass
{
  GObjectClass parent_class;
};

GType           xapp_display_get_type          (void);
XAppDisplay    *xapp_display_new               (void);
void            xapp_display_blank_other_monitors (XAppDisplay *self, GtkWindow *window);
void            xapp_display_unblank_monitors (XAppDisplay *self);
gboolean        xapp_display_are_monitors_blanked (XAppDisplay *self);

G_END_DECLS

#endif  /* __XAPP_DISPLAY_H__ */

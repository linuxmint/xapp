#ifndef __XAPP_GTK_WINDOW_H__
#define __XAPP_GTK_WINDOW_H__

#include <stdio.h>

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define XAPP_TYPE_GTK_WINDOW            (xapp_gtk_window_get_type ())
#define XAPP_GTK_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAPP_TYPE_GTK_WINDOW, XAppGtkWindow))
#define XAPP_GTK_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  XAPP_TYPE_GTK_WINDOW, XAppGtkWindowClass))
#define XAPP_IS_GTK_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XAPP_TYPE_GTK_WINDOW))
#define XAPP_IS_GTK_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  XAPP_TYPE_GTK_WINDOW))
#define XAPP_GTK_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  XAPP_TYPE_GTK_WINDOW, XAppGtkWindowClass))

typedef struct _XAppGtkWindowPrivate XAppGtkWindowPrivate;
typedef struct _XAppGtkWindow XAppGtkWindow;
typedef struct _XAppGtkWindowClass XAppGtkWindowClass;

struct _XAppGtkWindow
{
    GtkWindow parent_object;

    XAppGtkWindowPrivate *priv;
};

struct _XAppGtkWindowClass
{
    GtkWindowClass parent_class;
};

/* Class */
GType                    xapp_gtk_window_get_type                        (void);
GtkWidget               *xapp_gtk_window_new                             (GtkWindowType type);

void                     xapp_gtk_window_set_icon_name                   (XAppGtkWindow   *window,
                                                                          const gchar     *icon_name);

void                     xapp_gtk_window_set_icon_from_file              (XAppGtkWindow   *window,
                                                                          const gchar     *file_name,
                                                                          GError         **error);
void                     xapp_gtk_window_set_progress                    (XAppGtkWindow   *window,
                                                                          gint             progress);
void                     xapp_gtk_window_set_progress_pulse              (XAppGtkWindow   *window,
                                                                          gboolean         pulse);
/* Wrappers (for GtkWindow subclasses like GtkDialog)*/

void                     xapp_set_window_icon_name                       (GtkWindow       *window,
                                                                          const gchar     *icon_name);

void                     xapp_set_window_icon_from_file                  (GtkWindow   *window,
                                                                          const gchar *file_name,
                                                                          GError     **error);
void                     xapp_set_window_progress                        (GtkWindow   *window,
                                                                          gint         progress);
void                     xapp_set_window_progress_pulse                  (GtkWindow   *window,
                                                                          gboolean     pulse);

G_END_DECLS

#endif  /* __XAPP_GTK_WINDOW_H__ */

#ifndef __XAPP_GTK_WINDOW_H__
#define __XAPP_GTK_WINDOW_H__

#include <stdio.h>

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define XAPP_TYPE_GTK_WINDOW (xapp_gtk_window_get_type ())

G_DECLARE_DERIVABLE_TYPE (XAppGtkWindow, xapp_gtk_window, XAPP, GTK_WINDOW, GtkWindow)

struct _XAppGtkWindowClass
{
  GtkWindowClass parent_class;

  gpointer padding[12];
};

/* Class */
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

void                     xapp_set_window_icon_from_file                  (GtkWindow       *window,
                                                                          const gchar     *file_name,
                                                                          GError         **error);
void                     xapp_set_window_progress                        (GtkWindow       *window,
                                                                          gint             progress);
void                     xapp_set_window_progress_pulse                  (GtkWindow       *window,
                                                                          gboolean         pulse);
/* Low level for X11 Window xid's */
void                     xapp_set_xid_icon_name                          (gulong           xid,
                                                                          const gchar     *icon_name);
void                     xapp_set_xid_icon_from_file                     (gulong           xid,
                                                                          const gchar     *file_name);
void                     xapp_set_xid_progress                           (gulong           xid,
                                                                          gint             progress);
void                     xapp_set_xid_progress_pulse                     (gulong           xid,
                                                                          gboolean         pulse);

G_END_DECLS

#endif  /* __XAPP_GTK_WINDOW_H__ */

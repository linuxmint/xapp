#ifndef _XAPP_PREFERENCES_WINDOW_H_
#define _XAPP_PREFERENCES_WINDOW_H_

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define XAPP_TYPE_PREFERENCES_WINDOW (xapp_preferences_window_get_type ())

G_DECLARE_DERIVABLE_TYPE (XAppPreferencesWindow, xapp_preferences_window, XAPP, PREFERENCES_WINDOW, GtkWindow)

struct _XAppPreferencesWindowClass
{
    GtkWindowClass parent_class;

    /* Keybinding signals */
    void (* close) (XAppPreferencesWindow *window);
};

XAppPreferencesWindow *xapp_preferences_window_new        (void);

void                   xapp_preferences_window_add_page   (XAppPreferencesWindow *window,
                                                           GtkWidget             *widget,
                                                           const gchar           *name,
                                                           const gchar           *title);

void                   xapp_preferences_window_add_button (XAppPreferencesWindow *window,
                                                           GtkWidget             *button,
                                                           GtkPackType            pack_type);

G_END_DECLS

#endif /* _XAPP_PREFERENCES_WINDOW_H_ */
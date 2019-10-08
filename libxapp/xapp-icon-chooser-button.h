#ifndef _XAPP_ICON_CHOOSER_BUTTON_H_
#define _XAPP_ICON_CHOOSER_BUTTON_H_

#include <glib-object.h>
#include <gtk/gtk.h>
#include "xapp-icon-chooser-dialog.h"
#include "xapp-enums.h"

G_BEGIN_DECLS

#define XAPP_TYPE_ICON_CHOOSER_BUTTON   (xapp_icon_chooser_button_get_type ())

G_DECLARE_FINAL_TYPE (XAppIconChooserButton, xapp_icon_chooser_button, XAPP, ICON_CHOOSER_BUTTON, GtkButton)

XAppIconChooserButton *   xapp_icon_chooser_button_new                   (void);

XAppIconChooserButton *   xapp_icon_chooser_button_new_with_size         (GtkIconSize   icon_size);

void                      xapp_icon_chooser_button_set_icon_size         (XAppIconChooserButton *button,
                                                                          GtkIconSize            icon_size);

void                      xapp_icon_chooser_button_set_icon              (XAppIconChooserButton *button,
                                                                          const gchar           *icon);

void                      xapp_icon_chooser_button_set_default_category  (XAppIconChooserButton *button,
                                                                          const gchar           *category);

const gchar*              xapp_icon_chooser_button_get_icon              (XAppIconChooserButton *button);
XAppIconChooserDialog *   xapp_icon_chooser_button_get_dialog            (XAppIconChooserButton *button);

G_END_DECLS

#endif /* _XAPP_ICON_CHOOSER_DIALOG_H_ */

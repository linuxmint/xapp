#ifndef _XAPP_ICON_CHOOSER_DIALOG_H_
#define _XAPP_ICON_CHOOSER_DIALOG_H_

#include <glib-object.h>
#include <gtk/gtk.h>

#include "xapp-gtk-window.h"

G_BEGIN_DECLS

#define XAPP_TYPE_ICON_CHOOSER_DIALOG   (xapp_icon_chooser_dialog_get_type ())

G_DECLARE_FINAL_TYPE (XAppIconChooserDialog, xapp_icon_chooser_dialog, XAPP, ICON_CHOOSER_DIALOG, XAppGtkWindow)

typedef enum
{
    XAPP_ICON_SIZE_16 = 16,
    XAPP_ICON_SIZE_22 = 22,
    XAPP_ICON_SIZE_24 = 24,
    XAPP_ICON_SIZE_32 = 32,
    XAPP_ICON_SIZE_48 = 48,
    XAPP_ICON_SIZE_96 = 96
} XAppIconSize;

XAppIconChooserDialog *     xapp_icon_chooser_dialog_new               (void);

gint                        xapp_icon_chooser_dialog_run                (XAppIconChooserDialog *dialog);

gint                        xapp_icon_chooser_dialog_run_with_icon      (XAppIconChooserDialog *dialog,
                                                                         gchar                 *icon);

gint                        xapp_icon_chooser_dialog_run_with_category  (XAppIconChooserDialog *dialog,
                                                                         gchar                 *category);

gchar *                     xapp_icon_chooser_dialog_get_icon_string    (XAppIconChooserDialog *dialog);

void                        xapp_icon_chooser_dialog_add_button         (XAppIconChooserDialog *dialog,
                                                                         GtkWidget             *button,
                                                                         GtkPackType            packing,
                                                                         GtkResponseType        response_id);

gchar *                     xapp_icon_chooser_dialog_get_default_icon  (XAppIconChooserDialog *dialog);
void                        xapp_icon_chooser_dialog_set_default_icon  (XAppIconChooserDialog *dialog,
                                                                        const gchar           *icon);

void                        xapp_icon_chooser_dialog_add_custom_category   (XAppIconChooserDialog *dialog,
                                                                            const gchar           *name,
                                                                            GList                 *icons);

G_END_DECLS

#endif /* _XAPP_ICON_CHOOSER_DIALOG_H_ */

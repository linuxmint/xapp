#ifndef _XAPP_ICON_CHOOSER_DIALOG_H_
#define _XAPP_ICON_CHOOSER_DIALOG_H_

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

G_DECLARE_DERIVABLE_TYPE (XAppIconChooserDialog, xapp_icon_chooser_dialog, XAPP, ICON_CHOOSER_DIALOG, GtkWindow)

#define XAPP_TYPE_ICON_CHOOSER_DIALOG   (xapp_icon_chooser_dialog_get_type ())
#define XAPP_ICON_CHOOSER_DIALOG(obj)   (G_TYPE_CHECK_INSTANCE_CAST ((obj), XAPP_TYPE_ICON_CHOOSER_DIALOG, XAppIconChooserDialog))

struct _XAppIconChooserDialogClass
{
    GtkWindowClass parent_class;

    /* Keybinding signals */
    gboolean (* select_event) (XAppIconChooserDialog *dialog,
                               GdkEventAny *event);
};

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


G_END_DECLS

#endif /* _XAPP_ICON_CHOOSER_DIALOG_H_ */

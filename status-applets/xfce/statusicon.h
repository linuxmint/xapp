#ifndef _STATUS_ICON_H_
#define _STATUS_ICON_H_

#include <glib-object.h>
#include <gtk/gtk.h>
#include <libxapp/xapp-statusicon-interface.h>

G_BEGIN_DECLS

#define STATUS_TYPE_ICON (status_icon_get_type ())

G_DECLARE_FINAL_TYPE (StatusIcon, status_icon, STATUS, ICON, GtkToggleButton)

#define INDICATOR_BOX_BORDER        1
#define INDICATOR_BOX_BORDER_COMP   (INDICATOR_BOX_BORDER + 1)
#define ICON_SIZE_REDUCTION         (INDICATOR_BOX_BORDER * 2)
#define ICON_SPACING                5
#define VISIBLE_LABEL_MARGIN        5 // When an icon has a label, add a margin between icon and label

StatusIcon *status_icon_new             (XAppStatusIconInterface      *proxy);

void        status_icon_set_size        (StatusIcon                   *icon,
                                         gint                          size);
void        status_icon_set_orientation (StatusIcon                   *icon,
                                         GtkPositionType               orientation);

G_END_DECLS

#endif /*_STATUS_ICON_H_ */

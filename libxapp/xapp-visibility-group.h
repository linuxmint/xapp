#ifndef __XAPP_VISIBILITY_GROUP_H__
#define __XAPP_VISIBILITY_GROUP_H__

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_VISIBILITY_GROUP xapp_visibility_group_get_type ()
typedef struct _XAppVisibilityGroup XAppVisibilityGroup;

/**
 * XAppVisibilityGroup:
 * @widgets: (element-type Gtk.Widget) (transfer none) (nullable): The #GtkWidget members of this group.
 * @visible: The current visible state of the group. There is no guarantee that all members are actually in this state.
 * @sensitive: The current sensitive state of the group. There is no guarantee that all members are actually in this state.
 *
 * A group of widgets that can have their visibility and sensitivity controlled together.
 */
struct _XAppVisibilityGroup
{
    GSList *widgets;
    gboolean visible;
    gboolean sensitive;
};

GType                 xapp_visibility_group_get_type             (void) G_GNUC_CONST;
XAppVisibilityGroup  *xapp_visibility_group_new                  (gboolean visible, gboolean sensitive, GSList *widgets);
void               xapp_visibility_group_free                    (XAppVisibilityGroup *group);
void               xapp_visibility_group_add_widget              (XAppVisibilityGroup *group, GtkWidget *widget);
gboolean           xapp_visibility_group_remove_widget           (XAppVisibilityGroup *group, GtkWidget *widget);
void               xapp_visibility_group_hide                    (XAppVisibilityGroup *group);
void               xapp_visibility_group_show                    (XAppVisibilityGroup *group);
void               xapp_visibility_group_set_visible             (XAppVisibilityGroup *group, gboolean visible);
gboolean           xapp_visibility_group_get_visible             (XAppVisibilityGroup *group);
void               xapp_visibility_group_set_sensitive           (XAppVisibilityGroup *group, gboolean sensitive);
gboolean           xapp_visibility_group_get_sensitive           (XAppVisibilityGroup *group);
GSList *           xapp_visibility_group_get_widgets             (XAppVisibilityGroup *group);
void               xapp_visibility_group_set_widgets             (XAppVisibilityGroup *group, GSList *widgets);

G_END_DECLS

#endif  /* __XAPP_VISIBILITY_GROUP_H__  */

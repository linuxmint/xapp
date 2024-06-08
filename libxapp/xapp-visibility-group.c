#include <config.h>
#include "xapp-visibility-group.h"

#define DEBUG_FLAG XAPP_DEBUG_VISIBILITY_GROUP
#include "xapp-debug.h"

XAppVisibilityGroup  *xapp_visibility_group_copy     (const XAppVisibilityGroup *group);
void                  xapp_visibility_group_free     (XAppVisibilityGroup *group);

G_DEFINE_BOXED_TYPE (XAppVisibilityGroup, xapp_visibility_group, xapp_visibility_group_copy, xapp_visibility_group_free);
/**
 * SECTION:xapp-visibility-group
 * @Short_description: Groups widgets to have their visibility or sensitivity controlled as a group,
 * independent of their positions in any widget hierarchy.
 * @Title: XAppVisibilityGroup
 * 
 * You can use this to hide and show groups of widgets all at once, without having to
 * specify each one. They do not have to have the same parents, or even have the same toplevel
 * window.
 *
 * If a widget is destroyed before the group is, it will be automatically removed from the 
 * visibility group. You only ever need to free the XAppVisibilityGroup itself.
 * 
 * Only the specific members of the group have their visibility and sensitivity set, not their
 * descendants. No effort is made to track or prevent state changes made to individual widgets
 * by outside influence.
 */

XAppVisibilityGroup *
xapp_visibility_group_copy (const XAppVisibilityGroup *group)
{
    DEBUG ("XAppVisibilityGroup copy");
    g_return_val_if_fail (group != NULL, NULL);

    XAppVisibilityGroup *_group = g_memdup2 (group, sizeof (XAppVisibilityGroup));
    _group->widgets = NULL;
    _group->visible = group->visible;
    _group->sensitive = group->sensitive;

    return _group;
}

static void
widget_disposed (XAppVisibilityGroup *group,
                 GObject             *object)
{
    DEBUG ("Widget destroyed callback: %p", object);
    group->widgets = g_slist_remove (group->widgets, (gpointer) object);
}

static void
add_one_widget (XAppVisibilityGroup *group,
                GtkWidget           *widget)
{
    if (g_slist_find (group->widgets, (gpointer) widget) != NULL)
    {
        return;
    }
    DEBUG ("Add one widget: %p", widget);

    group->widgets = g_slist_prepend (group->widgets, (gpointer) widget);
    g_object_weak_ref (G_OBJECT (widget), (GWeakNotify) widget_disposed, group);

    g_object_set (widget,
                  "visible", group->visible,
                  "sensitive", group->sensitive,
                  NULL);
}

static void
add_widgets (XAppVisibilityGroup *group,
             GSList              *widgets)
{
    if (widgets != NULL)
    {
        GSList *l;

        for (l = widgets; l != NULL; l = l->next)
        {
            add_one_widget (group, GTK_WIDGET (l->data));
        }
    }
}

static gboolean
remove_one_widget (XAppVisibilityGroup *group,
                   GtkWidget           *widget)
{
    GSList *ptr = g_slist_find (group->widgets, (gpointer) widget);

    DEBUG ("Remove one widget: %p", widget);

    g_object_weak_unref (G_OBJECT (widget), (GWeakNotify) widget_disposed, group);
    group->widgets = g_slist_remove (group->widgets, ptr->data);

    return TRUE;
}

static void
remove_widgets (XAppVisibilityGroup *group)
{
    GSList *l;

    for (l = group->widgets; l != NULL; l = l->next)
    {
        remove_one_widget (group, GTK_WIDGET (l->data));
    }

    g_clear_pointer (&group->widgets, g_slist_free);
}

/**
 * xapp_visibility_group_free:
 * @group: The #XAppVisibilityGroup to free.
 *
 * Destroys the #XAppVisibilityGroup.
 *
 * Since 2.2.15
 */
void
xapp_visibility_group_free (XAppVisibilityGroup *group)
{
    DEBUG ("XAppVisibilityGroup free");
    g_return_if_fail (group != NULL);

    remove_widgets (group);

    g_free (group);
}

/**
 * xapp_visibility_group_new:
 * @visible: starting visibility state
 * @sensitive: starting sensitivity state
 * @widgets: (element-type GtkWidget) (transfer none) (nullable): list of #GtkWidgets to add to the group.
 * 
 * Creates a new #XAppVisibilityGroup.
 * 
 * If @widgets is not NULL, adds these widgets to the group with the starting visibility and
 * sensitivity state.
 *
 * Returns: (transfer full): a new #XAppVisibilityGroup. Use xapp_visibility_group_free when finished.
 *
 * Since: 2.2.15
 */
XAppVisibilityGroup *
xapp_visibility_group_new (gboolean  visible,
                           gboolean  sensitive,
                           GSList   *widgets)
{
    XAppVisibilityGroup *group;

    group = g_new0 (XAppVisibilityGroup, 1);
    group->visible = visible;
    group->sensitive = sensitive;

    add_widgets (group, widgets);

    return group;
}

static void
group_show_element (gpointer data, gpointer user_data)
{
    gtk_widget_show (GTK_WIDGET (data));
}

/**
 * xapp_visibility_group_show:
 * @group: the visibility group
 *
 * Show all widgets in the group.
 * 
 * Since: 2.2.15
 */
void
xapp_visibility_group_show (XAppVisibilityGroup *group)
{
    g_return_if_fail (group != NULL && group->widgets != NULL);

    g_slist_foreach (group->widgets, (GFunc) group_show_element, group);
    group->visible = TRUE;
}

static void
group_hide_element (gpointer data, gpointer user_data)
{
    gtk_widget_hide (GTK_WIDGET (data));
}

/**
 * xapp_visibility_group_hide:
 * @group: the visibility group
 *
 * Hide all widgets in the group.
 * 
 * Since: 2.2.15
 */
void
xapp_visibility_group_hide (XAppVisibilityGroup *group)
{
    g_return_if_fail (group != NULL && group->widgets != NULL);

    g_slist_foreach (group->widgets, (GFunc) group_hide_element, group);
    group->visible = FALSE;

}

/**
 * xapp_visibility_group_set_visible:
 * @group: the visibility group
 * @visible: TRUE to make the widgets visible.
 *
 * Set the visibility of all widgets in the group.
 * 
 * Since: 2.2.15
 */
void
xapp_visibility_group_set_visible (XAppVisibilityGroup *group,
                                   gboolean             visible)
{
    g_return_if_fail (group != NULL && group->widgets != NULL);

    if (visible)
    {
        xapp_visibility_group_show (group);
    }
    else
    {
        xapp_visibility_group_hide (group);
    }
}

/**
 * xapp_visibility_group_get_visible:
 * @group: the visibility group.
 *
 * Get the visibility of the group.
 * 
 * There is no guarantee that all widgets in the group actually are
 * in the returned state, if they've had their visibility individually
 * modified since the last time the group was set.
 *
 * Returns: The visibility state of the group.
 * Since: 2.2.15
 */
gboolean
xapp_visibility_group_get_visible (XAppVisibilityGroup *group)
{
    g_return_val_if_fail (group != NULL, FALSE);

    return group->visible;
}

/**
 * xapp_visibility_group_set_sensitive:
 * @group: the visibility group.
 * @sensitive: TRUE to make the widgets sensitive.
 *
 * Set the sensitivity of all widgets in group.
 *
 * Since: 2.2.15
 */
void
xapp_visibility_group_set_sensitive (XAppVisibilityGroup *group,
                                     gboolean             sensitive)
{
    g_return_if_fail (group != NULL && group->widgets != NULL);

    GSList *l;

    for (l = group->widgets; l != NULL; l = l->next)
    {
        gtk_widget_set_sensitive (GTK_WIDGET (l->data), sensitive);
    }
}

/**
 * xapp_visibility_group_get_sensitive:
 * @group: the visibility group.
 *
 * Get the sensitivity of the group.
 * 
 * There is no guarantee that all widgets in the group actually are
 * in the returned state, if they've had their sensitivity individually
 * modified since the last time the group was set.
 *
 * Returns: The sensitivity state of the group.
 * Since: 2.2.15
 */
gboolean
xapp_visibility_group_get_sensitive (XAppVisibilityGroup *group)
{
    g_return_val_if_fail (group != NULL, FALSE);

    return group->sensitive;
}

/**
 * xapp_visibility_group_add_widget:
 * @group: the visibility group
 * @widget: the #GtkWidget to add to the group
 *
 * Adds widget to the visibility group.
 *
 * Since: 2.2.15
 */
void
xapp_visibility_group_add_widget (XAppVisibilityGroup *group,
                                  GtkWidget           *widget)
{
    g_return_if_fail (group != NULL);

    add_one_widget (group, widget);
}

/**
 * xapp_visibility_group_remove_widget:
 * @group: the visibility group
 * @widget: the #GtkWidget to remove from the group
 *
 * Returns: TRUE if the widget was found and removed.
 *
 * Since: 2.2.15
 */
gboolean
xapp_visibility_group_remove_widget (XAppVisibilityGroup *group,
                                     GtkWidget           *widget)
{
    g_return_val_if_fail (group != NULL && group->widgets != NULL, FALSE);

    return remove_one_widget (group, widget);
}

/**
 * xapp_visibility_group_get_widgets:
 * @group: the visibility group

 * Returns the members of the group or NULL if the group is empty.
 * 
 * Returns: (element-type GtkWidget) (transfer none): a list of members of the group.
 *
 * The list is owned by XApp, do not free.
 * 
 * Since: 2.2.15
 */
GSList *
xapp_visibility_group_get_widgets (XAppVisibilityGroup *group)
{
    g_return_val_if_fail (group != NULL, NULL);

    return group->widgets;
}

/**
 * xapp_visibility_group_set_widgets:
 * @group: the visibility group
 * @widgets: (nullable) (element-type GtkWidget) (transfer none): The widgets to add to this group, replacing any existing ones.
 * 
 * Since: 2.2.15
 */
void
xapp_visibility_group_set_widgets (XAppVisibilityGroup *group,
                                   GSList              *widgets)
{
    g_return_if_fail (group != NULL);

    remove_widgets (group);
    add_widgets (group, widgets);
}

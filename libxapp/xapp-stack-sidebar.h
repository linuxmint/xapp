#ifndef _XAPP_STACK_SIDEBAR_H_
#define _XAPP_STACK_SIDEBAR_H_

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define XAPP_TYPE_STACK_SIDEBAR (xapp_stack_sidebar_get_type ())

G_DECLARE_FINAL_TYPE (XAppStackSidebar, xapp_stack_sidebar, XAPP, STACK_SIDEBAR, GtkBin)

XAppStackSidebar *xapp_stack_sidebar_new (void);

void xapp_stack_sidebar_set_stack (XAppStackSidebar *sidebar,
                                   GtkStack         *stack);

GtkStack *xapp_stack_sidebar_get_stack (XAppStackSidebar *sidebar);

G_END_DECLS

#endif /*_XAPP_STACK_SIDEBAR_H_ */

#ifndef __XAPP_STYLE_MANAGER_H__
#define __XAPP_STYLE_MANAGER_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define XAPP_TYPE_STYLE_MANAGER xapp_style_manager_get_type ()
G_DECLARE_FINAL_TYPE (XAppStyleManager, xapp_style_manager, XAPP, STYLE_MANAGER, GObject)

XAppStyleManager  *xapp_style_manager_new                   (void);

GtkWidget         *xapp_style_manager_get_widget            (XAppStyleManager *style_manager);
void               xapp_style_manager_set_widget            (XAppStyleManager *style_manager,
                                                             GtkWidget        *widget);
void               xapp_style_manager_set_style_property    (XAppStyleManager *style_manager,
                                                             const gchar      *name,
                                                             const gchar      *value);
void               xapp_style_manager_remove_style_property (XAppStyleManager *style_manager,
                                                             const gchar      *name);

void               xapp_style_manager_set_from_pango_font_string (XAppStyleManager *style_manager,
                                                                  const gchar      *desc_string);

G_END_DECLS

#endif  /* __XAPP_STYLE_MANAGER_H__  */

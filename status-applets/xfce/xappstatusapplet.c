/*
 * Copyright (c) 2005-2007 Jasper Huijsmans <jasper@xfce.org>
 * Copyright (c) 2007-2010 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>

#include <libxapp/xapp-statusicon-interface.h>
#include <libxapp/xapp-status-icon-monitor.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include "xappstatusapplet.h"
#include "statusicon.h"

struct _XAppStatusAppletPluginClass
{
  XfcePanelPluginClass __parent__;
};

struct _XAppStatusAppletPlugin
{
  XfcePanelPlugin __parent__;

  /* dbus monitor */
  XAppStatusIconMonitor *monitor;

  /* A quick reference to our list box items */
  GHashTable *lookup_table;

  /* A GtkListBox to hold our icons */
  GtkWidget *icon_box;
};

/* define the plugin */
XFCE_PANEL_DEFINE_PLUGIN (XAppStatusAppletPlugin, xapp_status_applet_plugin)

static gboolean xapp_status_applet_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                                                        gint             size);
static void     xapp_status_applet_plugin_screen_position_changed (XfcePanelPlugin   *panel_plugin,
                                                                   XfceScreenPosition position);

static void
xapp_status_applet_plugin_init (XAppStatusAppletPlugin *plugin)
{
  plugin->monitor = NULL;
  plugin->lookup_table = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
}

static void
on_icon_added (XAppStatusIconMonitor        *monitor,
               XAppStatusIconInterface      *proxy,
               gpointer                      user_data)
{
    XAppStatusAppletPlugin *plugin = XAPP_STATUS_APPLET_PLUGIN (user_data);
    XfcePanelPlugin *panel_plugin = XFCE_PANEL_PLUGIN (plugin);
    StatusIcon *icon;
    const gchar *name;

    name = xapp_status_icon_interface_get_name (XAPP_STATUS_ICON_INTERFACE (proxy));

    icon = g_hash_table_lookup (plugin->lookup_table,
                                name);

    if (icon)
    {
        // Or should we remove the existing one and add this one??
        return;
    }

    icon = status_icon_new (proxy);
    gtk_widget_show (GTK_WIDGET (icon));

    gtk_container_add (GTK_CONTAINER (plugin->icon_box),
                       GTK_WIDGET (icon));

    g_hash_table_insert (plugin->lookup_table,
                         g_strdup (name),
                         icon);

    xapp_status_applet_plugin_size_changed (panel_plugin,
                                            xfce_panel_plugin_get_size (panel_plugin));

    xapp_status_applet_plugin_screen_position_changed (panel_plugin,
                                                       xfce_panel_plugin_get_screen_position (panel_plugin));
}

static void
on_icon_removed (XAppStatusIconMonitor        *monitor,
                 XAppStatusIconInterface      *proxy,
                 gpointer                      user_data)
{
    XAppStatusAppletPlugin *plugin = XAPP_STATUS_APPLET_PLUGIN (user_data);
    XfcePanelPlugin *panel_plugin = XFCE_PANEL_PLUGIN (plugin);
    StatusIcon *icon;
    const gchar *name;

    name = xapp_status_icon_interface_get_name (XAPP_STATUS_ICON_INTERFACE (proxy));
    icon = g_hash_table_lookup (plugin->lookup_table,
                                name);

    if (!icon)
    {
        return;
    }

    gtk_container_remove (GTK_CONTAINER (plugin->icon_box),
                          GTK_WIDGET (icon));

    g_hash_table_remove (plugin->lookup_table,
                         name);

    xapp_status_applet_plugin_size_changed (XFCE_PANEL_PLUGIN (plugin),
                                            xfce_panel_plugin_get_size (panel_plugin));

    xapp_status_applet_plugin_screen_position_changed (XFCE_PANEL_PLUGIN (plugin),
                                                       xfce_panel_plugin_get_screen_position (panel_plugin));
}

static void
xapp_status_applet_plugin_construct (XfcePanelPlugin *panel_plugin)
{
    XAppStatusAppletPlugin *plugin = XAPP_STATUS_APPLET_PLUGIN (panel_plugin);

    plugin->monitor = xapp_status_icon_monitor_new ();

    g_signal_connect (plugin->monitor,
                      "icon-added",
                      G_CALLBACK (on_icon_added),
                      plugin);


    g_signal_connect (plugin->monitor,
                      "icon-removed",
                      G_CALLBACK (on_icon_removed),
                      plugin);

    plugin->icon_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,
                                    ICON_SPACING);

    gtk_widget_show (plugin->icon_box);
    gtk_container_set_border_width (GTK_CONTAINER (plugin->icon_box),
                                    INDICATOR_BOX_BORDER);

    gtk_container_add (GTK_CONTAINER (plugin),
                       plugin->icon_box);

    gtk_widget_show_all (GTK_WIDGET (plugin));

    xfce_panel_plugin_set_small (panel_plugin, TRUE);

    xapp_status_applet_plugin_screen_position_changed (panel_plugin,
                                                       xfce_panel_plugin_get_orientation (panel_plugin));
}

/* This is our dispose */
static void
xapp_status_applet_plugin_free_data (XfcePanelPlugin *panel_plugin)
{
  XAppStatusAppletPlugin *plugin = XAPP_STATUS_APPLET_PLUGIN (panel_plugin);

  g_hash_table_destroy (plugin->lookup_table);

  if (G_LIKELY (plugin->monitor != NULL))
    {
      g_clear_object (&plugin->monitor);
    }
}

static void
xapp_status_applet_plugin_screen_position_changed (XfcePanelPlugin   *panel_plugin,
                                                   XfceScreenPosition position)
{
    XAppStatusAppletPlugin *plugin = XAPP_STATUS_APPLET_PLUGIN (panel_plugin);
    GtkPositionType xapp_orientation = GTK_POS_TOP;
    GtkOrientation widget_orientation = GTK_ORIENTATION_HORIZONTAL;
    GHashTableIter iter;
    gpointer key, value;

    if (xfce_screen_position_is_top (position))
    {
        xapp_orientation = GTK_POS_TOP;
    }
    else
    if (xfce_screen_position_is_bottom (position))
    {
        xapp_orientation = GTK_POS_BOTTOM;
    }
    else
    if (xfce_screen_position_is_left (position))
    {
        xapp_orientation = GTK_POS_LEFT;
        widget_orientation = GTK_ORIENTATION_VERTICAL;
    }
    else
    if (xfce_screen_position_is_right (position))
    {
        xapp_orientation = GTK_POS_RIGHT;
        widget_orientation = GTK_ORIENTATION_VERTICAL;
    }

    g_hash_table_iter_init (&iter, plugin->lookup_table);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        StatusIcon *icon = STATUS_ICON (value);

        status_icon_set_orientation (icon, xapp_orientation);
    }

    gtk_orientable_set_orientation (GTK_ORIENTABLE (plugin->icon_box),
                                    widget_orientation);
}

static gboolean
xapp_status_applet_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                                        gint             size)
{
    XAppStatusAppletPlugin *applet = XAPP_STATUS_APPLET_PLUGIN (panel_plugin);

    GHashTableIter iter;
    gpointer key, value;
    gint max_size;

    max_size = size / xfce_panel_plugin_get_nrows (panel_plugin);

    g_hash_table_iter_init (&iter, applet->lookup_table);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        StatusIcon *icon = STATUS_ICON (value);

        gtk_widget_set_size_request (GTK_WIDGET (icon),
                                     max_size,
                                     max_size);

        status_icon_set_size (icon,
                              xfce_panel_plugin_get_icon_size (panel_plugin));
    }

    gtk_widget_queue_resize (GTK_WIDGET (panel_plugin));

    return TRUE;
}

static void
xapp_status_applet_plugin_class_init (XAppStatusAppletPluginClass *klass)
{
  XfcePanelPluginClass *plugin_class;

  plugin_class = XFCE_PANEL_PLUGIN_CLASS (klass);
  plugin_class->construct = xapp_status_applet_plugin_construct;
  plugin_class->free_data = xapp_status_applet_plugin_free_data;
  plugin_class->size_changed = xapp_status_applet_plugin_size_changed;
  plugin_class->screen_position_changed = xapp_status_applet_plugin_screen_position_changed;
}

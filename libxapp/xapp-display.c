
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include "xapp-display.h"

struct _XAppDisplayPrivate
{
    int num_outputs;
    gboolean blanked;
    GtkWidget **windows;
};

G_DEFINE_TYPE (XAppDisplay, xapp_display, G_TYPE_OBJECT);

GtkWidget *create_blanking_window (GdkScreen *screen,
                                   int        monitor);

static void
xapp_display_init (XAppDisplay *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, XAPP_TYPE_DISPLAY, XAppDisplayPrivate);
    self->priv->num_outputs = 0;
    self->priv->blanked = FALSE;
    self->priv->windows = NULL;
}

static void
xapp_display_finalize (GObject *object)
{
    XAppDisplay *self = XAPP_DISPLAY (object);

    if (self->priv->windows != NULL)
    {
        xapp_display_unblank_monitors (XAPP_DISPLAY(self));
        g_free (self->priv->windows);
    }

    G_OBJECT_CLASS (xapp_display_parent_class)->finalize (object);
}

static void
xapp_display_class_init (XAppDisplayClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = xapp_display_finalize;

    g_type_class_add_private (gobject_class, sizeof (XAppDisplayPrivate));
}

XAppDisplay *
xapp_display_new (void)
{
    return g_object_new (XAPP_TYPE_DISPLAY, NULL);
}

GtkWidget *
create_blanking_window (GdkScreen *screen,
                        int        monitor)
{
    GdkRectangle fullscreen;
    GtkWidget *window;
    GdkColor color;

    gdk_screen_get_monitor_geometry(screen, monitor, &fullscreen);

    window = gtk_window_new (GTK_WINDOW_POPUP);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
    gtk_window_resize (GTK_WINDOW (window), fullscreen.width, fullscreen.height);
    gtk_window_move (GTK_WINDOW (window), fullscreen.x, fullscreen.y);
    gtk_widget_set_visible (window, TRUE);

    color.red = 0x00C0;
    color.green = 0x00DE;
    color.blue = 0x00ED;
    gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &color);

    return window;
}

void
xapp_display_blank_other_monitors (XAppDisplay *self,
                                   GtkWindow   *window)
{
    GdkScreen *screen;
    int active_monitor;

    g_return_if_fail (XAPP_IS_DISPLAY (self));

    if (self->priv->windows != NULL)
        return;

    screen = gtk_window_get_screen (window);
    active_monitor = gdk_screen_get_monitor_at_window (screen, gtk_widget_get_window (GTK_WIDGET (window)));
    self->priv->num_outputs = gdk_screen_get_n_monitors (screen);
    self->priv->windows = g_new (GtkWidget *, self->priv->num_outputs);

    for (int i = 0; i < self->priv->num_outputs; i++)
    {
        if (i != active_monitor)
        {
            self->priv->windows[i] = create_blanking_window (screen, i);
        }
        else
        {
            // initialize at NULL so it gets properly skipped when windows get destroyed
            self->priv->windows[i] = NULL;
        }
    }

    self->priv->blanked = TRUE;
}

void
xapp_display_unblank_monitors (XAppDisplay *self)
{
    g_return_if_fail (XAPP_IS_DISPLAY (self));

    if (self->priv->windows == NULL)
        return;

    for (int i = 0; i < self->priv->num_outputs; i++)
    {
        if (self->priv->windows[i] != NULL)
        {
            gtk_widget_destroy (self->priv->windows[i]);
            self->priv->windows[i] = NULL;
        }
    }
    g_free (self->priv->windows);
    self->priv->windows = NULL;
    self->priv->blanked = FALSE;
}

gboolean
xapp_display_are_monitors_blanked (XAppDisplay *self)
{
    return self->priv->blanked;
}


#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include "xapp-monitor-blanker.h"

struct _XAppMonitorBlankerPrivate
{
    int num_outputs;
    gboolean blanked;
    GtkWidget **windows;
};

G_DEFINE_TYPE (XAppMonitorBlanker, xapp_monitor_blanker, G_TYPE_OBJECT);

GtkWidget *create_blanking_window (GdkScreen *screen,
                                   int        monitor);

static void
xapp_monitor_blanker_init (XAppMonitorBlanker *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, XAPP_TYPE_MONITOR_BLANKER, XAppMonitorBlankerPrivate);
    self->priv->num_outputs = 0;
    self->priv->blanked = FALSE;
    self->priv->windows = NULL;
}

static void
xapp_monitor_blanker_finalize (GObject *object)
{
    XAppMonitorBlanker *self = XAPP_MONITOR_BLANKER (object);

    if (self->priv->windows != NULL)
    {
        xapp_monitor_blanker_unblank_monitors (XAPP_MONITOR_BLANKER(self));
        g_free (self->priv->windows);
    }

    G_OBJECT_CLASS (xapp_monitor_blanker_parent_class)->finalize (object);
}

static void
xapp_monitor_blanker_class_init (XAppMonitorBlankerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize = xapp_monitor_blanker_finalize;

    g_type_class_add_private (gobject_class, sizeof (XAppMonitorBlankerPrivate));
}

XAppMonitorBlanker *
xapp_monitor_blanker_new (void)
{
    return g_object_new (XAPP_TYPE_MONITOR_BLANKER, NULL);
}

GtkWidget *
create_blanking_window (GdkScreen *screen,
                        int        monitor)
{
    GdkRectangle fullscreen;
    GtkWidget *window;
    GtkStyleContext *context;
    GtkCssProvider *provider;

    gdk_screen_get_monitor_geometry(screen, monitor, &fullscreen);

    window = gtk_window_new (GTK_WINDOW_POPUP);
    gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
    gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
    gtk_window_resize (GTK_WINDOW (window), fullscreen.width, fullscreen.height);
    gtk_window_move (GTK_WINDOW (window), fullscreen.x, fullscreen.y);
    gtk_widget_set_visible (window, TRUE);

    context = gtk_widget_get_style_context (GTK_WIDGET (window));
    gtk_style_context_add_class (context, "xapp-blanking-window");
    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (provider,
                                     ".xapp-blanking-window { background-color: rgb(0, 0, 0); }",
                                     -1, NULL);
    gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    return window;
}

void
xapp_monitor_blanker_blank_other_monitors (XAppMonitorBlanker *self,
                                   GtkWindow   *window)
{
    GdkScreen *screen;
    int active_monitor;
    int i;

    g_return_if_fail (XAPP_IS_MONITOR_BLANKER (self));

    if (self->priv->windows != NULL)
        return;

    screen = gtk_window_get_screen (window);
    active_monitor = gdk_screen_get_monitor_at_window (screen, gtk_widget_get_window (GTK_WIDGET (window)));
    self->priv->num_outputs = gdk_screen_get_n_monitors (screen);
    self->priv->windows = g_new (GtkWidget *, self->priv->num_outputs);

    for (i = 0; i < self->priv->num_outputs; i++)
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
xapp_monitor_blanker_unblank_monitors (XAppMonitorBlanker *self)
{
    int i;
    g_return_if_fail (XAPP_IS_MONITOR_BLANKER (self));

    if (self->priv->windows == NULL)
        return;

    for (i = 0; i < self->priv->num_outputs; i++)
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
xapp_monitor_blanker_are_monitors_blanked (XAppMonitorBlanker *self)
{
    return self->priv->blanked;
}

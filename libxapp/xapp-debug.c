/*
 * xapp-debug: debug loggers for xapp
 *
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Copied from nemo
 */

#include <stdarg.h>

#include <glib.h>

#include "xapp-debug.h"

#ifdef ENABLE_DEBUG

static DebugFlags flags = 0;
static gboolean initialized = FALSE;

static GDebugKey keys[] = {
  { "GtkWindow", XAPP_DEBUG_WINDOW },
  { "Favorites", XAPP_DEBUG_FAVORITES },
  { "FavoriteVfs", XAPP_DEBUG_FAVORITE_VFS },
  { "StatusIcon", XAPP_DEBUG_STATUS_ICON },
  { "SnWatcher", XAPP_DEBUG_SN_WATCHER },
  { "GtkModule", XAPP_DEBUG_MODULE},
  { "VisibilityGroup", XAPP_DEBUG_VISIBILITY_GROUP},
  { "GpuOffload", XAPP_DEBUG_GPU_OFFLOAD},
  { "DarkModeManager", XAPP_DEBUG_DARK_MODE_MANAGER},
  { 0, }
};

const gchar *
debug_flag_to_string (DebugFlags flag)
{
    switch (flag)
    {
    case XAPP_DEBUG_WINDOW:
        return "GtkWindow";
    case XAPP_DEBUG_FAVORITES:
        return "Favorites";
    case XAPP_DEBUG_FAVORITE_VFS:
        return "FavoriteVFS";
    case XAPP_DEBUG_STATUS_ICON:
        return "StatusIcon";
    case XAPP_DEBUG_SN_WATCHER:
        return "SnWatcher";
    case XAPP_DEBUG_MODULE:
        return "GtkModule";
    case XAPP_DEBUG_VISIBILITY_GROUP:
        return "VisibilityGroup";
    case XAPP_DEBUG_GPU_OFFLOAD:
        return "GpuOffload";
    case XAPP_DEBUG_DARK_MODE_MANAGER:
        return "DarkModeManager";
    }
    return "";
}

static void
xapp_debug_set_flags_from_env (void)
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("XAPP_DEBUG");

  if (flags_string)
    xapp_debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));

  initialized = TRUE;
}

void
xapp_debug_set_flags (DebugFlags new_flags)
{
  flags |= new_flags;
  initialized = TRUE;
}

gboolean
xapp_debug_flag_is_set (DebugFlags flag)
{
  return flag & flags;
}

void
xapp_debug (DebugFlags flag,
                const gchar *format,
                ...)
{
  va_list args;
  va_start (args, format);
  xapp_debug_valist (flag, format, args);
  va_end (args);
}

void
xapp_debug_valist (DebugFlags flag,
                       const gchar *format,
                       va_list args)
{
  if (G_UNLIKELY(!initialized))
    xapp_debug_set_flags_from_env ();

  if (flag & flags)
  {
    // I would think this should be G_LOG_LEVEL_DEBUG, but I can't get it to show up
    // in the journal/xsession-errors unless I drop it to message (even with G_MESSAGES_DEBUG=all)
    g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, format, args);
  }
}

#endif /* ENABLE_DEBUG */

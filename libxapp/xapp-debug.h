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

#ifndef __XAPP_DEBUG_H__
#define __XAPP_DEBUG_H__

#include <config.h>
#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  XAPP_DEBUG_WINDOW = 1 << 1,
  XAPP_DEBUG_FAVORITES = 1 << 2,
  XAPP_DEBUG_FAVORITE_VFS = 1 << 3,
  XAPP_DEBUG_STATUS_ICON = 1 << 4,
  XAPP_DEBUG_SN_WATCHER = 1 << 5,
  XAPP_DEBUG_MODULE = 1 << 6
} DebugFlags;

void xapp_debug_set_flags (DebugFlags flags);
gboolean xapp_debug_flag_is_set (DebugFlags flag);

void xapp_debug_valist (DebugFlags flag,
                            const gchar *format, va_list args);

void xapp_debug (DebugFlags flag, const gchar *format, ...)
  G_GNUC_PRINTF (2, 3);

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  xapp_debug (DEBUG_FLAG, "%s: %s: " format, G_STRFUNC, G_STRLOC, \
                  ##__VA_ARGS__)

#define DEBUGGING xapp_debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  G_STMT_START { } G_STMT_END

#define DEBUGGING 0

#endif /* DEBUG_FLAG */

G_END_DECLS

#endif /* __XAPP_DEBUG_H__ */

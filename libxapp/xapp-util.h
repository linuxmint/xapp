/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#ifndef __XAPP_UTIL_H__
#define __XAPP_UTIL_H__

G_BEGIN_DECLS

gboolean xapp_util_gpu_offload_supported (void);
gboolean xapp_util_get_session_is_running (void);

gchar *xapp_pango_font_string_to_css (const char *pango_font_string);


G_END_DECLS
#endif /* __XAPP_UTIL_H__ */

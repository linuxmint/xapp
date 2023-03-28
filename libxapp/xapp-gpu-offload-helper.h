#ifndef __XAPP_GPU_OFFLOAD_HELPER_H__
#define __XAPP_GPU_OFFLOAD_HELPER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define XAPP_TYPE_GPU_OFFLOAD_HELPER xapp_gpu_offload_helper_get_type ()
G_DECLARE_FINAL_TYPE (XAppGpuOffloadHelper, xapp_gpu_offload_helper, XAPP, GPU_OFFLOAD_HELPER, GObject)

#define XAPP_TYPE_GPU_INFO (xapp_gpu_info_get_type ())
typedef struct _XAppGpuInfo XAppGpuInfo;

GType xapp_gpu_info_get_type (void) G_GNUC_CONST;

/**
 * XAppGpuInfo:
 * @id: An identifier that can be used to refer to this GPU.
 * @is_default: Whether this GPU is used by default.
 * @display_name: User-visible name of the GPU.
 * @env_strv: (array zero-terminated=1): A string array containing alternating environment variables names and values to use to enable the gpu.
 *
 * Information about a single GPU used for offloading. The length of @env_strv will always be an even number.
 */
struct _XAppGpuInfo
{
    gint id;
    gboolean is_default;
    gchar *display_name;
    gchar **env_strv;
};

XAppGpuOffloadHelper    *xapp_gpu_offload_helper_get                     (void);
XAppGpuOffloadHelper    *xapp_gpu_offload_helper_get_sync                (void);
gboolean                 xapp_gpu_offload_helper_is_ready                (XAppGpuOffloadHelper *helper);
gboolean                 xapp_gpu_offload_helper_is_offload_supported    (XAppGpuOffloadHelper *helper);
gint                     xapp_gpu_offload_helper_get_n_gpus              (XAppGpuOffloadHelper *helper);
GList                   *xapp_gpu_offload_helper_get_offload_infos       (XAppGpuOffloadHelper *helper);
XAppGpuInfo             *xapp_gpu_offload_helper_get_default_info        (XAppGpuOffloadHelper *helper);
XAppGpuInfo             *xapp_gpu_offload_helper_get_info_by_id          (XAppGpuOffloadHelper *helper, gint id);
gchar                   *xapp_gpu_info_get_shell_env_prefix              (XAppGpuInfo *info);

G_END_DECLS

#endif  /* __XAPP_GPU_OFFLOAD_HELPER_H__  */

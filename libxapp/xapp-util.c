
#include "config.h"

#include <glib.h>
#include <gio/gio.h>

#include "xapp-util.h"

#define PRIME_SUPPORTED_TEST_FILE "/var/lib/ubuntu-drivers-common/requires_offloading"
#define PRIME_MODE_FILE "/etc/prime-discrete"

/**
 * xapp_util_gpu_offload_supported:
 *
 * Performs a check to see if on-demand mode for discrete graphics
 * is supported.
 *
 * Returns: %TRUE if supported.
 *
 * Since: 1.8
 */
gboolean
xapp_util_gpu_offload_supported (void)
{
    g_autoptr(GFile) modefile;
    g_autofree gchar *contents;

    if (!g_file_test (PRIME_SUPPORTED_TEST_FILE, G_FILE_TEST_EXISTS))
    {
        return FALSE;
    }

    modefile = g_file_new_for_path (PRIME_MODE_FILE);

    if (!g_file_load_contents (modefile,
                               NULL,
                               &contents,
                               NULL,
                               NULL,
                               NULL))
    {
        return FALSE;
    }

    return g_strstr_len (contents, -1, "on-demand") != NULL;
}

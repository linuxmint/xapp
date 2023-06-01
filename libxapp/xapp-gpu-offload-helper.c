#include "config.h"

#define DEBUG_FLAG XAPP_DEBUG_GPU_OFFLOAD
#include "xapp-debug.h"

#include "xapp-gpu-offload-helper.h"
#include "xapp-switcheroo-interface.h"
#include "xapp-util.h"

static const gchar *DEFAULT_ENV_ARGV[5] = {
    "__NV_PRIME_RENDER_OFFLOAD", "1",
    "__GLX_VENDOR_LIBRARY_NAME", "nvidia",
    NULL
};

XAppGpuInfo  *xapp_gpu_info_copy     (const XAppGpuInfo *info);
void          xapp_gpu_info_free     (XAppGpuInfo       *info);

G_DEFINE_BOXED_TYPE (XAppGpuInfo, xapp_gpu_info, xapp_gpu_info_copy, xapp_gpu_info_free);
/**
 * SECTION:xapp-gpu-offload-helper
 * @Short_description: Simple interface for Switcheroo.
 * @Title: XAppGpuOffloadHelper
 *
 * #XAppGpuOffloadHelper is class that provides a reliable property cache and simple return methods
 * for getting offload_helper parameters and conditions from the Switcheroo interface.
 *
 * Since 2.6
 */

XAppGpuInfo *
xapp_gpu_info_copy (const XAppGpuInfo *info)
{
    DEBUG ("XAppGpuInfo copy");
    g_return_val_if_fail (info != NULL, NULL);

#if GLIB_CHECK_VERSION(2, 68, 0)
    XAppGpuInfo *_info = g_memdup2 (info, sizeof(XAppGpuInfo));
#else
    XAppGpuInfo *_info = g_memdup (info, sizeof(XAppGpuInfo));
#endif
    _info->id = info->id;
    _info->is_default = info->is_default;
    _info->display_name = g_strdup (info->display_name);
    _info->env_strv = g_strdupv (info->env_strv);

    return _info;
}

/**
 * xapp_gpu_info_free:
 * @group: The #XAppGpuInfo to free.
 *
 * Destroys the #XAppGpuInfo.
 *
 * Since 2.6
 */
void
xapp_gpu_info_free (XAppGpuInfo *info)
{
    DEBUG ("XAppGpuInfo free");
    g_return_if_fail (info != NULL);

    g_strfreev (info->env_strv);
    g_free (info->display_name);
    g_free (info);
}

/**
 * xapp_gpu_info_get_shell_env_prefix:
 * @info: An #XAppGpuInfo.
 *
 * Creates a new string in a form intended to prefix a shell command, containing
 * the appropriate name/values for this gpu. For example:
 * 
 * __GLX_VENDOR_LIBRARY_NAME=nvidia __NV_PRIME_RENDER_OFFLOAD=1
 * 
 * Returns: (transfer full): A new string, free with g_free().
 *
 * Since 2.6
 */
gchar *
xapp_gpu_info_get_shell_env_prefix(XAppGpuInfo *info)
{
    g_return_val_if_fail (info != NULL, g_strdup (""));

    if (info->env_strv == NULL)
    {
        return g_strdup ("");
    }

    g_return_val_if_fail (g_strv_length (info->env_strv) % 2 == 0, g_strdup (""));

    GString *args = g_string_new (NULL);

    for (gint i = 0; i < g_strv_length (info->env_strv); i++)
    {
        g_string_append_printf (args, "%s=", info->env_strv[i++]);
        g_string_append_printf (args, "%s ", info->env_strv[i]);
    }

    DEBUG ("%s", args->str);
    return g_string_free (args, FALSE);
}

struct _XAppGpuOffloadHelper
{
    GObject parent_instance;

    GCancellable *cancellable;
    GDBusProxy *control;

    guint num_gpus;

    GMutex gpu_infos_mutex;
    GList *gpu_infos; // XAppGpuInfos

    gboolean ready;

    gboolean ubuntu_offload_support_found;
};

G_DEFINE_TYPE (XAppGpuOffloadHelper, xapp_gpu_offload_helper, G_TYPE_OBJECT)

enum
{
    READY,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

static void
process_gpus_property (XAppGpuOffloadHelper  *helper,
                       GError               **error)
{
    GVariant *gpus;
    GList *infos = NULL;
    gint num_children, i;
    gboolean default_found = FALSE;

    gpus = xapp_switcheroo_control_get_gpus (XAPP_SWITCHEROO_CONTROL (helper->control));

    num_children = g_variant_n_children(gpus);

    for (i = 0; i < num_children; i++)
    {
        g_autoptr(GVariant) gpu;
        g_autoptr(GVariant) vname = NULL;
        g_autoptr(GVariant) venv = NULL;
        g_autoptr(GVariant) vdefault = NULL;
        const char *name;
        g_autofree const char **env_strv = NULL;
        gsize env_len;
        gboolean is_default;

        gpu = g_variant_get_child_value (gpus, i);
        if (!gpu || !g_variant_is_of_type (gpu, G_VARIANT_TYPE ("a{s*}")))
        {
            continue;
        }

        vname = g_variant_lookup_value (gpu, "Name", NULL);
        venv = g_variant_lookup_value (gpu, "Environment", NULL);
        vdefault= g_variant_lookup_value (gpu, "Default", NULL);

        if (!vname || !venv)
          continue;

        name = g_variant_get_string (vname, NULL);
        env_strv = g_variant_get_strv (venv, &env_len);

        if (env_strv != NULL && env_len % 2 != 0)
        {
          g_autofree char *debug = NULL;
          debug = g_strjoinv ("\n", (char **) env_strv);
          g_warning ("Invalid environment returned from switcheroo:\n%s", debug);
          g_clear_pointer (&env_strv, g_free);
          continue;
        }

        is_default = vdefault ? g_variant_get_boolean (vdefault) : FALSE;

        if (is_default)
        {
            default_found = TRUE;
        }

        XAppGpuInfo *info = g_new0 (XAppGpuInfo, 1);
        info->id = i;
        info->display_name = g_strdup (name);
        info->env_strv = g_strdupv ((gchar **) env_strv);
        info->is_default = is_default;

        infos = g_list_append (infos, info);
    }

    if (infos == NULL)
    {
        *error = g_error_new (g_dbus_error_quark (),
                              G_DBUS_ERROR_FAILED,
                              "GPUs property didn't contain any valid gpu info.");
    }

    if (!default_found)
    {
        *error = g_error_new (g_dbus_error_quark (),
                              G_DBUS_ERROR_FAILED,
                              "No default GPU exists.");
        g_list_free_full (infos, (GDestroyNotify) xapp_gpu_info_free);
        infos = NULL;
    }

    g_mutex_lock (&helper->gpu_infos_mutex);

    g_list_free_full (helper->gpu_infos, (GDestroyNotify) xapp_gpu_info_free);
    helper->gpu_infos = infos;
    helper->num_gpus = g_list_length (helper->gpu_infos);

    g_mutex_unlock (&helper->gpu_infos_mutex);
}

static void
populate_gpu_info (XAppGpuOffloadHelper *helper,
                   GError              **error)
{
    GVariant *gpus;
    g_auto(GStrv) cached_props = NULL;

    cached_props = g_dbus_proxy_get_cached_property_names (helper->control);

    if (cached_props != NULL && g_strv_contains ((const gchar * const *) cached_props, "GPUs"))
    {
        // (already cached)
        DEBUG ("GPUs property already cached, skipping Get");
        process_gpus_property (helper, error);
        return;
    }

    gpus = g_dbus_connection_call_sync (g_dbus_proxy_get_connection (helper->control),
                                        g_dbus_proxy_get_name (helper->control),
                                        g_dbus_proxy_get_object_path (helper->control),
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        g_variant_new ("(ss)",
                                                       g_dbus_proxy_get_interface_name (helper->control),
                                                       "GPUs"),
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        helper->cancellable,
                                        error);

    if (gpus == NULL)
    {
        return;
    }

    g_dbus_proxy_set_cached_property (helper->control, "(GPUs)", gpus);
    process_gpus_property (helper, error);
}

static void
on_bus_connection_closed (GDBusConnection *connection,
                          gboolean         remote_peer_vanished,
                          GError          *error,
                          gpointer         user_data)
{
    XAppGpuOffloadHelper *helper = XAPP_GPU_OFFLOAD_HELPER (user_data);

    if (error != NULL)
    {
        g_critical ("Bus connection unexpectedly lost: (%d) %s", error->code, error->message);
        g_error_free (error);
    }

    g_object_unref (helper);
}

static void
helper_init_thread (GTask        *task,
                    gpointer      source_object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
    XAppGpuOffloadHelper *helper = XAPP_GPU_OFFLOAD_HELPER (source_object);
    XAppSwitcherooControl *control;
    GError *error = NULL;

    control = xapp_switcheroo_control_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                              G_DBUS_PROXY_FLAGS_NONE,
                                                              "net.hadess.SwitcherooControl",
                                                              "/net/hadess/SwitcherooControl",
                                                              helper->cancellable,
                                                              &error);

    if (error != NULL)
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            g_task_return_boolean (task, TRUE);
        }

        g_critical ("Could not create switcheroo proxy: (%d): %s", error->code, error->message);
        g_task_return_error (task, error);
    }

    if (g_dbus_proxy_get_name_owner (G_DBUS_PROXY (control)) != NULL)
    {
        DEBUG ("Got switcheroo-control proxy successfully");

        helper->control = G_DBUS_PROXY (control);
        g_signal_connect (g_dbus_proxy_get_connection (helper->control), "closed", G_CALLBACK (on_bus_connection_closed), helper);

        populate_gpu_info (helper, &error);

        if (error != NULL)
        {
            if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            {
                g_task_return_boolean (task, TRUE);
            }

            g_critical ("Could not get gpu info from switcheroo proxy: (%d): %s", error->code, error->message);
            g_task_return_error (task, error);
        }
    }
    else
    {
        g_warning ("Switcheroo-control doesn't appear to be running, checking for Ubuntu support...");

        // Add a default GPU no matter what just so we're 'valid'

        XAppGpuInfo *info;
        GList *infos = NULL;

        info = g_new0 (XAppGpuInfo, 1);
        info->id = 0;
        info->is_default = TRUE;
        info->display_name = g_strdup ("Integrated GPU");
        infos = g_list_append (infos, info);

        // If there's support, make up a default and non-default XAppGpuInfo.
        if (xapp_util_gpu_offload_supported ())
        {
            info = g_new0 (XAppGpuInfo, 1);
            info->id = 1;
            info->is_default = FALSE;
            info->display_name = g_strdup ("NVIDIA GPU");
            info->env_strv = g_strdupv ((gchar **) DEFAULT_ENV_ARGV);

            infos = g_list_append (infos, info);
        }

        g_mutex_lock (&helper->gpu_infos_mutex);

        g_list_free_full (helper->gpu_infos, (GDestroyNotify) xapp_gpu_info_free);
        helper->gpu_infos = infos;
        helper->num_gpus = g_list_length (helper->gpu_infos);

        g_mutex_unlock (&helper->gpu_infos_mutex);
    }

    g_task_return_boolean (task, TRUE);
}

static void
helper_task_callback (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
    XAppGpuOffloadHelper *helper = XAPP_GPU_OFFLOAD_HELPER (source_object);

    // Todo with an error? This callback is only used in async mode.
    gboolean success = g_task_propagate_boolean (G_TASK (res), NULL);

    if (success)
    {
        helper->ready = TRUE;

        if (DEBUGGING)
        {
            DEBUG ("Gpu infos:");
            for (GList *l = helper->gpu_infos; l != NULL; l = l->next)
            {
                XAppGpuInfo *info = l->data;

                gchar *shell_str = xapp_gpu_info_get_shell_env_prefix (info);
                gchar *debug = g_strdup_printf ("%s: %s", info->display_name, shell_str);

                DEBUG ("%s", debug);
                g_free (shell_str);
                g_free (debug);
            }
        }
    }

    g_signal_emit (helper, signals[READY], 0, helper->ready);
}

static void
init_helper (XAppGpuOffloadHelper *helper,
             gboolean              synchronous)
{
    GTask *task = g_task_new (helper, helper->cancellable, helper_task_callback, NULL);

    if (synchronous)
    {
        g_task_run_in_thread_sync (task, helper_init_thread);
        helper_task_callback (G_OBJECT (helper), G_ASYNC_RESULT (task), NULL);
    }
    else
    {
        g_task_run_in_thread (task, helper_init_thread);
    }

    g_object_unref (task);
}

static void
xapp_gpu_offload_helper_init (XAppGpuOffloadHelper *helper)
{
    helper->cancellable = g_cancellable_new ();
    g_mutex_init (&helper->gpu_infos_mutex);
}

static void
xapp_gpu_offload_helper_dispose (GObject *object)
{
    XAppGpuOffloadHelper *helper = XAPP_GPU_OFFLOAD_HELPER (object);

    if (helper->gpu_infos != NULL)
    {
        g_list_free_full (helper->gpu_infos, (GDestroyNotify) xapp_gpu_info_free);
        helper->gpu_infos = NULL;
    }

    g_clear_object (&helper->cancellable);
    g_clear_object (&helper->control);

    helper->ready = FALSE;

    G_OBJECT_CLASS (xapp_gpu_offload_helper_parent_class)->dispose (object);
}

static void
xapp_gpu_offload_helper_class_init (XAppGpuOffloadHelperClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = xapp_gpu_offload_helper_dispose;

    /**
     * XAppGpuOffloadHelper::ready:
     * @helper: the #XAppGpuOffloadHelper
     * @success: Whether or not the helper initialize successfully.
     *
     * This signal is emitted by the helper when it has completed
     * gathering GPU information. It will only be sent once.
     */
      signals[READY] =
          g_signal_new ("ready",
                        XAPP_TYPE_GPU_OFFLOAD_HELPER,
                        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                        0,
                        NULL, NULL, NULL,
                        G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static gboolean
idle_init_helper (gpointer user_data)
{
    XAppGpuOffloadHelper *helper = XAPP_GPU_OFFLOAD_HELPER (user_data);

    init_helper (helper, FALSE);

    return G_SOURCE_REMOVE;
}

static XAppGpuOffloadHelper *
helper_get_common (gboolean synchronous)
{
    static XAppGpuOffloadHelper *gpu_offload_helper = NULL;
    static gsize once_init_value = 0;

    if (g_once_init_enter (&once_init_value))
    {
        gpu_offload_helper = g_object_new (XAPP_TYPE_GPU_OFFLOAD_HELPER, NULL);

        if (synchronous)
        {
            DEBUG ("Initializing helper synchronously.");
            init_helper (gpu_offload_helper, TRUE);
        }
        else
        {
            DEBUG ("Initializing helper asynchronously");
            g_idle_add ((GSourceFunc) idle_init_helper, gpu_offload_helper);
        }

        g_once_init_leave (&once_init_value, 1);
    }

    return gpu_offload_helper;
}

/**
 * xapp_gpu_offload_helper_get:
 * 
 * Creates a new #XAppGpuOffloadHelper instance.
 * 
 * The #XAppGpuOffloadHelper::ready signal will be emitted when the helper is initialized (successfully or not).
 *
 * Returns: (transfer none): a new #XAppGpuOffloadHelper.
 *
 * Since: 2.6
 */
XAppGpuOffloadHelper *
xapp_gpu_offload_helper_get (void)
{
    return helper_get_common (FALSE);
}

/**
 * xapp_gpu_offload_helper_get_sync:
 * 
 * Creates a new #XAppGpuOffloadHelper instance. This performs initialization synchronously,
 * and can potentially block.
 * 
 * Use xapp_gpu_offload_helper_is_ready() to see if the helper was initialized successfully.
 *
 * Returns: (transfer none): a new #XAppGpuOffloadHelper, fully initialized.
 *
 * Since: 2.6
 */
XAppGpuOffloadHelper *
xapp_gpu_offload_helper_get_sync (void)
{
    return helper_get_common (TRUE);
}

static void
warn_if_not_ready (XAppGpuOffloadHelper *helper)
{
    if (!helper->ready)
    {
        g_warning ("Helper not initialized or failed to do so.");
    }
}

/**
 * xapp_gpu_offload_helper_is_ready:
 * @helper: The #XAppGpuOffloadHelper.
 *
 * Checks if the helper is ready and valid. This does not mean
 * offload support exists.
 *
 * Returns: %TRUE if the helper has been successfully initialized.
 *
 * Since: 2.6
 */
gboolean
xapp_gpu_offload_helper_is_ready (XAppGpuOffloadHelper *helper)
{
    g_return_val_if_fail (XAPP_IS_GPU_OFFLOAD_HELPER (helper), 1);

    return helper->ready;
}

/**
 * xapp_gpu_offload_helper_is_offload_supported:
 * @helper: The #XAppGpuOffloadHelper.
 *
 * Checks if there is a non-default GPU available for offloading.
 *
 * Returns: %TRUE if there is an extra GPU available.
 *
 * Since: 2.6
 */
gboolean
xapp_gpu_offload_helper_is_offload_supported (XAppGpuOffloadHelper *helper)
{
    g_return_val_if_fail (XAPP_IS_GPU_OFFLOAD_HELPER (helper), 1);

    return helper->num_gpus > 1;
}

/**
 * xapp_gpu_offload_helper_get_n_gpus:
 * @helper: The #XAppGpuOffloadHelper.
 *
 * Gets the number of GPUs noticed by Switcheroo.
 *
 * Returns: the total number of GPUs. A return value larger than
 * 1 implies there are offloadable GPUs available.
 *
 * Since: 2.6
 */
gint
xapp_gpu_offload_helper_get_n_gpus (XAppGpuOffloadHelper *helper)
{
    g_return_val_if_fail (XAPP_IS_GPU_OFFLOAD_HELPER (helper), 1);
    warn_if_not_ready (helper);

    return helper->num_gpus;
}

/**
 * xapp_gpu_offload_helper_get_offload_infos:
 * @helper: The #XAppGpuOffloadHelper.
 *
 * Generates a list of #XAppGpuInfos that can be offloaded to, if there are any.
 *
 * Returns: (element-type XAppGpuInfo) (transfer container): a list of #XAppGpuInfos or %NULL if there is only
 * a single GPU. The elements are owned by @helper but the container itself should be freed.
 *
 * Since: 2.6
 */
GList *
xapp_gpu_offload_helper_get_offload_infos (XAppGpuOffloadHelper *helper)
{
    g_return_val_if_fail (XAPP_IS_GPU_OFFLOAD_HELPER (helper), NULL);
    warn_if_not_ready (helper);

    GList *retval = NULL;

    GList *l;
    for (l = helper->gpu_infos; l != NULL; l = l->next)
    {
        XAppGpuInfo *info = l->data;

        if (info->is_default)
            continue;

        retval = g_list_append (retval, info);
    }

    return retval;
}

/**
 * xapp_gpu_offload_helper_get_default_info:
 * @helper: The #XAppGpuOffloadHelper.
 *
 * Returns an #XAppGpuInfo for the default GPU.
 *
 * Returns: (transfer none): the default #XAppGpuInfo. Do not free
 *
 * Since: 2.6
 */
XAppGpuInfo *
xapp_gpu_offload_helper_get_default_info (XAppGpuOffloadHelper *helper)
{
    g_return_val_if_fail (XAPP_IS_GPU_OFFLOAD_HELPER (helper), NULL);
    warn_if_not_ready (helper);

    GList *l;
    for (l = helper->gpu_infos; l != NULL; l = l->next)
    {
        XAppGpuInfo *info = l->data;

        if (info->is_default)
            return info;
    }

    g_warning ("No default GPU found by switcheroo!");
    return NULL;
}

/**
 * xapp_gpu_offload_helper_get_info_by_id:
 * @helper: The #XAppGpuOffloadHelper.
 * @id: The ID of the info to retrieve.
 *
 * Returns an #XAppGpuInfo with the given ID.
 *
 * Returns: (transfer none): the appropriate #XAppGpuInfo, or %NULL if @id was invalid.
 *
 * Since: 2.6
 */
XAppGpuInfo *
xapp_gpu_offload_helper_get_info_by_id (XAppGpuOffloadHelper *helper, gint id)
{
    g_return_val_if_fail (XAPP_IS_GPU_OFFLOAD_HELPER (helper), NULL);
    warn_if_not_ready (helper);

    GList *l;
    for (l = helper->gpu_infos; l != NULL; l = l->next)
    {
        XAppGpuInfo *info = l->data;

        if (info->id == id)
            return info;
    }

    g_warning ("No GPU with id %d found.", id);
    return NULL;
}


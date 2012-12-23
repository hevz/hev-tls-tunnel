/*
 ============================================================================
 Name        : hev-client.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#include "hev-client.h"

#define HEV_CLIENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HEV_TYPE_CLIENT, HevClientPrivate))

enum
{
    PROP_ZERO,
    PROP_TARGET_ADDR,
    PROP_TARGET_PORT,
    PROP_LOCAL_ADDR,
    PROP_LOCAL_PORT,
    N_PROPERTIES
};

typedef struct _HevClientPrivate HevClientPrivate;

struct _HevClientPrivate
{
    gchar *target_addr;
    gint target_port;
    gchar *local_addr;
    gint local_port;
};

static void hev_client_async_initable_iface_init (GAsyncInitableIface *iface);
static void hev_client_async_initable_init_async (GAsyncInitable *initable,
            gint io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data);
static gboolean hev_client_async_initable_init_finish (GAsyncInitable *initable,
            GAsyncResult *result, GError **error);

static GParamSpec *hev_client_properties[N_PROPERTIES] = { NULL };

G_DEFINE_TYPE_WITH_CODE (HevClient, hev_client, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, hev_client_async_initable_iface_init));

static void
hev_client_dispose (GObject *obj)
{
    HevClient *self = HEV_CLIENT (obj);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (hev_client_parent_class)->dispose (obj);
}

static void
hev_client_finalize (GObject *obj)
{
    HevClient *self = HEV_CLIENT (obj);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (priv->target_addr) {
        g_free (priv->target_addr);
        priv->target_addr = NULL;
    }

    if (priv->local_addr) {
        g_free (priv->local_addr);
        priv->local_addr = NULL;
    }

    G_OBJECT_CLASS (hev_client_parent_class)->finalize (obj);
}

static GObject *
hev_client_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    return G_OBJECT_CLASS (hev_client_parent_class)->constructor (type, n, param);
}

static void
hev_client_constructed (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (hev_client_parent_class)->constructed (obj);
}

static void
hev_client_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec)
{
    HevClient *self = HEV_CLIENT (obj);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (id) {
    case PROP_TARGET_ADDR:
        g_value_set_string (value, priv->target_addr);
        break;
    case PROP_TARGET_PORT:
        g_value_set_int (value, priv->target_port);
        break;
    case PROP_LOCAL_ADDR:
        g_value_set_string (value, priv->local_addr);
        break;
    case PROP_LOCAL_PORT:
        g_value_set_int (value, priv->local_port);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
hev_client_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec)
{
    HevClient *self = HEV_CLIENT (obj);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (id) {
    case PROP_TARGET_ADDR:
        if (priv->target_addr)
          g_free (priv->target_addr);
        priv->target_addr = g_strdup (g_value_get_string (value));
        break;
    case PROP_TARGET_PORT:
        priv->target_port = g_value_get_int (value);
        break;
    case PROP_LOCAL_ADDR:
        if (priv->local_addr)
          g_free (priv->local_addr);
        priv->local_addr = g_strdup (g_value_get_string (value));
        break;
    case PROP_LOCAL_PORT:
        priv->local_port = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
hev_client_class_init (HevClientClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    obj_class->constructor = hev_client_constructor;
    obj_class->constructed = hev_client_constructed;
    obj_class->set_property = hev_client_set_property;
    obj_class->get_property = hev_client_get_property;
    obj_class->dispose = hev_client_dispose;
    obj_class->finalize = hev_client_finalize;

    /* Properties */
    hev_client_properties[PROP_TARGET_ADDR] =
        g_param_spec_string ("target-addr",
                    "target addr", "Target addr",
                    "127.0.0.1",
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_client_properties[PROP_TARGET_PORT] =
        g_param_spec_int ("target-port",
                    "target port", "Target port",
                    0, G_MAXUINT16, 22,
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_client_properties[PROP_LOCAL_ADDR] =
        g_param_spec_string ("local-addr",
                    "Local addr", "Local addr",
                    "0.0.0.0",
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_client_properties[PROP_LOCAL_PORT] =
        g_param_spec_int ("local-port",
                    "local port", "Local port",
                    0, G_MAXUINT16, 6000,
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_properties (obj_class, N_PROPERTIES,
                hev_client_properties);

    g_type_class_add_private (klass, sizeof (HevClientPrivate));
}

static void
hev_client_async_initable_iface_init (GAsyncInitableIface *iface)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    iface->init_async = hev_client_async_initable_init_async;
    iface->init_finish = hev_client_async_initable_init_finish;
}

static void
hev_client_init (HevClient *self)
{
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    priv->target_addr = g_strdup ("127.0.0.1");
    priv->target_port = 22;
    priv->local_addr = g_strdup ("0.0.0.0");
    priv->local_port = 6000;
}

static void
async_result_run_in_thread_handler (GSimpleAsyncResult *simple,
            GObject *object, GCancellable *cancellable)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
}

static void
hev_client_async_initable_init_async (GAsyncInitable *initable,
            gint io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data)
{
    HevClient *self = HEV_CLIENT (initable);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);
    GSimpleAsyncResult *simple = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
    
    simple = g_simple_async_result_new (G_OBJECT(initable),
                callback, user_data, hev_client_async_initable_init_async);
    g_simple_async_result_set_check_cancellable (simple, cancellable);
    g_simple_async_result_run_in_thread (simple, async_result_run_in_thread_handler,
                io_priority, cancellable);
    g_object_unref (simple);
}

static gboolean
hev_client_async_initable_init_finish (GAsyncInitable *initable,
            GAsyncResult *result, GError **error)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_val_if_fail (g_simple_async_result_is_valid (result,
                    G_OBJECT (initable), hev_client_async_initable_init_async),
                FALSE);
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                    error))
      return FALSE;

    return TRUE;
}

void
hev_client_new_async (gchar *target_addr, gint target_port,
            gchar *local_addr, gint local_port,
            GCancellable *cancellable, GAsyncReadyCallback callback,
            gpointer user_data)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_async_initable_new_async (HEV_TYPE_CLIENT, G_PRIORITY_DEFAULT,
                cancellable, callback, user_data,
                "target-addr", target_addr,
                "target-port", target_port,
                "local-addr", local_addr,
                "local-port", local_port,
                NULL);
}

HevClient *
hev_client_new_finish (GAsyncResult *res, GError **error)
{
    GObject *object;
    GObject *source_object;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    source_object = g_async_result_get_source_object (res);
    g_assert (NULL != source_object);

    object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                res, error);

    g_object_unref (source_object);

    if (NULL != object)
      return HEV_CLIENT (object);

    return NULL;
}


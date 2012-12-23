/*
 ============================================================================
 Name        : hev-server.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#include "hev-server.h"

#define HEV_SERVER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HEV_TYPE_SERVER, HevServerPrivate))

enum
{
    PROP_ZERO,
    PROP_TARGET_ADDR,
    PROP_TARGET_PORT,
    PROP_LISTEN_ADDR,
    PROP_LISTEN_PORT,
    N_PROPERTIES
};

typedef struct _HevServerPrivate HevServerPrivate;

struct _HevServerPrivate
{
    gchar *target_addr;
    gint target_port;
    gchar *listen_addr;
    gint listen_port;
};

static void hev_server_async_initable_iface_init (GAsyncInitableIface *iface);
static void hev_server_async_initable_init_async (GAsyncInitable *initable,
            gint io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data);
static gboolean hev_server_async_initable_init_finish (GAsyncInitable *initable,
            GAsyncResult *result, GError **error);

static GParamSpec *hev_server_properties[N_PROPERTIES] = { NULL };

G_DEFINE_TYPE_WITH_CODE (HevServer, hev_server, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, hev_server_async_initable_iface_init));

static void
hev_server_dispose (GObject *obj)
{
    HevServer *self = HEV_SERVER (obj);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (hev_server_parent_class)->dispose (obj);
}

static void
hev_server_finalize (GObject *obj)
{
    HevServer *self = HEV_SERVER (obj);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (priv->target_addr) {
        g_free (priv->target_addr);
        priv->target_addr = NULL;
    }

    if (priv->listen_addr) {
        g_free (priv->listen_addr);
        priv->listen_addr = NULL;
    }

    G_OBJECT_CLASS (hev_server_parent_class)->finalize (obj);
}

static GObject *
hev_server_constructor (GType type,
            guint n,
            GObjectConstructParam *param)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    return G_OBJECT_CLASS (hev_server_parent_class)->constructor (type, n, param);
}

static void
hev_server_constructed (GObject *obj)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    G_OBJECT_CLASS (hev_server_parent_class)->constructed (obj);
}

static void
hev_server_get_property (GObject *obj, guint id,
            GValue *value, GParamSpec *pspec)
{
    HevServer *self = HEV_SERVER (obj);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    switch (id) {
    case PROP_TARGET_ADDR:
        g_value_set_string (value, priv->target_addr);
        break;
    case PROP_TARGET_PORT:
        g_value_set_int (value, priv->target_port);
        break;
    case PROP_LISTEN_ADDR:
        g_value_set_string (value, priv->listen_addr);
        break;
    case PROP_LISTEN_PORT:
        g_value_set_int (value, priv->listen_port);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
hev_server_set_property (GObject *obj, guint id,
            const GValue *value, GParamSpec *pspec)
{
    HevServer *self = HEV_SERVER (obj);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);

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
    case PROP_LISTEN_ADDR:
        if (priv->listen_addr)
          g_free (priv->listen_addr);
        priv->listen_addr = g_strdup (g_value_get_string (value));
        break;
    case PROP_LISTEN_PORT:
        priv->listen_port = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
        break;
    }
}

static void
hev_server_class_init (HevServerClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    obj_class->constructor = hev_server_constructor;
    obj_class->constructed = hev_server_constructed;
    obj_class->set_property = hev_server_set_property;
    obj_class->get_property = hev_server_get_property;
    obj_class->dispose = hev_server_dispose;
    obj_class->finalize = hev_server_finalize;

    /* Properties */
    hev_server_properties[PROP_TARGET_ADDR] =
        g_param_spec_string ("target-addr",
                    "target addr", "Target addr",
                    "127.0.0.1",
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_server_properties[PROP_TARGET_PORT] =
        g_param_spec_int ("target-port",
                    "target port", "Target port",
                    0, G_MAXUINT16, 22,
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_server_properties[PROP_LISTEN_ADDR] =
        g_param_spec_string ("listen-addr",
                    "Listen addr", "Listen addr",
                    "0.0.0.0",
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_server_properties[PROP_LISTEN_PORT] =
        g_param_spec_int ("listen-port",
                    "listen port", "Listen port",
                    0, G_MAXUINT16, 6000,
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_properties (obj_class, N_PROPERTIES,
                hev_server_properties);

    g_type_class_add_private (klass, sizeof (HevServerPrivate));
}

static void
hev_server_async_initable_iface_init (GAsyncInitableIface *iface)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    iface->init_async = hev_server_async_initable_init_async;
    iface->init_finish = hev_server_async_initable_init_finish;
}

static void
hev_server_init (HevServer *self)
{
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    priv->target_addr = g_strdup ("127.0.0.1");
    priv->target_port = 22;
    priv->listen_addr = g_strdup ("0.0.0.0");
    priv->listen_port = 6000;
}

static void
async_result_run_in_thread_handler (GSimpleAsyncResult *simple,
            GObject *object, GCancellable *cancellable)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
}

static void
hev_server_async_initable_init_async (GAsyncInitable *initable,
            gint io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data)
{
    HevServer *self = HEV_SERVER (initable);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);
    GSimpleAsyncResult *simple = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);
    
    simple = g_simple_async_result_new (G_OBJECT(initable),
                callback, user_data, hev_server_async_initable_init_async);
    g_simple_async_result_set_check_cancellable (simple, cancellable);
    g_simple_async_result_run_in_thread (simple, async_result_run_in_thread_handler,
                io_priority, cancellable);
    g_object_unref (simple);
}

static gboolean
hev_server_async_initable_init_finish (GAsyncInitable *initable,
            GAsyncResult *result, GError **error)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_val_if_fail (g_simple_async_result_is_valid (result,
                    G_OBJECT (initable), hev_server_async_initable_init_async),
                FALSE);
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                    error))
      return FALSE;

    return TRUE;
}

void
hev_server_new_async (gchar *target_addr, gint target_port,
            gchar *listen_addr, gint listen_port,
            GCancellable *cancellable, GAsyncReadyCallback callback,
            gpointer user_data)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_async_initable_new_async (HEV_TYPE_SERVER, G_PRIORITY_DEFAULT,
                cancellable, callback, user_data,
                "target-addr", target_addr,
                "target-port", target_port,
                "listen-addr", listen_addr,
                "listen-port", listen_port,
                NULL);
}

HevServer *
hev_server_new_finish (GAsyncResult *res, GError **error)
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
      return HEV_SERVER (object);

    return NULL;
}


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
    PROP_SERVER_ADDR,
    PROP_SERVER_PORT,
    PROP_LOCAL_ADDR,
    PROP_LOCAL_PORT,
    N_PROPERTIES
};

typedef struct _HevClientPrivate HevClientPrivate;
typedef struct _HevClientClientData HevClientClientData;

struct _HevClientPrivate
{
    gchar *server_addr;
    gint server_port;
    gchar *local_addr;
    gint local_port;

    GSocketService *service;
};

struct _HevClientClientData
{
    GIOStream *tls_stream;
    GIOStream *lcl_stream;
};

static void hev_client_async_initable_iface_init (GAsyncInitableIface *iface);
static void hev_client_async_initable_init_async (GAsyncInitable *initable,
            gint io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data);
static gboolean hev_client_async_initable_init_finish (GAsyncInitable *initable,
            GAsyncResult *result, GError **error);
static gboolean socket_service_incoming_handler (GSocketService *service,
            GSocketConnection *connection, GObject *source_object,
            gpointer user_data);
static void socket_client_connect_to_host_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void io_stream_splice_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void io_stream_close_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);

static GParamSpec *hev_client_properties[N_PROPERTIES] = { NULL };

G_DEFINE_TYPE_WITH_CODE (HevClient, hev_client, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, hev_client_async_initable_iface_init));

GQuark
hev_client_error_quark (void)
{
    return g_quark_from_static_string ("hev-client-error-quark");
}

static void
hev_client_dispose (GObject *obj)
{
    HevClient *self = HEV_CLIENT (obj);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (priv->service) {
        g_signal_handlers_disconnect_by_func (priv->service,
                    G_CALLBACK (socket_service_incoming_handler),
                    self);
        g_object_unref (priv->service);
        priv->service = NULL;
    }

    G_OBJECT_CLASS (hev_client_parent_class)->dispose (obj);
}

static void
hev_client_finalize (GObject *obj)
{
    HevClient *self = HEV_CLIENT (obj);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (priv->server_addr) {
        g_free (priv->server_addr);
        priv->server_addr = NULL;
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
    case PROP_SERVER_ADDR:
        g_value_set_string (value, priv->server_addr);
        break;
    case PROP_SERVER_PORT:
        g_value_set_int (value, priv->server_port);
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
    case PROP_SERVER_ADDR:
        if (priv->server_addr)
          g_free (priv->server_addr);
        priv->server_addr = g_strdup (g_value_get_string (value));
        break;
    case PROP_SERVER_PORT:
        priv->server_port = g_value_get_int (value);
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
    hev_client_properties[PROP_SERVER_ADDR] =
        g_param_spec_string ("server-addr",
                    "server addr", "Server addr",
                    "127.0.0.1",
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_client_properties[PROP_SERVER_PORT] =
        g_param_spec_int ("server-port",
                    "server port", "Server port",
                    0, G_MAXUINT16, 22,
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_client_properties[PROP_LOCAL_ADDR] =
        g_param_spec_string ("local-addr",
                    "local addr", "Local addr",
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

    priv->server_addr = g_strdup ("127.0.0.1");
    priv->server_port = 22;
    priv->local_addr = g_strdup ("0.0.0.0");
    priv->local_port = 6000;
}

static void
async_result_run_in_thread_handler (GSimpleAsyncResult *simple,
            GObject *object, GCancellable *cancellable)
{
    HevClient *self = HEV_CLIENT (object);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);
    GInetAddress *iaddr = NULL;
    GSocketAddress *saddr = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    priv->service = g_socket_service_new ();
    if (!priv->service) {
        g_simple_async_result_set_error (simple,
                    HEV_CLIENT_ERROR,
                    HEV_CLIENT_ERROR_SERVICE,
                    "Create socket service failed!");
        goto service_fail;
    }

    iaddr = g_inet_address_new_from_string (priv->local_addr);
    saddr = g_inet_socket_address_new (iaddr, priv->local_port);

    if (!g_socket_listener_add_address (G_SOCKET_LISTENER (priv->service),
                saddr, G_SOCKET_TYPE_STREAM,
                G_SOCKET_PROTOCOL_TCP, NULL,
                NULL, &error)) {
        g_simple_async_result_take_error (simple, error);
        goto add_addr_fail;
    }

    g_object_unref (saddr);
    g_object_unref (iaddr);

    g_signal_connect (priv->service, "incoming",
                G_CALLBACK (socket_service_incoming_handler),
                self);

    return;

add_addr_fail:
    g_object_unref (saddr);
    g_object_unref (iaddr);
    g_object_unref (priv->service);
service_fail:

    return;
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
hev_client_new_async (gchar *server_addr, gint server_port,
            gchar *local_addr, gint local_port,
            GCancellable *cancellable, GAsyncReadyCallback callback,
            gpointer user_data)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_async_initable_new_async (HEV_TYPE_CLIENT, G_PRIORITY_DEFAULT,
                cancellable, callback, user_data,
                "server-addr", server_addr,
                "server-port", server_port,
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

void
hev_client_start (HevClient *self)
{
    HevClientPrivate *priv = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_if_fail (HEV_IS_CLIENT (self));
    priv = HEV_CLIENT_GET_PRIVATE (self);

    g_socket_service_start (priv->service);
}

void
hev_client_stop (HevClient *self)
{
    HevClientPrivate *priv = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_if_fail (HEV_IS_CLIENT (self));
    priv = HEV_CLIENT_GET_PRIVATE (self);

    g_socket_service_stop (priv->service);
}

static gboolean
socket_service_incoming_handler (GSocketService *service,
            GSocketConnection *connection,
            GObject *source_object,
            gpointer user_data)
{
    HevClient *self = HEV_CLIENT (user_data);
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (self);
    HevClientClientData *cdat = NULL;
    GSocketClient *client = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    client = g_socket_client_new ();
    if (!client) {
        g_critical ("Create socket client failed!");
        goto client_fail;
    }

    cdat = g_slice_new0 (HevClientClientData);
    if (!cdat) {
        g_critical ("Alloc client data failed!");
        goto cdat_fail;
    }
    cdat->lcl_stream = G_IO_STREAM (g_object_ref (connection));

    g_socket_client_connect_to_host_async (client,
                priv->server_addr, priv->server_port, NULL,
                socket_client_connect_to_host_async_handler,
                cdat);

    return FALSE;

cdat_fail:
    g_object_unref (client);
client_fail:

    return FALSE;
}

static void
socket_client_connect_to_host_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    GSocketConnection *conn = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    conn = g_socket_client_connect_to_host_finish (
                G_SOCKET_CLIENT (source_object), res, &error);
    if (!conn) {
        g_critical ("Connect to server host failed: %s", error->message);
        g_clear_error (&error);
        goto connect_fail;
    }

    cdat->tls_stream = g_tls_client_connection_new (G_IO_STREAM (conn),
                NULL, &error);
    if (!cdat->tls_stream) {
        g_critical ("Create tls client stream failed: %s", error->message);
        g_clear_error (&error);
        goto tls_stream_fail;
    }

    g_io_stream_splice_async (cdat->lcl_stream, cdat->tls_stream,
                G_IO_STREAM_SPLICE_NONE, G_PRIORITY_DEFAULT, NULL,
                io_stream_splice_async_handler, cdat);

    g_object_unref (conn);
    g_object_unref (source_object);

    return;

tls_stream_fail:
    g_object_unref (conn);
connect_fail:
    g_object_unref (source_object);
    g_io_stream_close_async (cdat->lcl_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                NULL);
    g_slice_free (HevClientClientData, cdat);

    return;
}

static void
io_stream_splice_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!g_io_stream_splice_finish (res, &error)) {
        g_debug ("Splice tls and server stream failed: %s", error->message);
        g_clear_error (&error);
    }

    g_io_stream_close_async (cdat->tls_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                NULL);
    g_io_stream_close_async (cdat->lcl_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                NULL);
    g_slice_free (HevClientClientData, cdat);
}

static void
io_stream_close_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_io_stream_close_finish (G_IO_STREAM (source_object),
                res, NULL);

    g_object_unref (source_object);
}


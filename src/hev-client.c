/*
 ============================================================================
 Name        : hev-client.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#include <string.h>

#include "hev-client.h"
#include "hev-protocol.h"
#include "hev-utils.h"

#define HEV_CLIENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HEV_TYPE_CLIENT, HevClientPrivate))

enum
{
    PROP_ZERO,
    PROP_SERVER_ADDR,
    PROP_SERVER_PORT,
    PROP_LOCAL_ADDR,
    PROP_LOCAL_PORT,
    PROP_CA_FILE,
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
    gchar *ca_file;

    GSocketService *service;
    GSocketClient *client;
    gboolean use_tls;
};

struct _HevClientClientData
{
    GIOStream *tun_stream;
    GIOStream *lcl_stream;

    HevClient *self;
    guint8 req_buffer[HEV_PROTO_HTTP_REQUEST_LENGTH +
        HEV_PROTO_HEADER_MAXN_SIZE];
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
static void buffered_input_stream_fill_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void input_stream_skip_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void io_stream_splice_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void output_stream_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void io_stream_close_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static gboolean tls_connection_accept_certificate_handler (GTlsConnection *conn,
            GTlsCertificate *peer_cert, GTlsCertificateFlags errors,
            gpointer user_data);

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

    if (priv->client) {
        g_object_unref (priv->client);
        priv->client = NULL;
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
    case PROP_CA_FILE:
        g_value_set_string (value, priv->ca_file);
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
    case PROP_CA_FILE:
        if (priv->ca_file)
          g_free (priv->ca_file);
        priv->ca_file = g_strdup (g_value_get_string (value));
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
    hev_client_properties[PROP_CA_FILE] =
        g_param_spec_string ("ca-file",
                    "trusted ca file", "Trusted CA file",
                    NULL,
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

    priv->client = g_socket_client_new ();
    if (!priv->client) {
        g_simple_async_result_set_error (simple,
                    HEV_CLIENT_ERROR,
                    HEV_CLIENT_ERROR_CLIENT,
                    "Create socket client failed!");
        goto client_fail;
    }

    priv->use_tls = !g_str_equal ("None", priv->ca_file);

    return;

client_fail:
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
            gchar *local_addr, gint local_port, gchar *ca_file,
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
                "ca-file", ca_file,
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

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    cdat = g_slice_new0 (HevClientClientData);
    if (!cdat) {
        g_critical ("Alloc client data failed!");
        goto cdat_fail;
    }
    cdat->lcl_stream = G_IO_STREAM (g_object_ref (connection));
    cdat->self = g_object_ref (self);

    g_socket_client_connect_to_host_async (priv->client,
                priv->server_addr, priv->server_port, NULL,
                socket_client_connect_to_host_async_handler,
                cdat);

    return FALSE;

cdat_fail:

    return FALSE;
}

static void
socket_client_connect_to_host_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (cdat->self);
    GSocketConnection *conn = NULL;
    GOutputStream *tun_output = NULL;
    HevProtocolHeader *proto_header = NULL;
    guint32 length = 0;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    conn = g_socket_client_connect_to_host_finish (
                G_SOCKET_CLIENT (source_object), res, &error);
    if (!conn) {
        g_critical ("Connect to server host failed: %s", error->message);
        g_clear_error (&error);
        goto connect_fail;
    }

    if (priv->use_tls) {
        cdat->tun_stream = g_tls_client_connection_new (G_IO_STREAM (conn),
                    NULL, &error);
        if (!cdat->tun_stream) {
            g_critical ("Create TLS client stream failed: %s", error->message);
            g_clear_error (&error);
            goto tun_stream_fail;
        }

        g_signal_connect (cdat->tun_stream, "accept-certificate",
                    G_CALLBACK (tls_connection_accept_certificate_handler),
                    cdat);
    } else {
        cdat->tun_stream = g_object_ref (conn);
    }

    memcpy (cdat->req_buffer, HEV_PROTO_HTTP_REQUEST,
                HEV_PROTO_HTTP_REQUEST_LENGTH);
    proto_header = (HevProtocolHeader *)
        (cdat->req_buffer + HEV_PROTO_HTTP_REQUEST_LENGTH);
    length = g_random_int_range (HEV_PROTO_HEADER_MINN_SIZE,
                HEV_PROTO_HEADER_MAXN_SIZE);
    hev_protocol_header_set (proto_header, length);
    tun_output = g_io_stream_get_output_stream (cdat->tun_stream);
    g_output_stream_write_async (tun_output, cdat->req_buffer,
                HEV_PROTO_HTTP_REQUEST_LENGTH + length,
                G_PRIORITY_DEFAULT, NULL,
                output_stream_write_async_handler,
                cdat);

    g_object_unref (conn);

    return;

tun_stream_fail:
    g_object_unref (conn);
connect_fail:
    g_io_stream_close_async (cdat->lcl_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);

    return;
}

static void
output_stream_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    GInputStream *tun_input = NULL;
    GInputStream *tun_bufed_input = NULL;
    gssize size = 0;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    size = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object),
                res, &error);
    switch (size) {
    case -1:
        g_critical ("Tunnel connection write failed: %s", error->message);
        g_clear_error (&error);
    case 0:
        goto closed;
    }

    tun_input = g_io_stream_get_input_stream (cdat->tun_stream);
    tun_bufed_input = g_buffered_input_stream_new_sized (tun_input,
                HEV_PROTO_HTTP_RESPONSE_VALID_LENGTH + HEV_PROTO_HEADER_REAL_SIZE);
    g_filter_input_stream_set_close_base_stream (
                G_FILTER_INPUT_STREAM (tun_bufed_input),
                FALSE);
    g_buffered_input_stream_fill_async (G_BUFFERED_INPUT_STREAM (tun_bufed_input),
                HEV_PROTO_HTTP_RESPONSE_VALID_LENGTH + HEV_PROTO_HEADER_REAL_SIZE,
                G_PRIORITY_DEFAULT, NULL, buffered_input_stream_fill_async_handler,
                cdat);

    return;

closed:
    g_io_stream_close_async (cdat->tun_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_io_stream_close_async (cdat->lcl_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);

    return;
}

static void
buffered_input_stream_fill_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    GInputStream *tun_input = NULL;
    GBufferedInputStream *tun_bufed_input = NULL;
    gssize size = 0;
    gconstpointer res_buffer;
    const HevProtocolHeader *proto_header = NULL;
    gsize len = 0;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    tun_bufed_input = G_BUFFERED_INPUT_STREAM (source_object);
    size = g_buffered_input_stream_fill_finish (tun_bufed_input,
                res, &error);
    switch (size) {
    case -1:
        g_critical ("Buffered input stream fill failed: %s",
                    error->message);
        g_clear_error (&error);
    case 0:
        goto closed;
    }

    res_buffer = g_buffered_input_stream_peek_buffer (tun_bufed_input, &len);
    proto_header = (HevProtocolHeader *)
        (res_buffer + HEV_PROTO_HTTP_RESPONSE_VALID_LENGTH);
    if (!hev_protocol_header_is_valid (proto_header)) {
        g_warning ("Server's response message is invalid!");
        goto closed;
    }
    tun_input = g_filter_input_stream_get_base_stream (
                G_FILTER_INPUT_STREAM (tun_bufed_input));
    g_input_stream_skip_async (tun_input,
                proto_header->length - HEV_PROTO_HEADER_REAL_SIZE,
                G_PRIORITY_DEFAULT, NULL, 
                input_stream_skip_async_handler,
                cdat);

    g_object_unref (tun_bufed_input);

    return;

closed:
    g_object_unref (tun_bufed_input);
    g_io_stream_close_async (cdat->tun_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_io_stream_close_async (cdat->lcl_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);

    return;
}

static void
input_stream_skip_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (cdat->self);
    GSocket *sock = NULL, *sock2 = NULL;
    GIOStream *tun_base = NULL;
    gssize size = 0;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    size = g_input_stream_skip_finish (G_INPUT_STREAM (source_object),
                res, &error);
    switch (size) {
    case -1:
        g_critical ("Buffered input stream fill failed: %s",
                    error->message);
        g_clear_error (&error);
    case 0:
        goto closed;
    }

    sock = g_socket_connection_get_socket (G_SOCKET_CONNECTION (cdat->lcl_stream));
    if (priv->use_tls) {
        g_object_get (cdat->tun_stream,
                    "base-io-stream", &tun_base,
                    NULL);
    } else {
        tun_base = g_object_ref (cdat->tun_stream);
    }
    sock2 = g_socket_connection_get_socket (G_SOCKET_CONNECTION (tun_base));
    hev_socket_io_stream_splice_async (sock, cdat->lcl_stream,
                sock2, cdat->tun_stream, G_PRIORITY_DEFAULT, NULL, NULL, NULL,
                NULL, io_stream_splice_async_handler, cdat);
    g_object_unref (tun_base);

    return;

closed:
    g_io_stream_close_async (cdat->tun_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);

    return;
}

static void
io_stream_splice_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!hev_socket_io_stream_splice_finish (res, &error)) {
        g_debug ("Splice tunnel and server stream failed: %s", error->message);
        g_clear_error (&error);
    }

    g_io_stream_close_async (cdat->tun_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_io_stream_close_async (cdat->lcl_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
}

static void
io_stream_close_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClientClientData *cdat = user_data;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_io_stream_close_finish (G_IO_STREAM (source_object),
                res, NULL);

    if (G_IO_STREAM (source_object) == cdat->tun_stream)
      cdat->tun_stream = NULL;
    else if (G_IO_STREAM (source_object) == cdat->lcl_stream)
      cdat->lcl_stream = NULL;
    g_object_unref (source_object);

    if (!cdat->tun_stream && !cdat->lcl_stream) {
        g_object_unref (cdat->self);
        g_slice_free (HevClientClientData, cdat);
    }
}

static gboolean
tls_connection_accept_certificate_handler (GTlsConnection *conn,
            GTlsCertificate *peer_cert, GTlsCertificateFlags errors,
            gpointer user_data)
{
    HevClientClientData *cdat = user_data;
    HevClientPrivate *priv = HEV_CLIENT_GET_PRIVATE (cdat->self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (0 == errors)
      return TRUE;

    if (priv->ca_file &&
            (G_TLS_CERTIFICATE_UNKNOWN_CA == errors)) {
        GTlsCertificate *ca = NULL;
        GError *error = NULL;

        ca = g_tls_certificate_new_from_file (priv->ca_file,
                    &error);
        if (!ca) {
            g_critical ("Create ca certificate failed: %s",
                        error->message);
            g_clear_error (&error);
        } else {
            gboolean ret = FALSE;

            if (0 == g_tls_certificate_verify (peer_cert, NULL, ca))
              ret = TRUE;

            g_object_unref (ca);
            return ret;
        }
    }

    if (G_TLS_CERTIFICATE_UNKNOWN_CA & errors)
      g_warning ("TLS certificate unknown ca!");
    if (G_TLS_CERTIFICATE_BAD_IDENTITY & errors)
      g_warning ("TLS certificate bad identify!");
    if (G_TLS_CERTIFICATE_NOT_ACTIVATED & errors)
      g_warning ("TLS certificate not activated!");
    if (G_TLS_CERTIFICATE_EXPIRED & errors)
      g_warning ("TLS certificate expired!");
    if (G_TLS_CERTIFICATE_REVOKED & errors)
      g_warning ("TLS certificate revoked!");
    if (G_TLS_CERTIFICATE_INSECURE & errors)
      g_warning ("TLS certificate insecure!");
    if (G_TLS_CERTIFICATE_GENERIC_ERROR & errors)
      g_warning ("TLS certificate generic error!");

    return FALSE;
}


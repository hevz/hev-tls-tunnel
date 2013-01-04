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
#include "hev-protocol.h"

#define HEV_SERVER_TIMEOUT_SECONDS      20
#define HEV_SERVER_TIMEOUT_MAX_COUNT    3

#define HEV_SERVER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HEV_TYPE_SERVER, HevServerPrivate))

enum
{
    PROP_ZERO,
    PROP_TARGET_ADDR,
    PROP_TARGET_PORT,
    PROP_LISTEN_ADDR,
    PROP_LISTEN_PORT,
    PROP_CERT_FILE,
    PROP_KEY_FILE,
    N_PROPERTIES
};

typedef struct _HevServerPrivate HevServerPrivate;
typedef struct _HevServerClientData HevServerClientData;

struct _HevServerPrivate
{
    gchar *target_addr;
    gint target_port;
    gchar *listen_addr;
    gint listen_port;
    gchar *cert_file;
    gchar *key_file;

    GSocketService *service;
    GSocketClient *client;
};

struct _HevServerClientData
{
    GIOStream *tls_stream;
    GIOStream *tgt_stream;

    guint timeout_count;
    GCancellable *cancellable;

    HevServer *self;
    GMainLoop *loop;
};

static void hev_server_async_initable_iface_init (GAsyncInitableIface *iface);
static void hev_server_async_initable_init_async (GAsyncInitable *initable,
            gint io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data);
static gboolean hev_server_async_initable_init_finish (GAsyncInitable *initable,
            GAsyncResult *result, GError **error);
static gboolean socket_source_handler (GSocket *socket, GIOCondition condition,
            gpointer user_data);
static gboolean timeout_handler (gpointer user_data);
static gboolean socket_service_run_handler (GThreadedSocketService *service,
            GSocketConnection *connection, GObject *source_object,
            gpointer user_data);
static void tls_connection_handshake_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void buffered_input_stream_fill_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void input_stream_skip_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void output_stream_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void socket_client_connect_to_host_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void io_stream_splice_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void io_stream_close_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);

static GParamSpec *hev_server_properties[N_PROPERTIES] = { NULL };

G_DEFINE_TYPE_WITH_CODE (HevServer, hev_server, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, hev_server_async_initable_iface_init));

GQuark
hev_server_error_quark (void)
{
    return g_quark_from_static_string ("hev-server-error-quark");
}

static void
hev_server_dispose (GObject *obj)
{
    HevServer *self = HEV_SERVER (obj);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (priv->service) {
        g_signal_handlers_disconnect_by_func (priv->service,
                    G_CALLBACK (socket_service_run_handler),
                    self);
        g_object_unref (priv->service);
        priv->service = NULL;
    }

    if (priv->client) {
        g_object_unref (priv->client);
        priv->client = NULL;
    }

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

    if (priv->cert_file) {
        g_free (priv->cert_file);
        priv->cert_file = NULL;
    }

    if (priv->key_file) {
        g_free (priv->key_file);
        priv->key_file = NULL;
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
    case PROP_CERT_FILE:
        g_value_set_string (value, priv->cert_file);
        break;
    case PROP_KEY_FILE:
        g_value_set_string (value, priv->key_file);
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
    case PROP_CERT_FILE:
        if (priv->cert_file)
          g_free (priv->cert_file);
        priv->cert_file = g_strdup (g_value_get_string (value));
        break;
    case PROP_KEY_FILE:
        if (priv->key_file)
          g_free (priv->key_file);
        priv->key_file = g_strdup (g_value_get_string (value));
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
                    "listen addr", "Listen addr",
                    "0.0.0.0",
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_server_properties[PROP_LISTEN_PORT] =
        g_param_spec_int ("listen-port",
                    "listen port", "Listen port",
                    0, G_MAXUINT16, 6000,
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_server_properties[PROP_CERT_FILE] =
        g_param_spec_string ("cert-file",
                    "certificate file", "Certificate file",
                    NULL,
                    G_PARAM_READWRITE |
                    G_PARAM_CONSTRUCT_ONLY);
    hev_server_properties[PROP_KEY_FILE] =
        g_param_spec_string ("key-file",
                    "private key file", "Private key file",
                    NULL,
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
    priv->cert_file = NULL;
    priv->key_file = NULL;
}

static void
async_result_run_in_thread_handler (GSimpleAsyncResult *simple,
            GObject *object, GCancellable *cancellable)
{
    HevServer *self = HEV_SERVER (object);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);
    GInetAddress *iaddr = NULL;
    GSocketAddress *saddr = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    priv->service = g_threaded_socket_service_new (-1);
    if (!priv->service) {
        g_simple_async_result_set_error (simple,
                    HEV_SERVER_ERROR,
                    HEV_SERVER_ERROR_SERVICE,
                    "Create socket service failed!");
        goto service_fail;
    }
    g_object_set (priv->service, "listen-backlog", 100, NULL);

    iaddr = g_inet_address_new_from_string (priv->listen_addr);
    saddr = g_inet_socket_address_new (iaddr, priv->listen_port);

    if (!g_socket_listener_add_address (G_SOCKET_LISTENER (priv->service),
                saddr, G_SOCKET_TYPE_STREAM,
                G_SOCKET_PROTOCOL_TCP, NULL,
                NULL, &error)) {
        g_simple_async_result_take_error (simple, error);
        goto add_addr_fail;
    }

    g_object_unref (saddr);
    g_object_unref (iaddr);

    g_signal_connect (priv->service, "run",
                G_CALLBACK (socket_service_run_handler),
                self);

    priv->client = g_socket_client_new ();
    if (!priv->client) {
        g_simple_async_result_set_error (simple,
                    HEV_SERVER_ERROR,
                    HEV_SERVER_ERROR_CLIENT,
                    "Create socket client failed!");
        goto client_fail;
    }

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
hev_server_async_initable_init_async (GAsyncInitable *initable,
            gint io_priority, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data)
{
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
            gchar *cert_file, gchar *key_file,
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
                "cert-file", cert_file,
                "key-file", key_file,
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

void
hev_server_start (HevServer *self)
{
    HevServerPrivate *priv = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_if_fail (HEV_IS_SERVER (self));
    priv = HEV_SERVER_GET_PRIVATE (self);

    g_socket_service_start (priv->service);
}

void
hev_server_stop (HevServer *self)
{
    HevServerPrivate *priv = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_if_fail (HEV_IS_SERVER (self));
    priv = HEV_SERVER_GET_PRIVATE (self);

    g_socket_service_stop (priv->service);
}

static gboolean
socket_source_handler (GSocket *socket,
            GIOCondition condition,
            gpointer user_data)
{
    HevServerClientData *cdat = user_data;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    cdat->timeout_count = 0;

    return FALSE;
}

static gboolean
timeout_handler (gpointer user_data)
{
    HevServerClientData *cdat = user_data;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    cdat->timeout_count ++;

    if (HEV_SERVER_TIMEOUT_MAX_COUNT <= cdat->timeout_count) {
        g_cancellable_cancel (cdat->cancellable);
    } else {
        GSocket *sock = NULL;
        GSource *sock_src = NULL;
        GSocketConnection *connection = NULL;

        g_object_get (cdat->tls_stream,
                    "base-io-stream",
                    &connection,
                    NULL);
        sock = g_socket_connection_get_socket (connection);
        sock_src = g_socket_create_source (sock, G_IO_IN, NULL);
        if (!sock_src) {
            g_critical ("Create socket source failed!");
            goto leave;
        }
        g_source_set_callback (sock_src,
                    (GSourceFunc) socket_source_handler,
                    cdat, NULL);
        g_source_set_priority (sock_src, G_PRIORITY_LOW);
        g_source_attach (sock_src, g_main_loop_get_context (cdat->loop));
        g_source_unref (sock_src);
        g_object_unref (connection);

        if (!cdat->tgt_stream)
          goto leave;
        sock = g_socket_connection_get_socket (
                    G_SOCKET_CONNECTION (cdat->tgt_stream));
        sock_src = g_socket_create_source (sock, G_IO_IN, NULL);
        if (!sock_src) {
            g_critical ("Create socket source failed!");
            goto leave;
        }
        g_source_set_callback (sock_src,
                    (GSourceFunc) socket_source_handler,
                    cdat, NULL);
        g_source_set_priority (sock_src, G_PRIORITY_LOW);
        g_source_attach (sock_src, g_main_loop_get_context (cdat->loop));
        g_source_unref (sock_src);
    }

leave:
    return TRUE;
}

static gboolean
socket_service_run_handler (GThreadedSocketService *service,
            GSocketConnection *connection,
            GObject *source_object,
            gpointer user_data)
{
    HevServer *self = HEV_SERVER (user_data);
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (self);
    GMainContext *context = NULL;
    GMainLoop *loop = NULL;
    GTlsCertificate *cert = NULL;
    GIOStream *tls_stream = NULL;
    HevServerClientData *cdat = NULL;
    GSocket *sock = NULL;
    GSource *sock_src = NULL, *timeout_src = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    context = g_main_context_new ();
    loop = g_main_loop_new (context, FALSE);
    g_main_context_push_thread_default (context);

    cert = g_tls_certificate_new_from_files (priv->cert_file,
                priv->key_file, &error);
    if (!cert) {
        g_critical ("Create tls certificate failed: %s", error->message);
        g_clear_error (&error);
        goto cert_fail;
    }

    tls_stream = g_tls_server_connection_new (G_IO_STREAM (connection),
                cert, &error);
    if (!tls_stream) {
        g_critical ("Create tls server connection failed: %s", error->message);
        g_clear_error (&error);
        goto tls_stream_fail;
    }

    cdat = g_slice_new0 (HevServerClientData);
    if (!cdat) {
        g_critical ("Alloc client data failed!");
        goto cdat_fail;
    }
    cdat->tls_stream = tls_stream;
    cdat->cancellable = g_cancellable_new ();
    cdat->self = g_object_ref (self);
    cdat->loop = g_main_loop_ref (loop);

    sock = g_socket_connection_get_socket (connection);
    sock_src = g_socket_create_source (sock, G_IO_IN, NULL);
    if (!sock_src) {
        g_critical ("Create socket source failed!");
        goto sock_src_fail;
    }
    g_source_set_callback (sock_src,
                (GSourceFunc) socket_source_handler,
                cdat, NULL);
    g_source_set_priority (sock_src, G_PRIORITY_LOW);
    g_source_attach (sock_src, context);
    g_source_unref (sock_src);

    timeout_src = g_timeout_source_new_seconds (HEV_SERVER_TIMEOUT_SECONDS);
    g_source_set_callback (timeout_src,
                (GSourceFunc) timeout_handler,
                cdat, NULL);
    g_source_set_priority (timeout_src, G_PRIORITY_LOW);
    g_source_attach (timeout_src, context);
    g_source_unref (timeout_src);

    g_object_unref (cert);

    g_tls_connection_handshake_async (G_TLS_CONNECTION (tls_stream),
                G_PRIORITY_DEFAULT, cdat->cancellable,
                tls_connection_handshake_async_handler,
                cdat);

    g_main_loop_run (loop);

    g_source_destroy (timeout_src);

    g_object_unref (cdat->cancellable);
    g_object_unref (cdat->self);
    g_main_loop_unref (cdat->loop);
    g_slice_free (HevServerClientData, cdat);

    g_main_context_pop_thread_default (context);
    g_main_loop_unref (loop);
    g_main_context_unref (context);

    return FALSE;

sock_src_fail:
    g_object_unref (cdat->self);
    g_main_loop_unref (cdat->loop);
    g_slice_free (HevServerClientData, cdat);
cdat_fail:
    g_object_unref (tls_stream);
tls_stream_fail:
    g_object_unref (cert);
cert_fail:
    g_main_context_pop_thread_default (context);
    g_main_loop_unref (loop);
    g_main_context_unref (context);

    return FALSE;
}

static void
tls_connection_handshake_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServerClientData *cdat = user_data;
    GIOStream *tls_base = NULL;
    GInputStream *tls_input = NULL;
    GInputStream *tls_bufed_input = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!g_tls_connection_handshake_finish (G_TLS_CONNECTION (source_object),
                    res, &error)) {
        g_critical ("TLS connection handshake failed: %s", error->message);
        g_clear_error (&error);
        goto handshake_fail;
    }

    tls_input = g_io_stream_get_input_stream (cdat->tls_stream);
    tls_bufed_input = g_buffered_input_stream_new_sized (tls_input,
                HEV_PROTO_HEADER_REAL_SIZE);
    g_filter_input_stream_set_close_base_stream (
                G_FILTER_INPUT_STREAM (tls_bufed_input),
                FALSE);
    g_buffered_input_stream_fill_async (
                G_BUFFERED_INPUT_STREAM (tls_bufed_input),
                HEV_PROTO_HEADER_REAL_SIZE, G_PRIORITY_DEFAULT,
                cdat->cancellable, buffered_input_stream_fill_async_handler,
                cdat);

    return;

handshake_fail:
    g_object_get (cdat->tls_stream,
                "base-io-stream", &tls_base,
                NULL);
    g_io_stream_close_async (tls_base,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_object_unref (cdat->tls_stream);
    cdat->tls_stream = tls_base;

    return;
}

static void
buffered_input_stream_fill_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServerClientData *cdat = user_data;
    GBufferedInputStream *tls_bufed_input = NULL;
    GIOStream *tls_base = NULL;
    gssize size = 0;
    const HevProtocolHeader *header = NULL;
    gsize len = 0;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    tls_bufed_input = G_BUFFERED_INPUT_STREAM (source_object);
    size = g_buffered_input_stream_fill_finish (tls_bufed_input,
                res, &error);
    switch (size) {
    case -1:
        g_critical ("Buffered input stream fill failed: %s",
                    error->message);
        g_clear_error (&error);
    case 0:
        goto closed;
    }

    header = g_buffered_input_stream_peek_buffer (tls_bufed_input, &len);
    if (hev_protocol_header_is_valid (header)) {
        GInputStream *tls_input = NULL;

        tls_input = g_filter_input_stream_get_base_stream (
                    G_FILTER_INPUT_STREAM (tls_bufed_input));
        g_input_stream_skip_async (tls_input,
                    header->length - HEV_PROTO_HEADER_REAL_SIZE,
                    G_PRIORITY_DEFAULT, cdat->cancellable,
                    input_stream_skip_async_handler,
                    cdat);
    } else {
        GOutputStream *tls_output = NULL;
        const gchar *buffer = NULL;
        gsize count = 0;

        tls_output = g_io_stream_get_output_stream (cdat->tls_stream);
        buffer = hev_protocol_get_invalid_message (&count);
        g_output_stream_write_async (tls_output, buffer, count,
                    G_PRIORITY_DEFAULT, cdat->cancellable,
                    output_stream_write_async_handler,
                    cdat);
    }

    g_object_unref (tls_bufed_input);

    return;

closed:
    g_object_unref (tls_bufed_input);
    g_object_get (cdat->tls_stream,
                "base-io-stream", &tls_base,
                NULL);
    g_io_stream_close_async (tls_base,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_object_unref (cdat->tls_stream);
    cdat->tls_stream = tls_base;

    return;
}

static void
input_stream_skip_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServerClientData *cdat = user_data;
    HevServerPrivate *priv = HEV_SERVER_GET_PRIVATE (cdat->self);
    GIOStream *tls_base = NULL;
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

    g_socket_client_connect_to_host_async (priv->client,
                priv->target_addr, priv->target_port, cdat->cancellable,
                socket_client_connect_to_host_async_handler,
                cdat);

    return;

closed:
    g_object_get (cdat->tls_stream,
                "base-io-stream", &tls_base,
                NULL);
    g_io_stream_close_async (tls_base,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_object_unref (cdat->tls_stream);
    cdat->tls_stream = tls_base;

    return;
}

static void
output_stream_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServerClientData *cdat = user_data;
    GIOStream *tls_base = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_output_stream_write_finish (G_OUTPUT_STREAM (source_object),
                res, NULL);

    g_object_get (cdat->tls_stream,
                "base-io-stream", &tls_base,
                NULL);
    g_io_stream_close_async (tls_base,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_object_unref (cdat->tls_stream);
    cdat->tls_stream = tls_base;
}

static void
socket_client_connect_to_host_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServerClientData *cdat = user_data;
    GSocketConnection *conn = NULL;
    GIOStream *tls_base = NULL;
    GSocket *sock = NULL;
    GSource *sock_src = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    conn = g_socket_client_connect_to_host_finish (
                G_SOCKET_CLIENT (source_object), res, &error);
    if (!conn) {
        g_critical ("Connect to target host failed: %s", error->message);
        g_clear_error (&error);
        goto connect_fail;
    }
    cdat->tgt_stream = G_IO_STREAM (conn);

    sock = g_socket_connection_get_socket (conn);
    sock_src = g_socket_create_source (sock, G_IO_IN, NULL);
    if (!sock_src) {
        g_critical ("Create socket source failed!");
        goto sock_src_fail;
    }
    g_source_set_callback (sock_src,
                (GSourceFunc) socket_source_handler,
                cdat, NULL);
    g_source_set_priority (sock_src, G_PRIORITY_LOW);
    g_source_attach (sock_src, g_main_loop_get_context (cdat->loop));
    g_source_unref (sock_src);
    
    g_io_stream_splice_async (cdat->tgt_stream, cdat->tls_stream,
                G_IO_STREAM_SPLICE_NONE, G_PRIORITY_DEFAULT,
                cdat->cancellable, io_stream_splice_async_handler,
                cdat);

    return;

sock_src_fail:
    g_io_stream_close_async (cdat->tgt_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
connect_fail:
    g_object_get (cdat->tls_stream,
                "base-io-stream", &tls_base,
                NULL);
    g_io_stream_close_async (tls_base,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_object_unref (cdat->tls_stream);
    cdat->tls_stream = tls_base;

    return;
}

static void
io_stream_splice_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServerClientData *cdat = user_data;
    GIOStream *tls_base = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!g_io_stream_splice_finish (res, &error)) {
        g_debug ("Splice tls and target stream failed: %s", error->message);
        g_clear_error (&error);
    }

    g_object_get (cdat->tls_stream,
                "base-io-stream", &tls_base,
                NULL);
    g_io_stream_close_async (tls_base,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
    g_object_unref (cdat->tls_stream);
    cdat->tls_stream = tls_base;
    g_io_stream_close_async (cdat->tgt_stream,
                G_PRIORITY_DEFAULT, NULL,
                io_stream_close_async_handler,
                cdat);
}

static void
io_stream_close_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServerClientData *cdat = user_data;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_io_stream_close_finish (G_IO_STREAM (source_object),
                res, NULL);

    if (G_IO_STREAM (source_object) == cdat->tls_stream)
      cdat->tls_stream = NULL;
    else if (G_IO_STREAM (source_object) == cdat->tgt_stream)
      cdat->tgt_stream = NULL;
    g_object_unref (source_object);

    if (!cdat->tls_stream && !cdat->tgt_stream)
      g_main_loop_quit (cdat->loop);
}


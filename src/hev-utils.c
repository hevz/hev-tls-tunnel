/*
 ============================================================================
 Name        : hev-utils.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#include "hev-utils.h"

#define HEV_SOCKET_IO_STREAM_SPLICE_BUFFER_SIZE 4096

typedef enum _HevSocketIOStreamSpliceStatus HevSocketIOStreamSpliceStatus;

enum _HevSocketIOStreamSpliceStatus
{
    HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL = 0,
    HEV_SOCKET_IO_STREAM_SPLICE_STATUS_READING,
    HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END
};

typedef struct _HevSocketIOStreamSpliceData HevSocketIOStreamSpliceData;

struct _HevSocketIOStreamSpliceData
{
    GSocket *sock1;
    GIOStream *stream1;
    GSocket *sock2;
    GIOStream *stream2;
    GCancellable *cancellable;
    gint io_priority;
    HevSocketIOStreamSplicePrereadFunc preread_callback;
    HevSocketIOStreamSplicePrewriteFunc prewrite_callback;
    gpointer callback_data;

    GSource *sock1_src;
    GSource *sock2_src;

    HevSocketIOStreamSpliceStatus s1_status;
    HevSocketIOStreamSpliceStatus s2_status;

    guint8 buffer1[HEV_SOCKET_IO_STREAM_SPLICE_BUFFER_SIZE];
    guint8 buffer2[HEV_SOCKET_IO_STREAM_SPLICE_BUFFER_SIZE];
    
    gsize buffer1_curr;
    gssize buffer1_size;
    gsize buffer2_curr;
    gssize buffer2_size;
};

static void hev_socket_io_stream_splice_data_free (HevSocketIOStreamSpliceData *data);
static gboolean hev_socket_io_stream_splice_sock1_source_handler (GSocket *socket,
            GIOCondition condition, gpointer user_data);
static gboolean hev_socket_io_stream_splice_sock2_source_handler (GSocket *socket,
            GIOCondition condition, gpointer user_data);
static void hev_socket_io_stream_splice_stream1_read_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void hev_socket_io_stream_splice_stream2_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void hev_socket_io_stream_splice_stream2_read_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);
static void hev_socket_io_stream_splice_stream1_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data);

void
hev_socket_io_stream_splice_async (GSocket *sock1, GIOStream *stream1,
            GSocket *sock2, GIOStream *stream2, gint io_priority,
            HevSocketIOStreamSplicePrereadFunc preread_callback,
            HevSocketIOStreamSplicePrewriteFunc prewrite_callback,
            gpointer callback_data, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data)
{
    GSimpleAsyncResult *simple = NULL;
    HevSocketIOStreamSpliceData *data = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    data = g_slice_new0 (HevSocketIOStreamSpliceData);
    data->sock1 = g_object_ref (sock1);
    data->stream1 = g_object_ref (stream1);
    data->sock2 = g_object_ref (sock2);
    data->stream2 = g_object_ref (stream2);
    if (cancellable)
      data->cancellable = g_object_ref (cancellable);
    data->io_priority = io_priority;
    data->preread_callback = preread_callback;
    data->prewrite_callback = prewrite_callback;
    data->callback_data = callback_data;
    
    simple = g_simple_async_result_new (G_OBJECT (sock1),
                callback, user_data, hev_socket_io_stream_splice_async);
    g_simple_async_result_set_check_cancellable (simple, cancellable);
    g_simple_async_result_set_op_res_gpointer (simple, data,
                (GDestroyNotify) hev_socket_io_stream_splice_data_free);

    /* sock1 */
    data->sock1_src = g_socket_create_source (sock1, G_IO_IN, cancellable);
    g_source_set_callback (data->sock1_src,
                (GSourceFunc) hev_socket_io_stream_splice_sock1_source_handler,
                g_object_ref (simple), (GDestroyNotify) g_object_unref);
    g_source_set_priority (data->sock1_src, io_priority);
    g_source_attach (data->sock1_src, NULL);
    g_source_unref (data->sock1_src);

    /* sock2 */
    data->sock2_src = g_socket_create_source (sock2, G_IO_IN, cancellable);
    g_source_set_callback (data->sock2_src,
                (GSourceFunc) hev_socket_io_stream_splice_sock2_source_handler,
                g_object_ref (simple), (GDestroyNotify) g_object_unref);
    g_source_set_priority (data->sock2_src, io_priority);
    g_source_attach (data->sock2_src, NULL);
    g_source_unref (data->sock2_src);

    g_object_unref (simple);
}

gboolean
hev_socket_io_stream_splice_finish (GAsyncResult *result,
            GError **error)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                    error))
      return FALSE;

    return TRUE;
}

static void
hev_socket_io_stream_splice_data_free (HevSocketIOStreamSpliceData *data)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_object_unref (data->sock1);
    g_object_unref (data->stream1);
    g_object_unref (data->sock2);
    g_object_unref (data->stream2);
    if (data->cancellable)
      g_object_unref (data->cancellable);
    g_slice_free (HevSocketIOStreamSpliceData, data);
}

static gboolean
hev_socket_io_stream_splice_sock1_source_handler (GSocket *socket,
            GIOCondition condition,
            gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevSocketIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL == data->s1_status) {
        GInputStream *in = NULL;
        gpointer buffer = data->buffer1;
        gssize len = HEV_SOCKET_IO_STREAM_SPLICE_BUFFER_SIZE;

        data->buffer1_curr = 0;
        data->s1_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_READING;
        in = g_io_stream_get_input_stream (data->stream1);
        if (data->preread_callback)
          data->preread_callback (socket, data->stream1,
                      buffer, len, &buffer, &len, data->callback_data);
        g_input_stream_read_async (in, buffer, len,
                    data->io_priority, data->cancellable,
                    hev_socket_io_stream_splice_stream1_read_async_handler,
                    g_object_ref (simple));
    }

    return G_SOURCE_REMOVE;
}

static gboolean
hev_socket_io_stream_splice_sock2_source_handler (GSocket *socket,
            GIOCondition condition,
            gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevSocketIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL == data->s2_status) {
        GInputStream *in = NULL;
        gpointer buffer = data->buffer2;
        gssize len = HEV_SOCKET_IO_STREAM_SPLICE_BUFFER_SIZE;

        data->buffer2_curr = 0;
        data->s2_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_READING;
        in = g_io_stream_get_input_stream (data->stream2);
        if (data->preread_callback)
          data->preread_callback (socket, data->stream2,
                      buffer, len, &buffer, &len, data->callback_data);
        g_input_stream_read_async (in, buffer, len,
                    data->io_priority, data->cancellable,
                    hev_socket_io_stream_splice_stream2_read_async_handler,
                    g_object_ref (simple));
    }

    return G_SOURCE_REMOVE;
}

static void
hev_socket_io_stream_splice_stream1_read_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevSocketIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    data->buffer1_size = g_input_stream_read_finish (G_INPUT_STREAM (source_object),
                res, &error);
    if (0 >= data->buffer1_size) {
        if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END != data->s2_status) {
            data->s1_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END;
            if (error)
              g_simple_async_result_take_error (simple, error);
            g_simple_async_result_complete (simple);
            if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL == data->s2_status)
              g_source_destroy (data->sock2_src);
            g_object_unref (simple);
        } else if (g_cancellable_is_cancelled (data->cancellable))
          g_object_unref (simple);
    } else {
        GOutputStream *out = g_io_stream_get_output_stream (data->stream2);
        gpointer buffer = data->buffer1;
        if (data->prewrite_callback)
          data->prewrite_callback (data->sock2, data->stream2,
                      buffer, data->buffer1_size,
                      &buffer, &data->buffer1_size,
                      data->callback_data);
        g_output_stream_write_async (out, buffer, data->buffer1_size,
                    data->io_priority, data->cancellable,
                    hev_socket_io_stream_splice_stream2_write_async_handler,
                    simple);
    }
}

static void
hev_socket_io_stream_splice_stream2_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevSocketIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    gssize size = 0;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    size = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object),
                res, &error);
    if (-1 == size) {
        if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END != data->s2_status) {
            data->s1_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END;
            g_simple_async_result_take_error (simple, error);
            g_simple_async_result_complete (simple);
            if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL == data->s2_status)
              g_source_destroy (data->sock2_src);
            g_object_unref (simple);
        } else if (g_cancellable_is_cancelled (data->cancellable))
          g_object_unref (simple);
    } else {
        data->buffer1_curr += size;
        if (data->buffer1_curr < data->buffer1_size) {
            g_output_stream_write_async (G_OUTPUT_STREAM (source_object),
                        data->buffer1 + data->buffer1_curr,
                        data->buffer1_size - data->buffer1_curr,
                        data->io_priority, data->cancellable,
                        hev_socket_io_stream_splice_stream2_write_async_handler,
                        simple);
        } else {
            data->s1_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL;

            data->sock1_src = g_socket_create_source (data->sock1, G_IO_IN,
                        data->cancellable);
            g_source_set_callback (data->sock1_src,
                        (GSourceFunc) hev_socket_io_stream_splice_sock1_source_handler,
                        simple, (GDestroyNotify) g_object_unref);
            g_source_set_priority (data->sock1_src, data->io_priority);
            g_source_attach (data->sock1_src, NULL);
            g_source_unref (data->sock1_src);
        }
    }
}

static void
hev_socket_io_stream_splice_stream2_read_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevSocketIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    data->buffer2_size = g_input_stream_read_finish (G_INPUT_STREAM (source_object),
                res, &error);
    if (0 >= data->buffer2_size) {
        if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END != data->s1_status) {
            data->s2_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END;
            if (error)
              g_simple_async_result_take_error (simple, error);
            g_simple_async_result_complete (simple);
            if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL == data->s1_status)
              g_source_destroy (data->sock1_src);
            g_object_unref (simple);
        } else if (g_cancellable_is_cancelled (data->cancellable))
          g_object_unref (simple);
    } else {
        GOutputStream *out = g_io_stream_get_output_stream (data->stream1);
        gpointer buffer = data->buffer2;
        if (data->prewrite_callback)
          data->prewrite_callback (data->sock1, data->stream1,
                      buffer, data->buffer2_size,
                      &buffer, &data->buffer2_size,
                      data->callback_data);
        g_output_stream_write_async (out, buffer, data->buffer2_size,
                    data->io_priority, data->cancellable,
                    hev_socket_io_stream_splice_stream1_write_async_handler,
                    simple);
    }
}

static void
hev_socket_io_stream_splice_stream1_write_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevSocketIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    gssize size = 0;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    size = g_output_stream_write_finish (G_OUTPUT_STREAM (source_object),
                res, &error);
    if (-1 == size) {
        if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END != data->s1_status) {
            data->s2_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_END;
            g_simple_async_result_take_error (simple, error);
            g_simple_async_result_complete (simple);
            if (HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL == data->s1_status)
              g_source_destroy (data->sock1_src);
            g_object_unref (simple);
        } else if (g_cancellable_is_cancelled (data->cancellable))
          g_object_unref (simple);
    } else {
        data->buffer2_curr += size;
        if (data->buffer2_curr < data->buffer2_size) {
            g_output_stream_write_async (G_OUTPUT_STREAM (source_object),
                        data->buffer2 + data->buffer2_curr,
                        data->buffer2_size - data->buffer2_curr,
                        data->io_priority, data->cancellable,
                        hev_socket_io_stream_splice_stream1_write_async_handler,
                        simple);
        } else {
            data->s2_status = HEV_SOCKET_IO_STREAM_SPLICE_STATUS_NULL;

            data->sock2_src = g_socket_create_source (data->sock2, G_IO_IN,
                        data->cancellable);
            g_source_set_callback (data->sock2_src,
                        (GSourceFunc) hev_socket_io_stream_splice_sock2_source_handler,
                        simple, (GDestroyNotify) g_object_unref);
            g_source_set_priority (data->sock2_src, data->io_priority);
            g_source_attach (data->sock2_src, NULL);
            g_source_unref (data->sock2_src);
        }
    }
}


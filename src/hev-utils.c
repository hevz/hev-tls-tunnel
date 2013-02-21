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

#define HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE 4096

typedef struct _HevPollableIOStreamSpliceData HevPollableIOStreamSpliceData;

struct _HevPollableIOStreamSpliceData
{
    GIOStream *stream1;
    GIOStream *stream2;
    GCancellable *cancellable;
    gint io_priority;
    HevPollableIOStreamSplicePrereadFunc preread_callback;
    HevPollableIOStreamSplicePrewriteFunc prewrite_callback;
    gpointer callback_data;

    GSource *s1i_src;
    GSource *s2o_src;
    GSource *s2i_src;
    GSource *s1o_src;

    guint8 buffer1[HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE];
    guint8 buffer2[HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE];
    
    gsize buffer1_curr;
    gssize buffer1_size;
    gsize buffer2_curr;
    gssize buffer2_size;
};

static void hev_pollable_io_stream_splice_data_free (HevPollableIOStreamSpliceData *data);
static gboolean hev_pollable_io_stream_splice_stream1_input_source_handler (GObject *istream,
            gpointer user_data);
static gboolean hev_pollable_io_stream_splice_stream2_output_source_handler (GObject *ostream,
            gpointer user_data);
static gboolean hev_pollable_io_stream_splice_stream2_input_source_handler (GObject *istream,
            gpointer user_data);
static gboolean hev_pollable_io_stream_splice_stream1_output_source_handler (GObject *ostream,
            gpointer user_data);

void
hev_pollable_io_stream_splice_async (GIOStream *stream1,
            GIOStream *stream2, gint io_priority,
            HevPollableIOStreamSplicePrereadFunc preread_callback,
            HevPollableIOStreamSplicePrewriteFunc prewrite_callback,
            gpointer callback_data, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data)
{
    GSimpleAsyncResult *simple = NULL;
    HevPollableIOStreamSpliceData *data = NULL;
    GInputStream *istream = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    data = g_slice_new0 (HevPollableIOStreamSpliceData);
    data->stream1 = g_object_ref (stream1);
    data->stream2 = g_object_ref (stream2);
    if (cancellable)
      data->cancellable = g_object_ref (cancellable);
    data->io_priority = io_priority;
    data->preread_callback = preread_callback;
    data->prewrite_callback = prewrite_callback;
    data->callback_data = callback_data;
    
    simple = g_simple_async_result_new (G_OBJECT (stream1),
                callback, user_data, hev_pollable_io_stream_splice_async);
    g_simple_async_result_set_check_cancellable (simple, cancellable);
    g_simple_async_result_set_op_res_gpointer (simple, data,
                (GDestroyNotify) hev_pollable_io_stream_splice_data_free);

    /* stream1 */
    istream = g_io_stream_get_input_stream (stream1);
    data->s1i_src = g_pollable_input_stream_create_source (
                G_POLLABLE_INPUT_STREAM (istream), cancellable);
    g_source_set_callback (data->s1i_src,
                (GSourceFunc) hev_pollable_io_stream_splice_stream1_input_source_handler,
                g_object_ref (simple), (GDestroyNotify) g_object_unref);
    g_source_set_priority (data->s1i_src, io_priority);
    g_source_attach (data->s1i_src, NULL);
    g_source_unref (data->s1i_src);

    /* stream2 */
    istream = g_io_stream_get_input_stream (stream2);
    data->s2i_src = g_pollable_input_stream_create_source (
                G_POLLABLE_INPUT_STREAM (istream), cancellable);
    g_source_set_callback (data->s2i_src,
                (GSourceFunc) hev_pollable_io_stream_splice_stream2_input_source_handler,
                g_object_ref (simple), (GDestroyNotify) g_object_unref);
    g_source_set_priority (data->s2i_src, io_priority);
    g_source_attach (data->s2i_src, NULL);
    g_source_unref (data->s2i_src);

    g_object_unref (simple);
}

gboolean
hev_pollable_io_stream_splice_finish (GAsyncResult *result,
            GError **error)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                    error))
      return FALSE;

    return TRUE;
}

static void
hev_pollable_io_stream_splice_data_free (HevPollableIOStreamSpliceData *data)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_object_unref (data->stream1);
    g_object_unref (data->stream2);
    if (data->cancellable)
      g_object_unref (data->cancellable);
    g_slice_free (HevPollableIOStreamSpliceData, data);
}

static gboolean
hev_pollable_io_stream_splice_stream1_input_source_handler (GObject *istream,
            gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevPollableIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (g_pollable_input_stream_is_readable (G_POLLABLE_INPUT_STREAM (istream))) {
        gpointer buffer = data->buffer1;
        gssize len = HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE;

        data->buffer1_curr = 0;
        if (data->preread_callback)
          data->preread_callback (data->stream1,
                      buffer, len, &buffer, &len, data->callback_data);
        data->buffer1_size = g_pollable_input_stream_read_nonblocking (
                    G_POLLABLE_INPUT_STREAM (istream), buffer, len,
                    data->cancellable, &error);
        if (0 >= data->buffer1_size) {
            goto fail;
        } else {
            GOutputStream *ostream = NULL;

            ostream = g_io_stream_get_output_stream (data->stream2);
            data->s2o_src = g_pollable_output_stream_create_source (
                        G_POLLABLE_OUTPUT_STREAM (ostream), data->cancellable);
            g_source_set_callback (data->s2o_src,
                        (GSourceFunc) hev_pollable_io_stream_splice_stream2_output_source_handler,
                        g_object_ref (simple), (GDestroyNotify) g_object_unref);
            g_source_set_priority (data->s2o_src, data->io_priority);
            g_source_attach (data->s2o_src, NULL);
            g_source_unref (data->s2o_src);
        }

        goto remove;
    } else {
        if (g_cancellable_is_cancelled (data->cancellable)) {
            error = g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                        "Operation was cancelled.");
            goto fail;
        }
    }

    return G_SOURCE_CONTINUE;

fail:
    if (error)
      g_simple_async_result_take_error (simple, error);
    g_simple_async_result_complete (simple);
    if (data->s2i_src) {
        g_source_destroy (data->s2i_src);
        data->s2i_src = NULL;
    }
    if (data->s1o_src) {
        g_source_destroy (data->s1o_src);
        data->s1o_src = NULL;
    }

remove:
    data->s1i_src = NULL;
    return G_SOURCE_REMOVE;
}

static gboolean
hev_pollable_io_stream_splice_stream2_output_source_handler (GObject *ostream,
            gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevPollableIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (g_pollable_output_stream_is_writable (G_POLLABLE_OUTPUT_STREAM (ostream))) {
        gpointer buffer = data->buffer1;
        gssize size = 0;

        if (data->prewrite_callback)
          data->prewrite_callback (data->stream2,
                      buffer, data->buffer1_size,
                      &buffer, &data->buffer1_size,
                      data->callback_data);

        size = g_pollable_output_stream_write_nonblocking (
                    G_POLLABLE_OUTPUT_STREAM (ostream),
                    buffer, data->buffer1_size, data->cancellable,
                    &error);
        if (0 >= size) {
            goto fail;
        } else {
            data->buffer1_curr += size;
            if (data->buffer1_curr < data->buffer1_size) {
                return G_SOURCE_CONTINUE;
            } else {
                GInputStream *istream = NULL;

                istream = g_io_stream_get_input_stream (data->stream1);
                data->s1i_src = g_pollable_input_stream_create_source (
                            G_POLLABLE_INPUT_STREAM (istream), data->cancellable);
                g_source_set_callback (data->s1i_src,
                            (GSourceFunc) hev_pollable_io_stream_splice_stream1_input_source_handler,
                            g_object_ref (simple), (GDestroyNotify) g_object_unref);
                g_source_set_priority (data->s1i_src, data->io_priority);
                g_source_attach (data->s1i_src, NULL);
                g_source_unref (data->s1i_src);
            }
        }

        goto remove;
    } else {
        if (g_cancellable_is_cancelled (data->cancellable)) {
            error = g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                        "Operation was cancelled.");
            goto fail;
        }
    }

    return G_SOURCE_CONTINUE;

fail:
    if (error)
      g_simple_async_result_take_error (simple, error);
    g_simple_async_result_complete (simple);
    if (data->s2i_src) {
        g_source_destroy (data->s2i_src);
        data->s2i_src = NULL;
    }
    if (data->s1o_src) {
        g_source_destroy (data->s1o_src);
        data->s1o_src = NULL;
    }

remove:
    data->s2o_src = NULL;
    return G_SOURCE_REMOVE;
}

static gboolean
hev_pollable_io_stream_splice_stream2_input_source_handler (GObject *istream,
            gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevPollableIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (g_pollable_input_stream_is_readable (G_POLLABLE_INPUT_STREAM (istream))) {
        gpointer buffer = data->buffer2;
        gssize len = HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE;

        data->buffer2_curr = 0;
        if (data->preread_callback)
          data->preread_callback (data->stream2,
                      buffer, len, &buffer, &len, data->callback_data);
        data->buffer2_size = g_pollable_input_stream_read_nonblocking (
                    G_POLLABLE_INPUT_STREAM (istream), buffer, len,
                    data->cancellable, &error);
        if (0 >= data->buffer2_size) {
            goto fail;
        } else {
            GOutputStream *ostream = NULL;

            ostream = g_io_stream_get_output_stream (data->stream1);
            data->s1o_src = g_pollable_output_stream_create_source (
                        G_POLLABLE_OUTPUT_STREAM (ostream), data->cancellable);
            g_source_set_callback (data->s1o_src,
                        (GSourceFunc) hev_pollable_io_stream_splice_stream1_output_source_handler,
                        g_object_ref (simple), (GDestroyNotify) g_object_unref);
            g_source_set_priority (data->s1o_src, data->io_priority);
            g_source_attach (data->s1o_src, NULL);
            g_source_unref (data->s1o_src);
        }

        goto remove;
    } else {
        if (g_cancellable_is_cancelled (data->cancellable)) {
            error = g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                        "Operation was cancelled.");
            goto fail;
        }
    }

    return G_SOURCE_CONTINUE;

fail:
    if (error)
      g_simple_async_result_take_error (simple, error);
    g_simple_async_result_complete (simple);
    if (data->s1i_src) {
        g_source_destroy (data->s1i_src);
        data->s1i_src = NULL;
    }
    if (data->s2o_src) {
        g_source_destroy (data->s2o_src);
        data->s2o_src = NULL;
    }

remove:
    data->s2i_src = NULL;
    return G_SOURCE_REMOVE;
}

static gboolean
hev_pollable_io_stream_splice_stream1_output_source_handler (GObject *ostream,
            gpointer user_data)
{
    GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
    HevPollableIOStreamSpliceData *data =
        g_simple_async_result_get_op_res_gpointer (simple);
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (g_pollable_output_stream_is_writable (G_POLLABLE_OUTPUT_STREAM (ostream))) {
        gpointer buffer = data->buffer2;
        gssize size = 0;

        if (data->prewrite_callback)
          data->prewrite_callback (data->stream1,
                      buffer, data->buffer2_size,
                      &buffer, &data->buffer2_size,
                      data->callback_data);

        size = g_pollable_output_stream_write_nonblocking (
                    G_POLLABLE_OUTPUT_STREAM (ostream),
                    buffer, data->buffer2_size, data->cancellable,
                    &error);
        if (0 >= size) {
            goto fail;
        } else {
            data->buffer2_curr += size;
            if (data->buffer2_curr < data->buffer2_size) {
                return G_SOURCE_CONTINUE;
            } else {
                GInputStream *istream = NULL;

                istream = g_io_stream_get_input_stream (data->stream2);
                data->s2i_src = g_pollable_input_stream_create_source (
                            G_POLLABLE_INPUT_STREAM (istream), data->cancellable);
                g_source_set_callback (data->s2i_src,
                            (GSourceFunc) hev_pollable_io_stream_splice_stream2_input_source_handler,
                            g_object_ref (simple), (GDestroyNotify) g_object_unref);
                g_source_set_priority (data->s2i_src, data->io_priority);
                g_source_attach (data->s2i_src, NULL);
                g_source_unref (data->s2i_src);
            }
        }

        goto remove;
    } else {
        if (g_cancellable_is_cancelled (data->cancellable)) {
            error = g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                        "Operation was cancelled.");
            goto fail;
        }
    }

    return G_SOURCE_CONTINUE;

fail:
    if (error)
      g_simple_async_result_take_error (simple, error);
    g_simple_async_result_complete (simple);
    if (data->s1i_src) {
        g_source_destroy (data->s1i_src);
        data->s1i_src = NULL;
    }
    if (data->s2o_src) {
        g_source_destroy (data->s2o_src);
        data->s2o_src = NULL;
    }

remove:
    data->s1o_src = NULL;
    return G_SOURCE_REMOVE;
}


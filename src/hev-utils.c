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

#ifdef USE_SSE
#ifndef USE_MEM_ALIGN
#define USE_MEM_ALIGN
#endif /* !USE_MEM_ALIGN */
#endif /* USE_SSE */

#define HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE 4096

typedef struct _HevPollableIOStreamSpliceData HevPollableIOStreamSpliceData;
typedef struct _HevSpliceThread HevSpliceThread;

struct _HevPollableIOStreamSpliceData
{
    GIOStream *stream1;
    GIOStream *stream2;
    GCancellable *cancellable;
    GMainContext *context;
    gint io_priority;
    HevPollableIOStreamSplicePrereadFunc preread_callback;
    HevPollableIOStreamSplicePrewriteFunc prewrite_callback;
    gpointer callback_data;

    GSource *s1i_src;
    GSource *s2o_src;
    GSource *s2i_src;
    GSource *s1o_src;

    guint8 buffer_1[HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE];
    guint8 buffer_2[HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE];
    
    gpointer buffer1;
    gsize buffer1_curr;
    gssize buffer1_size;
    gssize buffer1_len;
    gpointer buffer2;
    gsize buffer2_curr;
    gssize buffer2_size;
    gssize buffer2_len;
};

struct _HevSpliceThread
{
    GThread *thread;
    GMainLoop *main_loop;
    gsize task_count;
};

struct _HevSpliceThreadPool
{
    gsize threads;
    HevSpliceThread *thread_list;
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
#ifdef USE_MEM_ALIGN
static inline gpointer hev_mem_align (gpointer p, gsize align);
#endif /* USE_MEM_ALIGN */

static gboolean
idle_attach_handler (gpointer user_data)
{
    HevPollableIOStreamSpliceData *data = user_data;

    g_source_attach (data->s1i_src, data->context);
    g_source_unref (data->s1i_src);
    g_source_attach (data->s2i_src, data->context);
    g_source_unref (data->s2i_src);

    return G_SOURCE_REMOVE;
}

void
hev_pollable_io_stream_splice_async (GIOStream *stream1,
            GIOStream *stream2, gint io_priority, GMainContext *context,
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
    if (context)
      data->context = g_main_context_ref (context);
    data->preread_callback = preread_callback;
    data->prewrite_callback = prewrite_callback;
    data->callback_data = callback_data;

#ifdef USE_MEM_ALIGN
    data->buffer1 = hev_mem_align (data->buffer_1, 16);
    data->buffer1_len = HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE -
        (data->buffer1 - (gpointer) data->buffer_1);
    data->buffer2 = hev_mem_align (data->buffer_2, 16);
    data->buffer2_len = HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE -
        (data->buffer2 - (gpointer) data->buffer_2);
#else
    data->buffer1 = data->buffer_1;
    data->buffer1_len = HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE;
    data->buffer2 = data->buffer_2;
    data->buffer2_len = HEV_POLLABLE_IO_STREAM_SPLICE_BUFFER_SIZE;
#endif /* USE_MEM_ALIGN */
    
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

    /* stream2 */
    istream = g_io_stream_get_input_stream (stream2);
    data->s2i_src = g_pollable_input_stream_create_source (
                G_POLLABLE_INPUT_STREAM (istream), cancellable);
    g_source_set_callback (data->s2i_src,
                (GSourceFunc) hev_pollable_io_stream_splice_stream2_input_source_handler,
                g_object_ref (simple), (GDestroyNotify) g_object_unref);
    g_source_set_priority (data->s2i_src, io_priority);

    /* attach in target main context */
    GSource *src = g_idle_source_new ();
    g_source_set_callback (src, idle_attach_handler, data, NULL);
    g_source_attach (src, context);
    g_source_unref (src);

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
    if (data->context)
      g_main_context_unref (data->context);
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
        gssize len = data->buffer1_len;

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
            g_source_attach (data->s2o_src, data->context);
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
    simple = g_object_ref (simple);
    if (data->s2i_src) {
        g_source_destroy (data->s2i_src);
        data->s2i_src = NULL;
    }
    if (data->s1o_src) {
        g_source_destroy (data->s1o_src);
        data->s1o_src = NULL;
    }
    g_simple_async_result_complete_in_idle (simple);
    g_object_unref (simple);

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
                g_source_attach (data->s1i_src, data->context);
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
    simple = g_object_ref (simple);
    if (data->s2i_src) {
        g_source_destroy (data->s2i_src);
        data->s2i_src = NULL;
    }
    if (data->s1o_src) {
        g_source_destroy (data->s1o_src);
        data->s1o_src = NULL;
    }
    g_simple_async_result_complete_in_idle (simple);
    g_object_unref (simple);

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
        gssize len = data->buffer2_len;

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
            g_source_attach (data->s1o_src, data->context);
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
    simple = g_object_ref (simple);
    if (data->s1i_src) {
        g_source_destroy (data->s1i_src);
        data->s1i_src = NULL;
    }
    if (data->s2o_src) {
        g_source_destroy (data->s2o_src);
        data->s2o_src = NULL;
    }
    g_simple_async_result_complete_in_idle (simple);
    g_object_unref (simple);

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
                g_source_attach (data->s2i_src, data->context);
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
    simple = g_object_ref (simple);
    if (data->s1i_src) {
        g_source_destroy (data->s1i_src);
        data->s1i_src = NULL;
    }
    if (data->s2o_src) {
        g_source_destroy (data->s2o_src);
        data->s2o_src = NULL;
    }
    g_simple_async_result_complete_in_idle (simple);
    g_object_unref (simple);

remove:
    data->s1o_src = NULL;
    return G_SOURCE_REMOVE;
}

HevSpliceThreadPool *
hev_splice_thread_pool_new (gsize threads)
{
    HevSpliceThreadPool *self = NULL;
    gsize i = 0;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    self = g_slice_new0 (HevSpliceThreadPool);
    if (!self)
      goto self_fail;
    self->threads = threads;
    self->thread_list = g_slice_alloc0 (sizeof (HevSpliceThread) * threads);
    if (!self->thread_list)
      goto thread_list_fail;
    for (i=0; i<threads; i++) {
        GMainContext *context = NULL;

        context = g_main_context_new ();
        self->thread_list[i].main_loop = g_main_loop_new (context, FALSE);
        g_main_context_unref (context);
        
        self->thread_list[i].thread = g_thread_try_new ("splice worker",
                    (GThreadFunc) g_main_loop_run, self->thread_list[i].main_loop,
                    NULL);
        if (!self->thread_list[i].main_loop ||
                    !self->thread_list[i].thread)
          goto threads_fail;
    }

    return self;

threads_fail:
    for (i=0; i<threads; i++) {
        if (self->thread_list[i].main_loop) {
            if (self->thread_list[i].thread) {
                g_main_loop_quit (self->thread_list[i].main_loop);
                g_thread_join (self->thread_list[i].thread);
            }
            g_main_loop_unref (self->thread_list[i].main_loop);
        }
    }
    g_slice_free1 (sizeof (HevSpliceThread) * threads, self->thread_list);
thread_list_fail:
    g_slice_free (HevSpliceThreadPool, self);
self_fail:
    return NULL;
}

void
hev_splice_thread_pool_free (HevSpliceThreadPool *self)
{
    gsize i = 0;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_if_fail (NULL != self);

    for (i=0; i<self->threads; i++) {
        if (self->thread_list[i].main_loop) {
            if (self->thread_list[i].thread) {
                g_main_loop_quit (self->thread_list[i].main_loop);
                g_thread_join (self->thread_list[i].thread);
            }
            g_main_loop_unref (self->thread_list[i].main_loop);
        }
    }
    g_slice_free1 (sizeof (HevSpliceThread) * self->threads, self->thread_list);

    g_slice_free (HevSpliceThreadPool, self);
}

GMainContext *
hev_splice_thread_pool_request (HevSpliceThreadPool *self)
{
    gsize i = 0, j = 0, count = 0;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_val_if_fail (NULL != self, NULL);

    count = self->thread_list[0].task_count;
    for (i=1; i<self->threads; i++) {
        if (self->thread_list[i].task_count <= count) {
            j = i;
            count = self->thread_list[i].task_count;
        }
    }

    self->thread_list[j].task_count ++;
    return g_main_loop_get_context (self->thread_list[j].main_loop);
}

void
hev_splice_thread_pool_release (HevSpliceThreadPool *self,
            GMainContext *context)
{
    gsize i = 0;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_return_if_fail (NULL != self);

    for (i=0; i<self->threads; i++) {
        GMainContext *ctx = NULL;

        ctx = g_main_loop_get_context (self->thread_list[i].main_loop);
        if (ctx == context) {
            self->thread_list[i].task_count --;
            break;
        }
    }
}

#ifdef USE_SSE
static inline void
hev_bytes_xor_sse (guint8 *data, gsize size, guint8 byte)
{
    gsize i = 0, c = 0, p128 = 0, p64 = 0;
    guint64 w = (byte << 8) | byte;

    asm volatile (
        "movq %0, %%mm0\t\n"
        "pshufw $0x00, %%mm0, %%mm1\t\n"
        "movq2dq %%mm1, %%xmm0\t\n"
        "pshufd $0x00, %%xmm0, %%xmm1\t\n"
        ::"m"(w)
        :"%mm0", "%mm1", "%xmm0", "%xmm1"
    );

    c = size >> 4;
    p128 = c << 4;
    for (i=0; i<p128; i+=16) {
        asm volatile (
            "movdqa 0(%1), %%xmm0\t\n"
            "pxor %%xmm1, %%xmm0\t\n"
            "movdqa %%xmm0, %0\t\n"
            :"=m"(data[i])
            :"r"(data+i)
            :"%xmm0"
        );
    }

    c = (size - p128) >> 3;
    p64 = (c << 3) + p128;
    for (i=p128; i<p64; i+=8) {
        asm volatile (
            "movq 0(%1), %%mm0\t\n"
            "pxor %%mm1, %%mm0\t\n"
            "movq %%mm0, %0\t\n"
            :"=m"(data[i])
            :"r"(data+i)
            :"%mm0"
        );
    }

    for (i=p64; i<size; i++) {
        data[i] ^= byte;
    }
}
#else /* USE_SSE */
static inline void
hev_bytes_xor_c (guint8 *data, gsize size, guint8 byte)
{
    gsize i = 0, c = 0, p64 = 0;

    c = size >> 3;
    p64 = c << 3;
    for (i=0; i<p64; i+=8) {
        data[i+0] ^= byte;
        data[i+1] ^= byte;
        data[i+2] ^= byte;
        data[i+3] ^= byte;
        data[i+4] ^= byte;
        data[i+5] ^= byte;
        data[i+6] ^= byte;
        data[i+7] ^= byte;
    }
    for (i=p64; i<size; i++)
      data[i] ^= byte;
}
#endif /* !USE_SSE */

void
hev_bytes_xor (guint8 *data, gsize size, guint8 byte)
{
#ifdef USE_SSE
    hev_bytes_xor_sse (data, size, byte);
#else /* USE_SSE */
    hev_bytes_xor_c (data, size, byte);
#endif /* !USE_SSE */
}

#ifdef USE_MEM_ALIGN
static inline gpointer
hev_mem_align (gpointer p, gsize align)
{
    return (gpointer) (((gsize) p + align -1) & (~(align - 1)));
}
#endif /* USE_MEM_ALIGN */


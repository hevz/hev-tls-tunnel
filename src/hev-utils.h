/*
 ============================================================================
 Name        : hev-utils.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#ifndef __HEV_UTILS_H__
#define __HEV_UTILS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

typedef void (*HevSocketIOStreamSplicePrereadFunc) (GSocket *sock,
            GIOStream *stream, gpointer data, gsize size,
            gpointer *buffer, gssize *len, gpointer user_data);
typedef void (*HevSocketIOStreamSplicePrewriteFunc) (GSocket *sock,
            GIOStream *stream, gpointer data, gsize size,
            gpointer *buffer, gssize *len, gpointer user_data);

void hev_socket_io_stream_splice_async (GSocket *sock1, GIOStream *stream1,
            GSocket *sock2, GIOStream *stream2, gint io_priority,
            HevSocketIOStreamSplicePrereadFunc preread_callback,
            HevSocketIOStreamSplicePrewriteFunc prewrite_callback,
            gpointer callback_data, GCancellable *cancellable,
            GAsyncReadyCallback callback, gpointer user_data);

gboolean hev_socket_io_stream_splice_finish (GAsyncResult *result,
            GError **error);

G_END_DECLS

#endif /* __HEV_UTILS_H__ */

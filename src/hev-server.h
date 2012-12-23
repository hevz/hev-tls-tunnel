/*
 ============================================================================
 Name        : hev-server.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#ifndef __HEV_SERVER_H__
#define __HEV_SERVER_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define HEV_TYPE_SERVER (hev_server_get_type ())
#define HEV_SERVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), HEV_TYPE_SERVER, HevServer))
#define HEV_IS_SERVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HEV_TYPE_SERVER))
#define HEV_SERVER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), HEV_TYPE_SERVER, HevServerClass))
#define HEV_IS_SERVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), HEV_TYPE_SERVER))
#define HEV_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), HEV_TYPE_SERVER, HevServerClass))

typedef struct _HevServer HevServer;
typedef struct _HevServerClass HevServerClass;

struct _HevServer
{
    GObject parent_instance;
};

struct _HevServerClass
{
    GObjectClass parent_class;
};

GType hev_server_get_type (void);

void hev_server_new_async (gchar *target_addr, gint target_port,
            gchar *listen_addr, gint listen_port,
            gchar *cert_file, gchar *key_file,
            GCancellable *cancellable, GAsyncReadyCallback callback,
            gpointer user_data);
HevServer * hev_server_new_finish (GAsyncResult *res, GError **error);

G_END_DECLS

#endif /* __HEV_SERVER_H__ */


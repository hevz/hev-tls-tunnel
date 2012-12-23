/*
 ============================================================================
 Name        : hev-client.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2012 everyone.
 Description : 
 ============================================================================
 */

#ifndef __HEV_CLIENT_H__
#define __HEV_CLIENT_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define HEV_TYPE_CLIENT (hev_client_get_type ())
#define HEV_CLIENT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), HEV_TYPE_CLIENT, HevClient))
#define HEV_IS_CLIENT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HEV_TYPE_CLIENT))
#define HEV_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), HEV_TYPE_CLIENT, HevClientClass))
#define HEV_IS_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), HEV_TYPE_CLIENT))
#define HEV_CLIENT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), HEV_TYPE_CLIENT, HevClientClass))

typedef struct _HevClient HevClient;
typedef struct _HevClientClass HevClientClass;

struct _HevClient
{
    GObject parent_instance;
};

struct _HevClientClass
{
    GObjectClass parent_class;
};

GType hev_client_get_type (void);

void hev_client_new_async (gchar *target_addr, gint target_port,
            gchar *listen_addr, gint listen_port,
            GCancellable *cancellable, GAsyncReadyCallback callback,
            gpointer user_data);
HevClient * hev_client_new_finish (GAsyncResult *res, GError **error);

G_END_DECLS

#endif /* __HEV_CLIENT_H__ */


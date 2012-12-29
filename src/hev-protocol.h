/*
 ============================================================================
 Name        : hev-protocol.h
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (c) 2012 everyone.
 Description : 
 ============================================================================
 */

#ifndef __HEV_PROTOCOL_H__
#define __HEV_PROTOCOL_H__

#include <glib.h>

#define HEV_PROTO_ACTIVATE_KEY  0xA0F0ABCD
#define HEV_PROTO_MESSAGE_SIZE  (sizeof (HevProtocolMessage))

typedef struct _HevProtocolMessage HevProtocolMessage;

struct _HevProtocolMessage
{
    guint32 activate_key;
};

gboolean hev_protocol_message_set (HevProtocolMessage *self);
gboolean hev_protocol_message_is_valid (const HevProtocolMessage *self);
const gchar * hev_protocol_get_invalid_message (gsize *count);

#endif /* __HEV_PROTOCOL_H__ */


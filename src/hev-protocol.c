/*
 ============================================================================
 Name        : hev-protocol.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (c) 2012 everyone.
 Description : 
 ============================================================================
 */

#include "hev-protocol.h"

gboolean
hev_protocol_header_set (HevProtocolHeader *self, guint32 length)
{
    guint32 i = 0;
    guint8 *data = (guint8 *) self;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!self)
      return FALSE;

    self->activate_key = HEV_PROTO_ACTIVATE_KEY;
    self->length = length;

    for (i=sizeof (HevProtocolHeader); i<length; i+=sizeof (guint32))
      *(guint32 *)(data + i) = g_random_int ();

    return TRUE;
}

gboolean
hev_protocol_header_is_valid (const HevProtocolHeader *self)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!self || HEV_PROTO_ACTIVATE_KEY != self->activate_key)
      return FALSE;
    if (HEV_PROTO_HEADER_MINN_SIZE > self->length ||
            HEV_PROTO_HEADER_MAXN_SIZE < self->length)
      return FALSE;

    return TRUE;
}

const gchar *
hev_protocol_get_invalid_message (gsize *count)
{
    if (count)
      *count = HEV_PROTO_HTTP_RESPONSE_INVALID_LENGTH;

    return HEV_PROTO_HTTP_RESPONSE_INVALID;
}


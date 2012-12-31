/*
 ============================================================================
 Name        : hev-protocol.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (c) 2012 everyone.
 Description : 
 ============================================================================
 */

#include <string.h>

#include "hev-protocol.h"

gboolean
hev_protocol_header_set (HevProtocolHeader *self, guint32 length)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!self)
      return FALSE;

    self->activate_key = HEV_PROTO_ACTIVATE_KEY;
    self->length = length;

    return TRUE;
}

gboolean
hev_protocol_header_is_valid (const HevProtocolHeader *self)
{
    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    if (!self || HEV_PROTO_ACTIVATE_KEY != self->activate_key)
      return FALSE;

    return TRUE;
}

const gchar *
hev_protocol_get_invalid_message (gsize *count)
{
    gchar *message =
        "HTTP/1.1 200 OK\n"
        "Content-Type: text/plain\n"
        "Content-Length: 18\n"
        "\n"
        "Service available!";

    if (count)
      *count = strlen (message);

    return message;
}


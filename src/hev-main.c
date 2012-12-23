/*
 ============================================================================
 Name        : hev-main.c
 Author      : Heiher <admin@heiher.info>
 Version     : 0.0.1
 Copyright   : Copyright (c) 2012 everyone.
 Description : 
 ============================================================================
 */

#include <glib/gprintf.h>

#include "hev-main.h"
#include "hev-server.h"
#include "hev-client.h"

static gchar *mode = NULL;

static gchar *target_addr = NULL;
static gint target_port = 0;
static gchar *listen_addr = NULL;
static gint listen_port = 0;
static gchar *cert_file = NULL;
static gchar *key_file = NULL;

static gchar *server_addr = NULL;
static gint server_port = 0;
static gchar *local_addr = NULL;
static gint local_port = 0;

static GObject *worker = NULL;

static const GOptionEntry main_entries[] =
{
    { "mode", 'm', 0, G_OPTION_ARG_STRING, &mode, "Work mode", "server|client" },
    { NULL }
};

static const GOptionEntry server_entries[] =
{
    { "target-addr", 't', 0, G_OPTION_ARG_STRING, &target_addr,
        "Target address", NULL },
    { "target-port", 'i', 0, G_OPTION_ARG_INT, &target_port,
        "Target port", NULL },
    { "listen-addr", 'l', 0, G_OPTION_ARG_STRING, &listen_addr,
        "Listen address", NULL },
    { "listen-port", 'n', 0, G_OPTION_ARG_INT, &listen_port,
        "Listen port", NULL },
    { "cert-file", 'c', 0, G_OPTION_ARG_STRING, &cert_file,
        "Certificate file (PEM)", NULL },
    { "key-file", 'k', 0, G_OPTION_ARG_STRING, &key_file,
        "Private key file (PEM)", NULL },
    { NULL }
};

static const GOptionEntry client_entries[] =
{
    { "server-addr", 's', 0, G_OPTION_ARG_STRING, &server_addr,
        "Server address", NULL },
    { "server-port", 'p', 0, G_OPTION_ARG_INT, &server_port,
        "Server port", NULL },
    { "local-addr", 'a', 0, G_OPTION_ARG_STRING, &local_addr,
        "Local address", NULL },
    { "local-port", 'x', 0, G_OPTION_ARG_INT, &local_port,
        "Local port", NULL },
    { NULL }
};

static void
hev_server_new_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevServer *server = NULL;
    GError *error = NULL;

    server = hev_server_new_finish (res, &error);
    if (!server) {
        g_critical ("Create server failed: %s", error->message);
        g_clear_error (&error);
    }

    worker = G_OBJECT (server);
    hev_server_start (server);
}

static void
hev_client_new_async_handler (GObject *source_object,
            GAsyncResult *res, gpointer user_data)
{
    HevClient *client = NULL;
    GError *error = NULL;

    client = hev_client_new_finish (res, &error);
    if (!client) {
        g_critical ("Create client failed: %s", error->message);
        g_clear_error (&error);
    }

    worker = G_OBJECT (client);
}

int
main (int argc, char *argv[])
{
    GMainLoop *main_loop = NULL;
    GOptionContext *context = NULL;
    GOptionGroup *srv_grp = NULL;
    GOptionGroup *cli_grp = NULL;
    GError *error = NULL;

    g_debug ("%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);

    g_type_init ();

    context = g_option_context_new (" - A TLS tunnel");
    g_option_context_add_main_entries (context, main_entries, NULL);
    /* Server mode */
    srv_grp = g_option_group_new ("server", "Server mode",
                "Show server mode help options", NULL, NULL);
    g_option_group_add_entries (srv_grp, server_entries);
    g_option_context_add_group (context, srv_grp);
    /* Client mode */
    cli_grp = g_option_group_new ("client", "Client mode",
                "Show client mode help options", NULL, NULL);
    g_option_group_add_entries (cli_grp, client_entries);
    g_option_context_add_group (context, cli_grp);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_critical ("Parse options failed!");
        goto option_fail;
    }
    if (!mode || (!g_str_equal (mode, "server") &&
                !g_str_equal (mode, "client"))) {
        gchar *help = g_option_context_get_help (context, TRUE, NULL);
        g_fprintf (stderr, "%s", help);
        g_free (help);

        goto option_chk_fail;
    }
    if (g_str_equal (mode, "server") &&
            (!target_addr || !listen_addr || !cert_file || !key_file)) {
        gchar *help = g_option_context_get_help (context, FALSE, srv_grp);
        g_fprintf (stderr, "%s", help);
        g_free (help);

        goto option_chk_fail;
    }
    if (g_str_equal (mode, "client") &&
                (!server_addr || !local_addr)) {
        gchar *help = g_option_context_get_help (context, FALSE, cli_grp);
        g_fprintf (stderr, "%s", help);
        g_free (help);

        goto option_chk_fail;
    }
    g_option_context_free (context);

    main_loop = g_main_loop_new (NULL, FALSE);
    if (!main_loop) {
        g_critical ("Create main loop failed!");
        goto main_loop_fail;
    }

    /* Create worker */
    if (g_str_equal (mode, "server")) {
        hev_server_new_async (target_addr, target_port,
                    listen_addr, listen_port,
                    cert_file, key_file, NULL,
                    hev_server_new_async_handler,
                    NULL);
    } else if (g_str_equal (mode, "client")) {
        hev_client_new_async (server_addr, server_port,
                    local_addr, local_port, NULL,
                    hev_client_new_async_handler,
                    NULL);
    }

    g_main_loop_run (main_loop);

    if (worker) {
        if (g_str_equal (mode, "server")) {
            hev_server_stop (HEV_SERVER (worker));
        } else if (g_str_equal (mode, "client")) {
        }
        g_object_unref (worker);
    }
    g_main_loop_unref (main_loop);
    g_free (mode);
    g_free (target_addr);
    g_free (listen_addr);
    g_free (cert_file);
    g_free (key_file);
    g_free (server_addr);
    g_free (local_addr);

    return 0;

main_loop_fail:
option_chk_fail:
    g_free (mode);
    g_free (target_addr);
    g_free (listen_addr);
    g_free (cert_file);
    g_free (key_file);
    g_free (server_addr);
    g_free (local_addr);
option_fail:
    
    return -1;
}


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

static gchar *mode = NULL;

static gchar *target_addr = NULL;
static gint target_port = 0;
static gchar *listen_addr = NULL;
static gchar *listen_port = 0;

static gchar *server_addr = NULL;
static gint server_port = 0;
static gchar *local_addr = NULL;
static gchar *local_port = 0;

static GOptionEntry main_entries[] =
{
    { "mode", 'm', 0, G_OPTION_ARG_STRING, &mode, "Work mode", "server|client" },
    { NULL }
};

static GOptionEntry server_entries[] =
{
    { "target-addr", 't', 0, G_OPTION_ARG_STRING, &target_addr,
        "Target address", NULL },
    { "target-port", 'i', 0, G_OPTION_ARG_INT, &target_port,
        "Target port", NULL },
    { "listen-addr", 'l', 0, G_OPTION_ARG_STRING, &listen_addr,
        "Listen address", NULL },
    { "listen-port", 'n', 0, G_OPTION_ARG_INT, &listen_port,
        "Listen port", NULL },
    { NULL }
};

static GOptionEntry client_entries[] =
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

int
main (int argc, char *argv[])
{
    GMainLoop *main_loop = NULL;
    GOptionContext *context = NULL;
    GOptionGroup *srv_grp = NULL;
    GOptionGroup *cli_grp = NULL;
    GError *error = NULL;

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
    if (!mode) {
        gchar *help = g_option_context_get_help (context, TRUE, NULL);
        g_fprintf (stderr, "%s", help);
        g_free (help);

        goto option_fail;
    }
    if (g_str_equal (mode, "server") &&
                (!target_addr || !listen_addr)) {
        gchar *help = g_option_context_get_help (context, FALSE, srv_grp);
        g_fprintf (stderr, "%s", help);
        g_free (help);

        goto option_fail;
    }
    if (g_str_equal (mode, "client") &&
                (!server_addr || !listen_addr)) {
        gchar *help = g_option_context_get_help (context, FALSE, cli_grp);
        g_fprintf (stderr, "%s", help);
        g_free (help);

        goto option_fail;
    }
    g_option_context_free (context);

    main_loop = g_main_loop_new (NULL, FALSE);
    if (!main_loop) {
        g_critical ("Create main loop failed!");
        goto main_loop_fail;
    }

    g_main_loop_run (main_loop);

    g_main_loop_unref (main_loop);

    return 0;

main_loop_fail:
option_fail:
    
    return -1;
}


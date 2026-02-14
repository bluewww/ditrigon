/* HexChat GTK4 frontend bootstrap */

#include "fe-gtk4.h"

#include <stdio.h>
#include <string.h>

GMainLoop *frontend_loop;
static GtkApplication *frontend_app;

static char *arg_cfgdir;
static gint arg_show_autoload;
static gint arg_show_config;
static gint arg_show_version;

static void
frontend_activate_cb (GApplication *app, gpointer userdata)
{
	(void) app;
	(void) userdata;

	if (!main_window)
		fe_gtk4_create_main_window ();
}

GtkApplication *
fe_gtk4_get_application (void)
{
	return frontend_app;
}

int
fe_args (int argc, char *argv[])
{
	GError *error;
	GOptionContext *context;
	static const GOptionEntry gopt_entries[] =
	{
		{"no-auto", 'a', 0, G_OPTION_ARG_NONE, &arg_dont_autoconnect, N_("Don't auto connect to servers"), NULL},
		{"cfgdir", 'd', 0, G_OPTION_ARG_STRING, &arg_cfgdir, N_("Use a different config directory"), "PATH"},
		{"no-plugins", 'n', 0, G_OPTION_ARG_NONE, &arg_skip_plugins, N_("Don't auto load any plugins"), NULL},
		{"plugindir", 'p', 0, G_OPTION_ARG_NONE, &arg_show_autoload, N_("Show plugin/script auto-load directory"), NULL},
		{"configdir", 'u', 0, G_OPTION_ARG_NONE, &arg_show_config, N_("Show user config directory"), NULL},
		{"url", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &arg_url, N_("Open an irc://server:port/channel?key URL"), "URL"},
		{"command", 'c', 0, G_OPTION_ARG_STRING, &arg_command, N_("Execute command:"), "COMMAND"},
#ifdef USE_DBUS
		{"existing", 'e', 0, G_OPTION_ARG_NONE, &arg_existing, N_("Open URL or execute command in an existing HexChat"), NULL},
#endif
		{"version", 'v', 0, G_OPTION_ARG_NONE, &arg_show_version, N_("Show version information"), NULL},
		{G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_STRING_ARRAY, &arg_urls, N_("Open an irc://server:port/channel?key URL"), "URL"},
		{NULL}
	};

	error = NULL;

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, gopt_entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);

	if (error)
	{
		if (error->message)
			printf ("%s\n", error->message);
		g_error_free (error);
		g_option_context_free (context);
		return 1;
	}

	g_option_context_free (context);

	if (arg_show_version)
	{
		printf (PACKAGE_NAME " " PACKAGE_VERSION "\n");
		return 0;
	}

	if (arg_show_autoload)
	{
#ifndef USE_PLUGIN
		printf (PACKAGE_NAME " was build without plugin support\n");
		return 1;
#else
		printf ("%s\n", HEXCHATLIBDIR);
		return 0;
#endif
	}

	if (arg_show_config)
	{
		printf ("%s\n", get_xdir ());
		return 0;
	}

	if (arg_cfgdir)
	{
		gsize len;

		g_free (xdir);
		xdir = g_strdup (arg_cfgdir);
		len = strlen (xdir);
		if (len > 0 && xdir[len - 1] == G_DIR_SEPARATOR)
			xdir[len - 1] = 0;
		g_free (arg_cfgdir);
		arg_cfgdir = NULL;
	}

	fe_gtk4_adw_init ();

	frontend_app = fe_gtk4_adw_application_new ();
	if (!frontend_app)
		return 1;
	g_signal_connect (frontend_app, "activate", G_CALLBACK (frontend_activate_cb), NULL);

	error = NULL;
	if (!g_application_register (G_APPLICATION (frontend_app), NULL, &error))
	{
		if (error && error->message)
			printf ("%s\n", error->message);
		g_clear_error (&error);
		g_clear_object (&frontend_app);
		return 1;
	}

	return -1;
}

void
fe_init (void)
{
	palette_load ();
	key_init ();
	pixmaps_init ();

	fe_gtk4_menu_init ();
	fe_gtk4_maingui_init ();
	fe_gtk4_tray_init ();
}

void
fe_main (void)
{
	fe_gtk4_create_main_window ();

	if (frontend_app)
	{
		g_application_run (G_APPLICATION (frontend_app), 0, NULL);
		g_clear_object (&frontend_app);
		return;
	}

	frontend_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (frontend_loop);
	g_main_loop_unref (frontend_loop);
	frontend_loop = NULL;
}

void
fe_cleanup (void)
{
	fe_gtk4_menu_cleanup ();
	fe_gtk4_maingui_cleanup ();
	fe_gtk4_tray_cleanup ();
	fe_gtk4_pixmaps_cleanup ();
}

void
fe_exit (void)
{
	if (frontend_app)
	{
		g_application_quit (G_APPLICATION (frontend_app));
		return;
	}

	if (frontend_loop)
		g_main_loop_quit (frontend_loop);
}

const char *
fe_get_default_font (void)
{
	return "Monospace 10";
}

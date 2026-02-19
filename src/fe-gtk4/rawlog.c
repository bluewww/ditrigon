/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 raw log output */

#include "fe-gtk4.h"

#include "../common/cfgfiles.h"

#define RAWLOG_UI_PATH "/org/ditrigon/ui/gtk4/dialogs/rawlog-window.ui"

typedef struct
{
	GtkWidget *window;
	GtkWidget *text_view;
	GtkTextBuffer *buffer;
	server *serv;
} HcRawlogView;

static HcRawlogView rawlog_view;
static GHashTable *rawlog_logs;

static void
rawlog_value_free (gpointer data)
{
	if (data)
		g_string_free ((GString *) data, TRUE);
}

static GString *
rawlog_log_ensure (server *serv)
{
	GString *log;

	if (!serv)
		return NULL;

	if (!rawlog_logs)
		rawlog_logs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			NULL, rawlog_value_free);

	log = g_hash_table_lookup (rawlog_logs, serv);
	if (log)
		return log;

	log = g_string_new ("");
	g_hash_table_insert (rawlog_logs, serv, log);
	return log;
}

static GString *
rawlog_log_lookup (server *serv)
{
	if (!rawlog_logs || !serv)
		return NULL;

	return g_hash_table_lookup (rawlog_logs, serv);
}

static void
rawlog_scroll_to_end (void)
{
	GtkTextIter end;

	if (!rawlog_view.buffer || !rawlog_view.text_view)
		return;

	gtk_text_buffer_get_end_iter (rawlog_view.buffer, &end);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (rawlog_view.text_view),
		&end, 0.0, FALSE, 0.0, 1.0);
}

static void
rawlog_append_visible (const char *text)
{
	GtkTextIter end;

	if (!rawlog_view.buffer || !text)
		return;

	gtk_text_buffer_get_end_iter (rawlog_view.buffer, &end);
	gtk_text_buffer_insert (rawlog_view.buffer, &end, text, -1);
	rawlog_scroll_to_end ();
}

static void
rawlog_refresh_visible (void)
{
	GString *log;

	if (!rawlog_view.buffer)
		return;

	log = rawlog_log_lookup (rawlog_view.serv);
	gtk_text_buffer_set_text (rawlog_view.buffer, log ? log->str : "", -1);
	rawlog_scroll_to_end ();
}

static void
rawlog_update_title (void)
{
	char title[512];
	const char *name;

	if (!rawlog_view.window)
		return;

	if (!rawlog_view.serv)
	{
		gtk_window_set_title (GTK_WINDOW (rawlog_view.window), _("Raw Log"));
		return;
	}

	name = rawlog_view.serv->servername;
	if (!name || !name[0])
		name = server_get_network (rawlog_view.serv, TRUE);
	if (!name || !name[0])
		name = _("Unknown");

	g_snprintf (title, sizeof (title), _("Raw Log (%s)"), name);
	gtk_window_set_title (GTK_WINDOW (rawlog_view.window), title);
}

static void
rawlog_switch_server (server *serv)
{
	rawlog_view.serv = serv;
	rawlog_update_title ();
	rawlog_refresh_visible ();
}

static gboolean
rawlog_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	rawlog_view.window = NULL;
	rawlog_view.text_view = NULL;
	rawlog_view.buffer = NULL;
	rawlog_view.serv = NULL;
	return FALSE;
}

static void
rawlog_clear_cb (GtkButton *button, gpointer userdata)
{
	GString *log;

	(void) button;
	(void) userdata;

	if (!rawlog_view.serv)
		return;

	log = rawlog_log_ensure (rawlog_view.serv);
	if (log)
		g_string_truncate (log, 0);

	if (rawlog_view.buffer)
		gtk_text_buffer_set_text (rawlog_view.buffer, "", -1);
}

static void
rawlog_save_file_cb (void *userdata, char *file)
{
	server *serv;
	GString *log;
	GError *error;

	serv = userdata;
	if (!serv || !file || !file[0])
		return;

	log = rawlog_log_lookup (serv);
	if (!log)
		return;

	error = NULL;
	if (!g_file_set_contents (file, log->str, log->len, &error))
	{
		if (error)
		{
			fe_message (error->message, FE_MSG_ERROR);
			g_error_free (error);
		}
	}
}

static void
rawlog_save_cb (GtkButton *button, gpointer userdata)
{
	char *initial;

	(void) button;
	(void) userdata;

	if (!rawlog_view.serv)
		return;

	initial = g_strdup_printf ("rawlog-%s.txt",
		rawlog_view.serv->servername[0] ? rawlog_view.serv->servername : "server");
	fe_get_file (_("Save As..."), initial,
		rawlog_save_file_cb, rawlog_view.serv, FRF_WRITE);
	g_free (initial);
}

static void
rawlog_append_line (server *serv, const char *line, gboolean outbound)
{
	GString *log;
	char *prefixed;

	if (!serv || !line)
		return;

	log = rawlog_log_ensure (serv);
	if (!log)
		return;

	prefixed = g_strdup_printf ("%s %s\n", outbound ? "<<" : ">>", line);
	g_string_append (log, prefixed);

	if (rawlog_view.window && rawlog_view.serv == serv)
		rawlog_append_visible (prefixed);

	g_free (prefixed);
}

void
open_rawlog (struct server *serv)
{
	GtkWidget *clear_button = NULL;
	GtkWidget *save_button = NULL;
	GtkWidget *close_button = NULL;
	GtkBuilder *builder;

	if (!serv)
		return;

	if (!rawlog_view.window)
	{
		builder = fe_gtk4_builder_new_from_resource (RAWLOG_UI_PATH);
		rawlog_view.window = fe_gtk4_builder_get_widget (builder, "rawlog_window", GTK_TYPE_WINDOW);
		rawlog_view.text_view = fe_gtk4_builder_get_widget (builder, "rawlog_text_view", GTK_TYPE_TEXT_VIEW);
		clear_button = fe_gtk4_builder_get_widget (builder, "rawlog_clear_button", GTK_TYPE_BUTTON);
		save_button = fe_gtk4_builder_get_widget (builder, "rawlog_save_button", GTK_TYPE_BUTTON);
		close_button = fe_gtk4_builder_get_widget (builder, "rawlog_close_button", GTK_TYPE_BUTTON);
		g_object_ref_sink (rawlog_view.window);
		g_object_unref (builder);

		rawlog_view.buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (rawlog_view.text_view));
		if (main_window)
			gtk_window_set_transient_for (GTK_WINDOW (rawlog_view.window), GTK_WINDOW (main_window));
		gtk_button_set_label (GTK_BUTTON (clear_button), _("Clear"));
		gtk_button_set_label (GTK_BUTTON (save_button), _("Save As..."));
		gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));

		g_signal_connect (clear_button, "clicked", G_CALLBACK (rawlog_clear_cb), NULL);
		g_signal_connect (save_button, "clicked", G_CALLBACK (rawlog_save_cb), NULL);
		g_signal_connect_swapped (close_button, "clicked",
			G_CALLBACK (gtk_window_close), rawlog_view.window);
		g_signal_connect (rawlog_view.window, "close-request",
			G_CALLBACK (rawlog_close_request_cb), NULL);
	}

	rawlog_switch_server (serv);
	gtk_window_present (GTK_WINDOW (rawlog_view.window));
}

void
fe_add_rawlog (struct server *serv, char *text, int len, int outbound)
{
	char *copy;
	char **split_text;
	gsize i;

	if (!serv || !text || len <= 0)
		return;

	copy = g_strndup (text, len);
	split_text = g_strsplit (copy, "\r\n", 0);
	for (i = 0; split_text && split_text[i]; i++)
	{
		if (split_text[i][0] == 0)
			continue;

		rawlog_append_line (serv, split_text[i], outbound != 0);
	}
	g_strfreev (split_text);
	g_free (copy);
}

void
fe_gtk4_rawlog_cleanup (void)
{
	if (rawlog_view.window)
		gtk_window_destroy (GTK_WINDOW (rawlog_view.window));

	rawlog_view.window = NULL;
	rawlog_view.text_view = NULL;
	rawlog_view.buffer = NULL;
	rawlog_view.serv = NULL;

	if (rawlog_logs)
	{
		g_hash_table_unref (rawlog_logs);
		rawlog_logs = NULL;
	}
}

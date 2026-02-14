/* HexChat GTK4 raw log output */
#include "fe-gtk4.h"

#include "../common/cfgfiles.h"

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
	GtkWidget *root;
	GtkWidget *scroll;
	GtkWidget *buttons;
	GtkWidget *clear_button;
	GtkWidget *save_button;
	GtkWidget *close_button;

	if (!serv)
		return;

	if (!rawlog_view.window)
	{
		rawlog_view.window = gtk_window_new ();
		gtk_window_set_default_size (GTK_WINDOW (rawlog_view.window), 720, 420);
		if (main_window)
			gtk_window_set_transient_for (GTK_WINDOW (rawlog_view.window), GTK_WINDOW (main_window));

		root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
		gtk_widget_set_margin_start (root, 12);
		gtk_widget_set_margin_end (root, 12);
		gtk_widget_set_margin_top (root, 12);
		gtk_widget_set_margin_bottom (root, 12);
		gtk_window_set_child (GTK_WINDOW (rawlog_view.window), root);

		scroll = gtk_scrolled_window_new ();
		gtk_widget_set_hexpand (scroll, TRUE);
		gtk_widget_set_vexpand (scroll, TRUE);
		gtk_box_append (GTK_BOX (root), scroll);

		rawlog_view.text_view = gtk_text_view_new ();
		rawlog_view.buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (rawlog_view.text_view));
		gtk_text_view_set_editable (GTK_TEXT_VIEW (rawlog_view.text_view), FALSE);
		gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (rawlog_view.text_view), FALSE);
		gtk_text_view_set_monospace (GTK_TEXT_VIEW (rawlog_view.text_view), TRUE);
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), rawlog_view.text_view);

		buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_widget_set_halign (buttons, GTK_ALIGN_END);
		gtk_box_append (GTK_BOX (root), buttons);

		clear_button = gtk_button_new_with_label (_("Clear"));
		g_signal_connect (clear_button, "clicked", G_CALLBACK (rawlog_clear_cb), NULL);
		gtk_box_append (GTK_BOX (buttons), clear_button);

		save_button = gtk_button_new_with_label (_("Save As..."));
		g_signal_connect (save_button, "clicked", G_CALLBACK (rawlog_save_cb), NULL);
		gtk_box_append (GTK_BOX (buttons), save_button);

		close_button = gtk_button_new_with_label (_("Close"));
		g_signal_connect_swapped (close_button, "clicked",
			G_CALLBACK (gtk_window_close), rawlog_view.window);
		gtk_box_append (GTK_BOX (buttons), close_button);

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

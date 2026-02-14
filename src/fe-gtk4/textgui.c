/* HexChat GTK4 text helpers */
#include "fe-gtk4.h"

#include "../common/text.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static GtkWidget *pevent_dialog;
static GtkWidget *pevent_textview;

static gboolean
text_line_matches (const char *line,
	const char *needle,
	gtk_xtext_search_flags flags,
	GRegex *regex)
{
	char *line_folded;
	char *needle_folded;
	gboolean match;

	if (!line || !line[0])
		return FALSE;

	if (regex)
		return g_regex_match (regex, line, 0, NULL);

	if (!needle || !needle[0])
		return FALSE;

	if (flags & case_match)
		return strstr (line, needle) != NULL;

	line_folded = g_utf8_casefold (line, -1);
	needle_folded = g_utf8_casefold (needle, -1);
	match = strstr (line_folded, needle_folded) != NULL;
	g_free (line_folded);
	g_free (needle_folded);

	return match;
}

void
fe_progressbar_start (struct session *sess)
{
	if (!sess)
		return;

	fe_print_text (sess, _("* Retrieving data...\n"), 0, TRUE);
}

void
fe_progressbar_end (struct server *serv)
{
	session *sess;

	if (!serv)
		return;

	sess = serv->front_session ? serv->front_session : serv->server_session;
	if (!sess)
		return;

	fe_print_text (sess, _("* Retrieval complete.\n"), 0, TRUE);
}

void
fe_lastlog (session *sess, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags)
{
	const char *buffer_text;
	session *target;
	GRegex *regex;
	GError *error;
	char **lines;
	gsize nlines;
	gsize i;
	int matched;

	if (!sess || !sstr || !sstr[0])
		return;

	target = lastlog_sess ? lastlog_sess : sess;
	buffer_text = fe_gtk4_xtext_get_session_text (sess);
	if (!buffer_text || !buffer_text[0])
	{
		fe_print_text (target, _("Search buffer is empty.\n"), 0, TRUE);
		return;
	}

	error = NULL;
	regex = NULL;
	if (flags & regexp)
	{
		GRegexCompileFlags compile_flags = (flags & case_match) ? 0 : G_REGEX_CASELESS;

		regex = g_regex_new (sstr, compile_flags, 0, &error);
		if (error)
		{
			fe_print_text (target, error->message, 0, TRUE);
			fe_print_text (target, "\n", 0, TRUE);
			g_error_free (error);
			return;
		}
	}

	lines = g_strsplit (buffer_text, "\n", -1);
	nlines = g_strv_length (lines);
	matched = 0;

	if (flags & backward)
	{
		for (i = nlines; i > 0; i--)
		{
			const char *line = lines[i - 1];

			if (!text_line_matches (line, sstr, flags, regex))
				continue;

			{
				char *out = g_strconcat (line, "\n", NULL);
				fe_print_text (target, out, 0, TRUE);
				g_free (out);
			}
			matched++;
		}
	}
	else
	{
		for (i = 0; i < nlines; i++)
		{
			const char *line = lines[i];

			if (!text_line_matches (line, sstr, flags, regex))
				continue;

			{
				char *out = g_strconcat (line, "\n", NULL);
				fe_print_text (target, out, 0, TRUE);
				g_free (out);
			}
			matched++;
		}
	}

	if (!matched)
		fe_print_text (target, _("No matches found.\n"), 0, TRUE);

	if (regex)
		g_regex_unref (regex);
	g_strfreev (lines);
}

static void
pevent_dialog_reload (void)
{
	GtkTextBuffer *buffer;
	char *text;
	int fd;

	if (!pevent_textview)
		return;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (pevent_textview));
	fd = hexchat_open_file ("pevents.conf", O_RDONLY, 0, 0);
	if (fd != -1)
	{
		GString *content = g_string_new ("");
		char chunk[1024];
		ssize_t nread;

		while ((nread = read (fd, chunk, sizeof (chunk))) > 0)
			g_string_append_len (content, chunk, nread);
		close (fd);
		text = g_string_free (content, FALSE);
	}
	else
	{
		text = g_strdup ("");
	}

	gtk_text_buffer_set_text (buffer, text, -1);
	g_free (text);
}

static void
pevent_dialog_reload_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;
	pevent_dialog_reload ();
}

static void
pevent_dialog_save_cb (GtkButton *button, gpointer userdata)
{
	GtkTextBuffer *buffer;
	GtkTextIter start;
	GtkTextIter end;
	char *contents;
	int fd;
	ssize_t len;

	(void) button;
	(void) userdata;

	if (!pevent_textview)
		return;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (pevent_textview));
	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter (buffer, &end);
	contents = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

	fd = hexchat_open_file ("pevents.conf", O_CREAT | O_TRUNC | O_WRONLY, 0600, XOF_DOMODE);
	if (fd != -1)
	{
		len = strlen (contents);
		if (len > 0)
			write (fd, contents, len);
		close (fd);
		pevent_load (NULL);
		pevent_make_pntevts ();
	}

	g_free (contents);
}

static gboolean
pevent_dialog_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;
	pevent_dialog = NULL;
	pevent_textview = NULL;
	return FALSE;
}

void
pevent_dialog_show (void)
{
	GtkWidget *root;
	GtkWidget *scroll;
	GtkWidget *buttons;
	GtkWidget *button;
	GtkWidget *hint;

	if (pevent_dialog)
	{
		gtk_window_present (GTK_WINDOW (pevent_dialog));
		return;
	}

	pevent_dialog = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (pevent_dialog), _("Text Events"));
	gtk_window_set_default_size (GTK_WINDOW (pevent_dialog), 760, 520);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (pevent_dialog), GTK_WINDOW (main_window));

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (root, 12);
	gtk_widget_set_margin_end (root, 12);
	gtk_widget_set_margin_top (root, 12);
	gtk_widget_set_margin_bottom (root, 12);
	gtk_window_set_child (GTK_WINDOW (pevent_dialog), root);

	hint = gtk_label_new (_("Raw pevents.conf editor"));
	gtk_label_set_xalign (GTK_LABEL (hint), 0.0f);
	gtk_widget_add_css_class (hint, "dim-label");
	gtk_box_append (GTK_BOX (root), hint);

	scroll = gtk_scrolled_window_new ();
	gtk_widget_set_hexpand (scroll, TRUE);
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_box_append (GTK_BOX (root), scroll);

	pevent_textview = gtk_text_view_new ();
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (pevent_textview), TRUE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), pevent_textview);
	pevent_dialog_reload ();

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (buttons, GTK_ALIGN_END);
	gtk_box_append (GTK_BOX (root), buttons);

	button = gtk_button_new_with_label (_("Reload"));
	g_signal_connect (button, "clicked", G_CALLBACK (pevent_dialog_reload_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), button);

	button = gtk_button_new_with_label (_("Save"));
	g_signal_connect (button, "clicked", G_CALLBACK (pevent_dialog_save_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), button);

	button = gtk_button_new_with_label (_("Close"));
	g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_window_close), pevent_dialog);
	gtk_box_append (GTK_BOX (buttons), button);

	g_signal_connect (pevent_dialog, "close-request",
		G_CALLBACK (pevent_dialog_close_request_cb), NULL);
	gtk_window_present (GTK_WINDOW (pevent_dialog));
}

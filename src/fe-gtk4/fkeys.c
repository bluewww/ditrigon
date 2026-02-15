/* HexChat GTK4 keyboard helpers */
#include "fe-gtk4.h"

#include "../common/history.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define TEXT_EDITOR_UI_PATH "/org/hexchat/ui/gtk4/dialogs/text-editor-window.ui"

static GtkWidget *key_dialog;
static GtkWidget *key_textview;

static char *
key_editor_read_file (void)
{
	int fd;
	ssize_t nread;
	char buf[1024];
	GString *text;

	fd = hexchat_open_file ("keybindings.conf", O_RDONLY, 0, 0);
	if (fd == -1)
		return g_strdup ("");

	text = g_string_new ("");
	while ((nread = read (fd, buf, sizeof (buf))) > 0)
		g_string_append_len (text, buf, nread);
	close (fd);

	return g_string_free (text, FALSE);
}

static gboolean
key_editor_write_file (const char *text)
{
	int fd;
	ssize_t len;

	fd = hexchat_open_file ("keybindings.conf", O_CREAT | O_TRUNC | O_WRONLY, 0600, XOF_DOMODE);
	if (fd == -1)
		return FALSE;

	if (!text)
		text = "";

	len = strlen (text);
	if (len > 0)
		write (fd, text, len);

	close (fd);
	return TRUE;
}

static void
key_editor_reload (void)
{
	GtkTextBuffer *buffer;
	char *text;

	if (!key_textview)
		return;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (key_textview));
	text = key_editor_read_file ();
	gtk_text_buffer_set_text (buffer, text, -1);
	g_free (text);
}

static gboolean
key_dialog_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;
	key_dialog = NULL;
	key_textview = NULL;
	return FALSE;
}

static void
key_dialog_reload_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;
	key_editor_reload ();
}

static void
key_dialog_save_cb (GtkButton *button, gpointer userdata)
{
	GtkTextBuffer *buffer;
	GtkTextIter start;
	GtkTextIter end;
	char *text;

	(void) button;
	(void) userdata;

	if (!key_textview)
		return;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (key_textview));
	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter (buffer, &end);
	text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

	if (!key_editor_write_file (text))
		fe_message (_("Unable to save keybindings.conf"), FE_MSG_ERROR);

	g_free (text);
}

void
key_init (void)
{
}

void
key_dialog_show (void)
{
	GtkWidget *hint = NULL;
	GtkBuilder *builder;
	GtkWidget *reload_button = NULL;
	GtkWidget *save_button = NULL;
	GtkWidget *close_button = NULL;

	if (key_dialog)
	{
		gtk_window_present (GTK_WINDOW (key_dialog));
		return;
	}

	builder = fe_gtk4_builder_new_from_resource (TEXT_EDITOR_UI_PATH);
	key_dialog = fe_gtk4_builder_get_widget (builder, "text_editor_window", GTK_TYPE_WINDOW);
	key_textview = fe_gtk4_builder_get_widget (builder, "text_editor_view", GTK_TYPE_TEXT_VIEW);
	hint = fe_gtk4_builder_get_widget (builder, "text_editor_hint", GTK_TYPE_LABEL);
	reload_button = fe_gtk4_builder_get_widget (builder, "text_editor_reload_button", GTK_TYPE_BUTTON);
	save_button = fe_gtk4_builder_get_widget (builder, "text_editor_save_button", GTK_TYPE_BUTTON);
	close_button = fe_gtk4_builder_get_widget (builder, "text_editor_close_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (key_dialog);
	g_object_unref (builder);

	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (key_dialog), GTK_WINDOW (main_window));
	gtk_window_set_title (GTK_WINDOW (key_dialog), _("Keyboard Shortcuts"));
	gtk_label_set_text (GTK_LABEL (hint), _("Raw keybindings.conf editor"));
	gtk_button_set_label (GTK_BUTTON (reload_button), _("Reload"));
	gtk_button_set_label (GTK_BUTTON (save_button), _("Save"));
	gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));
	g_signal_connect (reload_button, "clicked", G_CALLBACK (key_dialog_reload_cb), NULL);
	g_signal_connect (save_button, "clicked", G_CALLBACK (key_dialog_save_cb), NULL);
	g_signal_connect_swapped (close_button, "clicked", G_CALLBACK (gtk_window_close), key_dialog);

	g_signal_connect (key_dialog, "close-request", G_CALLBACK (key_dialog_close_request_cb), NULL);
	key_editor_reload ();
	gtk_window_present (GTK_WINDOW (key_dialog));
}

int
key_action_insert (GtkWidget *wid, guint keyval, GdkModifierType state, char *d1, char *d2, session *sess)
{
	int pos;

	(void) keyval;
	(void) state;
	(void) d2;
	(void) sess;

	if (!wid || !d1)
		return 0;

	pos = gtk_editable_get_position (GTK_EDITABLE (wid));
	gtk_editable_insert_text (GTK_EDITABLE (wid), d1, -1, &pos);
	gtk_editable_set_position (GTK_EDITABLE (wid), pos);
	return 2;
}

int
key_handle_key_press (GtkWidget *wid, guint keyval, GdkModifierType state, session *sess)
{
	char *line;

	(void) state;

	if (!wid || !sess)
		return 0;

	if (keyval == GDK_KEY_Up)
	{
		line = history_up (&sess->history, (char *) gtk_editable_get_text (GTK_EDITABLE (wid)));
		if (line)
		{
			gtk_editable_set_text (GTK_EDITABLE (wid), line);
			gtk_editable_set_position (GTK_EDITABLE (wid), -1);
		}
		return 1;
	}

	if (keyval == GDK_KEY_Down)
	{
		line = history_down (&sess->history);
		if (line)
		{
			gtk_editable_set_text (GTK_EDITABLE (wid), line);
			gtk_editable_set_position (GTK_EDITABLE (wid), -1);
		}
		return 1;
	}

	return 0;
}

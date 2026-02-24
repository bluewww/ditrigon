/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 keyboard helpers */

#include "fe-gtk4.h"

#include "../common/history.h"
#include "../common/userlist.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define TEXT_EDITOR_UI_PATH "/org/ditrigon/ui/gtk4/dialogs/text-editor-window.ui"

static GtkWidget *key_dialog;
static GtkWidget *key_textview;

typedef struct
{
	session *sess;
	char *prefix;
	char *postfix;
	GPtrArray *candidates;
	guint index;
	gboolean append_suffix;
} tab_completion_state;

static tab_completion_state nick_completion;

static gboolean
keyval_is_modifier_only (guint keyval)
{
	switch (keyval)
	{
	case GDK_KEY_Shift_L:
	case GDK_KEY_Shift_R:
	case GDK_KEY_Control_L:
	case GDK_KEY_Control_R:
	case GDK_KEY_Alt_L:
	case GDK_KEY_Alt_R:
	case GDK_KEY_Meta_L:
	case GDK_KEY_Meta_R:
	case GDK_KEY_Super_L:
	case GDK_KEY_Super_R:
	case GDK_KEY_Hyper_L:
	case GDK_KEY_Hyper_R:
	case GDK_KEY_ISO_Level3_Shift:
	case GDK_KEY_ISO_Level5_Shift:
	case GDK_KEY_Caps_Lock:
	case GDK_KEY_Shift_Lock:
	case GDK_KEY_Num_Lock:
	case GDK_KEY_Scroll_Lock:
		return TRUE;
	default:
		return FALSE;
	}
}

static void
nick_completion_reset (void)
{
	nick_completion.sess = NULL;
	g_clear_pointer (&nick_completion.prefix, g_free);
	g_clear_pointer (&nick_completion.postfix, g_free);
	g_clear_pointer (&nick_completion.candidates, g_ptr_array_unref);
	nick_completion.index = 0;
	nick_completion.append_suffix = FALSE;
}

static gint
talked_recent_cmp (gconstpointer a, gconstpointer b)
{
	const struct User *ua = (const struct User *) a;
	const struct User *ub = (const struct User *) b;

	if (ua->me)
		return -1;
	if (ub->me)
		return 1;

	if (ua->lasttalk < ub->lasttalk)
		return -1;
	if (ua->lasttalk > ub->lasttalk)
		return 1;

	return 0;
}

static GPtrArray *
nick_completion_build_candidates (session *sess, const char *seed, int seed_len)
{
	GPtrArray *candidates;
	GList *users;
	GList *iter;

	if (!sess || !seed || seed_len <= 0)
		return NULL;

	candidates = g_ptr_array_new_with_free_func (g_free);

	if (sess->type == SESS_DIALOG)
	{
		if (sess->channel[0] && rfc_ncasecmp (sess->channel, (char *) seed, seed_len) == 0)
			g_ptr_array_add (candidates, g_strdup (sess->channel));
		return candidates;
	}

	users = userlist_double_list (sess);
	if (prefs.hex_completion_sort == 1)
		users = g_list_sort (users, talked_recent_cmp);
	users = g_list_reverse (users);

	for (iter = users; iter; iter = iter->next)
	{
		struct User *user = (struct User *) iter->data;

		if (user && rfc_ncasecmp (user->nick, (char *) seed, seed_len) == 0)
			g_ptr_array_add (candidates, g_strdup (user->nick));
	}

	g_list_free (users);
	return candidates;
}

static gboolean
nick_completion_build_line_for_index (guint index, char **line_out, int *cursor_out)
{
	const char *candidate;
	GString *buf;
	gunichar suffix;

	if (!line_out || !nick_completion.candidates ||
		index >= nick_completion.candidates->len ||
		!nick_completion.prefix || !nick_completion.postfix)
		return FALSE;

	candidate = g_ptr_array_index (nick_completion.candidates, index);
	buf = g_string_sized_new (strlen (nick_completion.prefix) + strlen (candidate) +
		strlen (nick_completion.postfix) + 8);

	g_string_append (buf, nick_completion.prefix);
	g_string_append (buf, candidate);

	if (nick_completion.append_suffix && prefs.hex_completion_suffix[0] != '\0')
	{
		suffix = g_utf8_get_char_validated (prefs.hex_completion_suffix, -1);
		if (suffix != (gunichar) -1 && suffix != (gunichar) -2)
			g_string_append_unichar (buf, suffix);
	}

	g_string_append_c (buf, ' ');

	if (cursor_out)
		*cursor_out = g_utf8_strlen (buf->str, -1);

	g_string_append (buf, nick_completion.postfix);

	*line_out = g_string_free (buf, FALSE);
	return TRUE;
}

static gboolean
nick_completion_apply_index (GtkWidget *wid, guint index)
{
	char *line = NULL;
	int cursor = 0;

	if (!wid || !nick_completion_build_line_for_index (index, &line, &cursor))
		return FALSE;

	gtk_editable_set_text (GTK_EDITABLE (wid), line);
	gtk_editable_set_position (GTK_EDITABLE (wid), cursor);
	g_free (line);

	nick_completion.index = index;
	return TRUE;
}

static gboolean
nick_completion_matches_current_text (GtkWidget *wid)
{
	char *expected = NULL;
	const char *actual;
	int expected_cursor;
	int actual_cursor;
	gboolean match;

	if (!wid || !nick_completion.candidates)
		return FALSE;

	if (!nick_completion_build_line_for_index (nick_completion.index, &expected, &expected_cursor))
		return FALSE;

	actual = gtk_editable_get_text (GTK_EDITABLE (wid));
	actual_cursor = gtk_editable_get_position (GTK_EDITABLE (wid));
	match = (actual_cursor == expected_cursor && g_strcmp0 (actual, expected) == 0);
	g_free (expected);

	return match;
}

static gboolean
nick_completion_cycle (GtkWidget *wid, session *sess, gboolean backwards)
{
	guint len;
	gint next;

	if (!wid || !sess || !nick_completion.candidates || nick_completion.candidates->len == 0)
		return FALSE;

	if (nick_completion.sess != sess || !nick_completion_matches_current_text (wid))
	{
		nick_completion_reset ();
		return FALSE;
	}

	len = nick_completion.candidates->len;
	if (backwards)
		next = ((gint) nick_completion.index - 1 + (gint) len) % (gint) len;
	else
		next = ((gint) nick_completion.index + 1) % (gint) len;

	return nick_completion_apply_index (wid, (guint) next);
}

static gboolean
nick_completion_start (GtkWidget *wid, session *sess)
{
	const char *text;
	const char *cursor_ptr;
	const char *token_start;
	const char *comp_start;
	const char *prev;
	char *seed;
	char *prefix;
	char *postfix;
	GPtrArray *candidates;
	int cursor_pos;
	int seed_len;
	gboolean has_nick_prefix;
	gboolean append_suffix;

	if (!wid || !sess || !sess->server)
		return FALSE;

	text = gtk_editable_get_text (GTK_EDITABLE (wid));
	if (!text || !text[0])
		return FALSE;

	cursor_pos = gtk_editable_get_position (GTK_EDITABLE (wid));
	if (cursor_pos < 0)
		cursor_pos = g_utf8_strlen (text, -1);

	cursor_ptr = g_utf8_offset_to_pointer (text, cursor_pos);
	token_start = cursor_ptr;

	while (token_start > text)
	{
		prev = g_utf8_find_prev_char (text, token_start);
		if (!prev || *prev == ' ')
			break;
		token_start = prev;
	}

	if (token_start == cursor_ptr)
		return FALSE;

	if (token_start == text && text[0] == prefs.hex_input_command_char[0])
		return FALSE;

	if (sess->server->chantypes && strchr (sess->server->chantypes, *token_start) != NULL)
		return FALSE;

	has_nick_prefix = FALSE;
	comp_start = token_start;
	if (token_start == text && sess->server->nick_prefixes &&
		strchr (sess->server->nick_prefixes, *token_start) != NULL)
	{
		has_nick_prefix = TRUE;
		comp_start = g_utf8_next_char (token_start);
	}

	if (comp_start >= cursor_ptr)
		return FALSE;

	seed_len = (int) (cursor_ptr - comp_start);
	seed = g_strndup (comp_start, seed_len);
	prefix = g_strndup (text, (gsize) (comp_start - text));
	postfix = g_strdup (cursor_ptr);
	append_suffix = ((!prefix[0] || has_nick_prefix) && prefs.hex_completion_suffix[0] != '\0');

	candidates = nick_completion_build_candidates (sess, seed, strlen (seed));
	g_free (seed);

	if (!candidates || candidates->len == 0)
	{
		g_clear_pointer (&candidates, g_ptr_array_unref);
		g_free (prefix);
		g_free (postfix);
		return FALSE;
	}

	nick_completion_reset ();
	nick_completion.sess = sess;
	nick_completion.prefix = prefix;
	nick_completion.postfix = postfix;
	nick_completion.candidates = candidates;
	nick_completion.index = 0;
	nick_completion.append_suffix = append_suffix;

	return nick_completion_apply_index (wid, 0);
}

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
	if (len > 0 && write (fd, text, len) < 0)
	{
		g_warning ("Failed to write keybindings.conf");
		close (fd);
		return FALSE;
	}

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
	gboolean backwards;

	if (!wid || !sess)
		return 0;

	/* Keep completion state intact while modifier keys are pressed/released.
	 * This is required for seamless direction changes (Tab <-> Shift+Tab). */
	if (keyval_is_modifier_only (keyval))
		return 0;

	if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab)
	{
		backwards = (keyval == GDK_KEY_ISO_Left_Tab) || ((state & GDK_SHIFT_MASK) != 0);

		if (nick_completion_cycle (wid, sess, backwards))
			return 1;

		nick_completion_reset ();
		nick_completion_start (wid, sess);
		return 1;
	}

	nick_completion_reset ();

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

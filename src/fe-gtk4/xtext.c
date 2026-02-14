/* HexChat GTK4 text view/log buffer */
#include "fe-gtk4.h"
#include "../common/url.h"
#include "../common/userlist.h"

typedef struct
{
	int fg;
	int bg;
	gboolean bold;
	gboolean italic;
	gboolean underline;
	gboolean reverse;
} HcTextStyle;

static GHashTable *session_logs;
static GHashTable *color_tags;
static GtkTextTag *tag_stamp;
static GtkTextTag *tag_bold;
static GtkTextTag *tag_italic;
static GtkTextTag *tag_underline;
static GtkTextTag *tag_font;
static char *xtext_search_text;
static GtkTextMark *xtext_search_mark;

static void xtext_render_raw_append (const char *raw);

static gboolean
xtext_is_word_char (gunichar ch)
{
	if (g_unichar_isalnum (ch))
		return TRUE;

	switch (ch)
	{
	case '_':
	case '-':
	case '[':
	case ']':
	case '\\':
	case '`':
	case '^':
	case '{':
	case '}':
	case '|':
	case '#':
	case '&':
		return TRUE;
	default:
		break;
	}

	return FALSE;
}

static char *
xtext_word_at_point (GtkTextView *view, double x, double y)
{
	GtkTextIter iter;
	GtkTextIter start;
	GtkTextIter end;
	int buffer_x;
	int buffer_y;
	GtkTextBuffer *buffer;
	gunichar ch;
	char *word;

	if (!view)
		return NULL;

	gtk_text_view_window_to_buffer_coords (view, GTK_TEXT_WINDOW_WIDGET,
		(int) x, (int) y, &buffer_x, &buffer_y);
	if (!gtk_text_view_get_iter_at_location (view, &iter, buffer_x, buffer_y))
		return NULL;

	start = iter;
	end = iter;

	ch = gtk_text_iter_get_char (&iter);
	if (!xtext_is_word_char (ch))
	{
		GtkTextIter prev;

		prev = iter;
		if (!gtk_text_iter_backward_char (&prev))
			return NULL;
		ch = gtk_text_iter_get_char (&prev);
		if (!xtext_is_word_char (ch))
			return NULL;
		start = prev;
		end = prev;
	}

	while (!gtk_text_iter_starts_line (&start))
	{
		GtkTextIter prev;
		gunichar prev_ch;

		prev = start;
		if (!gtk_text_iter_backward_char (&prev))
			break;
		prev_ch = gtk_text_iter_get_char (&prev);
		if (!xtext_is_word_char (prev_ch))
			break;
		start = prev;
	}

	while (!gtk_text_iter_ends_line (&end))
	{
		GtkTextIter next;
		gunichar next_ch;

		next = end;
		next_ch = gtk_text_iter_get_char (&next);
		if (!xtext_is_word_char (next_ch))
			break;
		if (!gtk_text_iter_forward_char (&end))
			break;
	}

	if (gtk_text_iter_compare (&start, &end) >= 0)
		return NULL;

	buffer = gtk_text_view_get_buffer (view);
	word = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	if (!word || !word[0])
	{
		g_free (word);
		return NULL;
	}

	return word;
}

static void
xtext_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	session *sess;
	char *word;
	int type;
	const char *nick;

	(void) user_data;

	if (n_press != 1 || !log_view)
		return;

	sess = current_tab;
	if (!sess || !is_session (sess))
		return;

	word = xtext_word_at_point (GTK_TEXT_VIEW (log_view), x, y);
	if (!word)
		return;

	type = url_check_word (word);
	if (type == 0 && sess->type == SESS_DIALOG)
		type = WORD_DIALOG;

	if (type != WORD_NICK && type != WORD_DIALOG)
	{
		g_free (word);
		return;
	}

	if (type == WORD_DIALOG)
		nick = sess->channel;
	else
		nick = word;

	if (nick && nick[0])
	{
		fe_gtk4_menu_show_nickmenu (log_view, x, y, sess, nick);
		gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	}

	g_free (word);
}

static void
session_log_free (gpointer data)
{
	if (data)
		g_string_free ((GString *) data, TRUE);
}

static GString *
session_log_ensure (session *sess)
{
	GString *log;

	if (!session_logs || !sess || !is_session (sess))
		return NULL;

	log = g_hash_table_lookup (session_logs, sess);
	if (log)
		return log;

	log = g_string_new ("");
	g_hash_table_insert (session_logs, sess, log);
	return log;
}

static void
xtext_color_to_rgba (int color_index, GdkRGBA *rgba)
{
	int idx;

	idx = color_index;
	if (idx < 0)
		idx = 0;
	if (idx > 31)
		idx %= 32;

	rgba->red = ((double) colors[idx].red) / 65535.0;
	rgba->green = ((double) colors[idx].green) / 65535.0;
	rgba->blue = ((double) colors[idx].blue) / 65535.0;
	rgba->alpha = 1.0;
}

static void
xtext_reset_style (HcTextStyle *style)
{
	style->fg = -1;
	style->bg = -1;
	style->bold = FALSE;
	style->italic = FALSE;
	style->underline = FALSE;
	style->reverse = FALSE;
}

static GtkTextTag *
xtext_get_color_tag (int fg, int bg)
{
	guint key;
	GtkTextTag *tag;
	char tag_name[32];

	if (!log_buffer)
		return NULL;

	if (fg < 0 && bg < 0)
		return NULL;

	if (fg >= 0)
		fg %= 32;
	if (bg >= 0)
		bg %= 32;

	key = (((guint) (fg + 1)) & 0xffu) | ((((guint) (bg + 1)) & 0xffu) << 8);
	tag = color_tags ? g_hash_table_lookup (color_tags, GUINT_TO_POINTER (key)) : NULL;
	if (tag)
		return tag;

	g_snprintf (tag_name, sizeof (tag_name), "hc-color-%d-%d", fg, bg);
	tag = gtk_text_buffer_create_tag (log_buffer, tag_name, NULL);
	if (!tag)
		return NULL;

	if (fg >= 0)
	{
		GdkRGBA rgba;
		xtext_color_to_rgba (fg, &rgba);
		g_object_set (tag, "foreground-rgba", &rgba, NULL);
	}
	if (bg >= 0)
	{
		GdkRGBA rgba;
		xtext_color_to_rgba (bg, &rgba);
		g_object_set (tag, "background-rgba", &rgba, NULL);
	}

	if (!color_tags)
		color_tags = g_hash_table_new (g_direct_hash, g_direct_equal);
	g_hash_table_insert (color_tags, GUINT_TO_POINTER (key), tag);

	return tag;
}

static void
xtext_insert_segment (GtkTextIter *iter, const char *text, gsize len, const HcTextStyle *style, GtkTextTag *layout_tag)
{
	GtkTextTag *tags[6];
	int n;
	int fg;
	int bg;
	GtkTextTag *color_tag;

	if (!log_buffer || !iter || !text || len == 0)
		return;

	n = 0;
	if (style->bold && tag_bold)
		tags[n++] = tag_bold;
	if (style->italic && tag_italic)
		tags[n++] = tag_italic;
	if (style->underline && tag_underline)
		tags[n++] = tag_underline;

	fg = style->fg;
	bg = style->bg;
	if (style->reverse)
	{
		int tmp;

		tmp = fg;
		fg = bg;
		bg = tmp;
		if (fg < 0)
			fg = COL_BG;
		if (bg < 0)
			bg = COL_FG;
	}

	color_tag = xtext_get_color_tag (fg, bg);
	if (color_tag && n < 4)
		tags[n++] = color_tag;
	if (layout_tag && n < 5)
		tags[n++] = layout_tag;
	if (tag_font && n < 6)
		tags[n++] = tag_font;

	switch (n)
	{
	case 0:
		gtk_text_buffer_insert (log_buffer, iter, text, (int) len);
		break;
	case 1:
		gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len, tags[0], NULL);
		break;
	case 2:
		gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len,
			tags[0], tags[1], NULL);
		break;
	case 3:
		gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len,
			tags[0], tags[1], tags[2], NULL);
		break;
	case 4:
		gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len,
			tags[0], tags[1], tags[2], tags[3], NULL);
		break;
	case 5:
		gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len,
			tags[0], tags[1], tags[2], tags[3], tags[4], NULL);
		break;
	default:
		gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len,
			tags[0], tags[1], tags[2], tags[3], tags[4], tags[5], NULL);
		break;
	}
}

static gboolean
xtext_parse_color_number (const char *text, gsize len, gsize *index, int *value)
{
	int number;
	int digits;
	gsize i;

	number = 0;
	digits = 0;
	i = *index;

	while (i < len && digits < 2 && g_ascii_isdigit (text[i]))
	{
		number = (number * 10) + (text[i] - '0');
		i++;
		digits++;
	}

	if (digits == 0)
		return FALSE;

	*index = i;
	*value = number % 32;
	return TRUE;
}

static void
xtext_render_formatted (GtkTextIter *iter, const char *text, gsize len, GtkTextTag *layout_tag)
{
	HcTextStyle style;
	gsize i;
	gsize seg_start;

	if (!text || len == 0)
		return;

	xtext_reset_style (&style);
	seg_start = 0;
	i = 0;

	while (i < len)
	{
		unsigned char ch;

		ch = (unsigned char) text[i];

		if (ch == 0x02 || ch == 0x1d || ch == 0x1f || ch == 0x16 || ch == 0x0f || ch == 0x03 || ch == 0x04)
		{
			if (i > seg_start)
				xtext_insert_segment (iter, text + seg_start, i - seg_start, &style, layout_tag);

			if (ch == 0x02)
			{
				style.bold = !style.bold;
				i++;
			}
			else if (ch == 0x1d)
			{
				style.italic = !style.italic;
				i++;
			}
			else if (ch == 0x1f)
			{
				style.underline = !style.underline;
				i++;
			}
			else if (ch == 0x16)
			{
				style.reverse = !style.reverse;
				i++;
			}
			else if (ch == 0x0f)
			{
				xtext_reset_style (&style);
				i++;
			}
			else if (ch == 0x03)
			{
				int fg;
				int bg;
				gsize j;

				j = i + 1;
				if (!xtext_parse_color_number (text, len, &j, &fg))
				{
					style.fg = -1;
					style.bg = -1;
					i = j;
				}
				else
				{
					style.fg = fg;
					if (j < len && text[j] == ',')
					{
						j++;
						if (xtext_parse_color_number (text, len, &j, &bg))
							style.bg = bg;
						else
							style.bg = -1;
					}
					i = j;
				}
			}
			else
			{
				gsize j;

				/* Hex color sequence (rare); strip it safely. */
				j = i + 1;
				while (j < len && (g_ascii_isxdigit (text[j]) || text[j] == ','))
					j++;
				style.fg = -1;
				style.bg = -1;
				i = j;
			}

			seg_start = i;
			continue;
		}

		if (ch < 0x20 && ch != '\t')
		{
			if (i > seg_start)
				xtext_insert_segment (iter, text + seg_start, i - seg_start, &style, layout_tag);
			i++;
			seg_start = i;
			continue;
		}

		i++;
	}

	if (i > seg_start)
		xtext_insert_segment (iter, text + seg_start, i - seg_start, &style, layout_tag);
}

static void
xtext_render_line (GtkTextIter *iter, const char *line, gsize len, gboolean append_newline)
{
	gsize tab_pos;
	gboolean has_tab;

	if (!iter)
		return;

	has_tab = FALSE;
	tab_pos = 0;
	while (tab_pos < len)
	{
		if (line[tab_pos] == '\t')
		{
			has_tab = TRUE;
			break;
		}
		tab_pos++;
	}

	if (has_tab)
	{
		if (tab_pos > 0)
		{
			if (tag_stamp)
			{
				if (tag_font)
					gtk_text_buffer_insert_with_tags (log_buffer, iter, line, (int) tab_pos,
						tag_stamp, tag_font, NULL);
				else
					gtk_text_buffer_insert_with_tags (log_buffer, iter, line, (int) tab_pos,
						tag_stamp, NULL);
			}
			else
			{
				if (tag_font)
					gtk_text_buffer_insert_with_tags (log_buffer, iter, line, (int) tab_pos,
						tag_font, NULL);
				else
					gtk_text_buffer_insert (log_buffer, iter, line, (int) tab_pos);
			}
		}

		if (tab_pos + 1 < len)
		{
			if (tag_font)
				gtk_text_buffer_insert_with_tags (log_buffer, iter, " ", 1, tag_font, NULL);
			else
				gtk_text_buffer_insert (log_buffer, iter, " ", 1);
			xtext_render_formatted (iter, line + tab_pos + 1, len - tab_pos - 1, NULL);
		}
	}
	else
	{
		xtext_render_formatted (iter, line, len, NULL);
	}

	if (append_newline)
	{
		if (tag_font)
			gtk_text_buffer_insert_with_tags (log_buffer, iter, "\n", 1, tag_font, NULL);
		else
			gtk_text_buffer_insert (log_buffer, iter, "\n", 1);
	}
}

static void
xtext_render_raw_at_iter (GtkTextIter *iter, const char *raw)
{
	const char *cursor;

	if (!iter || !raw)
		return;

	cursor = raw;
	while (*cursor)
	{
		const char *nl;
		gsize len;
		gboolean add_nl;

		nl = strchr (cursor, '\n');
		if (nl)
		{
			len = (gsize) (nl - cursor);
			add_nl = TRUE;
		}
		else
		{
			len = strlen (cursor);
			add_nl = FALSE;
		}

		xtext_render_line (iter, cursor, len, add_nl);

		if (!nl)
			break;
		cursor = nl + 1;
	}
}

static void
xtext_render_raw_all (const char *raw)
{
	GtkTextIter start;
	GtkTextIter end;

	if (!log_buffer)
		return;

	gtk_text_buffer_get_bounds (log_buffer, &start, &end);
	gtk_text_buffer_delete (log_buffer, &start, &end);

	gtk_text_buffer_get_end_iter (log_buffer, &end);
	xtext_render_raw_at_iter (&end, raw ? raw : "");
}

static void
xtext_render_raw_append (const char *raw)
{
	GtkTextIter end;

	if (!log_buffer)
		return;

	gtk_text_buffer_get_end_iter (log_buffer, &end);
	xtext_render_raw_at_iter (&end, raw ? raw : "");
}

static void
xtext_scroll_to_end (void)
{
	GtkTextIter end;

	if (!log_buffer || !log_view)
		return;

	gtk_text_buffer_get_end_iter (log_buffer, &end);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (log_view), &end, 0.0, FALSE, 0.0, 1.0);
}

static void
xtext_apply_font_pref (void)
{
	PangoFontDescription *desc;

	if (!log_view || !tag_font)
		return;

	desc = NULL;
	if (prefs.hex_text_font[0])
		desc = pango_font_description_from_string (prefs.hex_text_font);

	gtk_text_view_set_monospace (GTK_TEXT_VIEW (log_view), desc ? FALSE : TRUE);
	g_object_set (tag_font, "font-desc", desc, NULL);

	if (desc)
		pango_font_description_free (desc);
}

static void
session_log_trim_head_lines (GString *log, int lines)
{
	gsize cut;

	if (!log || lines <= 0 || log->len == 0)
		return;

	cut = 0;
	while (lines > 0 && cut < log->len)
	{
		while (cut < log->len && log->str[cut] != '\n')
			cut++;
		if (cut < log->len)
			cut++;
		lines--;
	}

	if (cut > 0)
		g_string_erase (log, 0, cut);
}

static void
session_log_trim_tail_lines (GString *log, int lines)
{
	gsize cut;

	if (!log || lines <= 0 || log->len == 0)
		return;

	cut = log->len;
	while (lines > 0 && cut > 0)
	{
		if (cut > 0 && log->str[cut - 1] == '\n')
			cut--;
		while (cut > 0 && log->str[cut - 1] != '\n')
			cut--;
		lines--;
	}

	g_string_truncate (log, cut);
}

void
fe_gtk4_xtext_init (void)
{
	if (!session_logs)
		session_logs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			NULL, session_log_free);
	if (!color_tags)
		color_tags = g_hash_table_new (g_direct_hash, g_direct_equal);
}

void
fe_gtk4_xtext_cleanup (void)
{
	if (session_logs)
	{
		g_hash_table_unref (session_logs);
		session_logs = NULL;
	}
	if (color_tags)
	{
		g_hash_table_unref (color_tags);
		color_tags = NULL;
	}

	tag_stamp = NULL;
	tag_bold = NULL;
	tag_italic = NULL;
	tag_underline = NULL;
	tag_font = NULL;
	xtext_search_mark = NULL;
	g_free (xtext_search_text);
	xtext_search_text = NULL;
}

GtkWidget *
fe_gtk4_xtext_create_widget (void)
{
	GtkWidget *scroll;
	GdkRGBA stamp_rgba;

	scroll = gtk_scrolled_window_new ();
	gtk_widget_set_hexpand (scroll, TRUE);
	gtk_widget_set_vexpand (scroll, TRUE);

	log_view = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (log_view), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (log_view), FALSE);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (log_view), TRUE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (log_view), GTK_WRAP_WORD_CHAR);
	{
		GtkGesture *gesture;

		gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
		g_signal_connect (gesture, "pressed", G_CALLBACK (xtext_right_click_cb), NULL);
		gtk_widget_add_controller (log_view, GTK_EVENT_CONTROLLER (gesture));
	}
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), log_view);

	log_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (log_view));

	tag_bold = gtk_text_buffer_create_tag (log_buffer, "hc-bold",
		"weight", PANGO_WEIGHT_BOLD,
		NULL);
	tag_italic = gtk_text_buffer_create_tag (log_buffer, "hc-italic",
		"style", PANGO_STYLE_ITALIC,
		NULL);
	tag_underline = gtk_text_buffer_create_tag (log_buffer, "hc-underline",
		"underline", PANGO_UNDERLINE_SINGLE,
		NULL);
	tag_font = gtk_text_buffer_create_tag (log_buffer, "hc-font", NULL);

	xtext_color_to_rgba (COL_FG, &stamp_rgba);
	tag_stamp = gtk_text_buffer_create_tag (log_buffer, "hc-stamp",
		"foreground-rgba", &stamp_rgba,
		NULL);

	if (color_tags)
		g_hash_table_remove_all (color_tags);

	xtext_search_mark = NULL;
	xtext_apply_font_pref ();

	return scroll;
}

void
fe_gtk4_xtext_apply_prefs (void)
{
	xtext_apply_font_pref ();
	if (current_tab && is_session (current_tab))
		fe_gtk4_xtext_show_session (current_tab);
}

void
fe_gtk4_append_log_text (const char *text)
{
	if (!text)
		return;

	if (!log_buffer || !log_view)
	{
		fputs (text, stdout);
		fflush (stdout);
		return;
	}

	xtext_render_raw_append (text);
	xtext_scroll_to_end ();
}

void
fe_gtk4_xtext_append_for_session (session *sess, const char *text)
{
	GString *log;

	if (!text || !text[0])
		return;

	if (!sess || !is_session (sess))
	{
		fe_gtk4_append_log_text (text);
		return;
	}

	log = session_log_ensure (sess);
	if (log)
		g_string_append (log, text);

	if (sess == current_tab)
	{
		xtext_render_raw_append (text);
		xtext_scroll_to_end ();
	}
}

void
fe_gtk4_xtext_show_session (session *sess)
{
	GString *log;

	if (!log_buffer)
		return;

	log = NULL;
	if (session_logs && sess)
		log = g_hash_table_lookup (session_logs, sess);

	xtext_render_raw_all (log ? log->str : "");
	xtext_scroll_to_end ();
}

void
fe_gtk4_xtext_remove_session (session *sess)
{
	if (!session_logs || !sess)
		return;

	g_hash_table_remove (session_logs, sess);
}

const char *
fe_gtk4_xtext_get_session_text (session *sess)
{
	GString *log;

	if (!session_logs || !sess)
		return "";

	log = g_hash_table_lookup (session_logs, sess);
	return log ? log->str : "";
}

void
fe_gtk4_xtext_clear_session (session *sess, int lines)
{
	GString *log;

	if (!sess)
		sess = current_tab;

	if (!sess || !is_session (sess))
	{
		if (lines == 0 && log_buffer)
			gtk_text_buffer_set_text (log_buffer, "", -1);
		return;
	}

	log = session_log_ensure (sess);
	if (!log)
		return;

	if (lines == 0)
		g_string_truncate (log, 0);
	else if (lines > 0)
		session_log_trim_head_lines (log, lines);
	else
		session_log_trim_tail_lines (log, -lines);

	if (sess == current_tab)
		fe_gtk4_xtext_show_session (sess);
}

static void
xtext_search_mark_set (const GtkTextIter *iter)
{
	if (!log_buffer || !iter)
		return;

	if (!xtext_search_mark)
		xtext_search_mark = gtk_text_buffer_create_mark (log_buffer, NULL, iter, TRUE);
	else
		gtk_text_buffer_move_mark (log_buffer, xtext_search_mark, iter);
}

static gboolean
xtext_search_find (gboolean forward)
{
	GtkTextIter start;
	GtkTextIter match_start;
	GtkTextIter match_end;
	GtkTextIter begin;
	GtkTextIter end;
	GtkTextSearchFlags flags;
	gboolean found;

	if (!log_buffer || !log_view || !xtext_search_text || !xtext_search_text[0])
		return FALSE;

	flags = GTK_TEXT_SEARCH_TEXT_ONLY;
	if (!prefs.hex_text_search_case_match)
		flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;

	if (xtext_search_mark)
	{
		gtk_text_buffer_get_iter_at_mark (log_buffer, &start, xtext_search_mark);
	}
	else if (gtk_text_buffer_get_has_selection (log_buffer))
	{
		gtk_text_buffer_get_selection_bounds (log_buffer, &match_start, &match_end);
		start = forward ? match_end : match_start;
	}
	else
	{
		if (forward)
			gtk_text_buffer_get_start_iter (log_buffer, &start);
		else
			gtk_text_buffer_get_end_iter (log_buffer, &start);
	}

	if (forward)
		found = gtk_text_iter_forward_search (&start, xtext_search_text, flags,
			&match_start, &match_end, NULL);
	else
		found = gtk_text_iter_backward_search (&start, xtext_search_text, flags,
			&match_start, &match_end, NULL);

	if (!found)
	{
		gtk_text_buffer_get_start_iter (log_buffer, &begin);
		gtk_text_buffer_get_end_iter (log_buffer, &end);
		start = forward ? begin : end;
		if (forward)
			found = gtk_text_iter_forward_search (&start, xtext_search_text, flags,
				&match_start, &match_end, NULL);
		else
			found = gtk_text_iter_backward_search (&start, xtext_search_text, flags,
				&match_start, &match_end, NULL);
	}

	if (!found)
		return FALSE;

	gtk_text_buffer_select_range (log_buffer, &match_start, &match_end);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (log_view), &match_start, 0.2, FALSE, 0.0, 0.0);
	xtext_search_mark_set (forward ? &match_end : &match_start);
	return TRUE;
}

static void
xtext_search_prompt_cb (int cancel, char *text, gpointer userdata)
{
	GtkTextIter start;

	(void) userdata;

	if (cancel)
		return;

	g_free (xtext_search_text);
	xtext_search_text = g_strdup (text ? text : "");

	if (!xtext_search_text[0])
		return;

	if (log_buffer)
	{
		gtk_text_buffer_get_start_iter (log_buffer, &start);
		xtext_search_mark_set (&start);
	}

	if (!xtext_search_find (TRUE))
		fe_message (_("No matches found."), FE_MSG_INFO);
}

void
fe_gtk4_xtext_search_prompt (void)
{
	fe_get_str (_("Search for:"), xtext_search_text ? xtext_search_text : "",
		xtext_search_prompt_cb, NULL);
}

void
fe_gtk4_xtext_search_next (void)
{
	if ((!xtext_search_text || !xtext_search_text[0]) && log_view)
	{
		fe_gtk4_xtext_search_prompt ();
		return;
	}

	if (!xtext_search_find (TRUE))
		fe_message (_("No matches found."), FE_MSG_INFO);
}

void
fe_gtk4_xtext_search_prev (void)
{
	if ((!xtext_search_text || !xtext_search_text[0]) && log_view)
	{
		fe_gtk4_xtext_search_prompt ();
		return;
	}

	if (!xtext_search_find (FALSE))
		fe_message (_("No matches found."), FE_MSG_INFO);
}

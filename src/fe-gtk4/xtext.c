/* HexChat GTK4 text view/log buffer */
#include "fe-gtk4.h"
#include "../common/url.h"
#include "../common/userlist.h"

#define XTEXT_UI_PATH "/org/hexchat/ui/gtk4/maingui/xtext-scroll.ui"

typedef struct
{
	int fg;
	int bg;
	gboolean bold;
	gboolean italic;
	gboolean underline;
	gboolean reverse;
} HcTextStyle;

typedef struct
{
	const char *stamp;
	gsize stamp_len;
	const char *prefix;
	gsize prefix_len;
	const char *body;
	gsize body_len;
	gboolean has_columns;
} HcLineColumns;

typedef struct
{
	GString *plain;
	GArray *raw_map; /* guint byte offset in raw text for each plain byte (+sentinel at end). */
} HcVisibleMap;

#define HC_WRAP_RIGHT_PAD_PX 8
#define HC_WRAP_MIN_CONTENT_PX 80
#define HC_PREFIX_MAX_CHARS 32
#define HC_PREFIX_MAX_WORDS 2
#define HC_PREFIX_TWO_WORD_MAX_CHARS 16
#define HC_SPACE_WIDTH_FALLBACK_PX 6
#define HC_IRC_COLOR_COUNT 32
#define HC_COLOR_KEY_MASK G_MAXUINT8
#define HC_ASCII_PRINTABLE_MIN ((unsigned char) ' ')
#define HC_UTF8_LEAD_MIN 128
#define HC_NICK_PREFIXES "~+!@%&"

#define HC_IRC_CTRL_BOLD ((unsigned char) '\x02')
#define HC_IRC_CTRL_COLOR ((unsigned char) '\x03')
#define HC_IRC_CTRL_HEX_COLOR ((unsigned char) '\x04')
#define HC_IRC_CTRL_BELL ((unsigned char) '\x07')
#define HC_IRC_CTRL_RESET ((unsigned char) '\x0f')
#define HC_IRC_CTRL_REVERSE ((unsigned char) '\x16')
#define HC_IRC_CTRL_ITALIC ((unsigned char) '\x1d')
#define HC_IRC_CTRL_UNDERLINE ((unsigned char) '\x1f')

static GHashTable *session_logs;
static GHashTable *color_tags;
static GtkTextTag *tag_stamp;
static GtkTextTag *tag_bold;
static GtkTextTag *tag_italic;
static GtkTextTag *tag_underline;
static GtkTextTag *tag_link_hover;
static GtkTextTag *tag_nick_column;
static GtkTextTag *tag_font;
static PangoFontDescription *xtext_font_desc;
static int xtext_space_width_px;
static int xtext_message_col_px;
static char *xtext_search_text;
static GtkTextMark *xtext_search_mark;
static GtkTextMark *xtext_hover_start_mark;
static GtkTextMark *xtext_hover_end_mark;
static char *xtext_primary_pending_target;
static int xtext_secondary_pending_type;
static char *xtext_secondary_pending_target;
static double xtext_secondary_pending_x;
static double xtext_secondary_pending_y;
static guint xtext_resize_tick_id;
static guint xtext_resize_idle_id;
static int xtext_last_view_width;
static session *xtext_render_session;

static void xtext_render_raw_append (const char *raw);
static gboolean xtext_parse_color_number (const char *text, gsize len, gsize *index, int *value);
static void xtext_tabs_to_spaces (char *text);

static gboolean
xtext_is_space_char (gunichar ch)
{
	return g_unichar_isspace (ch) ? TRUE : FALSE;
}

static gboolean
xtext_is_nick_lead_delim (gunichar ch)
{
	switch (ch)
	{
	case '<':
	case '(':
	case '[':
	case '{':
	case '"':
	case '\'':
	case '`':
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
xtext_is_nick_trail_delim (gunichar ch)
{
	switch (ch)
	{
	case '>':
	case ')':
	case ']':
	case '}':
	case ':':
	case ';':
	case ',':
	case '.':
	case '!':
	case '?':
	case '"':
	case '\'':
	case '`':
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
xtext_session_has_nick (session *sess, const char *nick)
{
	if (!sess || !is_session (sess) || !nick || !nick[0])
		return FALSE;

	if (userlist_find (sess, (char *) nick))
		return TRUE;
	if (sess->server && userlist_find_global (sess->server, (char *) nick))
		return TRUE;
	if (sess->type == SESS_DIALOG && sess->channel[0] && rfc_casecmp (sess->channel, nick) == 0)
		return TRUE;

	return FALSE;
}

static gboolean
xtext_extract_nick_token (session *sess, const char *token, gsize *start_out, gsize *end_out, char **nick_out)
{
	const char *start_ptr;
	const char *nick_start;
	const char *end_ptr;
	char *nick;

	if (start_out)
		*start_out = 0;
	if (end_out)
		*end_out = 0;
	if (nick_out)
		*nick_out = NULL;

	if (!sess || !is_session (sess) || !token || !token[0])
		return FALSE;

	start_ptr = token;
	end_ptr = token + strlen (token);

	while (start_ptr < end_ptr)
	{
		gunichar ch;
		const char *next;

		ch = g_utf8_get_char (start_ptr);
		if (!xtext_is_nick_lead_delim (ch))
			break;
		next = g_utf8_next_char (start_ptr);
		start_ptr = (next > start_ptr) ? next : start_ptr + 1;
	}

	while (end_ptr > start_ptr)
	{
		const char *prev;
		gunichar ch;

		prev = g_utf8_find_prev_char (token, end_ptr);
		if (!prev)
			break;
		ch = g_utf8_get_char (prev);
		if (!xtext_is_nick_trail_delim (ch))
			break;
		end_ptr = prev;
	}

	if (end_ptr <= start_ptr)
		return FALSE;

	nick_start = start_ptr;
	nick = g_strndup (nick_start, (gsize) (end_ptr - nick_start));
	if (!nick[0])
	{
		g_free (nick);
		return FALSE;
	}

	if (!xtext_session_has_nick (sess, nick))
	{
		g_free (nick);
		nick = NULL;

		if (strchr (HC_NICK_PREFIXES, *nick_start) != NULL)
		{
			const char *next;

			next = g_utf8_next_char (nick_start);
			if (next < end_ptr)
			{
				nick_start = next;
				nick = g_strndup (nick_start, (gsize) (end_ptr - nick_start));
			}
		}
	}

	if (!nick || !nick[0] || !xtext_session_has_nick (sess, nick))
	{
		g_free (nick);
		return FALSE;
	}

	if (start_out)
		*start_out = (gsize) (nick_start - token);
	if (end_out)
		*end_out = (gsize) (end_ptr - token);
	if (nick_out)
		*nick_out = nick;
	else
		g_free (nick);

	return TRUE;
}

static gboolean
xtext_prefix_has_nick (session *sess, const char *prefix, gsize len)
{
	char *clean;
	char *trimmed;
	char *candidate;
	char *nick;
	gboolean has_nick;

	if (!sess || !is_session (sess) || !prefix || len == 0)
		return FALSE;

	clean = strip_color (prefix, (int) len, STRIP_ALL);
	if (!clean)
		return FALSE;

	xtext_tabs_to_spaces (clean);
	trimmed = g_strstrip (clean);
	if (!trimmed[0])
	{
		g_free (clean);
		return FALSE;
	}

	candidate = strrchr (trimmed, ' ');
	candidate = (candidate && candidate[1]) ? (candidate + 1) : trimmed;

	nick = NULL;
	has_nick = xtext_extract_nick_token (sess, candidate, NULL, NULL, &nick);
	g_free (nick);
	g_free (clean);

	return has_nick;
}

static char *
xtext_token_at_point (GtkTextView *view, double x, double y, GtkTextIter *start_out, GtkTextIter *end_out)
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
	if (xtext_is_space_char (ch))
	{
		GtkTextIter prev;

		prev = iter;
		if (!gtk_text_iter_backward_char (&prev))
			return NULL;
		ch = gtk_text_iter_get_char (&prev);
		if (xtext_is_space_char (ch))
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
		if (xtext_is_space_char (prev_ch))
			break;
		start = prev;
	}

	while (!gtk_text_iter_ends_line (&end))
	{
		GtkTextIter next;
		gunichar next_ch;

		next = end;
		next_ch = gtk_text_iter_get_char (&next);
		if (xtext_is_space_char (next_ch))
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

	if (start_out)
		*start_out = start;
	if (end_out)
		*end_out = end;

	return word;
}

static void
xtext_classify_at_point (GtkTextView *view, session *sess, double x, double y, int *type_out, char **target_out,
	GtkTextIter *match_start_out, GtkTextIter *match_end_out)
{
	char *token;
	char *nick_target;
	char *target;
	GtkTextIter token_start;
	GtkTextIter token_end;
	int type;
	int start;
	int end;
	gsize len;
	gsize nick_start;
	gsize nick_end;

	if (type_out)
		*type_out = 0;
	if (target_out)
		*target_out = NULL;
	if (match_start_out)
		gtk_text_buffer_get_start_iter (gtk_text_view_get_buffer (view), match_start_out);
	if (match_end_out)
		gtk_text_buffer_get_start_iter (gtk_text_view_get_buffer (view), match_end_out);

	token = xtext_token_at_point (view, x, y, &token_start, &token_end);
	if (!token)
		return;

	target = NULL;
	nick_target = NULL;
	start = 0;
	end = 0;
	type = url_check_word (token);
	if (type != 0)
		url_last (&start, &end);

	if (type == 0 && xtext_extract_nick_token (sess, token, &nick_start, &nick_end, &nick_target))
	{
		type = WORD_NICK;
		start = (int) nick_start;
		end = (int) nick_end;
		target = nick_target;
		nick_target = NULL;
	}

	if (type == 0 && sess && sess->type == SESS_DIALOG)
		type = WORD_DIALOG;

	if (type == 0)
	{
		g_free (nick_target);
		g_free (token);
		return;
	}

	if (type == WORD_DIALOG)
	{
		target = g_strdup (sess && sess->channel[0] ? sess->channel : "");
		if (match_start_out)
			*match_start_out = token_start;
		if (match_end_out)
			*match_end_out = token_end;
	}
	else
	{
		GtkTextIter match_start;
		GtkTextIter match_end;
		int start_chars;
		int end_chars;

		len = strlen (token);
		if (start < 0 || end <= start || (gsize) end > len)
		{
			start = 0;
			end = (int) len;
		}

		match_start = token_start;
		match_end = token_start;
		start_chars = g_utf8_pointer_to_offset (token, token + start);
		end_chars = g_utf8_pointer_to_offset (token, token + end);
		gtk_text_iter_forward_chars (&match_start, start_chars);
		gtk_text_iter_forward_chars (&match_end, end_chars);
		if (match_start_out)
			*match_start_out = match_start;
		if (match_end_out)
			*match_end_out = match_end;

		if (!target)
			target = g_strndup (token + start, end - start);
		if (type == WORD_EMAIL && target && target[0])
		{
			char *mailto;

			mailto = g_strdup_printf ("mailto:%s", target);
			g_free (target);
			target = mailto;
		}
	}
	g_free (token);

	if (!target || !target[0])
	{
		g_free (target);
		return;
	}

	if (type_out)
		*type_out = type;
	if (target_out)
		*target_out = target;
	else
		g_free (target);
}

static gboolean
xtext_type_is_url_like (int type)
{
	return (type == WORD_URL || type == WORD_HOST || type == WORD_HOST6 || type == WORD_EMAIL);
}

static void
xtext_primary_pending_clear (void)
{
	g_free (xtext_primary_pending_target);
	xtext_primary_pending_target = NULL;
}

static void
xtext_secondary_pending_clear (void)
{
	g_free (xtext_secondary_pending_target);
	xtext_secondary_pending_target = NULL;
	xtext_secondary_pending_type = 0;
	xtext_secondary_pending_x = 0.0;
	xtext_secondary_pending_y = 0.0;
}

static void
xtext_link_hover_clear (void)
{
	GtkTextIter start;
	GtkTextIter end;

	if (!log_buffer || !tag_link_hover || !xtext_hover_start_mark || !xtext_hover_end_mark)
		return;

	gtk_text_buffer_get_iter_at_mark (log_buffer, &start, xtext_hover_start_mark);
	gtk_text_buffer_get_iter_at_mark (log_buffer, &end, xtext_hover_end_mark);
	if (gtk_text_iter_compare (&start, &end) < 0)
		gtk_text_buffer_remove_tag (log_buffer, tag_link_hover, &start, &end);

	gtk_text_buffer_delete_mark (log_buffer, xtext_hover_start_mark);
	gtk_text_buffer_delete_mark (log_buffer, xtext_hover_end_mark);
	xtext_hover_start_mark = NULL;
	xtext_hover_end_mark = NULL;
}

static gboolean
xtext_link_hover_is_same_range (const GtkTextIter *start, const GtkTextIter *end)
{
	GtkTextIter current_start;
	GtkTextIter current_end;

	if (!log_buffer || !xtext_hover_start_mark || !xtext_hover_end_mark)
		return FALSE;

	gtk_text_buffer_get_iter_at_mark (log_buffer, &current_start, xtext_hover_start_mark);
	gtk_text_buffer_get_iter_at_mark (log_buffer, &current_end, xtext_hover_end_mark);

	return gtk_text_iter_equal (&current_start, start) && gtk_text_iter_equal (&current_end, end);
}

static void
xtext_link_hover_set (const GtkTextIter *start, const GtkTextIter *end)
{
	GtkTextIter start_iter;
	GtkTextIter end_iter;

	if (!log_buffer || !tag_link_hover || !start || !end)
	{
		xtext_link_hover_clear ();
		return;
	}
	start_iter = *start;
	end_iter = *end;
	if (gtk_text_iter_compare (&start_iter, &end_iter) >= 0)
	{
		xtext_link_hover_clear ();
		return;
	}

	if (xtext_link_hover_is_same_range (start, end))
		return;

	xtext_link_hover_clear ();
	gtk_text_buffer_apply_tag (log_buffer, tag_link_hover, &start_iter, &end_iter);
	xtext_hover_start_mark = gtk_text_buffer_create_mark (log_buffer, NULL, &start_iter, TRUE);
	xtext_hover_end_mark = gtk_text_buffer_create_mark (log_buffer, NULL, &end_iter, FALSE);
}

static void
xtext_primary_press_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	session *sess;
	int type;
	char *target;

	(void) user_data;

	if (n_press != 1 || !log_view)
		return;

	xtext_primary_pending_clear ();

	sess = current_tab;
	if (!sess || !is_session (sess))
		return;

	xtext_classify_at_point (GTK_TEXT_VIEW (log_view), sess, x, y, &type, &target, NULL, NULL);
	if (!target)
		return;

	if (xtext_type_is_url_like (type))
	{
		xtext_primary_pending_target = target;
		gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	}
	else
	{
		g_free (target);
	}
}

static void
xtext_primary_release_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	(void) x;
	(void) y;
	(void) user_data;

	if (n_press != 1 || !xtext_primary_pending_target)
		return;

	fe_open_url (xtext_primary_pending_target);
	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	xtext_primary_pending_clear ();
}

static void
xtext_secondary_press_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	session *sess;
	int type;
	char *target;

	(void) user_data;

	if (n_press != 1 || !log_view)
		return;

	xtext_secondary_pending_clear ();

	sess = current_tab;
	if (!sess || !is_session (sess))
		return;

	xtext_classify_at_point (GTK_TEXT_VIEW (log_view), sess, x, y, &type, &target, NULL, NULL);
	if (!target)
		return;

	if (xtext_type_is_url_like (type) || type == WORD_NICK || type == WORD_DIALOG)
	{
		xtext_secondary_pending_type = type;
		xtext_secondary_pending_target = target;
		xtext_secondary_pending_x = x;
		xtext_secondary_pending_y = y;
		gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	}
	else
	{
		g_free (target);
	}
}

static void
xtext_secondary_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	session *sess;

	(void) user_data;

	if (n_press != 1 || !log_view || !xtext_secondary_pending_target)
		return;

	sess = current_tab;
	if (!sess || !is_session (sess))
	{
		xtext_secondary_pending_clear ();
		return;
	}

	if (xtext_type_is_url_like (xtext_secondary_pending_type))
		fe_gtk4_menu_show_urlmenu (log_view, xtext_secondary_pending_x, xtext_secondary_pending_y,
			sess, xtext_secondary_pending_target);
	else if (xtext_secondary_pending_type == WORD_NICK || xtext_secondary_pending_type == WORD_DIALOG)
		fe_gtk4_menu_show_nickmenu (log_view, xtext_secondary_pending_x, xtext_secondary_pending_y,
			sess, xtext_secondary_pending_target);

	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	xtext_secondary_pending_clear ();
}

static void
xtext_motion_cb (GtkEventControllerMotion *controller, double x, double y, gpointer user_data)
{
	session *sess;
	int type;
	char *target;
	GtkTextIter match_start;
	GtkTextIter match_end;

	(void) controller;
	(void) user_data;

	if (!log_view)
		return;

	sess = current_tab;
	if (!sess || !is_session (sess))
	{
		gtk_widget_set_cursor_from_name (log_view, NULL);
		xtext_link_hover_clear ();
		return;
	}

	xtext_classify_at_point (GTK_TEXT_VIEW (log_view), sess, x, y, &type, &target, &match_start, &match_end);
	if (target && (xtext_type_is_url_like (type) || type == WORD_NICK || type == WORD_DIALOG))
	{
		gtk_widget_set_cursor_from_name (log_view, "pointer");
		xtext_link_hover_set (&match_start, &match_end);
	}
	else
	{
		gtk_widget_set_cursor_from_name (log_view, NULL);
		xtext_link_hover_clear ();
	}

	g_free (target);
}

static void
xtext_motion_leave_cb (GtkEventControllerMotion *controller, gpointer user_data)
{
	(void) controller;
	(void) user_data;

	if (log_view)
		gtk_widget_set_cursor_from_name (log_view, NULL);
	xtext_link_hover_clear ();
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
	if (idx >= HC_IRC_COLOR_COUNT)
		idx %= HC_IRC_COLOR_COUNT;

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
		fg %= HC_IRC_COLOR_COUNT;
	if (bg >= 0)
		bg %= HC_IRC_COLOR_COUNT;

	key = (((guint) (fg + 1)) & HC_COLOR_KEY_MASK) | ((((guint) (bg + 1)) & HC_COLOR_KEY_MASK) << 8);
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
xtext_tabs_to_spaces (char *text)
{
	char *p;

	if (!text)
		return;

	for (p = text; *p; p++)
	{
		if (*p == '\t')
			*p = ' ';
	}
}

static int
xtext_measure_plain_width (const char *text, gsize len)
{
	PangoLayout *layout;
	char *tmp;
	int width;

	if (!log_view || !text || len == 0)
		return 0;

	tmp = g_strndup (text, len);
	xtext_tabs_to_spaces (tmp);

	layout = gtk_widget_create_pango_layout (log_view, tmp);
	if (xtext_font_desc)
		pango_layout_set_font_description (layout, xtext_font_desc);
	pango_layout_get_pixel_size (layout, &width, NULL);
	g_object_unref (layout);
	g_free (tmp);

	return width;
}

static int
xtext_message_wrap_width_px (void)
{
	int avail;

	if (!log_view || !prefs.hex_text_wordwrap || xtext_message_col_px <= 0)
		return 0;

	avail = gtk_widget_get_width (log_view) - xtext_message_col_px - HC_WRAP_RIGHT_PAD_PX;
	if (avail < HC_WRAP_MIN_CONTENT_PX)
		return 0;

	return avail;
}

static gboolean
xtext_prefix_looks_reasonable (const char *text, gsize len)
{
	char *clean;
	char *trimmed;
	int words;
	glong chars;
	gboolean in_word;
	const char *p;
	const char *end;

	if (!text || len == 0)
		return FALSE;

	clean = strip_color (text, (int) len, STRIP_ALL);
	if (!clean)
		return FALSE;

	trimmed = g_strstrip (clean);
	if (!trimmed[0])
	{
		g_free (clean);
		return FALSE;
	}

	chars = g_utf8_strlen (trimmed, -1);
	if (chars <= 0 || chars > HC_PREFIX_MAX_CHARS)
	{
		g_free (clean);
		return FALSE;
	}

	words = 0;
	in_word = FALSE;
	p = trimmed;
	end = trimmed + strlen (trimmed);
	while (p < end)
	{
		gunichar ch;
		gboolean is_space;
		const char *next;

		ch = g_utf8_get_char (p);
		is_space = g_unichar_isspace (ch) ? TRUE : FALSE;
		if (is_space)
		{
			in_word = FALSE;
		}
		else if (!in_word)
		{
			words++;
			in_word = TRUE;
			if (words > HC_PREFIX_MAX_WORDS)
				break;
		}

		next = g_utf8_next_char (p);
		p = next > p ? next : p + 1;
	}

	if (words < 1 || words > HC_PREFIX_MAX_WORDS)
	{
		g_free (clean);
		return FALSE;
	}
	if (words == HC_PREFIX_MAX_WORDS && chars > HC_PREFIX_TWO_WORD_MAX_CHARS)
	{
		g_free (clean);
		return FALSE;
	}

	g_free (clean);
	return TRUE;
}

static int
xtext_line_left_width_px (const HcLineColumns *cols)
{
	int width;
	char *prefix_clean;

	if (!cols || !cols->has_columns)
		return 0;

	width = 0;
	if (cols->stamp_len > 0)
		width += xtext_measure_plain_width (cols->stamp, cols->stamp_len);
	if (cols->prefix_len == 0)
		return width + xtext_space_width_px;

	if (width > 0)
		width += xtext_space_width_px;

	prefix_clean = strip_color (cols->prefix, (int) cols->prefix_len, STRIP_ALL);
	if (!prefix_clean)
		return width + xtext_space_width_px;
	xtext_tabs_to_spaces (prefix_clean);
	width += xtext_measure_plain_width (prefix_clean, strlen (prefix_clean));
	g_free (prefix_clean);

	return width + xtext_space_width_px;
}

static void
xtext_split_line_columns (const char *line, gsize len, HcLineColumns *cols)
{
	const char *tab1;
	const char *tab2;
	const char *rest;
	gsize rest_len;

	memset (cols, 0, sizeof (*cols));
	if (!line || len == 0)
		return;

	tab1 = memchr (line, '\t', len);
	if (!tab1)
		return;

	rest = tab1 + 1;
	rest_len = len - (gsize) (rest - line);

	if (prefs.hex_stamp_text)
	{
		cols->stamp = line;
		cols->stamp_len = (gsize) (tab1 - line);
		cols->body = rest;
		cols->body_len = rest_len;
		cols->has_columns = TRUE;

		if (!prefs.hex_text_indent)
			return;

		tab2 = memchr (rest, '\t', rest_len);
		if (!tab2)
			return;
		if (!xtext_prefix_looks_reasonable (rest, (gsize) (tab2 - rest)))
			return;

		cols->prefix = rest;
		cols->prefix_len = (gsize) (tab2 - rest);
		cols->body = tab2 + 1;
		cols->body_len = len - (gsize) (cols->body - line);
		return;
	}

	if (!prefs.hex_text_indent)
		return;

	cols->prefix = line;
	cols->prefix_len = (gsize) (tab1 - line);
	cols->body = rest;
	cols->body_len = rest_len;
	cols->has_columns = TRUE;
}

static int
xtext_compute_message_column_px (const char *raw)
{
	const char *cursor;
	int col_px;
	int max_indent_px;

	if (!raw || !raw[0] || !log_view)
		return 0;

	col_px = 0;
	max_indent_px = prefs.hex_text_max_indent > 0 ? prefs.hex_text_max_indent : G_MAXINT;

	cursor = raw;
	while (*cursor)
	{
		const char *nl;
		gsize len;
		HcLineColumns cols;

		nl = strchr (cursor, '\n');
		len = nl ? (gsize) (nl - cursor) : strlen (cursor);
		xtext_split_line_columns (cursor, len, &cols);
		if (cols.has_columns)
			col_px = MAX (col_px, xtext_line_left_width_px (&cols));

		if (!nl)
			break;
		cursor = nl + 1;
	}

	if (col_px > max_indent_px)
		col_px = max_indent_px;
	if (col_px < 0)
		col_px = 0;

	return col_px;
}

static void
xtext_set_message_tab_stop (int px)
{
	PangoTabArray *tabs;

	if (!log_view)
		return;

	if (px <= 0)
	{
		gtk_text_view_set_tabs (GTK_TEXT_VIEW (log_view), NULL);
		xtext_message_col_px = 0;
		return;
	}

	tabs = pango_tab_array_new (1, TRUE);
	pango_tab_array_set_tab (tabs, 0, PANGO_TAB_LEFT, px);
	gtk_text_view_set_tabs (GTK_TEXT_VIEW (log_view), tabs);
	pango_tab_array_free (tabs);
	xtext_message_col_px = px;
}

static HcVisibleMap *
xtext_visible_map_build (const char *text, gsize len)
{
	HcVisibleMap *map;
	const char *p;
	const char *end;

	map = g_new0 (HcVisibleMap, 1);
	map->plain = g_string_sized_new (len ? len : 8);
	map->raw_map = g_array_sized_new (FALSE, FALSE, sizeof (guint), len + 1);

	p = text;
	end = text + len;
	while (p < end)
	{
		unsigned char ch;
		const char *next;

		ch = (unsigned char) *p;
		switch (ch)
		{
		case HC_IRC_CTRL_BOLD:
		case HC_IRC_CTRL_ITALIC:
		case HC_IRC_CTRL_UNDERLINE:
		case HC_IRC_CTRL_REVERSE:
		case HC_IRC_CTRL_RESET:
		case HC_IRC_CTRL_BELL:
			p++;
			continue;
		case HC_IRC_CTRL_COLOR:
		{
			int dummy;
			gsize j;

			j = (gsize) ((p + 1) - text);
			if (xtext_parse_color_number (text, len, &j, &dummy))
			{
				if (j < len && text[j] == ',')
				{
					j++;
					(void) xtext_parse_color_number (text, len, &j, &dummy);
				}
			}
			p = text + j;
			continue;
		}
		case HC_IRC_CTRL_HEX_COLOR:
		{
			gsize j;

			j = (gsize) ((p + 1) - text);
			while (j < len && (g_ascii_isxdigit (text[j]) || text[j] == ','))
				j++;
			p = text + j;
			continue;
		}
		case '\t':
		{
			guint pos;

			pos = (guint) (p - text);
			g_string_append_c (map->plain, ' ');
			g_array_append_val (map->raw_map, pos);
			p++;
			continue;
		}
		default:
			break;
		}

		if (ch < HC_ASCII_PRINTABLE_MIN)
		{
			p++;
			continue;
		}

		{
			gsize char_len;
			gsize k;
			gsize offset;

			next = g_utf8_next_char (p);
			char_len = (gsize) (next - p);
			if (char_len == 0 || p + char_len > end)
				char_len = 1;

			offset = (gsize) (p - text);
			g_string_append_len (map->plain, p, char_len);
			for (k = 0; k < char_len; k++)
			{
				guint pos;

				pos = (guint) (offset + k);
				g_array_append_val (map->raw_map, pos);
			}
			p += char_len;
		}
	}

	{
		guint end_pos;

		end_pos = (guint) len;
		g_array_append_val (map->raw_map, end_pos);
	}

	return map;
}

static void
xtext_visible_map_free (HcVisibleMap *map)
{
	if (!map)
		return;

	if (map->plain)
		g_string_free (map->plain, TRUE);
	if (map->raw_map)
		g_array_free (map->raw_map, TRUE);
	g_free (map);
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

static void
xtext_insert_plain_text (GtkTextIter *iter, const char *text, gsize len, gboolean with_stamp_tag)
{
	GtkTextTag *first_tag;
	GtkTextTag *second_tag;

	if (!log_buffer || !iter || !text || len == 0)
		return;

	first_tag = with_stamp_tag ? tag_stamp : NULL;
	second_tag = tag_font;

	if (!first_tag && !second_tag)
	{
		gtk_text_buffer_insert (log_buffer, iter, text, (int) len);
		return;
	}
	if (first_tag && second_tag)
	{
		gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len, first_tag, second_tag, NULL);
		return;
	}

	gtk_text_buffer_insert_with_tags (log_buffer, iter, text, (int) len,
		first_tag ? first_tag : second_tag, NULL);
}

static void
xtext_insert_plain_char (GtkTextIter *iter, char ch)
{
	char s[2];

	s[0] = ch;
	s[1] = 0;
	xtext_insert_plain_text (iter, s, 1, FALSE);
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
	*value = number % HC_IRC_COLOR_COUNT;
	return TRUE;
}

static void
xtext_render_formatted_stateful (GtkTextIter *iter, const char *text, gsize len, GtkTextTag *layout_tag,
	HcTextStyle *style_io)
{
	HcTextStyle style;
	gsize i;
	gsize seg_start;

	if (!text || len == 0)
		return;

	if (style_io)
		style = *style_io;
	else
		xtext_reset_style (&style);
	seg_start = 0;
	i = 0;

	while (i < len)
	{
		unsigned char ch;
		gsize next_i;

		ch = (unsigned char) text[i];
		switch (ch)
		{
		case HC_IRC_CTRL_BOLD:
		case HC_IRC_CTRL_ITALIC:
		case HC_IRC_CTRL_UNDERLINE:
		case HC_IRC_CTRL_REVERSE:
		case HC_IRC_CTRL_RESET:
		case HC_IRC_CTRL_COLOR:
		case HC_IRC_CTRL_HEX_COLOR:
		{
			if (i > seg_start)
				xtext_insert_segment (iter, text + seg_start, i - seg_start, &style, layout_tag);
			switch (ch)
			{
			case HC_IRC_CTRL_BOLD:
				style.bold = !style.bold;
				i++;
				break;
			case HC_IRC_CTRL_ITALIC:
				style.italic = !style.italic;
				i++;
				break;
			case HC_IRC_CTRL_UNDERLINE:
				style.underline = !style.underline;
				i++;
				break;
			case HC_IRC_CTRL_REVERSE:
				style.reverse = !style.reverse;
				i++;
				break;
			case HC_IRC_CTRL_RESET:
				xtext_reset_style (&style);
				i++;
				break;
			case HC_IRC_CTRL_COLOR:
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
					break;
				}

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
				break;
			}
			default:
			{
				gsize j;

				/* Hex color sequence (rare); strip it safely. */
				j = i + 1;
				while (j < len && (g_ascii_isxdigit (text[j]) || text[j] == ','))
					j++;
				style.fg = -1;
				style.bg = -1;
				i = j;
				break;
			}
			}
			seg_start = i;
			continue;
		}
			case '\t':
			{
				if (i > seg_start)
					xtext_insert_segment (iter, text + seg_start, i - seg_start, &style, layout_tag);
				xtext_insert_segment (iter, " ", 1, &style, layout_tag);
				i++;
				seg_start = i;
				continue;
			}
			default:
				break;
			}

		if (ch < HC_ASCII_PRINTABLE_MIN)
		{
			if (i > seg_start)
				xtext_insert_segment (iter, text + seg_start, i - seg_start, &style, layout_tag);
			i++;
			seg_start = i;
			continue;
		}

		next_i = i + 1;
		if (ch >= HC_UTF8_LEAD_MIN)
		{
			next_i = i + (gsize) g_utf8_skip[ch];
			if (next_i <= i || next_i > len)
				next_i = i + 1;
		}
		i = next_i;
	}

	if (i > seg_start)
		xtext_insert_segment (iter, text + seg_start, i - seg_start, &style, layout_tag);

	if (style_io)
		*style_io = style;
}

static void
xtext_render_formatted (GtkTextIter *iter, const char *text, gsize len, GtkTextTag *layout_tag)
{
	xtext_render_formatted_stateful (iter, text, len, layout_tag, NULL);
}

static void
xtext_render_formatted_wrapped (GtkTextIter *iter, const char *text, gsize len, GtkTextTag *layout_tag)
{
	int wrap_width;
	HcVisibleMap *map;
	PangoLayout *layout;
	int line_count;
	int line_idx;
	gsize raw_start;
	HcTextStyle style;

	wrap_width = xtext_message_wrap_width_px ();
	if (wrap_width <= 0 || !text || len == 0)
	{
		xtext_render_formatted (iter, text, len, layout_tag);
		return;
	}

	map = xtext_visible_map_build (text, len);
	if (!map || map->plain->len == 0)
	{
		xtext_visible_map_free (map);
		xtext_render_formatted (iter, text, len, layout_tag);
		return;
	}

	layout = gtk_widget_create_pango_layout (log_view, map->plain->str);
	if (xtext_font_desc)
		pango_layout_set_font_description (layout, xtext_font_desc);
	pango_layout_set_wrap (layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_width (layout, wrap_width * PANGO_SCALE);

	line_count = pango_layout_get_line_count (layout);
	if (line_count <= 1)
	{
		g_object_unref (layout);
		xtext_visible_map_free (map);
		xtext_render_formatted (iter, text, len, layout_tag);
		return;
	}

	raw_start = 0;
	xtext_reset_style (&style);
	for (line_idx = 1; line_idx < line_count; line_idx++)
	{
		PangoLayoutLine *line;
		int start_index;
		gsize raw_break;

		line = pango_layout_get_line_readonly (layout, line_idx);
		if (!line)
			continue;
		start_index = line->start_index;
		if (start_index <= 0 || (gsize) start_index >= map->raw_map->len)
			continue;

		raw_break = g_array_index (map->raw_map, guint, (gsize) start_index);
		if (raw_break <= raw_start || raw_break > len)
			continue;

		xtext_render_formatted_stateful (iter, text + raw_start, raw_break - raw_start, layout_tag, &style);
		xtext_insert_plain_char (iter, '\n');
		xtext_insert_plain_char (iter, '\t');
		raw_start = raw_break;
	}

	if (raw_start < len)
		xtext_render_formatted_stateful (iter, text + raw_start, len - raw_start, layout_tag, &style);

	g_object_unref (layout);
	xtext_visible_map_free (map);
}

static void
xtext_render_line (GtkTextIter *iter, const char *line, gsize len, gboolean append_newline)
{
	HcLineColumns cols;
	session *render_sess;
	GtkTextTag *prefix_tag;

	if (!iter)
		return;

	xtext_split_line_columns (line, len, &cols);
	if (cols.has_columns)
	{
		if (cols.stamp && cols.stamp_len > 0)
			xtext_insert_plain_text (iter, cols.stamp, cols.stamp_len, TRUE);

		if (cols.prefix && cols.prefix_len > 0)
		{
			if (cols.stamp && cols.stamp_len > 0)
				xtext_insert_plain_char (iter, ' ');
			render_sess = xtext_render_session;
			if (!render_sess || !is_session (render_sess))
				render_sess = current_tab;
			prefix_tag = NULL;
			if (tag_nick_column && render_sess && xtext_prefix_has_nick (render_sess, cols.prefix, cols.prefix_len))
				prefix_tag = tag_nick_column;
			xtext_render_formatted (iter, cols.prefix, cols.prefix_len, prefix_tag);
		}

		if (cols.body && cols.body_len > 0)
		{
			xtext_insert_plain_char (iter, '\t');
			if (prefs.hex_text_wordwrap)
				xtext_render_formatted_wrapped (iter, cols.body, cols.body_len, NULL);
			else
				xtext_render_formatted (iter, cols.body, cols.body_len, NULL);
		}
	}
	else
		xtext_render_formatted (iter, line, len, NULL);

	if (append_newline)
		xtext_insert_plain_char (iter, '\n');
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
	int col_px;
	const char *text;

	if (!log_buffer)
		return;

	text = raw ? raw : "";
	col_px = xtext_compute_message_column_px (text);
	xtext_set_message_tab_stop (col_px);

	xtext_link_hover_clear ();

	gtk_text_buffer_get_bounds (log_buffer, &start, &end);
	gtk_text_buffer_delete (log_buffer, &start, &end);

	gtk_text_buffer_get_end_iter (log_buffer, &end);
	xtext_render_raw_at_iter (&end, text);
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
xtext_show_session_rendered (session *sess, gboolean keep_scroll)
{
	GString *log;
	GtkAdjustment *vadj;
	double old_value;
	double upper;
	double page;
	double max_value;
	gboolean was_at_end;

	if (!log_buffer)
		return;

	log = NULL;
	if (session_logs && sess)
		log = g_hash_table_lookup (session_logs, sess);

	vadj = NULL;
	old_value = 0.0;
	was_at_end = TRUE;
	if (keep_scroll && log_view)
	{
		vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (log_view));
		if (vadj)
		{
			old_value = gtk_adjustment_get_value (vadj);
			upper = gtk_adjustment_get_upper (vadj);
			page = gtk_adjustment_get_page_size (vadj);
			was_at_end = (old_value + page) >= (upper - 2.0);
		}
	}

	xtext_render_session = (sess && is_session (sess)) ? sess : NULL;
	xtext_render_raw_all (log ? log->str : "");
	xtext_render_session = NULL;

	if (!keep_scroll || was_at_end || !vadj)
	{
		xtext_scroll_to_end ();
		return;
	}

	upper = gtk_adjustment_get_upper (vadj);
	page = gtk_adjustment_get_page_size (vadj);
	max_value = upper - page;
	if (max_value < 0.0)
		max_value = 0.0;
	if (old_value > max_value)
		old_value = max_value;
	if (old_value < 0.0)
		old_value = 0.0;

	gtk_adjustment_set_value (vadj, old_value);
}

static gboolean
xtext_resize_refresh_idle_cb (gpointer user_data)
{
	(void) user_data;
	xtext_resize_idle_id = 0;

	if (!log_view || !current_tab || !is_session (current_tab))
		return G_SOURCE_REMOVE;

	xtext_show_session_rendered (current_tab, TRUE);
	return G_SOURCE_REMOVE;
}

static gboolean
xtext_resize_tick_cb (GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
	int width;

	(void) frame_clock;
	(void) user_data;

	if (!widget || widget != log_view)
		return G_SOURCE_CONTINUE;

	width = gtk_widget_get_width (widget);
	if (width <= 0 || width == xtext_last_view_width)
		return G_SOURCE_CONTINUE;

	xtext_last_view_width = width;
	if (xtext_resize_idle_id == 0)
		xtext_resize_idle_id = g_idle_add (xtext_resize_refresh_idle_cb, NULL);

	return G_SOURCE_CONTINUE;
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
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (log_view),
		prefs.hex_text_wordwrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
	g_object_set (tag_font, "font-desc", desc, NULL);

	if (xtext_font_desc)
		pango_font_description_free (xtext_font_desc);
	xtext_font_desc = desc ? pango_font_description_copy (desc) : NULL;

	xtext_space_width_px = xtext_measure_plain_width (" ", 1);
	if (xtext_space_width_px <= 0)
		xtext_space_width_px = HC_SPACE_WIDTH_FALLBACK_PX;

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
	if (xtext_space_width_px <= 0)
		xtext_space_width_px = HC_SPACE_WIDTH_FALLBACK_PX;
	xtext_message_col_px = 0;
	xtext_render_session = NULL;
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
	if (xtext_font_desc)
	{
		pango_font_description_free (xtext_font_desc);
		xtext_font_desc = NULL;
	}

	tag_stamp = NULL;
	tag_bold = NULL;
	tag_italic = NULL;
	tag_underline = NULL;
	tag_link_hover = NULL;
	tag_nick_column = NULL;
	tag_font = NULL;
	xtext_search_mark = NULL;
	xtext_hover_start_mark = NULL;
	xtext_hover_end_mark = NULL;
	xtext_primary_pending_clear ();
	xtext_secondary_pending_clear ();
	g_free (xtext_search_text);
	xtext_search_text = NULL;
	if (xtext_resize_idle_id != 0)
	{
		g_source_remove (xtext_resize_idle_id);
		xtext_resize_idle_id = 0;
	}
	if (log_view && xtext_resize_tick_id != 0)
	{
		gtk_widget_remove_tick_callback (log_view, xtext_resize_tick_id);
		xtext_resize_tick_id = 0;
	}
	xtext_space_width_px = 0;
	xtext_message_col_px = 0;
	xtext_last_view_width = -1;
	xtext_render_session = NULL;
}

GtkWidget *
fe_gtk4_xtext_create_widget (void)
{
	GtkBuilder *builder;
	GtkWidget *scroll;
	GdkRGBA stamp_rgba;

	if (log_view && xtext_resize_tick_id != 0)
	{
		gtk_widget_remove_tick_callback (log_view, xtext_resize_tick_id);
		xtext_resize_tick_id = 0;
	}
	if (xtext_resize_idle_id != 0)
	{
		g_source_remove (xtext_resize_idle_id);
		xtext_resize_idle_id = 0;
	}
	xtext_last_view_width = -1;

	builder = fe_gtk4_builder_new_from_resource (XTEXT_UI_PATH);
	scroll = fe_gtk4_builder_get_widget (builder, "xtext_scroll", GTK_TYPE_SCROLLED_WINDOW);
	log_view = fe_gtk4_builder_get_widget (builder, "xtext_log_view", GTK_TYPE_TEXT_VIEW);
	g_object_ref (scroll);
	g_object_unref (builder);

	gtk_text_view_set_tabs (GTK_TEXT_VIEW (log_view), NULL);
	{
		GtkGesture *gesture;
		GtkEventController *motion;

		gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_PRIMARY);
		g_signal_connect (gesture, "pressed", G_CALLBACK (xtext_primary_press_cb), NULL);
		g_signal_connect (gesture, "released", G_CALLBACK (xtext_primary_release_cb), NULL);
		gtk_widget_add_controller (log_view, GTK_EVENT_CONTROLLER (gesture));

		gesture = gtk_gesture_click_new ();
		gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
		gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
		g_signal_connect (gesture, "pressed", G_CALLBACK (xtext_secondary_press_cb), NULL);
		g_signal_connect (gesture, "released", G_CALLBACK (xtext_secondary_click_cb), NULL);
		gtk_widget_add_controller (log_view, GTK_EVENT_CONTROLLER (gesture));

		motion = gtk_event_controller_motion_new ();
		g_signal_connect (motion, "motion", G_CALLBACK (xtext_motion_cb), NULL);
		g_signal_connect (motion, "leave", G_CALLBACK (xtext_motion_leave_cb), NULL);
		gtk_widget_add_controller (log_view, motion);
	}

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
	tag_link_hover = gtk_text_buffer_create_tag (log_buffer, "hc-link-hover",
		"underline", PANGO_UNDERLINE_SINGLE,
		NULL);
	tag_nick_column = gtk_text_buffer_create_tag (log_buffer, "hc-nick-column",
		"weight", PANGO_WEIGHT_SEMIBOLD,
		NULL);
	tag_font = gtk_text_buffer_create_tag (log_buffer, "hc-font", NULL);

	xtext_color_to_rgba (COL_FG, &stamp_rgba);
	tag_stamp = gtk_text_buffer_create_tag (log_buffer, "hc-stamp",
		"foreground-rgba", &stamp_rgba,
		NULL);

	if (color_tags)
		g_hash_table_remove_all (color_tags);

	xtext_search_mark = NULL;
	xtext_hover_start_mark = NULL;
	xtext_hover_end_mark = NULL;
	xtext_message_col_px = 0;
	xtext_resize_tick_id = gtk_widget_add_tick_callback (log_view, xtext_resize_tick_cb, NULL, NULL);
	xtext_render_session = NULL;
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

	xtext_render_session = (current_tab && is_session (current_tab)) ? current_tab : NULL;
	xtext_render_raw_append (text);
	xtext_render_session = NULL;
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
		if (log)
		{
			int added_col_px;

			added_col_px = xtext_compute_message_column_px (text);
			if (added_col_px > xtext_message_col_px)
			{
				/* Re-render to apply a wider tab stop uniformly. */
				fe_gtk4_xtext_show_session (sess);
				return;
			}
		}

		xtext_render_raw_append (text);
		xtext_scroll_to_end ();
	}
}

void
fe_gtk4_xtext_show_session (session *sess)
{
	xtext_show_session_rendered (sess, FALSE);
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

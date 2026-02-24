/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 text view/log buffer */

#include "fe-gtk4.h"
#include "../common/url.h"
#include "../common/userlist.h"

#define XTEXT_UI_PATH "/org/ditrigon/ui/gtk4/maingui/xtext-scroll.ui"

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
	GtkWidget *scroll;
	GtkWidget *view;
} HcSessionWidget;

#define HC_STICKY_BOTTOM_EPSILON_PX 70.0
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
static GHashTable *session_buffers;
static GHashTable *session_buffers_dirty;
static GHashTable *session_widgets;
static GHashTable *session_shown_once;
static GHashTable *session_replay_marklast;
static GHashTable *color_tags;
static GtkTextTagTable *shared_tag_table;
static GtkTextTag *tag_stamp;
static GtkTextTag *tag_bold;
static GtkTextTag *tag_italic;
static GtkTextTag *tag_underline;
static GtkTextTag *tag_link_hover;
static GtkTextTag *tag_nick_column;
static GtkTextTag *tag_message_hanging;
static GtkTextTag *tag_font;
static PangoFontDescription *xtext_font_desc;
static int xtext_space_width_px;
static int xtext_stamp_col_px;
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
static guint xtext_scroll_to_end_idle_id;
static int xtext_last_view_width;
static session *xtext_render_session;
static GtkWidget *xtext_stack;
static GtkWidget *xtext_empty_scroll;
static GtkWidget *xtext_empty_view;
static GtkWidget *xtext_scroll_to_end_view;
static session *xtext_scroll_to_end_replay_session;
static int xtext_scroll_debug_enabled_cached = -1;

static void xtext_render_raw_append (GtkTextBuffer *buf, const char *raw);
static gboolean xtext_is_at_end (void);
static gboolean xtext_parse_color_number (const char *text, gsize len, gsize *index, int *value);
static void xtext_tabs_to_spaces (char *text);
static void xtext_color_to_rgba (int color_index, GdkRGBA *rgba);
static GtkTextBuffer *xtext_create_buffer_with_marks (void);
static void xtext_setup_view_controllers (GtkWidget *view);
static HcSessionWidget *session_widget_ensure (session *sess);
static void session_buffer_mark_all_dirty (void);
static gboolean xtext_view_is_at_end (GtkWidget *view);
static gboolean xtext_should_stick_to_end (void);
static void xtext_scroll_to_end_idle_finish (void);
static void xtext_scroll_to_end_idle_cancel (void);
static void xtext_scroll_to_end_for_replay (session *sess);
static gboolean xtext_scroll_debug_enabled (void);
static const char *xtext_scroll_debug_session_label (session *sess);
static void xtext_scroll_debug_log_state (const char *event, session *sess,
	GtkWidget *view, GtkTextBuffer *buf);

static gboolean
xtext_scroll_debug_enabled (void)
{
	const char *env;

	if (xtext_scroll_debug_enabled_cached >= 0)
		return xtext_scroll_debug_enabled_cached ? TRUE : FALSE;

	env = g_getenv ("DITRIGON_XTEXT_SCROLL_DEBUG");
	if (!env || !env[0] || g_strcmp0 (env, "0") == 0 ||
		g_ascii_strcasecmp (env, "false") == 0 ||
		g_ascii_strcasecmp (env, "no") == 0)
		xtext_scroll_debug_enabled_cached = 0;
	else
		xtext_scroll_debug_enabled_cached = 1;

	return xtext_scroll_debug_enabled_cached ? TRUE : FALSE;
}

static const char *
xtext_scroll_debug_session_label (session *sess)
{
	if (!sess || !is_session (sess))
		return "-";

	if (sess->channel[0])
		return sess->channel;

	return "(server)";
}

static void
xtext_scroll_debug_log_state (const char *event, session *sess, GtkWidget *view,
	GtkTextBuffer *buf)
{
	GtkAdjustment *vadj;
	gboolean mapped;
	int width;
	int anchor_offset;
	int end_offset;
	double value;
	double lower;
	double upper;
	double page;
	double bottom;
	double distance;

	if (!xtext_scroll_debug_enabled ())
		return;

	mapped = view ? gtk_widget_get_mapped (view) : FALSE;
	width = view ? gtk_widget_get_width (view) : -1;
	anchor_offset = -1;
	end_offset = -1;

	vadj = view ? gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view)) : NULL;
	value = vadj ? gtk_adjustment_get_value (vadj) : -1.0;
	lower = vadj ? gtk_adjustment_get_lower (vadj) : -1.0;
	upper = vadj ? gtk_adjustment_get_upper (vadj) : -1.0;
	page = vadj ? gtk_adjustment_get_page_size (vadj) : -1.0;
	bottom = vadj ? MAX (lower, upper - page) : -1.0;
	distance = vadj ? MAX (0.0, bottom - value) : -1.0;

	if (buf)
	{
		GtkTextMark *anchor;
		GtkTextMark *end_mark;
		GtkTextIter iter;

		anchor = gtk_text_buffer_get_mark (buf, "anchor");
		if (anchor)
		{
			gtk_text_buffer_get_iter_at_mark (buf, &iter, anchor);
			anchor_offset = gtk_text_iter_get_offset (&iter);
		}

		end_mark = gtk_text_buffer_get_mark (buf, "end");
		if (end_mark)
		{
			gtk_text_buffer_get_iter_at_mark (buf, &iter, end_mark);
			end_offset = gtk_text_iter_get_offset (&iter);
		}
	}

	g_debug ("xtext-scroll:%s sess=%p(%s) view=%p mapped=%d width=%d adj=%.1f/%.1f..%.1f page=%.1f bottom=%.1f dist=%.1f anchor=%d end=%d idle_end=%u",
		event ? event : "-",
		(gpointer) sess,
		xtext_scroll_debug_session_label (sess),
		(gpointer) view,
		mapped ? 1 : 0,
		width,
		value,
		lower,
		upper,
		page,
		bottom,
		distance,
		anchor_offset,
		end_offset,
		xtext_scroll_to_end_idle_id);
}

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
xtext_trim_nick_token_bounds (const char *token, const char **start_out, const char **end_out)
{
	const char *start_ptr;
	const char *end_ptr;

	if (start_out)
		*start_out = NULL;
	if (end_out)
		*end_out = NULL;

	if (!token || !token[0])
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

	if (start_out)
		*start_out = start_ptr;
	if (end_out)
		*end_out = end_ptr;

	return TRUE;
}

static gboolean
xtext_is_plausible_nick_char (gunichar ch, gboolean is_first)
{
	if (g_unichar_isalnum (ch))
		return TRUE;
	if (ch > 0x7f)
		return TRUE;

	switch (ch)
	{
	case '[':
	case ']':
	case '\\':
	case '`':
	case '_':
	case '^':
	case '{':
	case '}':
	case '|':
		return TRUE;
	case '-':
		return is_first ? FALSE : TRUE;
	default:
		return FALSE;
	}
}

static gboolean
xtext_is_plausible_nick (const char *nick)
{
	const char *p;
	const char *end;
	gboolean is_first;

	if (!nick || !nick[0])
		return FALSE;

	p = nick;
	end = nick + strlen (nick);
	is_first = TRUE;
	while (p < end)
	{
		gunichar ch;
		const char *next;

		ch = g_utf8_get_char (p);
		if (!xtext_is_plausible_nick_char (ch, is_first))
			return FALSE;

		next = g_utf8_next_char (p);
		p = next > p ? next : p + 1;
		is_first = FALSE;
	}

	return TRUE;
}

static gboolean
xtext_extract_nick_token_relaxed (const char *token, gsize *start_out, gsize *end_out, char **nick_out)
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

	if (!token || !token[0])
		return FALSE;

	if (!xtext_trim_nick_token_bounds (token, &start_ptr, &end_ptr))
		return FALSE;

	nick_start = start_ptr;
	nick = g_strndup (nick_start, (gsize) (end_ptr - nick_start));
	if (!xtext_is_plausible_nick (nick))
		g_clear_pointer (&nick, g_free);

	if (!nick && strchr (HC_NICK_PREFIXES, *nick_start) != NULL)
	{
		const char *next;

		next = g_utf8_next_char (nick_start);
		if (next < end_ptr)
		{
			nick_start = next;
			nick = g_strndup (nick_start, (gsize) (end_ptr - nick_start));
			if (!xtext_is_plausible_nick (nick))
				g_clear_pointer (&nick, g_free);
		}
	}

	if (!nick)
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

static void
xtext_consider_session_nick_candidate (session *sess, const char *token,
	const char *token_end, const char *start_ptr, const char *end_ptr,
	const char **best_start, const char **best_end, char **best_nick,
	int *best_score, gsize *best_len)
{
	const char *candidate_start;
	int pass;

	if (!sess || !is_session (sess) || !token || !token_end || !start_ptr || !end_ptr ||
		end_ptr <= start_ptr || !best_start || !best_end || !best_nick ||
		!best_score || !best_len)
		return;

	candidate_start = start_ptr;
	for (pass = 0; pass < 2; pass++)
	{
		char *nick;
		gsize len;
		int score;
		gboolean better;

		if (candidate_start >= end_ptr)
			break;

		nick = g_strndup (candidate_start, (gsize) (end_ptr - candidate_start));
		if (!xtext_is_plausible_nick (nick) || !xtext_session_has_nick (sess, nick))
		{
			g_free (nick);
			nick = NULL;
		}

		if (nick)
		{
			len = (gsize) (end_ptr - candidate_start);
			score = (int) ((candidate_start - token) + (token_end - end_ptr));
			better = (!*best_nick || score < *best_score ||
				(score == *best_score && len > *best_len));
			if (better)
			{
				g_free (*best_nick);
				*best_nick = nick;
				*best_start = candidate_start;
				*best_end = end_ptr;
				*best_len = len;
				*best_score = score;
			}
			else
			{
				g_free (nick);
			}
		}

		if (pass > 0 || strchr (HC_NICK_PREFIXES, *candidate_start) == NULL)
			break;

		candidate_start = g_utf8_next_char (candidate_start);
	}

	return;
}

static gboolean
xtext_extract_nick_token (session *sess, const char *token, gsize *start_out, gsize *end_out, char **nick_out)
{
	const char *token_end;
	const char *start_ptr;
	const char *best_start;
	const char *best_end;
	char *best_nick;
	gsize best_len;
	int best_score;

	if (start_out)
		*start_out = 0;
	if (end_out)
		*end_out = 0;
	if (nick_out)
		*nick_out = NULL;

	if (!sess || !is_session (sess) || !token || !token[0])
		return FALSE;

	token_end = token + strlen (token);
	start_ptr = token;
	best_start = NULL;
	best_end = NULL;
	best_nick = NULL;
	best_len = 0;
	best_score = G_MAXINT;

	for (;;)
	{
		const char *end_ptr;

		end_ptr = token_end;
		for (;;)
		{
			const char *prev;
			gunichar ch;

			xtext_consider_session_nick_candidate (sess, token, token_end, start_ptr, end_ptr,
				&best_start, &best_end, &best_nick, &best_score, &best_len);

			if (end_ptr <= start_ptr)
				break;

			prev = g_utf8_find_prev_char (token, end_ptr);
			if (!prev)
				break;

			ch = g_utf8_get_char (prev);
			if (!xtext_is_nick_trail_delim (ch))
				break;

			end_ptr = prev;
		}

		{
			gunichar ch;
			const char *next;

			ch = g_utf8_get_char (start_ptr);
			if (!xtext_is_nick_lead_delim (ch) && strchr (HC_NICK_PREFIXES, *start_ptr) == NULL)
				break;

			next = g_utf8_next_char (start_ptr);
			if (next <= start_ptr || next >= token_end)
				break;
			start_ptr = next;
		}
	}

	if (!best_nick)
		return FALSE;

	if (start_out)
		*start_out = (gsize) (best_start - token);
	if (end_out)
		*end_out = (gsize) (best_end - token);
	if (nick_out)
		*nick_out = best_nick;
	else
		g_free (best_nick);

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

	if (!prefix || len == 0)
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
	has_nick = (sess && is_session (sess)) ?
		xtext_extract_nick_token (sess, candidate, NULL, NULL, &nick) : FALSE;
	if (!has_nick)
		has_nick = xtext_extract_nick_token_relaxed (candidate, NULL, NULL, &nick);
	g_free (nick);
	g_free (clean);

	return has_nick;
}

static gboolean
xtext_range_touches_tag (const GtkTextIter *start, const GtkTextIter *end, GtkTextTag *tag)
{
	GtkTextIter probe;
	GtkTextIter start_iter;
	GtkTextIter end_iter;

	if (!tag || !start || !end)
		return FALSE;
	start_iter = *start;
	end_iter = *end;
	if (gtk_text_iter_compare (&start_iter, &end_iter) >= 0)
		return FALSE;

	probe = start_iter;
	if (gtk_text_iter_has_tag (&probe, tag))
		return TRUE;

	probe = end_iter;
	if (!gtk_text_iter_backward_char (&probe))
		return FALSE;
	if (gtk_text_iter_compare (&probe, &start_iter) < 0)
		return FALSE;

	return gtk_text_iter_has_tag (&probe, tag);
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
	char *token;

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

	for (;;)
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

	for (;;)
	{
		gunichar next_ch;

		next_ch = gtk_text_iter_get_char (&end);
		if (xtext_is_space_char (next_ch))
			break;
		if (!gtk_text_iter_forward_char (&end))
			break;
	}

	if (gtk_text_iter_compare (&start, &end) >= 0)
		return NULL;

	buffer = gtk_text_view_get_buffer (view);
	token = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
	if (!token || !token[0])
	{
		g_free (token);
		return NULL;
	}

	if (start_out)
		*start_out = start;
	if (end_out)
		*end_out = end;

	return token;
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
	else if (type == 0 && xtext_range_touches_tag (&token_start, &token_end, tag_nick_column) &&
		xtext_extract_nick_token_relaxed (token, &nick_start, &nick_end, &nick_target))
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

	if (xtext_type_is_url_like (type) || type == WORD_NICK || type == WORD_DIALOG || type == WORD_CHANNEL)
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
	else if (xtext_secondary_pending_type == WORD_CHANNEL)
		fe_gtk4_menu_show_chanmenu (log_view, xtext_secondary_pending_x, xtext_secondary_pending_y,
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
	if (target && (xtext_type_is_url_like (type) || type == WORD_NICK || type == WORD_DIALOG || type == WORD_CHANNEL))
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
session_buffer_free (gpointer data)
{
	if (data)
		g_object_unref ((GtkTextBuffer *) data);
}

static GtkTextBuffer *
session_buffer_ensure (session *sess)
{
	GtkTextBuffer *buf;

	if (!session_buffers || !shared_tag_table || !sess || !is_session (sess))
		return NULL;

	buf = g_hash_table_lookup (session_buffers, sess);
	if (buf)
		return buf;

	buf = xtext_create_buffer_with_marks ();
	if (!buf)
		return NULL;

	g_hash_table_insert (session_buffers, sess, buf);
	return buf;
}

static gboolean
session_buffer_is_dirty (session *sess)
{
	if (!session_buffers_dirty || !sess || !is_session (sess))
		return FALSE;

	return g_hash_table_contains (session_buffers_dirty, sess);
}

static void
session_buffer_set_dirty (session *sess, gboolean dirty)
{
	if (!session_buffers_dirty || !sess || !is_session (sess))
		return;

	if (dirty)
		g_hash_table_insert (session_buffers_dirty, sess, GINT_TO_POINTER (TRUE));
	else
		g_hash_table_remove (session_buffers_dirty, sess);
}

static void
session_buffer_mark_all_dirty (void)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	if (!session_buffers_dirty || !session_buffers)
		return;

	g_hash_table_iter_init (&iter, session_buffers);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		(void) value;
		g_hash_table_insert (session_buffers_dirty, key, GINT_TO_POINTER (TRUE));
	}
}

static GtkTextBuffer *
xtext_create_buffer_with_marks (void)
{
	GtkTextBuffer *buf;
	GtkTextIter iter;

	if (!shared_tag_table)
		return NULL;

	buf = gtk_text_buffer_new (shared_tag_table);
	gtk_text_buffer_get_end_iter (buf, &iter);
	gtk_text_buffer_create_mark (buf, "end", &iter, FALSE);
	gtk_text_buffer_create_mark (buf, "anchor", &iter, TRUE);

	return buf;
}

static void
xtext_setup_view_controllers (GtkWidget *view)
{
	GtkGesture *gesture;
	GtkEventController *motion;

	if (!view)
		return;

	gesture = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_PRIMARY);
	g_signal_connect (gesture, "pressed", G_CALLBACK (xtext_primary_press_cb), NULL);
	g_signal_connect (gesture, "released", G_CALLBACK (xtext_primary_release_cb), NULL);
	gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (gesture));

	gesture = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
	gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
	g_signal_connect (gesture, "pressed", G_CALLBACK (xtext_secondary_press_cb), NULL);
	g_signal_connect (gesture, "released", G_CALLBACK (xtext_secondary_click_cb), NULL);
	gtk_widget_add_controller (view, GTK_EVENT_CONTROLLER (gesture));

	motion = gtk_event_controller_motion_new ();
	g_signal_connect (motion, "motion", G_CALLBACK (xtext_motion_cb), NULL);
	g_signal_connect (motion, "leave", G_CALLBACK (xtext_motion_leave_cb), NULL);
	gtk_widget_add_controller (view, motion);
}

static void
session_widget_free (gpointer data)
{
	HcSessionWidget *widget;
	GtkWidget *parent;

	widget = data;
	if (!widget)
		return;

	if (widget->view)
		fe_gtk4_menu_close_context_popovers (NULL);

	if (widget->scroll)
	{
		parent = gtk_widget_get_parent (widget->scroll);
		if (parent && GTK_IS_STACK (parent))
			gtk_stack_remove (GTK_STACK (parent), widget->scroll);
		g_object_unref (widget->scroll);
	}

	g_free (widget);
}

static HcSessionWidget *
session_widget_ensure (session *sess)
{
	HcSessionWidget *widget;
	GtkBuilder *builder;
	GtkWidget *scroll;
	GtkWidget *view;
	GtkTextBuffer *buf;

	if (!session_widgets || !xtext_stack || !sess || !is_session (sess))
		return NULL;

	widget = g_hash_table_lookup (session_widgets, sess);
	if (widget)
		return widget;

	builder = fe_gtk4_builder_new_from_resource (XTEXT_UI_PATH);
	scroll = fe_gtk4_builder_get_widget (builder, "xtext_scroll", GTK_TYPE_SCROLLED_WINDOW);
	view = fe_gtk4_builder_get_widget (builder, "xtext_log_view", GTK_TYPE_TEXT_VIEW);
	g_object_ref (scroll);
	g_object_unref (builder);

	gtk_text_view_set_tabs (GTK_TEXT_VIEW (view), NULL);
	gtk_text_view_set_monospace (GTK_TEXT_VIEW (view), xtext_font_desc ? FALSE : TRUE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view),
		prefs.hex_text_wordwrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
	xtext_setup_view_controllers (view);

	widget = g_new0 (HcSessionWidget, 1);
	widget->scroll = scroll;
	widget->view = view;

	buf = session_buffer_ensure (sess);
	if (buf)
		gtk_text_view_set_buffer (GTK_TEXT_VIEW (view), buf);

	gtk_stack_add_child (GTK_STACK (xtext_stack), scroll);
	g_hash_table_insert (session_widgets, sess, widget);
	return widget;
}

static GtkTextTag *
xtext_create_tag_in_table (GtkTextTagTable *table, const char *name,
	const char *first_property, ...)
{
	GtkTextTag *tag;
	va_list args;

	tag = gtk_text_tag_new (name);
	if (first_property)
	{
		va_start (args, first_property);
		g_object_set_valist (G_OBJECT (tag), first_property, args);
		va_end (args);
	}
	gtk_text_tag_table_add (table, tag);
	g_object_unref (tag); /* table holds a ref */
	return tag;
}

static void
xtext_create_shared_tag_table (void)
{
	GdkRGBA stamp_rgba;
	GdkRGBA nick_rgba;

	if (shared_tag_table)
		return;

	shared_tag_table = gtk_text_tag_table_new ();

	tag_bold = xtext_create_tag_in_table (shared_tag_table, "hc-bold",
		"weight", PANGO_WEIGHT_BOLD, NULL);
	tag_italic = xtext_create_tag_in_table (shared_tag_table, "hc-italic",
		"style", PANGO_STYLE_ITALIC, NULL);
	tag_underline = xtext_create_tag_in_table (shared_tag_table, "hc-underline",
		"underline", PANGO_UNDERLINE_SINGLE, NULL);
	tag_link_hover = xtext_create_tag_in_table (shared_tag_table, "hc-link-hover",
		"underline", PANGO_UNDERLINE_SINGLE, NULL);
	xtext_color_to_rgba (18, &nick_rgba);
	tag_nick_column = xtext_create_tag_in_table (shared_tag_table, "hc-nick-column",
		"weight", PANGO_WEIGHT_SEMIBOLD,
		"foreground-rgba", &nick_rgba,
		NULL);
	tag_message_hanging = xtext_create_tag_in_table (shared_tag_table, "hc-message-hanging",
		"left-margin", 0,
		"indent", 0,
		NULL);
	tag_font = xtext_create_tag_in_table (shared_tag_table, "hc-font", NULL);

	xtext_color_to_rgba (COL_FG, &stamp_rgba);
	tag_stamp = xtext_create_tag_in_table (shared_tag_table, "hc-stamp",
		"foreground-rgba", &stamp_rgba, NULL);
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

	if (!shared_tag_table)
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
	tag = gtk_text_tag_new (tag_name);

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

	gtk_text_tag_table_add (shared_tag_table, tag);
	g_object_unref (tag); /* table holds a ref */

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
	GtkWidget *measure_view;

	if (!text || len == 0)
		return 0;

	measure_view = log_view ? log_view : xtext_empty_view;
	if (!measure_view)
		return 0;

	tmp = g_strndup (text, len);
	xtext_tabs_to_spaces (tmp);

	layout = gtk_widget_create_pango_layout (measure_view, tmp);
	if (xtext_font_desc)
		pango_layout_set_font_description (layout, xtext_font_desc);
	pango_layout_get_pixel_size (layout, &width, NULL);
	g_object_unref (layout);
	g_free (tmp);

	return width;
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
xtext_line_stamp_width_px (const HcLineColumns *cols)
{
	if (!cols || !cols->has_columns || cols->stamp_len == 0)
		return 0;

	return xtext_measure_plain_width (cols->stamp, cols->stamp_len);
}

static int
xtext_line_prefix_width_px (const HcLineColumns *cols)
{
	char *prefix_clean;
	int width;

	if (!cols || !cols->has_columns || cols->prefix_len == 0)
		return 0;

	prefix_clean = strip_color (cols->prefix, (int) cols->prefix_len, STRIP_ALL);
	if (!prefix_clean)
		return 0;
	xtext_tabs_to_spaces (prefix_clean);
	width = xtext_measure_plain_width (prefix_clean, strlen (prefix_clean));
	g_free (prefix_clean);

	return width;
}

static int
xtext_line_left_width_px (const HcLineColumns *cols)
{
	int width;

	if (!cols || !cols->has_columns)
		return 0;

	width = xtext_line_stamp_width_px (cols);
	if (cols->prefix_len == 0)
		return width + xtext_space_width_px;

	if (width > 0)
		width += xtext_space_width_px;

	width += xtext_line_prefix_width_px (cols);

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
xtext_compute_message_column_px (const char *raw, int *out_stamp_col_px)
{
	const char *cursor;
	int col_px;
	int stamp_px;
	int max_indent_px;

	if (out_stamp_col_px)
		*out_stamp_col_px = 0;

	if (!raw || !raw[0] || !log_view)
		return 0;

	col_px = 0;
	stamp_px = 0;
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
		{
			col_px = MAX (col_px, xtext_line_left_width_px (&cols));
			stamp_px = MAX (stamp_px, xtext_line_stamp_width_px (&cols));
		}

		if (!nl)
			break;
		cursor = nl + 1;
	}

	if (col_px > max_indent_px)
		col_px = max_indent_px;
	if (col_px < 0)
		col_px = 0;

	if (out_stamp_col_px)
		*out_stamp_col_px = stamp_px;

	return col_px;
}

static void
xtext_set_message_tab_stop (int msg_px, int stamp_px)
{
	PangoTabArray *tabs;
	int nick_right_px;

	if (tag_message_hanging)
	{
		if (msg_px > 0)
			g_object_set (tag_message_hanging, "left-margin", 5, "indent", -msg_px, NULL);
		else
			g_object_set (tag_message_hanging, "left-margin", 0, "indent", 0, NULL);
	}

	if (!log_view)
		return;

	if (msg_px <= 0)
	{
		gtk_text_view_set_tabs (GTK_TEXT_VIEW (log_view), NULL);
		xtext_stamp_col_px = 0;
		xtext_message_col_px = 0;
		return;
	}

	/* Tab 0: right-aligned tab where the nick column ends.
	 * The nick text following this tab is right-aligned so it
	 * ends at nick_right_px (= message column minus a space gap).
	 * Tab 1: left-aligned tab where the message body starts. */
	nick_right_px = msg_px - xtext_space_width_px;
	if (nick_right_px < 0)
		nick_right_px = 0;

	tabs = pango_tab_array_new (2, TRUE);
	pango_tab_array_set_tab (tabs, 0, PANGO_TAB_RIGHT, nick_right_px);
	pango_tab_array_set_tab (tabs, 1, PANGO_TAB_LEFT, msg_px);
	gtk_text_view_set_tabs (GTK_TEXT_VIEW (log_view), tabs);
	pango_tab_array_free (tabs);
	xtext_stamp_col_px = stamp_px;
	xtext_message_col_px = msg_px;
}

static void
xtext_insert_segment (GtkTextBuffer *buf, GtkTextIter *iter, const char *text, gsize len, const HcTextStyle *style, GtkTextTag *layout_tag)
{
	GtkTextTag *tags[6];
	int n;
	int fg;
	int bg;
	GtkTextTag *color_tag;

	if (!buf || !iter || !text || len == 0)
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
		gtk_text_buffer_insert (buf, iter, text, (int) len);
		break;
	case 1:
		gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len, tags[0], NULL);
		break;
	case 2:
		gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len,
			tags[0], tags[1], NULL);
		break;
	case 3:
		gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len,
			tags[0], tags[1], tags[2], NULL);
		break;
	case 4:
		gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len,
			tags[0], tags[1], tags[2], tags[3], NULL);
		break;
	case 5:
		gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len,
			tags[0], tags[1], tags[2], tags[3], tags[4], NULL);
		break;
	default:
		gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len,
			tags[0], tags[1], tags[2], tags[3], tags[4], tags[5], NULL);
		break;
	}
}

static void
xtext_insert_plain_text (GtkTextBuffer *buf, GtkTextIter *iter, const char *text, gsize len, gboolean with_stamp_tag)
{
	GtkTextTag *first_tag;
	GtkTextTag *second_tag;

	if (!buf || !iter || !text || len == 0)
		return;

	first_tag = with_stamp_tag ? tag_stamp : NULL;
	second_tag = tag_font;

	if (!first_tag && !second_tag)
	{
		gtk_text_buffer_insert (buf, iter, text, (int) len);
		return;
	}
	if (first_tag && second_tag)
	{
		gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len, first_tag, second_tag, NULL);
		return;
	}

	gtk_text_buffer_insert_with_tags (buf, iter, text, (int) len,
		first_tag ? first_tag : second_tag, NULL);
}

static void
xtext_insert_plain_char (GtkTextBuffer *buf, GtkTextIter *iter, char ch)
{
	char s[2];

	s[0] = ch;
	s[1] = 0;
	xtext_insert_plain_text (buf, iter, s, 1, FALSE);
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
xtext_render_formatted_stateful (GtkTextBuffer *buf, GtkTextIter *iter, const char *text, gsize len, GtkTextTag *layout_tag)
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
				xtext_insert_segment (buf, iter, text + seg_start, i - seg_start, &style, layout_tag);
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
					xtext_insert_segment (buf, iter, text + seg_start, i - seg_start, &style, layout_tag);
				xtext_insert_segment (buf, iter, " ", 1, &style, layout_tag);
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
				xtext_insert_segment (buf, iter, text + seg_start, i - seg_start, &style, layout_tag);
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
		xtext_insert_segment (buf, iter, text + seg_start, i - seg_start, &style, layout_tag);
}

static void
xtext_render_formatted (GtkTextBuffer *buf, GtkTextIter *iter, const char *text, gsize len, GtkTextTag *layout_tag)
{
	xtext_render_formatted_stateful (buf, iter, text, len, layout_tag);
}

static void
xtext_render_line (GtkTextBuffer *buf, GtkTextIter *iter, const char *line, gsize len, gboolean append_newline)
{
	HcLineColumns cols;
	session *render_sess;
	GtkTextTag *prefix_tag;
	GtkTextMark *line_start_mark;
	GtkTextMark *line_end_mark;
	GtkTextIter tag_start;
	GtkTextIter tag_end;

	if (!buf || !iter)
		return;

	line_start_mark = NULL;
	line_end_mark = NULL;
	if (tag_message_hanging)
		line_start_mark = gtk_text_buffer_create_mark (buf, NULL, iter, TRUE);

	xtext_split_line_columns (line, len, &cols);
	if (cols.has_columns)
	{
		if (cols.stamp && cols.stamp_len > 0)
			xtext_insert_plain_text (buf, iter, cols.stamp, cols.stamp_len, TRUE);

		xtext_insert_plain_char (buf, iter, '\t');

		if (cols.prefix_len > 0)
		{
			render_sess = xtext_render_session;
			if (!render_sess || !is_session (render_sess))
				render_sess = current_tab;
			prefix_tag = NULL;
			if (tag_nick_column && xtext_prefix_has_nick (render_sess, cols.prefix, cols.prefix_len))
				prefix_tag = tag_nick_column;
			xtext_render_formatted (buf, iter, cols.prefix, cols.prefix_len, prefix_tag);
		}

		if (cols.body && cols.body_len > 0)
		{
			xtext_insert_plain_char (buf, iter, '\t');
			xtext_render_formatted (buf, iter, cols.body, cols.body_len, NULL);
		}
	}
	else
		xtext_render_formatted (buf, iter, line, len, NULL);

	if (append_newline)
		xtext_insert_plain_char (buf, iter, '\n');

	if (cols.has_columns && line_start_mark && tag_message_hanging)
	{
		GtkTextIter end_iter;

		end_iter = *iter;
		if (append_newline)
			gtk_text_iter_backward_char (&end_iter);
		line_end_mark = gtk_text_buffer_create_mark (buf, NULL, &end_iter, FALSE);
		gtk_text_buffer_get_iter_at_mark (buf, &tag_start, line_start_mark);
		gtk_text_buffer_get_iter_at_mark (buf, &tag_end, line_end_mark);
		if (gtk_text_iter_compare (&tag_start, &tag_end) < 0)
			gtk_text_buffer_apply_tag (buf, tag_message_hanging, &tag_start, &tag_end);
	}

	if (line_start_mark)
		gtk_text_buffer_delete_mark (buf, line_start_mark);
	if (line_end_mark)
		gtk_text_buffer_delete_mark (buf, line_end_mark);
}

static void
xtext_render_raw_at_iter (GtkTextBuffer *buf, GtkTextIter *iter, const char *raw)
{
	const char *cursor;

	if (!buf || !iter || !raw)
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

		xtext_render_line (buf, iter, cursor, len, add_nl);

		if (!nl)
			break;
		cursor = nl + 1;
	}
}

static void
xtext_render_raw_all (GtkTextBuffer *buf, const char *raw)
{
	GtkTextIter iter;
	GtkTextMark *mark;
	GtkTextMark *anchor;

	GtkTextIter start;
	GtkTextIter end;
	int col_px;
	int stamp_px;
	int anchor_offset;
	const char *text;

	if (!buf)
		return;

	/* Remember anchor offset so we can restore it after the
	 * delete-reinsert cycle (the delete collapses it to 0). */
	anchor = gtk_text_buffer_get_mark (buf, "anchor");
	anchor_offset = -1;
	if (anchor)
	{
		gtk_text_buffer_get_iter_at_mark (buf, &iter, anchor);
		anchor_offset = gtk_text_iter_get_offset (&iter);
	}

	text = raw ? raw : "";
	col_px = xtext_compute_message_column_px (text, &stamp_px);
	/* Only update tab stops when rendering the currently visible session */
	if (xtext_render_session == current_tab)
		xtext_set_message_tab_stop (col_px, stamp_px);

	xtext_link_hover_clear ();

	gtk_text_buffer_get_bounds (buf, &start, &end);
	gtk_text_buffer_delete (buf, &start, &end);

	mark = gtk_text_buffer_get_mark (buf, "end");
	gtk_text_buffer_get_iter_at_mark (buf, &iter, mark);
	xtext_render_raw_at_iter (buf, &iter, text);

	/* Restore anchor to its previous offset (clamped to new length). */
	if (anchor && anchor_offset > 0)
	{
		int len = gtk_text_buffer_get_char_count (buf);
		if (anchor_offset > len)
			anchor_offset = len;
		gtk_text_buffer_get_iter_at_offset (buf, &iter, anchor_offset);
		gtk_text_buffer_move_mark (buf, anchor, &iter);
	}
}

static void
xtext_render_raw_append (GtkTextBuffer *buf, const char *raw)
{
	GtkTextIter iter;
	GtkTextMark *mark;

	if (!buf)
		return;

	/* gtk_text_buffer_get_end_iter (buf, &end); */
	/* xtext_render_raw_at_iter (buf, &end, raw ? raw : ""); */

	mark = gtk_text_buffer_get_mark (buf, "end");
	gtk_text_buffer_get_iter_at_mark (buf, &iter, mark);

	xtext_render_raw_at_iter (buf, &iter, raw ? raw : "");
}

static void
xtext_scroll_to_end_idle_finish (void)
{
	xtext_scroll_debug_log_state ("scroll-end-idle-finish", current_tab,
		xtext_scroll_to_end_view,
		xtext_scroll_to_end_view ?
			gtk_text_view_get_buffer (GTK_TEXT_VIEW (xtext_scroll_to_end_view)) : NULL);
	xtext_scroll_to_end_idle_id = 0;
	xtext_scroll_to_end_view = NULL;
	xtext_scroll_to_end_replay_session = NULL;
}

static void
xtext_scroll_to_end_idle_cancel (void)
{
	xtext_scroll_debug_log_state ("scroll-end-idle-cancel", current_tab,
		xtext_scroll_to_end_view,
		xtext_scroll_to_end_view ?
			gtk_text_view_get_buffer (GTK_TEXT_VIEW (xtext_scroll_to_end_view)) : NULL);
	if (xtext_scroll_to_end_idle_id != 0)
		g_source_remove (xtext_scroll_to_end_idle_id);
	xtext_scroll_to_end_idle_finish ();
}

static gboolean
scroll_to_end_idle (gpointer data)
{
	GtkTextView *view = GTK_TEXT_VIEW (data);
	GtkTextBuffer *buffer;
	GtkTextMark *mark;

	xtext_scroll_debug_log_state ("scroll-end-idle-enter", current_tab,
		GTK_WIDGET (view), view ? gtk_text_view_get_buffer (view) : NULL);

	/* If the requested view is no longer the active one, drop the idle.
	 * Hidden stack children never gain a size allocation, so continuing
	 * would spin forever. */
	if (!view || GTK_WIDGET (view) != log_view)
	{
		xtext_scroll_debug_log_state ("scroll-end-idle-drop-not-active", current_tab,
			GTK_WIDGET (view), view ? gtk_text_view_get_buffer (view) : NULL);
		xtext_scroll_to_end_idle_finish ();
		return G_SOURCE_REMOVE;
	}

	/* Wait until the widget has a valid allocation before scrolling,
	 * otherwise the scrollbar's internal GtkGizmo may be snapshotted
	 * before it has been allocated. */
	if (!gtk_widget_get_mapped (GTK_WIDGET (view)) ||
		gtk_widget_get_width (GTK_WIDGET (view)) <= 0)
	{
		xtext_scroll_debug_log_state ("scroll-end-idle-wait-mapped", current_tab,
			GTK_WIDGET (view), gtk_text_view_get_buffer (view));
		return G_SOURCE_CONTINUE;
	}

	buffer = gtk_text_view_get_buffer (view);
	mark = gtk_text_buffer_get_mark (buffer, "end");
	if (mark)
	{
		xtext_scroll_debug_log_state ("scroll-end-idle-apply", current_tab,
			GTK_WIDGET (view), buffer);
		gtk_text_view_scroll_mark_onscreen (view, mark);

		if (xtext_scroll_to_end_replay_session && session_replay_marklast)
			g_hash_table_remove (session_replay_marklast,
				xtext_scroll_to_end_replay_session);
	}
	else
	{
		xtext_scroll_debug_log_state ("scroll-end-idle-no-end-mark", current_tab,
			GTK_WIDGET (view), buffer);
	}

	xtext_scroll_to_end_idle_finish ();
	return G_SOURCE_REMOVE;
}

static void
xtext_scroll_to_end (void)
{
	GtkTextMark *mark;

	if (!log_buffer || !log_view)
	{
		xtext_scroll_debug_log_state ("scroll-end-skip-no-view-or-buffer", current_tab,
			log_view, log_buffer);
		return;
	}

	mark = gtk_text_buffer_get_mark (log_buffer, "end");
	if (!mark)
	{
		xtext_scroll_debug_log_state ("scroll-end-skip-no-end-mark", current_tab,
			log_view, log_buffer);
		return;
	}

	if (xtext_scroll_to_end_idle_id != 0)
	{
		if (xtext_scroll_to_end_view == log_view)
		{
			xtext_scroll_debug_log_state ("scroll-end-coalesce-same-view", current_tab,
				log_view, log_buffer);
			return;
		}

		xtext_scroll_to_end_idle_cancel ();
	}

	/* Schedule restore in idle to avoid races with widget allocation. */
	xtext_scroll_to_end_view = log_view;
	xtext_scroll_to_end_replay_session = NULL;
	xtext_scroll_to_end_idle_id = g_idle_add (scroll_to_end_idle, log_view);
	xtext_scroll_debug_log_state ("scroll-end-scheduled", current_tab, log_view, log_buffer);
}

static void
xtext_scroll_to_end_for_replay (session *sess)
{
	GtkTextMark *mark;

	if (!log_buffer || !log_view)
	{
		xtext_scroll_debug_log_state ("scroll-end-replay-skip-no-view-or-buffer", current_tab,
			log_view, log_buffer);
		return;
	}

	mark = gtk_text_buffer_get_mark (log_buffer, "end");
	if (!mark)
	{
		xtext_scroll_debug_log_state ("scroll-end-replay-skip-no-end-mark", current_tab,
			log_view, log_buffer);
		return;
	}

	if (xtext_scroll_to_end_idle_id != 0)
	{
		if (xtext_scroll_to_end_view == log_view)
		{
			if (sess && !xtext_scroll_to_end_replay_session)
				xtext_scroll_to_end_replay_session = sess;
			xtext_scroll_debug_log_state ("scroll-end-replay-coalesce-same-view", current_tab,
				log_view, log_buffer);
			return;
		}

		xtext_scroll_to_end_idle_cancel ();
	}

	xtext_scroll_to_end_view = log_view;
	xtext_scroll_to_end_replay_session = sess;
	xtext_scroll_to_end_idle_id = g_idle_add (scroll_to_end_idle, log_view);
	xtext_scroll_debug_log_state ("scroll-end-replay-scheduled", current_tab, log_view, log_buffer);
}

/* check if we are scrolled to the bottom */
static gboolean
xtext_view_is_at_end (GtkWidget *view)
{
	GtkAdjustment *vadj;
	double lower;
	double value;
	double upper;
	double page;
	double bottom;
	double distance;

	if (!view)
		return TRUE;
	if (xtext_scroll_to_end_idle_id != 0 && xtext_scroll_to_end_view == view)
		return TRUE;

	vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view));
	if (!vadj)
		return TRUE;

	lower = gtk_adjustment_get_lower (vadj);
	value = gtk_adjustment_get_value (vadj);
	upper = gtk_adjustment_get_upper (vadj);
	page = gtk_adjustment_get_page_size (vadj);
	bottom = MAX (lower, upper - page);
	distance = bottom - value;
	if (distance < 0.0)
		distance = 0.0;

	return distance <= HC_STICKY_BOTTOM_EPSILON_PX;
}

static gboolean
xtext_is_at_end (void)
{
	return xtext_view_is_at_end (log_view);
}

static gboolean
xtext_should_stick_to_end (void)
{
	return xtext_is_at_end ();
}

static void
xtext_show_session_rendered (session *sess)
{
	HcSessionWidget *widget;
	GtkTextBuffer *buf;
	GString *log;
	GtkTextIter start;
	GtkTextIter end;
	gboolean is_empty;
	gboolean buffer_dirty;
	gboolean replay_marklast_pending;
	gboolean first_render;
	gboolean first_show;
	int col_px;
	int stamp_px;

	if (!xtext_stack)
		return;

	/* Clear hover/search state — marks belong to the old buffer */
	if (log_view)
		gtk_widget_set_cursor_from_name (log_view, NULL);
	xtext_link_hover_clear ();
	xtext_search_mark = NULL;
	xtext_hover_start_mark = NULL;
	xtext_hover_end_mark = NULL;

	if (!sess || !is_session (sess))
	{
		if (xtext_empty_scroll)
			gtk_stack_set_visible_child (GTK_STACK (xtext_stack), xtext_empty_scroll);
		if (xtext_empty_view)
		{
			log_view = xtext_empty_view;
			log_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (xtext_empty_view));
			gtk_text_view_set_tabs (GTK_TEXT_VIEW (xtext_empty_view), NULL);
		}
		xtext_stamp_col_px = 0;
		xtext_message_col_px = 0;
		xtext_scroll_debug_log_state ("show-session-empty", sess, log_view, log_buffer);
		return;
	}

	widget = session_widget_ensure (sess);
	buf = session_buffer_ensure (sess);
	if (!widget || !buf)
		return;

	log = session_logs ? g_hash_table_lookup (session_logs, sess) : NULL;
	first_show = session_shown_once ?
		!g_hash_table_contains (session_shown_once, sess) : TRUE;

	log_view = widget->view;
	log_buffer = buf;
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget->view), log_buffer);
	gtk_stack_set_visible_child (GTK_STACK (xtext_stack), widget->scroll);

	/* Check if buffer needs rendering */
	gtk_text_buffer_get_bounds (buf, &start, &end);
	is_empty = gtk_text_iter_equal (&start, &end);
	buffer_dirty = session_buffer_is_dirty (sess);
	first_render = is_empty;
	xtext_scroll_debug_log_state (first_render ? "show-session-first-render" :
		(buffer_dirty ? "show-session-rerender-dirty" : "show-session-reuse-buffer"),
		sess, widget->view, buf);

	if (is_empty || buffer_dirty)
	{
		xtext_render_session = sess;
		xtext_render_raw_all (buf, (log && log->len > 0) ? log->str : "");
		xtext_render_session = NULL;
		session_buffer_set_dirty (sess, FALSE);
		if (first_show)
		{
			xtext_scroll_debug_log_state ("show-session-first-show-scroll-end",
				sess, widget->view, buf);
			xtext_scroll_to_end ();
		}
	}
	else
	{
		/* Buffer already has content; just recompute tab stop */
		col_px = xtext_compute_message_column_px (log ? log->str : "", &stamp_px);
		xtext_set_message_tab_stop (col_px, stamp_px);
		if (first_show)
		{
			xtext_scroll_debug_log_state ("show-session-first-show-scroll-end",
				sess, widget->view, buf);
			xtext_scroll_to_end ();
		}
	}

	if (session_shown_once)
		g_hash_table_insert (session_shown_once, sess, GINT_TO_POINTER (TRUE));

	replay_marklast_pending = session_replay_marklast &&
		g_hash_table_contains (session_replay_marklast, sess);
	if (replay_marklast_pending)
	{
		xtext_scroll_debug_log_state ("show-session-replay-marklast", sess,
			widget->view, buf);
		xtext_scroll_to_end_for_replay (sess);
	}
}

static gboolean
xtext_resize_refresh_idle_cb (gpointer user_data)
{
	(void) user_data;
	xtext_resize_idle_id = 0;

	if (!xtext_stack || !current_tab || !is_session (current_tab))
		return G_SOURCE_REMOVE;

	/* GtkTextView wrapping updates with allocation changes. */
	return G_SOURCE_REMOVE;
}

static gboolean
xtext_resize_tick_cb (GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
	int width;

	(void) frame_clock;
	(void) user_data;

	if (!widget || widget != xtext_stack)
		return G_SOURCE_CONTINUE;

	if (!gtk_widget_get_realized (widget))
		return G_SOURCE_CONTINUE;

	width = gtk_widget_get_width (xtext_stack);
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
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	if (!tag_font)
		return;

	desc = NULL;
	if (prefs.hex_text_font[0])
		desc = pango_font_description_from_string (prefs.hex_text_font);

	if (xtext_empty_view)
	{
		gtk_text_view_set_monospace (GTK_TEXT_VIEW (xtext_empty_view), desc ? FALSE : TRUE);
		gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (xtext_empty_view),
			prefs.hex_text_wordwrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
	}

	if (session_widgets)
	{
		g_hash_table_iter_init (&iter, session_widgets);
		while (g_hash_table_iter_next (&iter, &key, &value))
		{
			HcSessionWidget *widget;

			(void) key;
			widget = value;
			if (!widget || !widget->view)
				continue;
			gtk_text_view_set_monospace (GTK_TEXT_VIEW (widget->view), desc ? FALSE : TRUE);
			gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (widget->view),
				prefs.hex_text_wordwrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
		}
	}

	if (log_view)
	{
		gtk_text_view_set_monospace (GTK_TEXT_VIEW (log_view), desc ? FALSE : TRUE);
		gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (log_view),
			prefs.hex_text_wordwrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
	}

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
	if (!session_buffers)
		session_buffers = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			NULL, session_buffer_free);
	if (!session_buffers_dirty)
		session_buffers_dirty = g_hash_table_new (g_direct_hash, g_direct_equal);
	if (!session_widgets)
		session_widgets = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			NULL, session_widget_free);
	if (!session_shown_once)
		session_shown_once = g_hash_table_new (g_direct_hash, g_direct_equal);
	if (!session_replay_marklast)
		session_replay_marklast = g_hash_table_new (g_direct_hash, g_direct_equal);
	if (!color_tags)
		color_tags = g_hash_table_new (g_direct_hash, g_direct_equal);
	xtext_create_shared_tag_table ();
	if (xtext_space_width_px <= 0)
		xtext_space_width_px = HC_SPACE_WIDTH_FALLBACK_PX;
	xtext_stamp_col_px = 0;
	xtext_message_col_px = 0;
	xtext_render_session = NULL;
	xtext_scroll_to_end_idle_id = 0;
	xtext_stack = NULL;
	xtext_empty_scroll = NULL;
	xtext_empty_view = NULL;
	xtext_scroll_to_end_view = NULL;
	xtext_scroll_to_end_replay_session = NULL;
}

void
fe_gtk4_xtext_cleanup (void)
{
	if (session_buffers)
	{
		g_hash_table_unref (session_buffers);
		session_buffers = NULL;
	}
	if (session_logs)
	{
		g_hash_table_unref (session_logs);
		session_logs = NULL;
	}
	if (session_buffers_dirty)
	{
		g_hash_table_unref (session_buffers_dirty);
		session_buffers_dirty = NULL;
	}
	if (session_widgets)
	{
		g_hash_table_unref (session_widgets);
		session_widgets = NULL;
	}
	if (session_shown_once)
	{
		g_hash_table_unref (session_shown_once);
		session_shown_once = NULL;
	}
	if (session_replay_marklast)
	{
		g_hash_table_unref (session_replay_marklast);
		session_replay_marklast = NULL;
	}
	if (color_tags)
	{
		g_hash_table_unref (color_tags);
		color_tags = NULL;
	}
	if (shared_tag_table)
	{
		g_object_unref (shared_tag_table);
		shared_tag_table = NULL;
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
	tag_message_hanging = NULL;
	tag_font = NULL;
	log_buffer = NULL;
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
	xtext_scroll_to_end_idle_cancel ();
	xtext_scroll_to_end_replay_session = NULL;
	if (xtext_stack && xtext_resize_tick_id != 0)
	{
		gtk_widget_remove_tick_callback (xtext_stack, xtext_resize_tick_id);
		xtext_resize_tick_id = 0;
	}
	xtext_space_width_px = 0;
	xtext_stamp_col_px = 0;
	xtext_message_col_px = 0;
	xtext_last_view_width = -1;
	xtext_render_session = NULL;
	xtext_stack = NULL;
	xtext_empty_scroll = NULL;
	xtext_empty_view = NULL;
	xtext_scroll_to_end_view = NULL;
	log_view = NULL;
	log_buffer = NULL;
}

GtkWidget *
fe_gtk4_xtext_create_widget (void)
{
	GtkBuilder *builder;
	GtkWidget *scroll;
	GtkWidget *stack;
	GtkTextBuffer *buf;

	if (xtext_stack && xtext_resize_tick_id != 0)
	{
		gtk_widget_remove_tick_callback (xtext_stack, xtext_resize_tick_id);
		xtext_resize_tick_id = 0;
	}
	if (xtext_resize_idle_id != 0)
	{
		g_source_remove (xtext_resize_idle_id);
		xtext_resize_idle_id = 0;
	}
	xtext_scroll_to_end_idle_cancel ();
	xtext_last_view_width = -1;

	builder = fe_gtk4_builder_new_from_resource (XTEXT_UI_PATH);
	scroll = fe_gtk4_builder_get_widget (builder, "xtext_scroll", GTK_TYPE_SCROLLED_WINDOW);
	xtext_empty_view = fe_gtk4_builder_get_widget (builder, "xtext_log_view", GTK_TYPE_TEXT_VIEW);
	g_object_ref (scroll);
	g_object_unref (builder);

	gtk_text_view_set_tabs (GTK_TEXT_VIEW (xtext_empty_view), NULL);
	xtext_setup_view_controllers (xtext_empty_view);

	buf = xtext_create_buffer_with_marks ();
	if (buf)
	{
		gtk_text_view_set_buffer (GTK_TEXT_VIEW (xtext_empty_view), buf);
		g_object_unref (buf);
	}

	stack = gtk_stack_new ();
	gtk_widget_set_hexpand (stack, TRUE);
	gtk_widget_set_vexpand (stack, TRUE);
	gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_stack_add_child (GTK_STACK (stack), scroll);
	gtk_stack_set_visible_child (GTK_STACK (stack), scroll);
	g_object_unref (scroll);

	xtext_stack = stack;
	xtext_empty_scroll = scroll;
	log_view = xtext_empty_view;
	log_buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (xtext_empty_view));

	xtext_search_mark = NULL;
	xtext_hover_start_mark = NULL;
	xtext_hover_end_mark = NULL;
	xtext_stamp_col_px = 0;
	xtext_message_col_px = 0;
	xtext_resize_tick_id = gtk_widget_add_tick_callback (xtext_stack, xtext_resize_tick_cb, NULL, NULL);
	xtext_render_session = NULL;
	xtext_apply_font_pref ();

	return stack;
}

void
fe_gtk4_xtext_apply_prefs (void)
{
	xtext_apply_font_pref ();
	session_buffer_mark_all_dirty ();
	if (current_tab && is_session (current_tab))
		fe_gtk4_xtext_show_session (current_tab);
}

void
fe_gtk4_append_log_text (const char *text)
{
	gboolean stick_to_end;

	if (!text)
		return;

	if (!log_buffer || !log_view || hexchat_is_quitting)
	{
		fputs (text, stdout);
		fflush (stdout);
		return;
	}

	stick_to_end = xtext_should_stick_to_end ();
	xtext_render_session = (current_tab && is_session (current_tab)) ? current_tab : NULL;
	xtext_render_raw_append (log_buffer, text);
	xtext_render_session = NULL;
	if (stick_to_end)
		xtext_scroll_to_end ();
}

void
fe_gtk4_xtext_append_for_session (session *sess, const char *text)
{
	GString *log;
	GtkTextBuffer *buf;
	HcSessionWidget *widget;
	gboolean stick_to_end;

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

	/* For background sessions, keep already-rendered buffers live so
	 * GtkScrolledWindow can preserve precise scroll state across tab switches.
	 * For unseen/stale sessions, keep deferring full render to first show. */
	if (sess != current_tab)
	{
		if (session_buffer_is_dirty (sess))
			return;

		buf = session_buffers ? g_hash_table_lookup (session_buffers, sess) : NULL;
		if (!buf)
		{
			session_buffer_set_dirty (sess, TRUE);
			return;
		}

		xtext_render_session = sess;
		xtext_render_raw_append (buf, text);
		xtext_render_session = NULL;
		return;
	}

	/* Only render for the currently visible session */
	widget = session_widget_ensure (sess);
	buf = session_buffer_ensure (sess);
	if (!buf || !widget)
		return;

	log_view = widget->view;
	log_buffer = buf;
	stick_to_end = xtext_should_stick_to_end ();
	xtext_render_session = sess;

	if (session_buffer_is_dirty (sess))
	{
		xtext_render_raw_all (buf, log ? log->str : "");
		xtext_render_session = NULL;
		session_buffer_set_dirty (sess, FALSE);
		if (stick_to_end)
			xtext_scroll_to_end ();
		return;
	}

	if (log)
	{
		int added_col_px;
		int added_stamp_px;

		added_col_px = xtext_compute_message_column_px (text, &added_stamp_px);
		if (added_col_px > xtext_message_col_px || added_stamp_px > xtext_stamp_col_px)
			xtext_set_message_tab_stop (MAX (added_col_px, xtext_message_col_px),
				MAX (added_stamp_px, xtext_stamp_col_px));
	}

	xtext_render_raw_append (buf, text);
	xtext_render_session = NULL;
	session_buffer_set_dirty (sess, FALSE);

	if (stick_to_end)
		xtext_scroll_to_end ();
}

void
fe_gtk4_xtext_force_scroll_to_end (void)
{
	xtext_scroll_to_end ();
}

void
fe_gtk4_xtext_set_marker_last (session *sess)
{
	if (!sess || !is_session (sess))
		return;

	xtext_scroll_debug_log_state ("set-marker-last", sess, log_view, log_buffer);

	if (session_replay_marklast)
		g_hash_table_insert (session_replay_marklast, sess, GINT_TO_POINTER (TRUE));

	/* If the replayed session is currently visible, apply immediately. */
	if (sess == current_tab && log_view)
	{
		xtext_scroll_debug_log_state ("set-marker-last-visible-schedule", sess,
			log_view, log_buffer);
		xtext_scroll_to_end_for_replay (sess);
	}
}

void
fe_gtk4_xtext_show_session (session *sess)
{
	xtext_show_session_rendered (sess);
}

void
fe_gtk4_xtext_remove_session (session *sess)
{
	GtkTextBuffer *buf;

	if (!sess)
		return;

	if (session_logs)
		g_hash_table_remove (session_logs, sess);

	if (session_buffers_dirty)
		g_hash_table_remove (session_buffers_dirty, sess);

	if (session_shown_once)
		g_hash_table_remove (session_shown_once, sess);

	if (session_replay_marklast)
		g_hash_table_remove (session_replay_marklast, sess);

	if (session_widgets)
	{
		HcSessionWidget *widget;

		widget = g_hash_table_lookup (session_widgets, sess);
		if (widget && widget->view == log_view)
		{
			log_view = xtext_empty_view;
			log_buffer = xtext_empty_view ?
				gtk_text_view_get_buffer (GTK_TEXT_VIEW (xtext_empty_view)) : NULL;
			if (xtext_stack && xtext_empty_scroll)
				gtk_stack_set_visible_child (GTK_STACK (xtext_stack), xtext_empty_scroll);
			xtext_stamp_col_px = 0;
			xtext_message_col_px = 0;
		}
		if (widget && widget->view)
			fe_gtk4_menu_close_context_popovers (NULL);
		g_hash_table_remove (session_widgets, sess);
	}

	if (session_buffers)
	{
		buf = g_hash_table_lookup (session_buffers, sess);
		if (buf && buf == log_buffer)
			log_buffer = NULL;
		g_hash_table_remove (session_buffers, sess);
	}
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
	GtkTextBuffer *buf;
	HcSessionWidget *widget;

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

	if (sess != current_tab)
	{
		session_buffer_set_dirty (sess, TRUE);
		return;
	}

	widget = session_widget_ensure (sess);
	buf = session_buffer_ensure (sess);
	if (buf)
	{
		if (widget && widget->view)
			log_view = widget->view;
		log_buffer = buf;
		xtext_render_session = sess;
		xtext_render_raw_all (buf, log->str);
		xtext_render_session = NULL;
		session_buffer_set_dirty (sess, FALSE);
	}

	if (sess == current_tab)
		xtext_scroll_to_end ();
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

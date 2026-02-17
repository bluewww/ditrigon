/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 channel list window */

#include "fe-gtk4.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../common/text.h"

#define CHANLIST_UI_PATH "/org/hexchat/ui/gtk4/dialogs/chanlist-window.ui"

enum
{
	COL_CHANNEL,
	COL_USERS,
	COL_TOPIC,
	N_COLUMNS
};

typedef struct
{
	char *channel;
	char *topic;
	char *collation_key;
	guint32 users;
} chanlistrow;

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
	GtkWidget *label;
	GtkWidget *wild;
	GtkWidget *min_spin;
	GtkWidget *max_spin;
	GtkWidget *refresh;
	GtkWidget *join;
	GtkWidget *savelist;
	GtkWidget *search;
	GtkWidget *match_channel;
	GtkWidget *match_topic;
	GtkWidget *search_type;
	GtkWidget *sort_button[N_COLUMNS];
	server *serv;
	GSList *data_stored_rows;
	GSList *pending_rows;
	guint tag;
	guint flash_tag;
	gboolean flash_on;
	gboolean match_wants_channel;
	gboolean match_wants_topic;
	GRegex *match_regex;
	gboolean have_regex;
	guint users_found_count;
	guint users_shown_count;
	guint channels_found_count;
	guint channels_shown_count;
	guint32 maxusers;
	guint32 minusers;
	guint32 minusers_downloaded;
	int search_type_index;
	gboolean caption_is_stale;
	int sort_column;
	gboolean sort_desc;
	char request_filter[256];
} chanlist_info;

static chanlist_info chanlist;

static gboolean chanlist_timeout (gpointer userdata);
static void chanlist_update_caption (server *serv);
static void chanlist_update_buttons (server *serv);
static void chanlist_reset_counters (server *serv);
static void chanlist_data_free (server *serv);
static void chanlist_flush_pending (server *serv);
static void chanlist_place_row_in_gui (server *serv, chanlistrow *next_row, gboolean force);
static void chanlist_do_refresh (server *serv);
static void chanlist_build_gui_list (server *serv);

static void
chanlist_row_free (chanlistrow *row)
{
	if (!row)
		return;

	g_free (row->channel);
	g_free (row->topic);
	g_free (row->collation_key);
	g_free (row);
}

static gboolean
chanlist_match (server *serv, const char *str)
{
	const char *pattern;

	(void) serv;

	pattern = chanlist.wild ?
		gtk_editable_get_text (GTK_EDITABLE (chanlist.wild)) : "";
	if (!pattern)
		pattern = "";

	switch (chanlist.search_type_index)
	{
	case 1:
		return match ((char *) pattern, str ? str : "") ? TRUE : FALSE;
	case 2:
		if (!chanlist.have_regex)
			return FALSE;
		return g_regex_match (chanlist.match_regex, str ? str : "", 0, NULL);
	default:
		return nocasestrstr (str ? str : "", pattern) ? TRUE : FALSE;
	}
}

static gboolean
chanlist_row_passes (server *serv, chanlistrow *row)
{
	const char *pattern;

	(void) serv;

	if (!row)
		return FALSE;

	if (row->users < chanlist.minusers)
		return FALSE;
	if (chanlist.maxusers > 0 && row->users > chanlist.maxusers)
		return FALSE;

	pattern = chanlist.wild ?
		gtk_editable_get_text (GTK_EDITABLE (chanlist.wild)) : "";
	if (!pattern || !pattern[0])
		return TRUE;

	if (chanlist.match_wants_channel == chanlist.match_wants_topic)
	{
		if (!chanlist_match (serv, row->channel) && !chanlist_match (serv, row->topic))
			return FALSE;
	}
	else if (chanlist.match_wants_channel)
	{
		if (!chanlist_match (serv, row->channel))
			return FALSE;
	}
	else if (chanlist.match_wants_topic)
	{
		if (!chanlist_match (serv, row->topic))
			return FALSE;
	}

	return TRUE;
}

static void
chanlist_update_caption (server *serv)
{
	char tbuf[256];

	(void) serv;
	if (!chanlist.label)
		return;

	g_snprintf (tbuf, sizeof (tbuf),
		_("Displaying %d/%d users on %d/%d channels."),
		chanlist.users_shown_count,
		chanlist.users_found_count,
		chanlist.channels_shown_count,
		chanlist.channels_found_count);

	gtk_label_set_text (GTK_LABEL (chanlist.label), tbuf);
	chanlist.caption_is_stale = FALSE;
}

static void
chanlist_update_buttons (server *serv)
{
	(void) serv;

	if (!chanlist.join || !chanlist.savelist)
		return;

	if (chanlist.channels_shown_count)
	{
		gtk_widget_set_sensitive (chanlist.join, TRUE);
		gtk_widget_set_sensitive (chanlist.savelist, TRUE);
	}
	else
	{
		gtk_widget_set_sensitive (chanlist.join, FALSE);
		gtk_widget_set_sensitive (chanlist.savelist, FALSE);
	}
}

static void
chanlist_reset_counters (server *serv)
{
	chanlist.users_found_count = 0;
	chanlist.users_shown_count = 0;
	chanlist.channels_found_count = 0;
	chanlist.channels_shown_count = 0;

	chanlist_update_caption (serv);
	chanlist_update_buttons (serv);
}

static void
chanlist_data_free (server *serv)
{
	GSList *list;

	(void) serv;

	for (list = chanlist.data_stored_rows; list; list = list->next)
		chanlist_row_free ((chanlistrow *) list->data);

	g_slist_free (chanlist.data_stored_rows);
	chanlist.data_stored_rows = NULL;

	g_slist_free (chanlist.pending_rows);
	chanlist.pending_rows = NULL;
}

static void
chanlist_clear_gui_rows (void)
{
	GtkWidget *row;

	if (!chanlist.list)
		return;

	while ((row = gtk_widget_get_first_child (chanlist.list)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (chanlist.list), row);
}

static GtkWidget *
chanlist_create_row_widget (chanlistrow *rowdata)
{
	GtkWidget *row;
	GtkWidget *grid;
	GtkWidget *chan_label;
	GtkWidget *users_label;
	GtkWidget *topic_label;
	char users_buf[32];

	row = gtk_list_box_row_new ();
	grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_widget_set_margin_start (grid, 8);
	gtk_widget_set_margin_end (grid, 8);
	gtk_widget_set_margin_top (grid, 2);
	gtk_widget_set_margin_bottom (grid, 2);

	chan_label = gtk_label_new (rowdata->channel ? rowdata->channel : "");
	gtk_label_set_xalign (GTK_LABEL (chan_label), 0.0f);
	gtk_widget_set_hexpand (chan_label, FALSE);
	gtk_grid_attach (GTK_GRID (grid), chan_label, 0, 0, 1, 1);

	g_snprintf (users_buf, sizeof (users_buf), "%u", (unsigned int) rowdata->users);
	users_label = gtk_label_new (users_buf);
	gtk_label_set_xalign (GTK_LABEL (users_label), 1.0f);
	gtk_widget_set_hexpand (users_label, FALSE);
	gtk_grid_attach (GTK_GRID (grid), users_label, 1, 0, 1, 1);

	topic_label = gtk_label_new (rowdata->topic ? rowdata->topic : "");
	gtk_label_set_xalign (GTK_LABEL (topic_label), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (topic_label), FALSE);
	gtk_label_set_ellipsize (GTK_LABEL (topic_label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (topic_label, TRUE);
	gtk_grid_attach (GTK_GRID (grid), topic_label, 2, 0, 1, 1);

	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), grid);
	g_object_set_data (G_OBJECT (row), "hc-rowdata", rowdata);

	return row;
}

static void
chanlist_append_row_widget (chanlistrow *rowdata)
{
	GtkWidget *row;

	if (!chanlist.list || !rowdata)
		return;

	row = chanlist_create_row_widget (rowdata);
	gtk_list_box_append (GTK_LIST_BOX (chanlist.list), row);
}

static int
chanlist_list_sort_cb (GtkListBoxRow *a, GtkListBoxRow *b, gpointer userdata)
{
	chanlistrow *ra;
	chanlistrow *rb;
	int result;

	(void) userdata;

	ra = g_object_get_data (G_OBJECT (a), "hc-rowdata");
	rb = g_object_get_data (G_OBJECT (b), "hc-rowdata");
	if (!ra || !rb)
		return 0;

	switch (chanlist.sort_column)
	{
	case COL_USERS:
		if (ra->users < rb->users)
			result = -1;
		else if (ra->users > rb->users)
			result = 1;
		else
			result = g_strcmp0 (ra->collation_key, rb->collation_key);
		break;
	case COL_TOPIC:
		result = g_utf8_collate (ra->topic ? ra->topic : "", rb->topic ? rb->topic : "");
		if (result == 0)
			result = g_strcmp0 (ra->collation_key, rb->collation_key);
		break;
	case COL_CHANNEL:
	default:
		result = g_strcmp0 (ra->collation_key, rb->collation_key);
		break;
	}

	if (chanlist.sort_desc)
		result = -result;

	return result;
}

static void
chanlist_flush_pending (server *serv)
{
	GSList *pending;
	GSList *list;

	if (!chanlist.pending_rows)
	{
		if (chanlist.caption_is_stale)
			chanlist_update_caption (serv);
		return;
	}

	pending = g_slist_reverse (chanlist.pending_rows);
	chanlist.pending_rows = NULL;

	for (list = pending; list; list = list->next)
		chanlist_append_row_widget ((chanlistrow *) list->data);

	g_slist_free (pending);
	gtk_list_box_invalidate_sort (GTK_LIST_BOX (chanlist.list));
	chanlist_update_caption (serv);
}

static gboolean
chanlist_timeout (gpointer userdata)
{
	server *serv;

	serv = userdata;
	if (!serv || !chanlist.serv || chanlist.serv != serv || !chanlist.window)
	{
		chanlist.tag = 0;
		return G_SOURCE_REMOVE;
	}

	chanlist_flush_pending (serv);
	return G_SOURCE_CONTINUE;
}

static void
chanlist_place_row_in_gui (server *serv, chanlistrow *next_row, gboolean force)
{
	chanlist.users_found_count += next_row->users;
	chanlist.channels_found_count++;

	if (!chanlist_row_passes (serv, next_row))
	{
		chanlist.caption_is_stale = TRUE;
		return;
	}

	if (force || chanlist.channels_shown_count < 20)
	{
		chanlist_append_row_widget (next_row);
		chanlist_update_caption (serv);
	}
	else
	{
		chanlist.pending_rows = g_slist_prepend (chanlist.pending_rows, next_row);
	}

	chanlist.users_shown_count += next_row->users;
	chanlist.channels_shown_count++;
	chanlist_update_buttons (serv);
}

static gboolean
chanlist_flash (gpointer userdata)
{
	(void) userdata;

	if (!chanlist.refresh)
		return G_SOURCE_REMOVE;

	if (chanlist.flash_on)
		gtk_widget_remove_css_class (chanlist.refresh, "suggested-action");
	else
		gtk_widget_add_css_class (chanlist.refresh, "suggested-action");

	chanlist.flash_on = !chanlist.flash_on;
	return G_SOURCE_CONTINUE;
}

static void
chanlist_stop_flash (void)
{
	if (chanlist.flash_tag)
	{
		g_source_remove (chanlist.flash_tag);
		chanlist.flash_tag = 0;
	}
	if (chanlist.refresh)
		gtk_widget_remove_css_class (chanlist.refresh, "suggested-action");
	chanlist.flash_on = FALSE;
}

static void
chanlist_do_refresh (server *serv)
{
	const char *request;

	if (!serv)
		return;

	chanlist_stop_flash ();

	if (!serv->connected)
	{
		fe_message (_("Not connected."), FE_MSG_ERROR);
		return;
	}

	chanlist_clear_gui_rows ();
	if (chanlist.refresh)
		gtk_widget_set_sensitive (chanlist.refresh, FALSE);

	chanlist_data_free (serv);
	chanlist_reset_counters (serv);

	request = chanlist.request_filter;
	if (!request)
		request = "";

	if (serv->use_listargs)
	{
		serv->p_list_channels (serv, (char *) request, chanlist.minusers);
		chanlist.minusers_downloaded = chanlist.minusers;
	}
	else
	{
		serv->p_list_channels (serv, (char *) request, 1);
		chanlist.minusers_downloaded = 1;
	}
}

static void
chanlist_refresh (GtkWidget *wid, server *serv)
{
	const char *text;

	(void) wid;
	(void) serv;
	text = chanlist.wild ? gtk_editable_get_text (GTK_EDITABLE (chanlist.wild)) : "";
	g_strlcpy (chanlist.request_filter, text ? text : "", sizeof (chanlist.request_filter));
	chanlist_do_refresh (chanlist.serv);
}

static void
chanlist_build_gui_list (server *serv)
{
	GSList *rows;

	if (!serv)
		return;

	if (chanlist.data_stored_rows == NULL)
	{
		chanlist_do_refresh (serv);
		return;
	}

	chanlist_clear_gui_rows ();
	g_slist_free (chanlist.pending_rows);
	chanlist.pending_rows = NULL;

	chanlist_reset_counters (serv);

	for (rows = chanlist.data_stored_rows; rows != NULL; rows = rows->next)
		chanlist_place_row_in_gui (serv, (chanlistrow *) rows->data, TRUE);

	gtk_list_box_invalidate_sort (GTK_LIST_BOX (chanlist.list));
	chanlist_update_buttons (serv);
}

void
fe_add_chan_list (server *serv, char *chan, char *users, char *topic)
{
	chanlistrow *next_row;
	int len;

	if (!chan || !chan[0] || !serv || serv != chanlist.serv)
		return;

	next_row = g_new0 (chanlistrow, 1);
	next_row->channel = g_strdup (chan);
	next_row->topic = strip_color (topic ? topic : "", -1, STRIP_ALL);
	len = strlen (chan);
	next_row->collation_key = g_utf8_collate_key (chan, len);
	if (!next_row->collation_key)
		next_row->collation_key = g_strdup (chan);
	next_row->users = (guint32) atoi (users ? users : "0");

	chanlist.data_stored_rows = g_slist_prepend (chanlist.data_stored_rows, next_row);
	chanlist_place_row_in_gui (serv, next_row, FALSE);
}

void
fe_chan_list_end (server *serv)
{
	if (!serv || serv != chanlist.serv)
		return;

	chanlist_flush_pending (serv);
	if (chanlist.refresh)
		gtk_widget_set_sensitive (chanlist.refresh, TRUE);
	if (chanlist.list)
		gtk_list_box_invalidate_sort (GTK_LIST_BOX (chanlist.list));
	chanlist_update_buttons (serv);
}

static void
chanlist_search_pressed (GtkWidget *button, server *serv)
{
	(void) button;
	(void) serv;
	chanlist_build_gui_list (chanlist.serv);
}

static void
chanlist_find_cb (GtkWidget *wid, server *serv)
{
	const char *pattern;
	GError *error;

	(void) serv;
	pattern = gtk_editable_get_text (GTK_EDITABLE (wid));

	if (chanlist.have_regex)
	{
		chanlist.have_regex = FALSE;
		g_regex_unref (chanlist.match_regex);
		chanlist.match_regex = NULL;
	}

	error = NULL;
	chanlist.match_regex = g_regex_new (pattern ? pattern : "",
		G_REGEX_CASELESS | G_REGEX_EXTENDED,
		G_REGEX_MATCH_NOTBOL,
		&error);
	if (!error && chanlist.match_regex)
		chanlist.have_regex = TRUE;
	if (error)
		g_error_free (error);
}

static void
chanlist_match_channel_button_toggled (GtkWidget *wid, server *serv)
{
	(void) serv;
	chanlist.match_wants_channel = gtk_check_button_get_active (GTK_CHECK_BUTTON (wid));
}

static void
chanlist_match_topic_button_toggled (GtkWidget *wid, server *serv)
{
	(void) serv;
	chanlist.match_wants_topic = gtk_check_button_get_active (GTK_CHECK_BUTTON (wid));
}

static chanlistrow *
chanlist_get_selected (server *serv)
{
	GtkListBoxRow *row;

	(void) serv;
	if (!chanlist.list)
		return NULL;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (chanlist.list));
	if (!row)
		return NULL;

	return g_object_get_data (G_OBJECT (row), "hc-rowdata");
}

static void
chanlist_join (GtkWidget *wid, server *serv)
{
	char tbuf[CHANLEN + 6];
	chanlistrow *row;

	(void) wid;

	if (!serv)
		serv = chanlist.serv;
	if (!serv)
		return;

	row = chanlist_get_selected (serv);
	if (!row)
		return;

	if (serv->connected && strcmp (row->channel, "*") != 0)
	{
		g_snprintf (tbuf, sizeof (tbuf), "join %s", row->channel);
		handle_command (serv->server_session, tbuf, FALSE);
	}
}

static void
chanlist_filereq_done (void *userdata, char *file)
{
	server *serv;
	time_t t;
	int fh;
	char buf[1024];
	GtkWidget *node;

	serv = userdata;
	if (!serv || !file)
		return;

	fh = hexchat_open_file (file, O_TRUNC | O_WRONLY | O_CREAT, 0600,
		XOF_DOMODE | XOF_FULLPATH);
	if (fh == -1)
		return;

	t = time (0);
	g_snprintf (buf, sizeof (buf), "HexChat Channel List: %s - %s\n",
		serv->servername,
		ctime (&t));
	write (fh, buf, strlen (buf));

	for (node = gtk_widget_get_first_child (chanlist.list); node; node = gtk_widget_get_next_sibling (node))
	{
		chanlistrow *rowdata;

		rowdata = g_object_get_data (G_OBJECT (node), "hc-rowdata");
		if (!rowdata)
			continue;

		g_snprintf (buf, sizeof (buf), "%-16s %-5u%s\n",
			rowdata->channel ? rowdata->channel : "",
			(unsigned int) rowdata->users,
			rowdata->topic ? rowdata->topic : "");
		write (fh, buf, strlen (buf));
	}

	close (fh);
}

static void
chanlist_save (GtkWidget *wid, server *serv)
{
	(void) wid;
	(void) serv;

	if (!chanlist.serv || !chanlist.channels_shown_count)
		return;

	fe_get_file (_("Select an output filename"), NULL,
		chanlist_filereq_done, chanlist.serv, FRF_WRITE);
}

static void
chanlist_minusers (GtkSpinButton *wid, server *serv)
{
	chanlist.minusers = (guint32) gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_minusers = (int) chanlist.minusers;
	save_config ();

	if (chanlist.minusers < chanlist.minusers_downloaded)
	{
		if (chanlist.flash_tag == 0)
			chanlist.flash_tag = g_timeout_add (500, chanlist_flash, chanlist.serv);
	}
	else
	{
		chanlist_stop_flash ();
	}
}

static void
chanlist_maxusers (GtkSpinButton *wid, server *serv)
{
	(void) serv;
	chanlist.maxusers = (guint32) gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_maxusers = (int) chanlist.maxusers;
	save_config ();
}

static void
chanlist_row_activated_cb (GtkListBox *view, GtkListBoxRow *row,
	gpointer data)
{
	(void) view;
	(void) row;
	(void) data;
	chanlist_join (NULL, chanlist.serv);
}

static void
chanlist_combo_cb (GObject *object, GParamSpec *pspec, gpointer userdata)
{
	GtkDropDown *dd;
	guint index;

	(void) pspec;
	(void) userdata;
	dd = GTK_DROP_DOWN (object);
	index = gtk_drop_down_get_selected (dd);
	if (index == GTK_INVALID_LIST_POSITION)
		return;

	chanlist.search_type_index = (int) index;
	chanlist_find_cb (chanlist.wild, chanlist.serv);
}

static void
chanlist_sort_label_update (int column)
{
	const char *base[N_COLUMNS] =
	{
		N_("Channel"),
		N_("Users"),
		N_("Topic")
	};
	char *label;
	int i;

	for (i = 0; i < N_COLUMNS; i++)
	{
		if (!chanlist.sort_button[i])
			continue;

		if (i == column)
			label = g_strdup_printf ("%s %s", _(base[i]), chanlist.sort_desc ? "\xE2\x96\xBE" : "\xE2\x96\xB4");
		else
			label = g_strdup (_(base[i]));

		gtk_button_set_label (GTK_BUTTON (chanlist.sort_button[i]), label);
		g_free (label);
	}
}

static void
chanlist_sort_cb (GtkWidget *button, gpointer userdata)
{
	int column;

	(void) button;
	column = GPOINTER_TO_INT (userdata);

	if (chanlist.sort_column == column)
		chanlist.sort_desc = !chanlist.sort_desc;
	else
	{
		chanlist.sort_column = column;
		chanlist.sort_desc = FALSE;
	}

	chanlist_sort_label_update (chanlist.sort_column);
	if (chanlist.list)
		gtk_list_box_invalidate_sort (GTK_LIST_BOX (chanlist.list));
}

static void
chanlist_destroy_widget (GtkWidget *wid, server *serv)
{
	(void) wid;
	(void) serv;

	chanlist_stop_flash ();

	if (chanlist.tag)
	{
		g_source_remove (chanlist.tag);
		chanlist.tag = 0;
	}

	if (chanlist.have_regex)
	{
		g_regex_unref (chanlist.match_regex);
		chanlist.match_regex = NULL;
		chanlist.have_regex = FALSE;
	}

	chanlist_data_free (chanlist.serv);
}

static gboolean
chanlist_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) userdata;

	chanlist_destroy_widget (GTK_WIDGET (window), chanlist.serv);
	memset (&chanlist, 0, sizeof (chanlist));

	return FALSE;
}

int
fe_is_chanwindow (struct server *serv)
{
	return chanlist.window != NULL &&
		chanlist.serv == serv &&
		gtk_widget_get_visible (chanlist.window);
}

static void
chanlist_open (server *serv, const char *filter, int do_refresh)
{
	GtkStringList *search_model;
	GtkBuilder *builder;
	char tbuf[256];
	const char *search_modes[] =
	{
		_("Simple Search"),
		_("Pattern Match (Wildcards)"),
		_("Regular Expression"),
		NULL
	};
	gboolean server_changed;

	(void) filter;

	server_changed = chanlist.serv && chanlist.serv != serv;

	if (chanlist.window)
	{
		gtk_window_present (GTK_WINDOW (chanlist.window));
		if (chanlist.serv == serv)
			return;
	}

	if (chanlist.minusers == 0)
	{
		if (prefs.hex_gui_chanlist_minusers < 1 || prefs.hex_gui_chanlist_minusers > 999999)
		{
			prefs.hex_gui_chanlist_minusers = 5;
			save_config ();
		}
		chanlist.minusers = (guint32) prefs.hex_gui_chanlist_minusers;
	}

	if (chanlist.maxusers == 0)
	{
		if (prefs.hex_gui_chanlist_maxusers < 1 || prefs.hex_gui_chanlist_maxusers > 999999)
		{
			prefs.hex_gui_chanlist_maxusers = 9999;
			save_config ();
		}
		chanlist.maxusers = (guint32) prefs.hex_gui_chanlist_maxusers;
	}

	if (!chanlist.window)
	{
		builder = fe_gtk4_builder_new_from_resource (CHANLIST_UI_PATH);

		chanlist.window = fe_gtk4_builder_get_widget (builder, "chanlist_window", GTK_TYPE_WINDOW);
		chanlist.label = fe_gtk4_builder_get_widget (builder, "chanlist_label", GTK_TYPE_LABEL);
		chanlist.list = fe_gtk4_builder_get_widget (builder, "chanlist_list", GTK_TYPE_LIST_BOX);
		chanlist.sort_button[COL_CHANNEL] = fe_gtk4_builder_get_widget (builder, "chanlist_sort_channel_button", GTK_TYPE_BUTTON);
		chanlist.sort_button[COL_USERS] = fe_gtk4_builder_get_widget (builder, "chanlist_sort_users_button", GTK_TYPE_BUTTON);
		chanlist.sort_button[COL_TOPIC] = fe_gtk4_builder_get_widget (builder, "chanlist_sort_topic_button", GTK_TYPE_BUTTON);
		chanlist.search = fe_gtk4_builder_get_widget (builder, "chanlist_search_button", GTK_TYPE_BUTTON);
		chanlist.refresh = fe_gtk4_builder_get_widget (builder, "chanlist_refresh_button", GTK_TYPE_BUTTON);
		chanlist.savelist = fe_gtk4_builder_get_widget (builder, "chanlist_save_button", GTK_TYPE_BUTTON);
		chanlist.join = fe_gtk4_builder_get_widget (builder, "chanlist_join_button", GTK_TYPE_BUTTON);
		chanlist.min_spin = fe_gtk4_builder_get_widget (builder, "chanlist_min_spin", GTK_TYPE_SPIN_BUTTON);
		chanlist.max_spin = fe_gtk4_builder_get_widget (builder, "chanlist_max_spin", GTK_TYPE_SPIN_BUTTON);
		chanlist.match_channel = fe_gtk4_builder_get_widget (builder, "chanlist_match_channel", GTK_TYPE_CHECK_BUTTON);
		chanlist.match_topic = fe_gtk4_builder_get_widget (builder, "chanlist_match_topic", GTK_TYPE_CHECK_BUTTON);
		chanlist.search_type = fe_gtk4_builder_get_widget (builder, "chanlist_search_type", GTK_TYPE_DROP_DOWN);
		chanlist.wild = fe_gtk4_builder_get_widget (builder, "chanlist_find_entry", GTK_TYPE_ENTRY);

		g_object_ref_sink (chanlist.window);
		g_object_unref (builder);

		if (main_window)
			gtk_window_set_transient_for (GTK_WINDOW (chanlist.window), GTK_WINDOW (main_window));

		search_model = gtk_string_list_new (search_modes);
		gtk_drop_down_set_model (GTK_DROP_DOWN (chanlist.search_type), G_LIST_MODEL (search_model));
		g_object_unref (search_model);

		gtk_spin_button_set_value (GTK_SPIN_BUTTON (chanlist.min_spin), chanlist.minusers);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (chanlist.max_spin), chanlist.maxusers);
		gtk_drop_down_set_selected (GTK_DROP_DOWN (chanlist.search_type),
			(guint) chanlist.search_type_index);

		gtk_list_box_set_sort_func (GTK_LIST_BOX (chanlist.list), chanlist_list_sort_cb, NULL, NULL);
		g_signal_connect (chanlist.list, "row-activated", G_CALLBACK (chanlist_row_activated_cb), NULL);

		g_signal_connect (chanlist.sort_button[COL_CHANNEL], "clicked",
			G_CALLBACK (chanlist_sort_cb), GINT_TO_POINTER (COL_CHANNEL));
		g_signal_connect (chanlist.sort_button[COL_USERS], "clicked",
			G_CALLBACK (chanlist_sort_cb), GINT_TO_POINTER (COL_USERS));
		g_signal_connect (chanlist.sort_button[COL_TOPIC], "clicked",
			G_CALLBACK (chanlist_sort_cb), GINT_TO_POINTER (COL_TOPIC));

		g_signal_connect (chanlist.search, "clicked", G_CALLBACK (chanlist_search_pressed), NULL);
		g_signal_connect (chanlist.refresh, "clicked", G_CALLBACK (chanlist_refresh), NULL);
		g_signal_connect (chanlist.savelist, "clicked", G_CALLBACK (chanlist_save), NULL);
		g_signal_connect (chanlist.join, "clicked", G_CALLBACK (chanlist_join), NULL);
		g_signal_connect (chanlist.min_spin, "value-changed", G_CALLBACK (chanlist_minusers), NULL);
		g_signal_connect (chanlist.max_spin, "value-changed", G_CALLBACK (chanlist_maxusers), NULL);
		g_signal_connect (chanlist.match_channel, "toggled",
			G_CALLBACK (chanlist_match_channel_button_toggled), NULL);
		g_signal_connect (chanlist.match_topic, "toggled",
			G_CALLBACK (chanlist_match_topic_button_toggled), NULL);
		g_signal_connect (chanlist.search_type, "notify::selected",
			G_CALLBACK (chanlist_combo_cb), NULL);
		g_signal_connect (chanlist.wild, "changed", G_CALLBACK (chanlist_find_cb), NULL);
		g_signal_connect (chanlist.wild, "activate", G_CALLBACK (chanlist_search_pressed), NULL);

		g_signal_connect (chanlist.window, "close-request",
			G_CALLBACK (chanlist_close_request_cb), NULL);
	}

	if (server_changed)
	{
		chanlist_clear_gui_rows ();
		chanlist_data_free (chanlist.serv);
		chanlist_reset_counters (serv);
	}

	chanlist.serv = serv;
	g_strlcpy (chanlist.request_filter, (filter && filter[0]) ? filter : "",
		sizeof (chanlist.request_filter));
	if (chanlist.wild)
		gtk_editable_set_text (GTK_EDITABLE (chanlist.wild), chanlist.request_filter);

	chanlist.match_wants_channel = TRUE;
	chanlist.match_wants_topic = TRUE;
	chanlist.sort_column = COL_CHANNEL;
	chanlist.sort_desc = FALSE;
	chanlist_sort_label_update (chanlist.sort_column);

	g_snprintf (tbuf, sizeof (tbuf), _("Channel List (%s) - %s"),
		server_get_network (serv, TRUE), PACKAGE_NAME);
	gtk_window_set_title (GTK_WINDOW (chanlist.window), tbuf);

	if (chanlist.tag)
		g_source_remove (chanlist.tag);
	chanlist.tag = g_timeout_add (250, chanlist_timeout, serv);

	chanlist_find_cb (chanlist.wild, chanlist.serv);
	chanlist_reset_counters (serv);
	chanlist_update_buttons (serv);
	gtk_window_present (GTK_WINDOW (chanlist.window));
	if (chanlist.refresh)
		gtk_widget_grab_focus (chanlist.refresh);

	if (do_refresh)
		chanlist_do_refresh (serv);
	else if (chanlist.data_stored_rows)
		chanlist_build_gui_list (serv);
}

void
chanlist_opengui (server *serv, int do_refresh)
{
	if (serv)
		chanlist_open (serv, NULL, do_refresh);
}

void
fe_open_chan_list (server *serv, char *filter, int do_refresh)
{
	if (!serv)
		return;

	if (filter && filter[0])
		chanlist_open (serv, filter, do_refresh);
	else
		chanlist_opengui (serv, do_refresh);
}

void
fe_gtk4_chanlist_cleanup (void)
{
	if (chanlist.window)
		gtk_window_destroy (GTK_WINDOW (chanlist.window));

	chanlist_destroy_widget (NULL, chanlist.serv);
	memset (&chanlist, 0, sizeof (chanlist));
}

/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 ban list */

#include "fe-gtk4.h"

#ifndef RPL_BANLIST
#define RPL_BANLIST 367
#define RPL_ENDOFBANLIST 368
#define RPL_INVITELIST 346
#define RPL_ENDOFINVITELIST 347
#define RPL_EXCEPTLIST 348
#define RPL_ENDOFEXCEPTLIST 349
#define RPL_QUIETLIST 728
#define RPL_ENDOFQUIETLIST 729
#endif

#define BANLIST_UI_PATH "/org/hexchat/ui/gtk4/dialogs/banlist-window.ui"

typedef struct
{
	char *mask;
	char mode;
	int rplcode;
} HcBanRowData;

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
	GtkWidget *status;
	GtkWidget *remove_button;
	GtkWidget *clear_button;
	session *sess;
	int pending;
} HcBanView;

static HcBanView ban_view;

static void
ban_row_data_free (gpointer data)
{
	HcBanRowData *row;

	row = data;
	if (!row)
		return;

	g_free (row->mask);
	g_free (row);
}

static char
ban_mode_from_rpl (int rplcode)
{
	switch (rplcode)
	{
	case RPL_BANLIST:
		return 'b';
	case RPL_EXCEPTLIST:
		return 'e';
	case RPL_INVITELIST:
		return 'I';
	case RPL_QUIETLIST:
		return 'q';
	default:
		return 'b';
	}
}

static const char *
ban_kind_from_rpl (int rplcode)
{
	switch (rplcode)
	{
	case RPL_BANLIST:
		return _("Ban");
	case RPL_EXCEPTLIST:
		return _("Exception");
	case RPL_INVITELIST:
		return _("Invite");
	case RPL_QUIETLIST:
		return _("Quiet");
	default:
		return _("Entry");
	}
}

static gboolean
ban_is_end_code (int rplcode)
{
	switch (rplcode)
	{
	case RPL_ENDOFBANLIST:
	case RPL_ENDOFEXCEPTLIST:
	case RPL_ENDOFINVITELIST:
	case RPL_ENDOFQUIETLIST:
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
ban_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	ban_view.window = NULL;
	ban_view.list = NULL;
	ban_view.status = NULL;
	ban_view.remove_button = NULL;
	ban_view.clear_button = NULL;
	ban_view.sess = NULL;
	ban_view.pending = 0;
	return FALSE;
}

static void
ban_clear_rows (void)
{
	GtkWidget *child;

	if (!ban_view.list)
		return;

	while ((child = gtk_widget_get_first_child (ban_view.list)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (ban_view.list), child);
}

static void
ban_update_status (const char *suffix)
{
	char *text;
	const char *chan;

	if (!ban_view.status)
		return;

	chan = (ban_view.sess && ban_view.sess->channel[0]) ? ban_view.sess->channel : _("(unknown channel)");
	text = g_strdup_printf (_("%s - pending replies: %d%s%s"),
		chan, ban_view.pending,
		suffix && suffix[0] ? " - " : "",
		suffix ? suffix : "");
	gtk_label_set_text (GTK_LABEL (ban_view.status), text);
	g_free (text);
}

static void
ban_selection_changed_cb (GtkListBox *box, gpointer userdata)
{
	GtkListBoxRow *row;
	gboolean has_rows;

	(void) box;
	(void) userdata;

	if (!ban_view.list)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (ban_view.list));
	has_rows = gtk_widget_get_first_child (ban_view.list) != NULL;

	if (ban_view.remove_button)
		gtk_widget_set_sensitive (ban_view.remove_button, row != NULL);
	if (ban_view.clear_button)
		gtk_widget_set_sensitive (ban_view.clear_button, has_rows);
}

static void
ban_remove_entry (HcBanRowData *row)
{
	char modebuf[512];

	if (!row || !ban_view.sess || !ban_view.sess->server || !row->mask || !row->mask[0])
		return;

	g_snprintf (modebuf, sizeof (modebuf), "-%c %s", row->mode, row->mask);
	ban_view.sess->server->p_mode (ban_view.sess->server, ban_view.sess->channel, modebuf);
}

static void
ban_remove_selected_cb (GtkButton *button, gpointer userdata)
{
	GtkListBoxRow *row;
	HcBanRowData *row_data;

	(void) button;
	(void) userdata;

	if (!ban_view.list)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (ban_view.list));
	if (!row)
		return;

	row_data = g_object_get_data (G_OBJECT (row), "hc-ban-row");
	ban_remove_entry (row_data);
}

static void
ban_clear_all_cb (GtkButton *button, gpointer userdata)
{
	GtkWidget *child;

	(void) button;
	(void) userdata;

	if (!ban_view.list)
		return;

	child = gtk_widget_get_first_child (ban_view.list);
	while (child)
	{
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		HcBanRowData *row_data = g_object_get_data (G_OBJECT (child), "hc-ban-row");

		ban_remove_entry (row_data);
		child = next;
	}
}

static void
ban_refresh_request (session *sess)
{
	if (!sess || !sess->server || !sess->channel[0])
		return;

	ban_view.pending = 0;

	sess->server->p_mode (sess->server, sess->channel, "+b");
	ban_view.pending++;

	if (sess->server->have_except)
	{
		sess->server->p_mode (sess->server, sess->channel, "+e");
		ban_view.pending++;
	}

	if (sess->server->have_invite)
	{
		sess->server->p_mode (sess->server, sess->channel, "+I");
		ban_view.pending++;
	}

	ban_update_status (_("Refreshing"));
}

static void
ban_refresh_clicked_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (!ban_view.sess)
		return;

	ban_clear_rows ();
	ban_refresh_request (ban_view.sess);
	ban_selection_changed_cb (GTK_LIST_BOX (ban_view.list), NULL);
}

void
banlist_opengui (session *sess)
{
	GtkWidget *refresh_button;
	GtkWidget *close_button;
	GtkBuilder *builder;
	char *title;

	if (!sess || !sess->server || sess->type != SESS_CHANNEL)
	{
		fe_message (_("Ban list is only available for channel tabs."), FE_MSG_WARN);
		return;
	}

	if (!ban_view.window)
	{
		builder = fe_gtk4_builder_new_from_resource (BANLIST_UI_PATH);
		ban_view.window = fe_gtk4_builder_get_widget (builder, "banlist_window", GTK_TYPE_WINDOW);
		ban_view.status = fe_gtk4_builder_get_widget (builder, "banlist_status", GTK_TYPE_LABEL);
		ban_view.list = fe_gtk4_builder_get_widget (builder, "banlist_list", GTK_TYPE_LIST_BOX);
		refresh_button = fe_gtk4_builder_get_widget (builder, "banlist_refresh_button", GTK_TYPE_BUTTON);
		ban_view.remove_button = fe_gtk4_builder_get_widget (builder, "banlist_remove_button", GTK_TYPE_BUTTON);
		ban_view.clear_button = fe_gtk4_builder_get_widget (builder, "banlist_clear_button", GTK_TYPE_BUTTON);
		close_button = fe_gtk4_builder_get_widget (builder, "banlist_close_button", GTK_TYPE_BUTTON);
		g_object_ref_sink (ban_view.window);
		g_object_unref (builder);

		gtk_button_set_label (GTK_BUTTON (refresh_button), _("Refresh"));
		gtk_button_set_label (GTK_BUTTON (ban_view.remove_button), _("Remove"));
		gtk_button_set_label (GTK_BUTTON (ban_view.clear_button), _("Clear"));
		gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));
		g_signal_connect (ban_view.list, "selected-rows-changed",
			G_CALLBACK (ban_selection_changed_cb), NULL);
		g_signal_connect (refresh_button, "clicked",
			G_CALLBACK (ban_refresh_clicked_cb), NULL);
		g_signal_connect (ban_view.remove_button, "clicked",
			G_CALLBACK (ban_remove_selected_cb), NULL);
		g_signal_connect (ban_view.clear_button, "clicked",
			G_CALLBACK (ban_clear_all_cb), NULL);
		g_signal_connect_swapped (close_button, "clicked",
			G_CALLBACK (gtk_window_close), ban_view.window);
		g_signal_connect (ban_view.window, "close-request",
			G_CALLBACK (ban_close_request_cb), NULL);
	}

	ban_view.sess = sess;
	title = g_strdup_printf (_("Ban List - %s"), sess->channel);
	gtk_window_set_title (GTK_WINDOW (ban_view.window), title);
	g_free (title);

	ban_clear_rows ();
	ban_refresh_request (sess);
	ban_selection_changed_cb (GTK_LIST_BOX (ban_view.list), NULL);
	gtk_window_present (GTK_WINDOW (ban_view.window));
}

gboolean
fe_add_ban_list (struct session *sess, char *mask, char *who, char *when, int rplcode)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *title;
	GtkWidget *subtitle;
	HcBanRowData *row_data;
	char *subtitle_text;

	if (!ban_view.window || !ban_view.list || ban_view.sess != sess || !mask || !mask[0])
		return FALSE;

	row_data = g_new0 (HcBanRowData, 1);
	row_data->mask = g_strdup (mask);
	row_data->mode = ban_mode_from_rpl (rplcode);
	row_data->rplcode = rplcode;

	title = gtk_label_new (mask);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);

	subtitle_text = g_strdup_printf ("%s - %s - %s",
		ban_kind_from_rpl (rplcode),
		(who && who[0]) ? who : _("unknown"),
		(when && when[0]) ? when : _("unknown"));
	subtitle = gtk_label_new (subtitle_text);
	gtk_label_set_xalign (GTK_LABEL (subtitle), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (subtitle), TRUE);
	gtk_widget_add_css_class (subtitle, "dim-label");

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_margin_start (box, 10);
	gtk_widget_set_margin_end (box, 10);
	gtk_widget_set_margin_top (box, 6);
	gtk_widget_set_margin_bottom (box, 6);
	gtk_box_append (GTK_BOX (box), title);
	gtk_box_append (GTK_BOX (box), subtitle);

	row = gtk_list_box_row_new ();
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
	g_object_set_data_full (G_OBJECT (row), "hc-ban-row", row_data, ban_row_data_free);
	gtk_list_box_append (GTK_LIST_BOX (ban_view.list), row);

	g_free (subtitle_text);
	ban_selection_changed_cb (GTK_LIST_BOX (ban_view.list), NULL);
	return TRUE;
}

gboolean
fe_ban_list_end (struct session *sess, int rplcode)
{
	if (!ban_view.window || ban_view.sess != sess || !ban_is_end_code (rplcode))
		return FALSE;

	if (ban_view.pending > 0)
		ban_view.pending--;

	if (ban_view.pending == 0)
		ban_update_status (_("Done"));
	else
		ban_update_status (_("Receiving"));

	return TRUE;
}

void
fe_gtk4_banlist_cleanup (void)
{
	if (!ban_view.window)
		return;

	gtk_window_destroy (GTK_WINDOW (ban_view.window));
	ban_view.window = NULL;
	ban_view.list = NULL;
	ban_view.status = NULL;
	ban_view.remove_button = NULL;
	ban_view.clear_button = NULL;
	ban_view.sess = NULL;
	ban_view.pending = 0;
}

/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 DCC windows */

#include "fe-gtk4.h"

#include "../common/dcc.h"
#include "../common/network.h"

#define DCC_FILE_UI_PATH "/org/ditrigon/ui/gtk4/dialogs/dcc-file-window.ui"
#define DCC_CHAT_UI_PATH "/org/ditrigon/ui/gtk4/dialogs/dcc-chat-window.ui"

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
	GtkWidget *accept_button;
	GtkWidget *resume_button;
	GtkWidget *abort_button;
	GtkWidget *clear_button;
} HcDccFileWindow;

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
	GtkWidget *accept_button;
	GtkWidget *abort_button;
} HcDccChatWindow;

typedef struct
{
	struct session *sess;
	char *nick;
	gint64 maxcps;
	int passive;
} HcDccSendRequest;

static HcDccFileWindow dcc_file_window;
static HcDccChatWindow dcc_chat_window;

static void dcc_file_refresh (void);
static void dcc_chat_refresh (void);

static void
proper_unit (guint64 size, char *buf, size_t buf_len)
{
	gchar *formatted_str;
	GFormatSizeFlags format_flags = G_FORMAT_SIZE_DEFAULT;

#ifndef __APPLE__
	if (prefs.hex_gui_filesize_iec)
		format_flags = G_FORMAT_SIZE_IEC_UNITS;
#endif

	formatted_str = g_format_size_full (size, format_flags);
	g_strlcpy (buf, formatted_str, buf_len);
	g_free (formatted_str);
}

static const char *
dcc_type_name (struct DCC *dcc)
{
	if (!dcc)
		return "";

	switch (dcc->type)
	{
	case TYPE_SEND:
		return _("Upload");
	case TYPE_RECV:
		return _("Download");
	case TYPE_CHATSEND:
	case TYPE_CHATRECV:
		return _("Chat");
	default:
		return "";
	}
}

static char *
dcc_row_subtitle (struct DCC *dcc)
{
	char size[24];
	char pos[24];
	char speed[24];
	char perc[24];
	guint64 done;
	double per;

	if (!dcc)
		return g_strdup ("");

	if (dcc->type == TYPE_SEND)
		done = dcc->ack;
	else
		done = dcc->pos;

	proper_unit (dcc->size, size, sizeof (size));
	proper_unit (done, pos, sizeof (pos));
	g_snprintf (speed, sizeof (speed), "%.1f KB/s", ((double) dcc->cps) / 1024.0);

	if (dcc->size > 0)
		per = ((double) done * 100.0) / (double) dcc->size;
	else
		per = 0.0;
	g_snprintf (perc, sizeof (perc), "%.0f%%", per);

	return g_strdup_printf ("%s | %s/%s | %s | %s",
		_(dccstat[dcc->dccstat].name), pos, size, perc, speed);
}

static char *
dcc_chat_subtitle (struct DCC *dcc)
{
	char recv[24];
	char sent[24];
	char started[64];
	char *date;

	if (!dcc)
		return g_strdup ("");

	proper_unit (dcc->pos, recv, sizeof (recv));
	proper_unit (dcc->size, sent, sizeof (sent));

	date = ctime (&dcc->starttime);
	if (!date)
		return g_strdup_printf ("%s | %s/%s", _(dccstat[dcc->dccstat].name), recv, sent);

	g_strlcpy (started, date, sizeof (started));
	if (strlen (started) > 0 && started[strlen (started) - 1] == '\n')
		started[strlen (started) - 1] = 0;

	return g_strdup_printf ("%s | %s/%s | %s", _(dccstat[dcc->dccstat].name), recv, sent, started);
}

static void
listbox_clear (GtkWidget *list)
{
	GtkWidget *child;

	if (!list)
		return;

	while ((child = gtk_widget_get_first_child (list)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (list), child);
}

static GList *
dcc_list_selected_rows (GtkWidget *list)
{
	if (!list)
		return NULL;

	return gtk_list_box_get_selected_rows (GTK_LIST_BOX (list));
}

static void
dcc_file_row_add (struct DCC *dcc)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *title;
	GtkWidget *subtitle;
	char *title_text;
	char *subtitle_text;
	const char *file;

	if (!dcc_file_window.list || !dcc)
		return;

	file = dcc->file ? file_part (dcc->file) : "";
	title_text = g_strdup_printf ("%s - %s (%s)", dcc_type_name (dcc), file,
		dcc->nick ? dcc->nick : "");
	subtitle_text = dcc_row_subtitle (dcc);

	title = gtk_label_new (title_text);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);

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
	g_object_set_data (G_OBJECT (row), "hc-dcc", dcc);
	gtk_list_box_append (GTK_LIST_BOX (dcc_file_window.list), row);

	g_free (title_text);
	g_free (subtitle_text);
}

static void
dcc_chat_row_add (struct DCC *dcc)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *title;
	GtkWidget *subtitle;
	char *title_text;
	char *subtitle_text;

	if (!dcc_chat_window.list || !dcc)
		return;

	title_text = g_strdup_printf ("%s (%s)",
		dcc->nick ? dcc->nick : "",
		dcc->type == TYPE_CHATRECV ? _("incoming") : _("outgoing"));
	subtitle_text = dcc_chat_subtitle (dcc);

	title = gtk_label_new (title_text);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);

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
	g_object_set_data (G_OBJECT (row), "hc-dcc", dcc);
	gtk_list_box_append (GTK_LIST_BOX (dcc_chat_window.list), row);

	g_free (title_text);
	g_free (subtitle_text);
}

static gboolean
dcc_file_has_completed (void)
{
	GSList *list;

	for (list = dcc_list; list; list = list->next)
	{
		struct DCC *dcc = list->data;

		if (!dcc)
			continue;
		if (dcc->type != TYPE_SEND && dcc->type != TYPE_RECV)
			continue;
		if (is_dcc_completed (dcc))
			return TRUE;
	}

	return FALSE;
}

static void
dcc_file_update_buttons (void)
{
	GList *rows;
	gboolean have_rows;
	gboolean can_accept;
	gboolean can_resume;
	gboolean can_abort;
	GList *iter;

	if (!dcc_file_window.list)
		return;

	rows = dcc_list_selected_rows (dcc_file_window.list);
	have_rows = rows != NULL;
	can_accept = FALSE;
	can_resume = FALSE;
	can_abort = FALSE;

	for (iter = rows; iter; iter = iter->next)
	{
		GtkListBoxRow *row = iter->data;
		struct DCC *dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");

		if (!dcc)
			continue;

		can_abort = TRUE;
		if (dcc->type == TYPE_RECV && dcc->dccstat == STAT_QUEUED)
		{
			can_accept = TRUE;
			can_resume = TRUE;
		}
	}

	if (dcc_file_window.accept_button)
		gtk_widget_set_sensitive (dcc_file_window.accept_button, have_rows && can_accept);
	if (dcc_file_window.resume_button)
		gtk_widget_set_sensitive (dcc_file_window.resume_button, have_rows && can_resume);
	if (dcc_file_window.abort_button)
		gtk_widget_set_sensitive (dcc_file_window.abort_button, have_rows && can_abort);
	if (dcc_file_window.clear_button)
		gtk_widget_set_sensitive (dcc_file_window.clear_button, dcc_file_has_completed ());

	g_list_free (rows);
}

static void
dcc_chat_update_buttons (void)
{
	GList *rows;
	gboolean have_rows;
	gboolean can_accept;
	gboolean can_abort;
	GList *iter;

	if (!dcc_chat_window.list)
		return;

	rows = dcc_list_selected_rows (dcc_chat_window.list);
	have_rows = rows != NULL;
	can_accept = FALSE;
	can_abort = FALSE;

	for (iter = rows; iter; iter = iter->next)
	{
		GtkListBoxRow *row = iter->data;
		struct DCC *dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");

		if (!dcc)
			continue;

		can_abort = TRUE;
		if (dcc->type == TYPE_CHATRECV && dcc->dccstat == STAT_QUEUED)
			can_accept = TRUE;
	}

	if (dcc_chat_window.accept_button)
		gtk_widget_set_sensitive (dcc_chat_window.accept_button, have_rows && can_accept);
	if (dcc_chat_window.abort_button)
		gtk_widget_set_sensitive (dcc_chat_window.abort_button, have_rows && can_abort);

	g_list_free (rows);
}

static void
dcc_file_selected_changed_cb (GtkListBox *box, gpointer userdata)
{
	(void) box;
	(void) userdata;
	dcc_file_update_buttons ();
}

static void
dcc_chat_selected_changed_cb (GtkListBox *box, gpointer userdata)
{
	(void) box;
	(void) userdata;
	dcc_chat_update_buttons ();
}

static void
dcc_file_accept_cb (GtkButton *button, gpointer userdata)
{
	GList *rows;
	GList *iter;

	(void) button;
	(void) userdata;

	rows = dcc_list_selected_rows (dcc_file_window.list);
	for (iter = rows; iter; iter = iter->next)
	{
		GtkListBoxRow *row = iter->data;
		struct DCC *dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");

		if (!dcc || dcc->type == TYPE_SEND)
			continue;
		dcc_get (dcc);
	}
	g_list_free (rows);
}

static void
dcc_file_resume_cb (GtkButton *button, gpointer userdata)
{
	GList *rows;
	GList *iter;

	(void) button;
	(void) userdata;

	rows = dcc_list_selected_rows (dcc_file_window.list);
	for (iter = rows; iter; iter = iter->next)
	{
		GtkListBoxRow *row = iter->data;
		struct DCC *dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");

		if (!dcc || dcc->type != TYPE_RECV)
			continue;
		dcc_resume (dcc);
	}
	g_list_free (rows);
}

static void
dcc_abort_selected_rows (GList *rows)
{
	GList *iter;

	for (iter = rows; iter; iter = iter->next)
	{
		GtkListBoxRow *row = iter->data;
		struct DCC *dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");

		if (!dcc || !dcc->serv || !dcc->serv->front_session)
			continue;
		dcc_abort (dcc->serv->front_session, dcc);
	}
}

static void
dcc_file_abort_cb (GtkButton *button, gpointer userdata)
{
	GList *rows;

	(void) button;
	(void) userdata;

	rows = dcc_list_selected_rows (dcc_file_window.list);
	dcc_abort_selected_rows (rows);
	g_list_free (rows);
}

static void
dcc_chat_accept_cb (GtkButton *button, gpointer userdata)
{
	GList *rows;
	GList *iter;

	(void) button;
	(void) userdata;

	rows = dcc_list_selected_rows (dcc_chat_window.list);
	for (iter = rows; iter; iter = iter->next)
	{
		GtkListBoxRow *row = iter->data;
		struct DCC *dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");

		if (!dcc || dcc->type != TYPE_CHATRECV)
			continue;
		dcc_get (dcc);
	}
	g_list_free (rows);
}

static void
dcc_chat_abort_cb (GtkButton *button, gpointer userdata)
{
	GList *rows;

	(void) button;
	(void) userdata;

	rows = dcc_list_selected_rows (dcc_chat_window.list);
	dcc_abort_selected_rows (rows);
	g_list_free (rows);
}

static void
dcc_file_clear_completed_cb (GtkButton *button, gpointer userdata)
{
	GSList *list;
	GSList *completed;

	(void) button;
	(void) userdata;

	completed = NULL;
	for (list = dcc_list; list; list = list->next)
	{
		struct DCC *dcc = list->data;

		if (!dcc)
			continue;
		if (dcc->type != TYPE_SEND && dcc->type != TYPE_RECV)
			continue;
		if (is_dcc_completed (dcc))
			completed = g_slist_prepend (completed, dcc);
	}

	for (list = completed; list; list = list->next)
	{
		struct DCC *dcc = list->data;

		if (!dcc || !dcc->serv || !dcc->serv->front_session)
			continue;
		dcc_abort (dcc->serv->front_session, dcc);
	}

	g_slist_free (completed);
}

static void
dcc_open_folder_cb (GtkButton *button, gpointer userdata)
{
	char *uri;
	const char *dir;

	(void) button;
	(void) userdata;

	dir = prefs.hex_dcc_completed_dir[0] ? prefs.hex_dcc_completed_dir : prefs.hex_dcc_dir;
	if (!dir || !dir[0])
		return;

	uri = g_strdup_printf ("file://%s", dir);
	fe_open_url (uri);
	g_free (uri);
}

static void
dcc_file_row_activated_cb (GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
	struct DCC *dcc;

	(void) box;
	(void) userdata;

	if (!row)
		return;

	dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");
	if (!dcc)
		return;

	if (dcc->type == TYPE_RECV && dcc->dccstat == STAT_QUEUED)
	{
		dcc_get (dcc);
		return;
	}

	if ((dcc->dccstat == STAT_DONE || dcc->dccstat == STAT_ABORTED ||
		dcc->dccstat == STAT_FAILED) && dcc->serv && dcc->serv->front_session)
	{
		dcc_abort (dcc->serv->front_session, dcc);
	}
}

static void
dcc_chat_row_activated_cb (GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
	struct DCC *dcc;

	(void) box;
	(void) userdata;

	if (!row)
		return;

	dcc = g_object_get_data (G_OBJECT (row), "hc-dcc");
	if (!dcc)
		return;

	if (dcc->type == TYPE_CHATRECV && dcc->dccstat == STAT_QUEUED)
		dcc_get (dcc);
}

static gboolean
dcc_file_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	dcc_file_window.window = NULL;
	dcc_file_window.list = NULL;
	dcc_file_window.accept_button = NULL;
	dcc_file_window.resume_button = NULL;
	dcc_file_window.abort_button = NULL;
	dcc_file_window.clear_button = NULL;
	return FALSE;
}

static gboolean
dcc_chat_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	dcc_chat_window.window = NULL;
	dcc_chat_window.list = NULL;
	dcc_chat_window.accept_button = NULL;
	dcc_chat_window.abort_button = NULL;
	return FALSE;
}

static void
dcc_file_refresh (void)
{
	GSList *list;

	if (!dcc_file_window.list)
		return;

	listbox_clear (dcc_file_window.list);
	for (list = dcc_list; list; list = list->next)
	{
		struct DCC *dcc = list->data;

		if (!dcc)
			continue;
		if (dcc->type == TYPE_SEND || dcc->type == TYPE_RECV)
			dcc_file_row_add (dcc);
	}

	dcc_file_update_buttons ();
}

static void
dcc_chat_refresh (void)
{
	GSList *list;

	if (!dcc_chat_window.list)
		return;

	listbox_clear (dcc_chat_window.list);
	for (list = dcc_list; list; list = list->next)
	{
		struct DCC *dcc = list->data;

		if (!dcc)
			continue;
		if (dcc->type == TYPE_CHATSEND || dcc->type == TYPE_CHATRECV)
			dcc_chat_row_add (dcc);
	}

	dcc_chat_update_buttons ();
}

static void
dcc_send_filereq_file (void *userdata, char *file)
{
	HcDccSendRequest *req;

	req = userdata;
	if (!req)
		return;

	if (file && file[0])
	{
		dcc_send (req->sess, req->nick, file, req->maxcps, req->passive);
		return;
	}

	g_free (req->nick);
	g_free (req);
}

void
fe_dcc_send_filereq (struct session *sess, char *nick, int maxcps, int passive)
{
	HcDccSendRequest *req;
	char *title;

	if (!sess || !nick || !nick[0])
		return;

	req = g_new0 (HcDccSendRequest, 1);
	req->sess = sess;
	req->nick = g_strdup (nick);
	req->maxcps = maxcps;
	req->passive = passive;

	title = g_strdup_printf (_("Send file to %s"), nick);
	fe_get_file (title, prefs.hex_dcc_dir,
		dcc_send_filereq_file, req,
		FRF_MULTIPLE | FRF_FILTERISINITIAL);
	g_free (title);
}

int
fe_dcc_open_recv_win (int passive)
{
	GtkWidget *open_button;
	GtkWidget *close_button;
	GtkBuilder *builder;

	if (dcc_file_window.window)
	{
		if (!passive)
			gtk_window_present (GTK_WINDOW (dcc_file_window.window));
		return TRUE;
	}

	builder = fe_gtk4_builder_new_from_resource (DCC_FILE_UI_PATH);
	dcc_file_window.window = fe_gtk4_builder_get_widget (builder, "dcc_file_window", GTK_TYPE_WINDOW);
	dcc_file_window.list = fe_gtk4_builder_get_widget (builder, "dcc_file_list", GTK_TYPE_LIST_BOX);
	dcc_file_window.accept_button = fe_gtk4_builder_get_widget (builder, "dcc_file_accept_button", GTK_TYPE_BUTTON);
	dcc_file_window.resume_button = fe_gtk4_builder_get_widget (builder, "dcc_file_resume_button", GTK_TYPE_BUTTON);
	dcc_file_window.abort_button = fe_gtk4_builder_get_widget (builder, "dcc_file_abort_button", GTK_TYPE_BUTTON);
	dcc_file_window.clear_button = fe_gtk4_builder_get_widget (builder, "dcc_file_clear_button", GTK_TYPE_BUTTON);
	open_button = fe_gtk4_builder_get_widget (builder, "dcc_file_open_button", GTK_TYPE_BUTTON);
	close_button = fe_gtk4_builder_get_widget (builder, "dcc_file_close_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (dcc_file_window.window);
	g_object_unref (builder);

	gtk_button_set_label (GTK_BUTTON (dcc_file_window.accept_button), _("Accept"));
	gtk_button_set_label (GTK_BUTTON (dcc_file_window.resume_button), _("Resume"));
	gtk_button_set_label (GTK_BUTTON (dcc_file_window.abort_button), _("Abort"));
	gtk_button_set_label (GTK_BUTTON (dcc_file_window.clear_button), _("Clear"));
	gtk_button_set_label (GTK_BUTTON (open_button), _("Open Folder..."));
	gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));
	g_signal_connect (dcc_file_window.list, "selected-rows-changed",
		G_CALLBACK (dcc_file_selected_changed_cb), NULL);
	g_signal_connect (dcc_file_window.list, "row-activated",
		G_CALLBACK (dcc_file_row_activated_cb), NULL);
	g_signal_connect (dcc_file_window.accept_button, "clicked",
		G_CALLBACK (dcc_file_accept_cb), NULL);
	g_signal_connect (dcc_file_window.resume_button, "clicked",
		G_CALLBACK (dcc_file_resume_cb), NULL);
	g_signal_connect (dcc_file_window.abort_button, "clicked",
		G_CALLBACK (dcc_file_abort_cb), NULL);
	g_signal_connect (dcc_file_window.clear_button, "clicked",
		G_CALLBACK (dcc_file_clear_completed_cb), NULL);
	g_signal_connect (open_button, "clicked", G_CALLBACK (dcc_open_folder_cb), NULL);
	g_signal_connect_swapped (close_button, "clicked",
		G_CALLBACK (gtk_window_close), dcc_file_window.window);
	g_signal_connect (dcc_file_window.window, "close-request",
		G_CALLBACK (dcc_file_close_request_cb), NULL);

	gtk_window_set_title (GTK_WINDOW (dcc_file_window.window), _("Uploads and Downloads"));
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (dcc_file_window.window), GTK_WINDOW (main_window));

	dcc_file_refresh ();
	if (!passive)
		gtk_window_present (GTK_WINDOW (dcc_file_window.window));

	return FALSE;
}

int
fe_dcc_open_send_win (int passive)
{
	return fe_dcc_open_recv_win (passive);
}

int
fe_dcc_open_chat_win (int passive)
{
	GtkWidget *close_button;
	GtkBuilder *builder;

	if (dcc_chat_window.window)
	{
		if (!passive)
			gtk_window_present (GTK_WINDOW (dcc_chat_window.window));
		return TRUE;
	}

	builder = fe_gtk4_builder_new_from_resource (DCC_CHAT_UI_PATH);
	dcc_chat_window.window = fe_gtk4_builder_get_widget (builder, "dcc_chat_window", GTK_TYPE_WINDOW);
	dcc_chat_window.list = fe_gtk4_builder_get_widget (builder, "dcc_chat_list", GTK_TYPE_LIST_BOX);
	dcc_chat_window.accept_button = fe_gtk4_builder_get_widget (builder, "dcc_chat_accept_button", GTK_TYPE_BUTTON);
	dcc_chat_window.abort_button = fe_gtk4_builder_get_widget (builder, "dcc_chat_abort_button", GTK_TYPE_BUTTON);
	close_button = fe_gtk4_builder_get_widget (builder, "dcc_chat_close_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (dcc_chat_window.window);
	g_object_unref (builder);

	gtk_button_set_label (GTK_BUTTON (dcc_chat_window.accept_button), _("Accept"));
	gtk_button_set_label (GTK_BUTTON (dcc_chat_window.abort_button), _("Abort"));
	gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));
	g_signal_connect (dcc_chat_window.list, "selected-rows-changed",
		G_CALLBACK (dcc_chat_selected_changed_cb), NULL);
	g_signal_connect (dcc_chat_window.list, "row-activated",
		G_CALLBACK (dcc_chat_row_activated_cb), NULL);
	g_signal_connect (dcc_chat_window.accept_button, "clicked",
		G_CALLBACK (dcc_chat_accept_cb), NULL);
	g_signal_connect (dcc_chat_window.abort_button, "clicked",
		G_CALLBACK (dcc_chat_abort_cb), NULL);
	g_signal_connect_swapped (close_button, "clicked",
		G_CALLBACK (gtk_window_close), dcc_chat_window.window);
	g_signal_connect (dcc_chat_window.window, "close-request",
		G_CALLBACK (dcc_chat_close_request_cb), NULL);

	gtk_window_set_title (GTK_WINDOW (dcc_chat_window.window), _("DCC Chat List"));
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (dcc_chat_window.window), GTK_WINDOW (main_window));

	dcc_chat_refresh ();
	if (!passive)
		gtk_window_present (GTK_WINDOW (dcc_chat_window.window));

	return FALSE;
}

void
fe_dcc_add (struct DCC *dcc)
{
	(void) dcc;
	dcc_file_refresh ();
	dcc_chat_refresh ();
}

void
fe_dcc_update (struct DCC *dcc)
{
	(void) dcc;
	dcc_file_refresh ();
	dcc_chat_refresh ();
}

void
fe_dcc_remove (struct DCC *dcc)
{
	(void) dcc;
	dcc_file_refresh ();
	dcc_chat_refresh ();
}

void
fe_gtk4_dccgui_cleanup (void)
{
	if (dcc_file_window.window)
		gtk_window_destroy (GTK_WINDOW (dcc_file_window.window));
	if (dcc_chat_window.window)
		gtk_window_destroy (GTK_WINDOW (dcc_chat_window.window));

	dcc_file_window.window = NULL;
	dcc_file_window.list = NULL;
	dcc_file_window.accept_button = NULL;
	dcc_file_window.resume_button = NULL;
	dcc_file_window.abort_button = NULL;
	dcc_file_window.clear_button = NULL;

	dcc_chat_window.window = NULL;
	dcc_chat_window.list = NULL;
	dcc_chat_window.accept_button = NULL;
	dcc_chat_window.abort_button = NULL;
}

/* HexChat GTK4 notify list */
#include "fe-gtk4.h"

#include "../common/notify.h"
#include "../common/server.h"
#include "../common/outbound.h"

#include <time.h>

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
	GtkWidget *open_button;
	GtkWidget *remove_button;
} HcNotifyView;

typedef struct
{
	GtkWidget *window;
	GtkWidget *name_entry;
	GtkWidget *net_entry;
	gboolean finished;
} HcNotifyAddDialog;

static HcNotifyView notify_view;

static char *
notify_time_ago (time_t seen)
{
	time_t now;
	int mins;

	if (seen <= 0)
		return g_strdup (_("Never"));

	now = time (NULL);
	if (now < seen)
		now = seen;

	mins = (int) ((now - seen) / 60);
	if (mins < 60)
		return g_strdup_printf (_("%d minutes ago"), mins);
	if (mins < 120)
		return g_strdup (_("An hour ago"));
	return g_strdup_printf (_("%d hours ago"), mins / 60);
}

static gboolean
notify_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	notify_save ();
	notify_view.window = NULL;
	notify_view.list = NULL;
	notify_view.open_button = NULL;
	notify_view.remove_button = NULL;
	return FALSE;
}

static void
notify_clear_rows (void)
{
	GtkWidget *child;

	if (!notify_view.list)
		return;

	while ((child = gtk_widget_get_first_child (notify_view.list)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (notify_view.list), child);
}

static void
notify_row_add (const char *name,
	const char *status,
	const char *network,
	time_t seen,
	struct notify_per_server *servnot,
	gboolean online)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *title;
	GtkWidget *subtitle;
	char *subtitle_text;
	char *seen_text;

	if (!notify_view.list || !name)
		return;

	title = gtk_label_new (name);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);

	seen_text = notify_time_ago (seen);
	subtitle_text = g_strdup_printf ("%s%s%s%s%s",
		status ? status : "",
		network && network[0] ? " - " : "",
		network && network[0] ? network : "",
		seen_text && seen_text[0] ? " - " : "",
		seen_text ? seen_text : "");

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
	g_object_set_data_full (G_OBJECT (row), "hc-notify-name", g_strdup (name), g_free);
	g_object_set_data (G_OBJECT (row), "hc-notify-servnot", servnot);
	g_object_set_data (G_OBJECT (row), "hc-notify-online", GINT_TO_POINTER (online ? 1 : 0));
	gtk_list_box_append (GTK_LIST_BOX (notify_view.list), row);

	g_free (seen_text);
	g_free (subtitle_text);
}

static struct notify *
notify_find_named (const char *name)
{
	GSList *list;

	if (!name || !name[0])
		return NULL;

	for (list = notify_list; list; list = list->next)
	{
		struct notify *notify = list->data;

		if (!notify || !notify->name)
			continue;
		if (g_ascii_strcasecmp (notify->name, name) == 0)
			return notify;
	}

	return NULL;
}

static struct notify_per_server *
notify_pick_online_server (struct notify *notify)
{
	GSList *list;

	if (!notify)
		return NULL;

	for (list = notify->server_list; list; list = list->next)
	{
		struct notify_per_server *servnot = list->data;

		if (servnot && servnot->ison)
			return servnot;
	}

	return NULL;
}

void
notify_gui_update (void)
{
	GSList *list;

	if (!notify_view.window || !notify_view.list)
		return;

	notify_clear_rows ();

	for (list = notify_list; list; list = list->next)
	{
		struct notify *notify;
		GSList *slist;
		gboolean online;
		time_t lastseen;
		int online_count;

		notify = list->data;
		if (!notify || !notify->name)
			continue;

		online = FALSE;
		lastseen = 0;
		online_count = 0;

		for (slist = notify->server_list; slist; slist = slist->next)
		{
			struct notify_per_server *servnot = slist->data;

			if (!servnot)
				continue;
			if (servnot->ison)
				online = TRUE;
			if (servnot->lastseen > lastseen)
				lastseen = servnot->lastseen;
		}

		if (!online)
		{
			notify_row_add (notify->name, _("Offline"), "", lastseen, NULL, FALSE);
			continue;
		}

		for (slist = notify->server_list; slist; slist = slist->next)
		{
			const char *network;
			struct notify_per_server *servnot = slist->data;

			if (!servnot || !servnot->ison)
				continue;

			network = server_get_network (servnot->server, TRUE);
			if (!network)
				network = "";

			notify_row_add (notify->name, _("Online"), network, servnot->lastseen,
				servnot, TRUE);
			online_count++;
		}

		if (online_count == 0)
			notify_row_add (notify->name, _("Offline"), "", lastseen, NULL, FALSE);
	}
}

static void
notify_row_changed_cb (GtkListBox *box, gpointer userdata)
{
	GtkListBoxRow *row;
	struct notify_per_server *servnot;
	gboolean can_open;

	(void) box;
	(void) userdata;

	if (!notify_view.list)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (notify_view.list));
	if (!row)
	{
		if (notify_view.open_button)
			gtk_widget_set_sensitive (notify_view.open_button, FALSE);
		if (notify_view.remove_button)
			gtk_widget_set_sensitive (notify_view.remove_button, FALSE);
		return;
	}

	servnot = g_object_get_data (G_OBJECT (row), "hc-notify-servnot");
	can_open = (servnot != NULL && servnot->ison);

	if (notify_view.open_button)
		gtk_widget_set_sensitive (notify_view.open_button, can_open);
	if (notify_view.remove_button)
		gtk_widget_set_sensitive (notify_view.remove_button, TRUE);
}

static void
notify_open_selected_cb (GtkButton *button, gpointer userdata)
{
	GtkListBoxRow *row;
	struct notify_per_server *servnot;
	const char *name;

	(void) button;
	(void) userdata;

	if (!notify_view.list)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (notify_view.list));
	if (!row)
		return;

	servnot = g_object_get_data (G_OBJECT (row), "hc-notify-servnot");
	if (!servnot)
	{
		struct notify *notify;

		name = g_object_get_data (G_OBJECT (row), "hc-notify-name");
		notify = notify_find_named (name);
		servnot = notify_pick_online_server (notify);
	}

	if (!servnot || !servnot->ison || !servnot->server || !servnot->notify)
		return;

	open_query (servnot->server, servnot->notify->name, TRUE);
}

static void
notify_row_activated_cb (GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
	(void) box;
	(void) row;
	(void) userdata;

	notify_open_selected_cb (NULL, NULL);
}

static void
notify_remove_selected_cb (GtkButton *button, gpointer userdata)
{
	GtkListBoxRow *row;
	const char *name;

	(void) button;
	(void) userdata;

	if (!notify_view.list)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (notify_view.list));
	if (!row)
		return;

	name = g_object_get_data (G_OBJECT (row), "hc-notify-name");
	if (!name || !name[0])
		return;

	notify_deluser ((char *) name);
	notify_save ();
	notify_gui_update ();
	notify_row_changed_cb (GTK_LIST_BOX (notify_view.list), NULL);
}

static void
notify_add_dialog_finish (HcNotifyAddDialog *dialog, gboolean cancel)
{
	const char *name;
	const char *networks;
	char *name_copy;
	char *nets_copy;

	if (!dialog || dialog->finished)
		return;

	dialog->finished = TRUE;

	if (!cancel)
	{
		name = gtk_editable_get_text (GTK_EDITABLE (dialog->name_entry));
		networks = gtk_editable_get_text (GTK_EDITABLE (dialog->net_entry));

		if (name && name[0])
		{
			name_copy = g_strdup (name);
			nets_copy = (networks && networks[0] &&
				g_ascii_strcasecmp (networks, "ALL") != 0) ? g_strdup (networks) : NULL;
			notify_adduser (name_copy, nets_copy);
			g_free (name_copy);
			g_free (nets_copy);
			notify_save ();
			notify_gui_update ();
		}
	}

	gtk_window_destroy (GTK_WINDOW (dialog->window));
	g_free (dialog);
}

static void
notify_add_dialog_ok_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	notify_add_dialog_finish ((HcNotifyAddDialog *) userdata, FALSE);
}

static void
notify_add_dialog_cancel_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	notify_add_dialog_finish ((HcNotifyAddDialog *) userdata, TRUE);
}

static gboolean
notify_add_dialog_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	notify_add_dialog_finish ((HcNotifyAddDialog *) userdata, TRUE);
	return TRUE;
}

static void
notify_add_dialog_activate_cb (GtkEntry *entry, gpointer userdata)
{
	(void) entry;
	notify_add_dialog_finish ((HcNotifyAddDialog *) userdata, FALSE);
}

static void
notify_open_add_dialog (const char *name, const char *networks)
{
	HcNotifyAddDialog *dialog;
	GtkWidget *window;
	GtkWidget *root;
	GtkWidget *name_label;
	GtkWidget *net_label;
	GtkWidget *buttons;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;

	dialog = g_new0 (HcNotifyAddDialog, 1);

	window = gtk_window_new ();
	dialog->window = window;
	gtk_window_set_title (GTK_WINDOW (window), _("Add to Friends List"));
	gtk_window_set_default_size (GTK_WINDOW (window), 420, 180);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (main_window));
	gtk_window_set_modal (GTK_WINDOW (window), TRUE);

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (root, 12);
	gtk_widget_set_margin_end (root, 12);
	gtk_widget_set_margin_top (root, 12);
	gtk_widget_set_margin_bottom (root, 12);
	gtk_window_set_child (GTK_WINDOW (window), root);

	name_label = gtk_label_new (_("Nickname:"));
	gtk_label_set_xalign (GTK_LABEL (name_label), 0.0f);
	gtk_box_append (GTK_BOX (root), name_label);

	dialog->name_entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (dialog->name_entry), name ? name : "");
	gtk_box_append (GTK_BOX (root), dialog->name_entry);

	net_label = gtk_label_new (_("Networks (comma separated, leave empty for all):"));
	gtk_label_set_xalign (GTK_LABEL (net_label), 0.0f);
	gtk_box_append (GTK_BOX (root), net_label);

	dialog->net_entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (dialog->net_entry),
		networks ? networks : "ALL");
	gtk_box_append (GTK_BOX (root), dialog->net_entry);

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (buttons, GTK_ALIGN_END);
	gtk_box_append (GTK_BOX (root), buttons);

	cancel_button = gtk_button_new_with_label (_("Cancel"));
	ok_button = gtk_button_new_with_label (_("Add"));
	gtk_box_append (GTK_BOX (buttons), cancel_button);
	gtk_box_append (GTK_BOX (buttons), ok_button);

	g_signal_connect (ok_button, "clicked", G_CALLBACK (notify_add_dialog_ok_cb), dialog);
	g_signal_connect (cancel_button, "clicked", G_CALLBACK (notify_add_dialog_cancel_cb), dialog);
	g_signal_connect (dialog->name_entry, "activate",
		G_CALLBACK (notify_add_dialog_activate_cb), dialog);
	g_signal_connect (dialog->net_entry, "activate",
		G_CALLBACK (notify_add_dialog_activate_cb), dialog);
	g_signal_connect (window, "close-request",
		G_CALLBACK (notify_add_dialog_close_request_cb), dialog);

	gtk_window_set_default_widget (GTK_WINDOW (window), ok_button);
	gtk_window_present (GTK_WINDOW (window));
}

static void
notify_add_clicked_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;
	notify_open_add_dialog (NULL, "ALL");
}

void
notify_opengui (void)
{
	GtkWidget *root;
	GtkWidget *scroll;
	GtkWidget *buttons;
	GtkWidget *add_button;
	GtkWidget *close_button;

	if (notify_view.window)
	{
		notify_gui_update ();
		gtk_window_present (GTK_WINDOW (notify_view.window));
		return;
	}

	notify_view.window = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (notify_view.window), _("Friends List"));
	gtk_window_set_default_size (GTK_WINDOW (notify_view.window), 620, 420);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (notify_view.window), GTK_WINDOW (main_window));

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (root, 12);
	gtk_widget_set_margin_end (root, 12);
	gtk_widget_set_margin_top (root, 12);
	gtk_widget_set_margin_bottom (root, 12);
	gtk_window_set_child (GTK_WINDOW (notify_view.window), root);

	scroll = gtk_scrolled_window_new ();
	gtk_widget_set_hexpand (scroll, TRUE);
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_box_append (GTK_BOX (root), scroll);

	notify_view.list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (notify_view.list), GTK_SELECTION_SINGLE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), notify_view.list);
	g_signal_connect (notify_view.list, "selected-rows-changed",
		G_CALLBACK (notify_row_changed_cb), NULL);
	g_signal_connect (notify_view.list, "row-activated",
		G_CALLBACK (notify_row_activated_cb), NULL);

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (buttons, GTK_ALIGN_END);
	gtk_box_append (GTK_BOX (root), buttons);

	add_button = gtk_button_new_with_label (_("Add..."));
	g_signal_connect (add_button, "clicked", G_CALLBACK (notify_add_clicked_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), add_button);

	notify_view.open_button = gtk_button_new_with_label (_("Open Dialog"));
	g_signal_connect (notify_view.open_button, "clicked",
		G_CALLBACK (notify_open_selected_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), notify_view.open_button);

	notify_view.remove_button = gtk_button_new_with_label (_("Remove"));
	g_signal_connect (notify_view.remove_button, "clicked",
		G_CALLBACK (notify_remove_selected_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), notify_view.remove_button);

	close_button = gtk_button_new_with_label (_("Close"));
	g_signal_connect_swapped (close_button, "clicked",
		G_CALLBACK (gtk_window_close), notify_view.window);
	gtk_box_append (GTK_BOX (buttons), close_button);

	g_signal_connect (notify_view.window, "close-request",
		G_CALLBACK (notify_close_request_cb), NULL);

	notify_gui_update ();
	notify_row_changed_cb (GTK_LIST_BOX (notify_view.list), NULL);
	gtk_window_present (GTK_WINDOW (notify_view.window));
}

void
fe_notify_update (char *name)
{
	(void) name;
	notify_gui_update ();
}

void
fe_notify_ask (char *name, char *networks)
{
	notify_open_add_dialog (name, networks);
}

void
fe_gtk4_notifygui_cleanup (void)
{
	if (!notify_view.window)
		return;

	gtk_window_destroy (GTK_WINDOW (notify_view.window));
	notify_view.window = NULL;
	notify_view.list = NULL;
	notify_view.open_button = NULL;
	notify_view.remove_button = NULL;
}

/* HexChat GTK4 join dialog */
#include "fe-gtk4.h"

#include "../common/server.h"
#include "../common/servlist.h"

typedef struct
{
	GtkWidget *window;
	GtkWidget *radio_nothing;
	GtkWidget *radio_join;
	GtkWidget *radio_list;
	GtkWidget *entry;
	GtkWidget *check;
	server *serv;
} HcJoinDialog;

static HcJoinDialog join_dialog;

static void
joind_reset_state (void)
{
	join_dialog.window = NULL;
	join_dialog.radio_nothing = NULL;
	join_dialog.radio_join = NULL;
	join_dialog.radio_list = NULL;
	join_dialog.entry = NULL;
	join_dialog.check = NULL;
	join_dialog.serv = NULL;
}

static gboolean
joind_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;
	joind_reset_state ();
	return FALSE;
}

static void
joind_entry_changed_cb (GtkEditable *editable, gpointer userdata)
{
	const char *text;

	(void) userdata;

	if (!join_dialog.radio_join)
		return;

	text = gtk_editable_get_text (editable);
	if (text && text[0])
		gtk_check_button_set_active (GTK_CHECK_BUTTON (join_dialog.radio_join), TRUE);
}

static void
joind_ok_apply (void)
{
	server *serv;
	const char *chan;

	serv = join_dialog.serv;
	if (!serv || !is_server (serv))
		return;

	if (join_dialog.radio_join &&
		gtk_check_button_get_active (GTK_CHECK_BUTTON (join_dialog.radio_join)))
	{
		chan = gtk_editable_get_text (GTK_EDITABLE (join_dialog.entry));
		if (!chan || !chan[0])
		{
			fe_message (_("Channel name too short, try again."), FE_MSG_ERROR);
			return;
		}
		serv->p_join (serv, (char *) chan, "");
	}
	else if (join_dialog.radio_list &&
		gtk_check_button_get_active (GTK_CHECK_BUTTON (join_dialog.radio_list)))
	{
		chanlist_opengui (serv, TRUE);
	}

	prefs.hex_gui_join_dialog = (join_dialog.check &&
		gtk_check_button_get_active (GTK_CHECK_BUTTON (join_dialog.check))) ? 1 : 0;
}

static void
joind_ok_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	joind_ok_apply ();
	if (join_dialog.window)
		gtk_window_close (GTK_WINDOW (join_dialog.window));
}

static void
joind_cancel_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (join_dialog.check)
		prefs.hex_gui_join_dialog = gtk_check_button_get_active (GTK_CHECK_BUTTON (join_dialog.check)) ? 1 : 0;

	if (join_dialog.window)
		gtk_window_close (GTK_WINDOW (join_dialog.window));
}

void
joind_open (server *serv)
{
	GtkWidget *root;
	GtkWidget *title;
	GtkWidget *desc;
	GtkWidget *join_row;
	GtkWidget *buttons;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	const char *network;
	char *title_text;

	if (!prefs.hex_gui_join_dialog || !serv || !is_server (serv))
		return;

	if (join_dialog.window)
	{
		if (join_dialog.serv == serv)
		{
			gtk_window_present (GTK_WINDOW (join_dialog.window));
			return;
		}

		gtk_window_destroy (GTK_WINDOW (join_dialog.window));
		joind_reset_state ();
	}

	network = server_get_network (serv, TRUE);
	if (!network || !network[0])
		network = serv->servername[0] ? serv->servername : _("Unknown");

	join_dialog.window = gtk_window_new ();
	join_dialog.serv = serv;
	gtk_window_set_default_size (GTK_WINDOW (join_dialog.window), 560, 280);
	gtk_window_set_resizable (GTK_WINDOW (join_dialog.window), FALSE);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (join_dialog.window), GTK_WINDOW (main_window));
	gtk_window_set_modal (GTK_WINDOW (join_dialog.window), TRUE);

	title_text = g_strdup_printf (_("Connection Complete - %s"), network);
	gtk_window_set_title (GTK_WINDOW (join_dialog.window), title_text);
	g_free (title_text);

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start (root, 14);
	gtk_widget_set_margin_end (root, 14);
	gtk_widget_set_margin_top (root, 14);
	gtk_widget_set_margin_bottom (root, 14);
	gtk_window_set_child (GTK_WINDOW (join_dialog.window), root);

	title = gtk_label_new (NULL);
	title_text = g_strdup_printf ("<b>%s</b>", _("Connection successful."));
	gtk_label_set_markup (GTK_LABEL (title), title_text);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);
	gtk_box_append (GTK_BOX (root), title);
	g_free (title_text);

	desc = gtk_label_new (_("No auto-join channel is configured for this network. What would you like to do next?"));
	gtk_label_set_xalign (GTK_LABEL (desc), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (desc), TRUE);
	gtk_box_append (GTK_BOX (root), desc);

	join_dialog.radio_nothing = gtk_check_button_new_with_label (_("Nothing, I'll join later."));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (join_dialog.radio_nothing), TRUE);
	gtk_box_append (GTK_BOX (root), join_dialog.radio_nothing);

	join_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append (GTK_BOX (root), join_row);

	join_dialog.radio_join = gtk_check_button_new_with_label (_("Join this channel:"));
	gtk_check_button_set_group (GTK_CHECK_BUTTON (join_dialog.radio_join),
		GTK_CHECK_BUTTON (join_dialog.radio_nothing));
	gtk_box_append (GTK_BOX (join_row), join_dialog.radio_join);

	join_dialog.entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (join_dialog.entry), "#");
	gtk_widget_set_hexpand (join_dialog.entry, TRUE);
	gtk_box_append (GTK_BOX (join_row), join_dialog.entry);
	g_signal_connect (join_dialog.entry, "changed", G_CALLBACK (joind_entry_changed_cb), NULL);

	join_dialog.radio_list = gtk_check_button_new_with_label (_("Open the channel list."));
	gtk_check_button_set_group (GTK_CHECK_BUTTON (join_dialog.radio_list),
		GTK_CHECK_BUTTON (join_dialog.radio_nothing));
	gtk_box_append (GTK_BOX (root), join_dialog.radio_list);

	join_dialog.check = gtk_check_button_new_with_label (_("Always show this dialog after connecting."));
	gtk_check_button_set_active (GTK_CHECK_BUTTON (join_dialog.check), prefs.hex_gui_join_dialog != 0);
	gtk_box_append (GTK_BOX (root), join_dialog.check);

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (buttons, GTK_ALIGN_END);
	gtk_box_append (GTK_BOX (root), buttons);

	cancel_button = gtk_button_new_with_label (_("Cancel"));
	ok_button = gtk_button_new_with_label (_("OK"));
	gtk_box_append (GTK_BOX (buttons), cancel_button);
	gtk_box_append (GTK_BOX (buttons), ok_button);

	g_signal_connect (ok_button, "clicked", G_CALLBACK (joind_ok_cb), NULL);
	g_signal_connect (cancel_button, "clicked", G_CALLBACK (joind_cancel_cb), NULL);
	g_signal_connect (join_dialog.window, "close-request",
		G_CALLBACK (joind_close_request_cb), NULL);

	if (serv->network)
	{
		ircnet *net = serv->network;

		if (net->name && g_ascii_strcasecmp (net->name, "Libera.Chat") == 0)
			gtk_editable_set_text (GTK_EDITABLE (join_dialog.entry), "#hexchat");
	}

	gtk_window_set_default_widget (GTK_WINDOW (join_dialog.window), ok_button);
	gtk_window_present (GTK_WINDOW (join_dialog.window));
}

void
joind_close (server *serv)
{
	if (!join_dialog.window)
		return;

	if (serv && join_dialog.serv && join_dialog.serv != serv)
		return;

	gtk_window_destroy (GTK_WINDOW (join_dialog.window));
	joind_reset_state ();
}

void
fe_gtk4_joind_cleanup (void)
{
	joind_close (NULL);
}

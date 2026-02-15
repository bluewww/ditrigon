/* HexChat GTK4 join dialog */
#include "fe-gtk4.h"

#include "../common/server.h"
#include "../common/servlist.h"

#define JOIND_UI_PATH "/org/hexchat/ui/gtk4/dialogs/joind-window.ui"

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
	GtkWidget *title;
	GtkWidget *desc;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkBuilder *builder;
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

	builder = fe_gtk4_builder_new_from_resource (JOIND_UI_PATH);
	join_dialog.window = fe_gtk4_builder_get_widget (builder, "joind_window", GTK_TYPE_WINDOW);
	title = fe_gtk4_builder_get_widget (builder, "joind_title", GTK_TYPE_LABEL);
	desc = fe_gtk4_builder_get_widget (builder, "joind_desc", GTK_TYPE_LABEL);
	join_dialog.radio_nothing = fe_gtk4_builder_get_widget (builder, "joind_radio_nothing", GTK_TYPE_CHECK_BUTTON);
	join_dialog.radio_join = fe_gtk4_builder_get_widget (builder, "joind_radio_join", GTK_TYPE_CHECK_BUTTON);
	join_dialog.entry = fe_gtk4_builder_get_widget (builder, "joind_entry", GTK_TYPE_ENTRY);
	join_dialog.radio_list = fe_gtk4_builder_get_widget (builder, "joind_radio_list", GTK_TYPE_CHECK_BUTTON);
	join_dialog.check = fe_gtk4_builder_get_widget (builder, "joind_check", GTK_TYPE_CHECK_BUTTON);
	cancel_button = fe_gtk4_builder_get_widget (builder, "joind_cancel_button", GTK_TYPE_BUTTON);
	ok_button = fe_gtk4_builder_get_widget (builder, "joind_ok_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (join_dialog.window);
	g_object_unref (builder);

	title_text = g_strdup_printf ("<b>%s</b>", _("Connection successful."));
	gtk_label_set_markup (GTK_LABEL (title), title_text);
	g_free (title_text);
	gtk_label_set_text (GTK_LABEL (desc),
		_("No auto-join channel is configured for this network. What would you like to do next?"));
	gtk_check_button_set_label (GTK_CHECK_BUTTON (join_dialog.radio_nothing), _("Nothing, I'll join later."));
	gtk_check_button_set_label (GTK_CHECK_BUTTON (join_dialog.radio_join), _("Join this channel:"));
	gtk_check_button_set_label (GTK_CHECK_BUTTON (join_dialog.radio_list), _("Open the channel list."));
	gtk_check_button_set_label (GTK_CHECK_BUTTON (join_dialog.check),
		_("Always show this dialog after connecting."));
	gtk_button_set_label (GTK_BUTTON (cancel_button), _("Cancel"));
	gtk_button_set_label (GTK_BUTTON (ok_button), _("OK"));

	join_dialog.serv = serv;
	gtk_window_set_default_size (GTK_WINDOW (join_dialog.window), 560, 280);
	gtk_window_set_resizable (GTK_WINDOW (join_dialog.window), FALSE);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (join_dialog.window), GTK_WINDOW (main_window));
	gtk_window_set_modal (GTK_WINDOW (join_dialog.window), TRUE);

	title_text = g_strdup_printf (_("Connection Complete - %s"), network);
	gtk_window_set_title (GTK_WINDOW (join_dialog.window), title_text);
	g_free (title_text);

	gtk_check_button_set_active (GTK_CHECK_BUTTON (join_dialog.radio_nothing), TRUE);
	gtk_check_button_set_group (GTK_CHECK_BUTTON (join_dialog.radio_join),
		GTK_CHECK_BUTTON (join_dialog.radio_nothing));
	gtk_check_button_set_group (GTK_CHECK_BUTTON (join_dialog.radio_list),
		GTK_CHECK_BUTTON (join_dialog.radio_nothing));
	gtk_editable_set_text (GTK_EDITABLE (join_dialog.entry), "#");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (join_dialog.check), prefs.hex_gui_join_dialog != 0);
	g_signal_connect (join_dialog.entry, "changed", G_CALLBACK (joind_entry_changed_cb), NULL);

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

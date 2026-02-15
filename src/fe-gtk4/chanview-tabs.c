/* HexChat GTK4 tab-style channel view */
#include "fe-gtk4.h"
#include "../common/chanopt.h"
#include "../common/text.h"

static GHashTable *tab_rows;
static gboolean tab_select_syncing;
static gboolean tab_css_loaded;
static GtkWidget *tab_scroller;
static GtkWidget *tab_list;

typedef struct
{
	GtkWidget *popover;
	session *sess;
	guint8 *setting;
	gboolean logging_setting;
} HcSessionToggleData;

typedef struct
{
	GtkWidget *popover;
	server *serv;
} HcServerToggleData;

static session *
tabs_popover_get_session (GtkWidget *popover)
{
	session *sess;

	sess = g_object_get_data (G_OBJECT (popover), "hc-session");
	if (!sess || !is_session (sess))
		return NULL;

	return sess;
}

static void
tabs_popover_close_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tabs_popover_get_session (popover);
	if (sess)
		fe_close_window (sess);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tabs_popover_detach_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tabs_popover_get_session (popover);
	if (sess)
		handle_command (sess, "GUI DETACH", FALSE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tabs_popover_disconnect_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tabs_popover_get_session (popover);
	if (sess)
		handle_command (sess, "DISCON", FALSE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tabs_popover_reconnect_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tabs_popover_get_session (popover);
	if (sess)
		handle_command (sess, "RECONNECT", FALSE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tabs_popover_chanlist_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tabs_popover_get_session (popover);
	if (sess && sess->server)
		chanlist_opengui (sess->server, TRUE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tabs_popover_session_toggle_cb (GtkCheckButton *check, gpointer userdata)
{
	HcSessionToggleData *data;
	session *sess;
	guint8 old_value;

	data = (HcSessionToggleData *) userdata;
	if (!data || !data->setting)
		return;

	sess = data->sess;
	if (!sess || !is_session (sess))
		return;

	old_value = *data->setting;
	*data->setting = gtk_check_button_get_active (check) ? SET_ON : SET_OFF;

	if (data->logging_setting && old_value != *data->setting)
		log_open_or_close (sess);

	chanopt_save (sess);
	chanopt_save_all (FALSE);
	gtk_popover_popdown (GTK_POPOVER (data->popover));
}

static void
tabs_popover_autoconn_cb (GtkCheckButton *check, gpointer userdata)
{
	HcServerToggleData *data;
	ircnet *net;

	data = (HcServerToggleData *) userdata;
	if (!data || !data->serv || !data->serv->network)
		return;

	net = (ircnet *) data->serv->network;
	if (gtk_check_button_get_active (check))
		net->flags |= FLAG_AUTO_CONNECT;
	else
		net->flags &= ~FLAG_AUTO_CONNECT;

	servlist_save ();
	gtk_popover_popdown (GTK_POPOVER (data->popover));
}

static GtkWidget *
tabs_popover_add_action (GtkWidget *box, const char *label, GCallback cb, GtkWidget *popover)
{
	GtkWidget *button;
	GtkWidget *child;

	button = gtk_button_new_with_mnemonic (label);
	gtk_widget_add_css_class (button, "flat");
	gtk_widget_set_halign (button, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (button, TRUE);
	child = gtk_button_get_child (GTK_BUTTON (button));
	if (child && GTK_IS_LABEL (child))
	{
		gtk_label_set_xalign (GTK_LABEL (child), 0.0f);
		gtk_widget_set_halign (child, GTK_ALIGN_START);
	}
	g_signal_connect (button, "clicked", cb, popover);
	gtk_box_append (GTK_BOX (box), button);
	return button;
}

static GtkWidget *
tabs_popover_add_toggle (GtkWidget *box, const char *label, gboolean active, GCallback cb,
	gpointer data, GClosureNotify destroy_data)
{
	GtkWidget *check;

	check = gtk_check_button_new_with_mnemonic (label);
	gtk_widget_set_halign (check, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (check, TRUE);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (check), active);
	g_signal_connect_data (check, "toggled", cb, data, destroy_data, 0);
	gtk_box_append (GTK_BOX (box), check);
	return check;
}

static void
tabs_popover_add_section_title (GtkWidget *box, const char *title)
{
	GtkWidget *label;

	label = gtk_label_new (title);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_widget_add_css_class (label, "heading");
	gtk_box_append (GTK_BOX (box), label);
}

static gboolean
tabs_toggle_initial (guint8 setting, int global_value)
{
	if (setting == SET_DEFAULT)
		return global_value != 0;
	return setting == SET_ON;
}

static void
tabs_show_context_menu (GtkWidget *parent, double x, double y, session *sess)
{
	GtkWidget *popover;
	GtkWidget *box;
	HcSessionToggleData *toggle_data;
	int alert_balloon;
	int alert_beep;
	int alert_tray;
	int alert_taskbar;
	GdkRectangle rect;

	if (!parent || !sess || !is_session (sess))
		return;

	popover = gtk_popover_new ();
	gtk_widget_set_parent (popover, parent);
	g_signal_connect_swapped (popover, "closed",
		G_CALLBACK (gtk_widget_unparent), popover);
	g_object_set_data (G_OBJECT (popover), "hc-session", sess);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start (box, 6);
	gtk_widget_set_margin_end (box, 6);
	gtk_widget_set_margin_top (box, 6);
	gtk_widget_set_margin_bottom (box, 6);
	gtk_widget_add_css_class (box, "menu");
	gtk_popover_set_child (GTK_POPOVER (popover), box);

	tabs_popover_add_action (box, _("_Close"), G_CALLBACK (tabs_popover_close_cb), popover);
	tabs_popover_add_action (box, _("_Detach"), G_CALLBACK (tabs_popover_detach_cb), popover);

	if (sess->server)
	{
		tabs_popover_add_action (box,
			sess->server->connected ? _("_Disconnect") : _("_Reconnect"),
			sess->server->connected ? G_CALLBACK (tabs_popover_disconnect_cb) : G_CALLBACK (tabs_popover_reconnect_cb),
			popover);
		tabs_popover_add_action (box, _("Channel _List"), G_CALLBACK (tabs_popover_chanlist_cb), popover);

		if (sess->type == SESS_SERVER && sess->server->network)
		{
			HcServerToggleData *auto_data;
			gboolean active;

			auto_data = g_new0 (HcServerToggleData, 1);
			auto_data->popover = popover;
			auto_data->serv = sess->server;
			active = (((ircnet *) sess->server->network)->flags & FLAG_AUTO_CONNECT) != 0;
			tabs_popover_add_toggle (box, _("_Auto-Connect"), active,
				G_CALLBACK (tabs_popover_autoconn_cb), auto_data, (GClosureNotify) g_free);
		}
	}

	gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
	tabs_popover_add_section_title (box, _("Extra Alerts"));

	if (sess->type == SESS_DIALOG)
	{
		alert_balloon = prefs.hex_input_balloon_priv;
		alert_beep = prefs.hex_input_beep_priv;
		alert_tray = prefs.hex_input_tray_priv;
		alert_taskbar = prefs.hex_input_flash_priv;
	}
	else
	{
		alert_balloon = prefs.hex_input_balloon_chans;
		alert_beep = prefs.hex_input_beep_chans;
		alert_tray = prefs.hex_input_tray_chans;
		alert_taskbar = prefs.hex_input_flash_chans;
	}

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->alert_balloon;
	tabs_popover_add_toggle (box, _("Show Notifications"),
		tabs_toggle_initial (sess->alert_balloon, alert_balloon),
		G_CALLBACK (tabs_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->alert_beep;
	tabs_popover_add_toggle (box, _("Beep on _Message"),
		tabs_toggle_initial (sess->alert_beep, alert_beep),
		G_CALLBACK (tabs_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->alert_tray;
	tabs_popover_add_toggle (box, _("Blink Tray _Icon"),
		tabs_toggle_initial (sess->alert_tray, alert_tray),
		G_CALLBACK (tabs_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->alert_taskbar;
	tabs_popover_add_toggle (box, _("Blink Task _Bar"),
		tabs_toggle_initial (sess->alert_taskbar, alert_taskbar),
		G_CALLBACK (tabs_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
	tabs_popover_add_section_title (box, _("Settings"));

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->text_logging;
	toggle_data->logging_setting = TRUE;
	tabs_popover_add_toggle (box, _("_Log to Disk"),
		tabs_toggle_initial (sess->text_logging, prefs.hex_irc_logging),
		G_CALLBACK (tabs_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->text_scrollback;
	tabs_popover_add_toggle (box, _("_Reload Scrollback"),
		tabs_toggle_initial (sess->text_scrollback, prefs.hex_text_replay),
		G_CALLBACK (tabs_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	rect.x = (int) x;
	rect.y = (int) y;
	rect.width = 1;
	rect.height = 1;
	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
	gtk_popover_popup (GTK_POPOVER (popover));
}

static void
tabs_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer userdata)
{
	GtkListBoxRow *row;
	session *sess;

	(void) userdata;

	if (n_press != 1 || !tab_list)
		return;

	row = gtk_list_box_get_row_at_y (GTK_LIST_BOX (tab_list), (int) y);
	if (!row)
		return;

	tab_select_syncing = TRUE;
	gtk_list_box_select_row (GTK_LIST_BOX (tab_list), row);
	tab_select_syncing = FALSE;

	sess = g_object_get_data (G_OBJECT (row), "hc-session");
	if (!sess || !is_session (sess))
		return;

	tabs_show_context_menu (tab_list, x, y, sess);
	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
tabs_load_css (void)
{
	GtkCssProvider *provider;
	GdkDisplay *display;

	if (tab_css_loaded)
		return;

	display = gdk_display_get_default ();
	if (!display)
		return;

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_string (provider,
		".hc-tab-data { color: @theme_fg_color; font-weight: 600; }"
		".hc-tab-msg { color: #b54708; font-weight: 700; }"
		".hc-tab-hilight { color: #c01c28; font-weight: 700; }");
	gtk_style_context_add_provider_for_display (display,
		GTK_STYLE_PROVIDER (provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
	tab_css_loaded = TRUE;
}

static char *
tabs_base_label (session *sess)
{
	const char *network;

	if (!sess)
		return g_strdup (_("<none>"));

	if (sess->type == SESS_SERVER)
	{
		network = server_get_network (sess->server, TRUE);
		if (!network || !network[0])
			network = sess->server && sess->server->servername[0] ? sess->server->servername : _("Server");
		return g_strdup_printf ("[%s]", network);
	}

	if (!sess->channel[0] && sess->server &&
		sess->type != SESS_DIALOG &&
		sess->type != SESS_NOTICES &&
		sess->type != SESS_SNOTICES)
	{
		network = server_get_network (sess->server, TRUE);
		if (!network || !network[0])
			network = sess->server->servername[0] ? sess->server->servername : _("Server");
		return g_strdup_printf ("[%s]", network);
	}

	if (sess->channel[0])
		return g_strdup (sess->channel);

	if (sess->type == SESS_DIALOG)
		return g_strdup (_("Dialog"));
	if (sess->type == SESS_NOTICES || sess->type == SESS_SNOTICES)
		return g_strdup (_("Notices"));
	return g_strdup (_("Session"));
}

static const char *
tabs_state_prefix (session *sess)
{
	if (!sess || sess == current_tab)
		return "";
	if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
		return "[!] ";
	if (sess->tab_state & TAB_STATE_NEW_MSG)
		return "[*] ";
	if (sess->tab_state & TAB_STATE_NEW_DATA)
		return "[+] ";
	return "";
}

static void
tabs_apply_classes (GtkWidget *row, GtkWidget *label, session *sess)
{
	gtk_widget_remove_css_class (row, "hc-tab-data");
	gtk_widget_remove_css_class (row, "hc-tab-msg");
	gtk_widget_remove_css_class (row, "hc-tab-hilight");
	gtk_widget_remove_css_class (label, "hc-tab-data");
	gtk_widget_remove_css_class (label, "hc-tab-msg");
	gtk_widget_remove_css_class (label, "hc-tab-hilight");

	if (!sess || sess == current_tab)
		return;

	if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
	{
		gtk_widget_add_css_class (row, "hc-tab-hilight");
		gtk_widget_add_css_class (label, "hc-tab-hilight");
		return;
	}
	if (sess->tab_state & TAB_STATE_NEW_MSG)
	{
		gtk_widget_add_css_class (row, "hc-tab-msg");
		gtk_widget_add_css_class (label, "hc-tab-msg");
		return;
	}
	if (sess->tab_state & TAB_STATE_NEW_DATA)
	{
		gtk_widget_add_css_class (row, "hc-tab-data");
		gtk_widget_add_css_class (label, "hc-tab-data");
	}
}

static void
tabs_row_selected_cb (GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
	session *sess;

	(void) box;
	(void) userdata;

	if (tab_select_syncing || !row)
		return;

	sess = g_object_get_data (G_OBJECT (row), "hc-session");
	if (!sess || !is_session (sess))
		return;

	fe_set_channel (sess);
}

void
fe_gtk4_chanview_tabs_init (void)
{
	if (!tab_rows)
		tab_rows = g_hash_table_new (g_direct_hash, g_direct_equal);

	tab_select_syncing = FALSE;
}

void
fe_gtk4_chanview_tabs_cleanup (void)
{
	if (tab_rows)
	{
		g_hash_table_unref (tab_rows);
		tab_rows = NULL;
	}

	tab_select_syncing = FALSE;
	tab_css_loaded = FALSE;
	tab_scroller = NULL;
	tab_list = NULL;
}

GtkWidget *
fe_gtk4_chanview_tabs_create_widget (void)
{
	if (!tab_scroller)
	{
		tab_scroller = gtk_scrolled_window_new ();
		gtk_widget_set_size_request (tab_scroller, 220, -1);
		gtk_widget_set_hexpand (tab_scroller, FALSE);
		gtk_widget_set_vexpand (tab_scroller, TRUE);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (tab_scroller),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		tab_list = gtk_list_box_new ();
		gtk_list_box_set_selection_mode (GTK_LIST_BOX (tab_list), GTK_SELECTION_SINGLE);
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (tab_scroller), tab_list);

		tabs_load_css ();
		g_signal_connect (tab_list, "row-selected",
			G_CALLBACK (tabs_row_selected_cb), NULL);
		{
			GtkGesture *gesture = gtk_gesture_click_new ();
			gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
			g_signal_connect (gesture, "pressed", G_CALLBACK (tabs_right_click_cb), NULL);
			gtk_widget_add_controller (tab_list, GTK_EVENT_CONTROLLER (gesture));
		}
	}

	session_scroller = tab_scroller;
	session_list = tab_list;

	return tab_scroller;
}

void
fe_gtk4_chanview_tabs_update_label (session *sess)
{
	GtkWidget *row;
	GtkWidget *label;
	char *base_text;
	char *full_text;
	const char *prefix;

	if (!tab_rows || !sess)
		return;

	row = g_hash_table_lookup (tab_rows, sess);
	if (!row)
		return;

	label = g_object_get_data (G_OBJECT (row), "hc-label");
	if (!label)
		return;

	base_text = tabs_base_label (sess);
	prefix = tabs_state_prefix (sess);
	full_text = g_strconcat (prefix, base_text, NULL);
	gtk_label_set_text (GTK_LABEL (label), full_text);
	tabs_apply_classes (row, label, sess);
	g_free (full_text);
	g_free (base_text);
}

void
fe_gtk4_chanview_tabs_add (session *sess)
{
	GtkWidget *row;
	GtkWidget *label;
	char *text;

	if (!tab_list || !tab_rows || !sess)
		return;

	row = g_hash_table_lookup (tab_rows, sess);
	if (row)
	{
		fe_gtk4_chanview_tabs_update_label (sess);
		return;
	}

	text = tabs_base_label (sess);
	label = gtk_label_new (text);
	g_free (text);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_widget_set_margin_top (label, 6);
	gtk_widget_set_margin_bottom (label, 6);
	gtk_widget_set_margin_start (label, 10);
	gtk_widget_set_margin_end (label, 10);

	row = gtk_list_box_row_new ();
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
	g_object_set_data (G_OBJECT (row), "hc-session", sess);
	g_object_set_data (G_OBJECT (row), "hc-label", label);

	gtk_list_box_append (GTK_LIST_BOX (tab_list), row);
	g_hash_table_insert (tab_rows, sess, row);
	fe_gtk4_chanview_tabs_update_label (sess);
}

void
fe_gtk4_chanview_tabs_remove (session *sess)
{
	GtkWidget *row;

	if (!tab_list || !tab_rows || !sess)
		return;

	row = g_hash_table_lookup (tab_rows, sess);
	if (!row)
		return;

	tab_select_syncing = TRUE;
	gtk_list_box_remove (GTK_LIST_BOX (tab_list), row);
	tab_select_syncing = FALSE;
	g_hash_table_remove (tab_rows, sess);
}

void
fe_gtk4_chanview_tabs_select (session *sess)
{
	GtkWidget *row;

	if (!tab_list || !tab_rows || !sess)
		return;

	row = g_hash_table_lookup (tab_rows, sess);
	if (!row)
		return;

	tab_select_syncing = TRUE;
	gtk_list_box_select_row (GTK_LIST_BOX (tab_list), GTK_LIST_BOX_ROW (row));
	tab_select_syncing = FALSE;
}

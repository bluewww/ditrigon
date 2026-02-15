/* HexChat GTK4 server list window */
#include "fe-gtk4.h"

#include <string.h>
#include <adwaita.h>

#ifdef USE_OPENSSL
#define DEFAULT_SERVER "newserver/6697"
#else
#define DEFAULT_SERVER "newserver/6667"
#endif

#define SERVLIST_X_PADDING 6
#define SERVLIST_Y_PADDING 2
#define SERVLIST_UI_PATH "/org/hexchat/ui/gtk4/dialogs/servlist-window.ui"
#define SERVLIST_EDIT_UI_PATH "/org/hexchat/ui/gtk4/dialogs/servlist-edit-window.ui"

enum
{
	SERVER_TREE,
	CHANNEL_TREE,
	CMD_TREE,
	N_TREES,
};

static GtkWidget *serverlist_win;
static GtkWidget *networks_list;
static GtkWidget *networks_search_entry;
static char *networks_filter_casefold;

static GtkWidget *entry_nick1;
static GtkWidget *entry_nick2;
static GtkWidget *entry_nick3;
static GtkWidget *entry_guser;

static GtkWidget *checkbutton_skip;
static GtkWidget *checkbutton_fav;
static GtkWidget *button_connect;

static GtkWidget *edit_win;
static GtkWidget *edit_lists[N_TREES];
static GtkWidget *edit_notebook;
static GtkWidget *edit_entry_netname;
static GtkWidget *edit_entry_nick;
static GtkWidget *edit_entry_nick2;
static GtkWidget *edit_entry_user;
static GtkWidget *edit_entry_real;
static GtkWidget *edit_entry_pass;
static GtkWidget *edit_entry_charset;
static GtkWidget *edit_label_nick;
static GtkWidget *edit_label_nick2;
static GtkWidget *edit_label_user;
static GtkWidget *edit_label_real;

static ircnet *selected_net;
static ircserver *selected_serv;
static commandentry *selected_cmd;
static favchannel *selected_chan;
static session *servlist_sess;

static int netlist_win_width;
static int netlist_win_height;
static int netedit_win_width;
static int netedit_win_height;
static int netedit_active_tab;

/* Keep these in sync with login_types[] order. */
static int login_types_conf[] =
{
	LOGIN_DEFAULT,
	LOGIN_SASL,
#ifdef USE_OPENSSL
	LOGIN_SASLEXTERNAL,
	LOGIN_SASL_SCRAM_SHA_1,
	LOGIN_SASL_SCRAM_SHA_256,
	LOGIN_SASL_SCRAM_SHA_512,
#endif
	LOGIN_PASS,
	LOGIN_MSG_NICKSERV,
	LOGIN_NICKSERV,
#ifdef USE_OPENSSL
	LOGIN_CHALLENGEAUTH,
#endif
	LOGIN_CUSTOM,
};

static const char *login_types[] =
{
	"Default",
	"SASL PLAIN (username + password)",
#ifdef USE_OPENSSL
	"SASL EXTERNAL (cert)",
	"SASL SCRAM-SHA-1",
	"SASL SCRAM-SHA-256",
	"SASL SCRAM-SHA-512",
#endif
	"Server password (/PASS password)",
	"NickServ (/MSG NickServ + password)",
	"NickServ (/NICKSERV + password)",
#ifdef USE_OPENSSL
	"Challenge Auth (username + password)",
#endif
	"Custom... (connect commands)",
	NULL
};

static const char *pages[] =
{
	IRC_DEFAULT_CHARSET,
	"CP1252 (Windows-1252)",
	"ISO-8859-15 (Western Europe)",
	"ISO-8859-2 (Central Europe)",
	"ISO-8859-7 (Greek)",
	"ISO-8859-8 (Hebrew)",
	"ISO-8859-9 (Turkish)",
	"ISO-2022-JP (Japanese)",
	"SJIS (Japanese)",
	"CP949 (Korean)",
	"KOI8-R (Cyrillic)",
	"CP1251 (Cyrillic)",
	"CP1256 (Arabic)",
	"CP1257 (Baltic)",
	"GB18030 (Chinese)",
	"TIS-620 (Thai)",
	NULL
};

static const char *const edit_page_names[N_TREES] =
{
	"servers",
	"channels",
	"commands"
};

static int
servlist_edit_get_current_page (void)
{
	const char *name;
	int i;

	if (!edit_notebook)
		return SERVER_TREE;

	name = adw_view_stack_get_visible_child_name (ADW_VIEW_STACK (edit_notebook));
	for (i = 0; i < N_TREES; i++)
	{
		if (g_strcmp0 (name, edit_page_names[i]) == 0)
			return i;
	}

	return SERVER_TREE;
}

static void
servlist_edit_set_current_page (int page)
{
	page = CLAMP (page, 0, N_TREES - 1);

	if (!edit_notebook)
		return;

	adw_view_stack_set_visible_child_name (ADW_VIEW_STACK (edit_notebook), edit_page_names[page]);
}

static void servlist_networks_populate (GtkWidget *list, GSList *netlist);
static GtkWidget *servlist_open_edit (GtkWidget *parent, ircnet *net);
static GSList *servlist_move_item (GSList *list, gpointer item, int delta);

static void
servlist_list_clear (GtkWidget *list)
{
	GtkWidget *child;

	if (!list)
		return;

	while ((child = gtk_widget_get_first_child (list)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (list), child);
}

/* convert "host:port" format to "host/port" */
static char *
servlist_sanitize_hostname (const char *host)
{
	char *ret;
	char *c;
	char *e;

	ret = g_strdup (host ? host : "");
	c = strchr (ret, ':');
	e = strrchr (ret, ':');

	/* if only one colon exists it's probably not IPv6 */
	if (c && c == e)
		*c = '/';

	return g_strstrip (ret);
}

/* remove leading slash */
static char *
servlist_sanitize_command (const char *cmd)
{
	if (cmd && cmd[0] == '/')
		return g_strdup (cmd + 1);

	return g_strdup (cmd ? cmd : "");
}

static void
servlist_update_from_entry (char **str, GtkWidget *entry)
{
	const char *text;

	if (!str || !entry)
		return;

	text = gtk_editable_get_text (GTK_EDITABLE (entry));
	g_free (*str);

	if (!text || text[0] == 0)
		*str = NULL;
	else
		*str = g_strdup (text);
}

static int
servlist_get_login_desc_index (int conf_value)
{
	int i;
	int length;

	length = (int) (sizeof (login_types_conf) / sizeof (login_types_conf[0]));
	for (i = 0; i < length; i++)
	{
		if (login_types_conf[i] == conf_value)
			return i;
	}

	return 0;
}

static GtkListBoxRow *
servlist_first_visible_network_row (GtkWidget *list)
{
	GtkWidget *child;

	for (child = gtk_widget_get_first_child (list);
		child;
		child = gtk_widget_get_next_sibling (child))
	{
		if (gtk_widget_get_visible (child))
			return GTK_LIST_BOX_ROW (child);
	}

	return NULL;
}

static gboolean
servlist_network_filter_cb (GtkListBoxRow *row, gpointer user_data)
{
	ircnet *net;
	char *name_casefolded;
	gboolean match;

	(void) user_data;

	if (!networks_filter_casefold || !networks_filter_casefold[0])
		return TRUE;

	net = g_object_get_data (G_OBJECT (row), "hc-network");
	if (!net || !net->name || !net->name[0])
		return FALSE;

	name_casefolded = g_utf8_casefold (net->name, -1);
	match = strstr (name_casefolded, networks_filter_casefold) != NULL;
	g_free (name_casefolded);

	return match;
}

static void
servlist_network_search_changed_cb (GtkEditable *editable, gpointer userdata)
{
	const char *text;
	GtkListBoxRow *row;

	(void) userdata;

	text = gtk_editable_get_text (editable);
	g_free (networks_filter_casefold);
	networks_filter_casefold = NULL;
	if (text && text[0])
		networks_filter_casefold = g_utf8_casefold (text, -1);

	if (!networks_list)
		return;

	gtk_list_box_invalidate_filter (GTK_LIST_BOX (networks_list));

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (networks_list));
	if (!row || !gtk_widget_get_visible (GTK_WIDGET (row)))
	{
		row = servlist_first_visible_network_row (networks_list);
		gtk_list_box_select_row (GTK_LIST_BOX (networks_list), row);
	}
}

static void
servlist_network_row_cb (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
	int pos;

	(void) box;
	(void) user_data;

	selected_net = NULL;
	if (!row)
		return;

	selected_net = g_object_get_data (G_OBJECT (row), "hc-network");
	if (!selected_net)
		return;

	pos = g_slist_index (network_list, selected_net);
	if (pos >= 0)
		prefs.hex_gui_slist_select = pos;
}

static void
servlist_networks_populate_ (GtkWidget *list, GSList *netlist, gboolean favorites, ircnet *prefer)
{
	ircnet *net;
	GtkListBoxRow *selected_row;
	GtkWidget *row_widget;
	int i;

	if (!netlist)
	{
		net = servlist_net_add (_("New Network"), "", FALSE);
		net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
		servlist_server_add (net, DEFAULT_SERVER);
		netlist = network_list;
	}

	servlist_list_clear (list);
	selected_row = NULL;
	i = 0;

	for (; netlist; netlist = netlist->next)
	{
		GtkWidget *label;
		GtkWidget *row;
		char *text;

		net = netlist->data;
		if (!net)
		{
			i++;
			continue;
		}
		if (favorites && !(net->flags & FLAG_FAVORITE))
		{
			i++;
			continue;
		}

		if (net->flags & FLAG_FAVORITE)
			text = g_strdup_printf ("★ %s", net->name ? net->name : _("New Network"));
		else
			text = g_strdup (net->name ? net->name : _("New Network"));

		label = gtk_label_new (text);
		g_free (text);
		gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
		gtk_widget_set_margin_start (label, 8);
		gtk_widget_set_margin_end (label, 8);
		gtk_widget_set_margin_top (label, 4);
		gtk_widget_set_margin_bottom (label, 4);

		row = gtk_list_box_row_new ();
		gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
		g_object_set_data (G_OBJECT (row), "hc-network", net);
		gtk_list_box_append (GTK_LIST_BOX (list), row);

		if (prefer && net == prefer)
			selected_row = GTK_LIST_BOX_ROW (row);
		else if (!prefer && i == prefs.hex_gui_slist_select)
			selected_row = GTK_LIST_BOX_ROW (row);

		i++;
	}

	if (!selected_row)
	{
		row_widget = gtk_widget_get_first_child (list);
		if (row_widget)
			selected_row = GTK_LIST_BOX_ROW (row_widget);
	}

	gtk_list_box_invalidate_filter (GTK_LIST_BOX (list));
	if (selected_row && !gtk_widget_get_visible (GTK_WIDGET (selected_row)))
		selected_row = NULL;
	if (!selected_row)
		selected_row = servlist_first_visible_network_row (list);

	if (selected_row)
		gtk_list_box_select_row (GTK_LIST_BOX (list), selected_row);
	else
		selected_net = NULL;
}

static void
servlist_networks_populate (GtkWidget *list, GSList *netlist)
{
	servlist_networks_populate_ (list, netlist, prefs.hex_gui_slist_fav, selected_net);
}

static gint
servlist_compare (ircnet *net1, ircnet *net2)
{
	char *net1_casefolded;
	char *net2_casefolded;
	int result;

	net1_casefolded = g_utf8_casefold (net1->name ? net1->name : "", -1);
	net2_casefolded = g_utf8_casefold (net2->name ? net2->name : "", -1);
	result = g_utf8_collate (net1_casefolded, net2_casefolded);
	g_free (net1_casefolded);
	g_free (net2_casefolded);

	return result;
}

static void
servlist_validate_user_entries (void)
{
	const char *nick1;
	const char *nick2;
	const char *user;
	gboolean ok;

	if (!button_connect || !entry_nick1 || !entry_nick2 || !entry_guser)
		return;

	nick1 = gtk_editable_get_text (GTK_EDITABLE (entry_nick1));
	nick2 = gtk_editable_get_text (GTK_EDITABLE (entry_nick2));
	user = gtk_editable_get_text (GTK_EDITABLE (entry_guser));

	ok = TRUE;
	if (!user || user[0] == 0)
		ok = FALSE;
	if (!nick1 || nick1[0] == 0)
		ok = FALSE;
	if (!nick2 || nick2[0] == 0)
		ok = FALSE;
	if (ok && !rfc_casecmp (nick1, nick2))
		ok = FALSE;

	gtk_widget_set_sensitive (button_connect, ok);
}

static void
servlist_username_changed_cb (GtkEditable *editable, gpointer userdata)
{
	(void) editable;
	(void) userdata;
	servlist_validate_user_entries ();
}

static void
servlist_nick_changed_cb (GtkEditable *editable, gpointer userdata)
{
	(void) editable;
	(void) userdata;
	servlist_validate_user_entries ();
}

static int
servlist_savegui (void)
{
	char *sp;
	const char *nick1;
	const char *nick2;
	const char *user;

	if (!entry_nick1 || !entry_nick2 || !entry_nick3 || !entry_guser)
		return 1;

	nick1 = gtk_editable_get_text (GTK_EDITABLE (entry_nick1));
	nick2 = gtk_editable_get_text (GTK_EDITABLE (entry_nick2));
	user = gtk_editable_get_text (GTK_EDITABLE (entry_guser));

	if (!user || user[0] == 0)
		return 1;
	if (!nick1 || !nick2 || nick1[0] == 0 || nick2[0] == 0)
		return 2;
	if (!rfc_casecmp (nick1, nick2))
		return 2;

	safe_strcpy (prefs.hex_irc_nick1, nick1, sizeof (prefs.hex_irc_nick1));
	safe_strcpy (prefs.hex_irc_nick2, nick2, sizeof (prefs.hex_irc_nick2));
	safe_strcpy (prefs.hex_irc_nick3,
		gtk_editable_get_text (GTK_EDITABLE (entry_nick3)),
		sizeof (prefs.hex_irc_nick3));
	safe_strcpy (prefs.hex_irc_user_name, user, sizeof (prefs.hex_irc_user_name));

	sp = strchr (prefs.hex_irc_user_name, ' ');
	if (sp)
		sp[0] = 0; /* spaces break login */

	servlist_save ();
	save_config (); /* nicks are in hexchat.conf */

	return 0;
}

static void
servlist_deletenetwork (ircnet *net)
{
	if (!networks_list || !net)
		return;

	servlist_net_remove (net);
	selected_net = NULL;
	servlist_networks_populate (networks_list, network_list);
}

static void
servlist_deletenetdialog_cb (int confirmed, void *userdata)
{
	ircnet *net;

	net = userdata;
	if (!confirmed || !net)
		return;
	if (!g_slist_find (network_list, net))
		return;

	servlist_deletenetwork (net);
}

static void
servlist_addnet_cb (GtkWidget *item, gpointer userdata)
{
	ircnet *net;

	(void) item;
	(void) userdata;

	net = servlist_net_add (_("New Network"), "", TRUE);
	net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
	servlist_server_add (net, DEFAULT_SERVER);
	selected_net = net;
	servlist_networks_populate (networks_list, network_list);
}

static void
servlist_deletenet_cb (GtkWidget *item, gpointer userdata)
{
	char *prompt;

	(void) item;
	(void) userdata;

	if (!selected_net)
		return;

	prompt = g_strdup_printf (_("Really remove network \"%s\"?"),
		selected_net->name ? selected_net->name : _("Unnamed"));
	fe_get_bool (_("Remove Network"), prompt, servlist_deletenetdialog_cb, selected_net);
	g_free (prompt);
}

static void
servlist_sort_cb (GtkWidget *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	network_list = g_slist_sort (network_list, (GCompareFunc) servlist_compare);
	servlist_networks_populate (networks_list, network_list);
}

static GSList *
servlist_move_item (GSList *list, gpointer item, int delta)
{
	int pos;
	int new_pos;
	int len;

	if (!list || !item || delta == 0)
		return list;

	pos = g_slist_index (list, item);
	if (pos < 0)
		return list;

	len = g_slist_length (list);
	new_pos = pos + delta;
	if (new_pos < 0 || new_pos >= len)
		return list;

	list = g_slist_remove (list, item);
	list = g_slist_insert (list, item, new_pos);

	return list;
}

static gboolean
servlist_net_keypress_cb (GtkEventControllerKey *controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	gpointer userdata)
{
	int delta;

	(void) controller;
	(void) keycode;
	(void) userdata;

	if (!selected_net || prefs.hex_gui_slist_fav)
		return FALSE;
	if (!(state & GDK_SHIFT_MASK))
		return FALSE;

	delta = 0;
	if (keyval == GDK_KEY_Up)
		delta = -1;
	else if (keyval == GDK_KEY_Down)
		delta = 1;
	if (!delta)
		return FALSE;

	network_list = servlist_move_item (network_list, selected_net, delta);
	servlist_networks_populate (networks_list, network_list);

	return TRUE;
}

static void
servlist_favor_cb (GtkWidget *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (!selected_net)
		return;

	if (selected_net->flags & FLAG_FAVORITE)
		selected_net->flags &= ~FLAG_FAVORITE;
	else
		selected_net->flags |= FLAG_FAVORITE;

	servlist_networks_populate (networks_list, network_list);
}

static void
no_servlist (GtkWidget *check, gpointer userdata)
{
	(void) userdata;
	prefs.hex_gui_slist_skip =
		gtk_check_button_get_active (GTK_CHECK_BUTTON (check)) ? TRUE : FALSE;
}

static void
fav_servlist (GtkWidget *check, gpointer userdata)
{
	(void) userdata;
	prefs.hex_gui_slist_fav =
		gtk_check_button_get_active (GTK_CHECK_BUTTON (check)) ? TRUE : FALSE;

	servlist_networks_populate (networks_list, network_list);
}

static void
servlist_connect_cb (GtkWidget *button, gpointer userdata)
{
	int servlist_err;

	(void) button;
	(void) userdata;

	if (!selected_net)
		return;

	servlist_err = servlist_savegui ();
	if (servlist_err == 1)
	{
		fe_message (_("User name cannot be left blank."), FE_MSG_ERROR);
		return;
	}
	if (servlist_err == 2)
	{
		fe_message (_("You must have two unique nick names."), FE_MSG_ERROR);
		return;
	}

	if (!is_session (servlist_sess))
		servlist_sess = NULL;

	{
		GSList *list;
		session *sess;
		session *chosen;

		chosen = servlist_sess;
		servlist_sess = NULL;

		for (list = sess_list; list; list = list->next)
		{
			sess = list->data;
			if (sess->server->network == selected_net)
			{
				servlist_sess = sess;
				if (sess->server->connected)
					servlist_sess = NULL;
				break;
			}
		}

		if (!servlist_sess &&
			chosen &&
			!chosen->server->connected &&
			chosen->server->server_session->channel[0] == 0)
		{
			servlist_sess = chosen;
		}
	}

	servlist_connect (servlist_sess, selected_net, TRUE);

	if (serverlist_win)
		gtk_window_close (GTK_WINDOW (serverlist_win));
}

static gboolean
servlist_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) userdata;

	if (window)
	{
		int width;
		int height;

		width = gtk_widget_get_width (GTK_WIDGET (window));
		height = gtk_widget_get_height (GTK_WIDGET (window));
		if (width > 0)
			netlist_win_width = width;
		if (height > 0)
			netlist_win_height = height;
	}

	if (edit_win)
		gtk_window_destroy (GTK_WINDOW (edit_win));

	servlist_savegui ();

	serverlist_win = NULL;
	networks_list = NULL;
	networks_search_entry = NULL;
	g_free (networks_filter_casefold);
	networks_filter_casefold = NULL;
	entry_nick1 = NULL;
	entry_nick2 = NULL;
	entry_nick3 = NULL;
	entry_guser = NULL;
	checkbutton_skip = NULL;
	checkbutton_fav = NULL;
	button_connect = NULL;
	selected_net = NULL;
	servlist_sess = NULL;

	if (sess_list == NULL)
		hexchat_exit ();

	return FALSE;
}

static void
servlist_close_clicked_cb (GtkWidget *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (serverlist_win)
		gtk_window_close (GTK_WINDOW (serverlist_win));
}

static void
servlist_server_row_cb (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
	int pos;

	(void) box;
	(void) user_data;

	selected_serv = NULL;
	if (!row || !selected_net)
		return;

	selected_serv = g_object_get_data (G_OBJECT (row), "hc-server");
	if (!selected_serv)
		return;

	pos = g_slist_index (selected_net->servlist, selected_serv);
	if (pos >= 0)
		selected_net->selected = pos;
}

static void
servlist_command_row_cb (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
	(void) box;
	(void) user_data;

	selected_cmd = NULL;
	if (!row)
		return;

	selected_cmd = g_object_get_data (G_OBJECT (row), "hc-command");
}

static void
servlist_channel_row_cb (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
	(void) box;
	(void) user_data;

	selected_chan = NULL;
	if (!row)
		return;

	selected_chan = g_object_get_data (G_OBJECT (row), "hc-channel");
}

static void
servlist_server_changed_cb (GtkEditable *editable, gpointer user_data)
{
	ircserver *serv;
	char *tmp;

	serv = user_data;
	if (!serv)
		return;

	tmp = g_strdup (gtk_editable_get_text (editable));
	g_free (serv->hostname);
	serv->hostname = tmp;
}

static void
servlist_command_changed_cb (GtkEditable *editable, gpointer user_data)
{
	commandentry *entry;
	char *tmp;

	entry = user_data;
	if (!entry)
		return;

	tmp = g_strdup (gtk_editable_get_text (editable));
	g_free (entry->command);
	entry->command = tmp;
}

static void
servlist_channel_name_changed_cb (GtkEditable *editable, gpointer user_data)
{
	favchannel *fav;
	char *tmp;

	fav = user_data;
	if (!fav)
		return;

	tmp = g_strdup (gtk_editable_get_text (editable));
	g_free (fav->name);
	fav->name = tmp;
}

static void
servlist_channel_key_changed_cb (GtkEditable *editable, gpointer user_data)
{
	favchannel *fav;
	const char *text;

	fav = user_data;
	if (!fav)
		return;

	text = gtk_editable_get_text (editable);
	g_free (fav->key);
	if (text && text[0])
		fav->key = g_strdup (text);
	else
		fav->key = NULL;
}

static GtkWidget *
servlist_server_row_new (ircserver *serv)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *entry;

	row = gtk_list_box_row_new ();
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start (box, 8);
	gtk_widget_set_margin_end (box, 8);
	gtk_widget_set_margin_top (box, 4);
	gtk_widget_set_margin_bottom (box, 4);

	entry = gtk_entry_new ();
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_editable_set_text (GTK_EDITABLE (entry), serv && serv->hostname ? serv->hostname : "");
	g_signal_connect (entry, "changed", G_CALLBACK (servlist_server_changed_cb), serv);

	gtk_box_append (GTK_BOX (box), entry);
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

	g_object_set_data (G_OBJECT (row), "hc-entry-primary", entry);
	g_object_set_data (G_OBJECT (row), "hc-server", serv);

	return row;
}

static GtkWidget *
servlist_command_row_new (commandentry *entry_data)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *entry;

	row = gtk_list_box_row_new ();
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start (box, 8);
	gtk_widget_set_margin_end (box, 8);
	gtk_widget_set_margin_top (box, 4);
	gtk_widget_set_margin_bottom (box, 4);

	entry = gtk_entry_new ();
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_editable_set_text (GTK_EDITABLE (entry),
		entry_data && entry_data->command ? entry_data->command : "");
	g_signal_connect (entry, "changed", G_CALLBACK (servlist_command_changed_cb), entry_data);

	gtk_box_append (GTK_BOX (box), entry);
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

	g_object_set_data (G_OBJECT (row), "hc-entry-primary", entry);
	g_object_set_data (G_OBJECT (row), "hc-command", entry_data);

	return row;
}

static GtkWidget *
servlist_channel_row_new (favchannel *fav)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *name_entry;
	GtkWidget *key_entry;

	row = gtk_list_box_row_new ();
	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start (box, 8);
	gtk_widget_set_margin_end (box, 8);
	gtk_widget_set_margin_top (box, 4);
	gtk_widget_set_margin_bottom (box, 4);

	name_entry = gtk_entry_new ();
	gtk_widget_set_hexpand (name_entry, TRUE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (name_entry), _("Channel"));
	gtk_editable_set_text (GTK_EDITABLE (name_entry), fav && fav->name ? fav->name : "");
	g_signal_connect (name_entry, "changed", G_CALLBACK (servlist_channel_name_changed_cb), fav);

	key_entry = gtk_entry_new ();
	gtk_widget_set_hexpand (key_entry, TRUE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (key_entry), _("Key (Password)"));
	gtk_editable_set_text (GTK_EDITABLE (key_entry), fav && fav->key ? fav->key : "");
	g_signal_connect (key_entry, "changed", G_CALLBACK (servlist_channel_key_changed_cb), fav);

	gtk_box_append (GTK_BOX (box), name_entry);
	gtk_box_append (GTK_BOX (box), key_entry);
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

	g_object_set_data (G_OBJECT (row), "hc-entry-primary", name_entry);
	g_object_set_data (G_OBJECT (row), "hc-channel", fav);

	return row;
}

static void
servlist_servers_populate (ircnet *net, GtkWidget *list, ircserver *prefer)
{
	GSList *iter;
	GtkListBoxRow *selected_row;
	GtkWidget *row;
	int i;

	if (!net || !list)
		return;

	servlist_list_clear (list);
	selected_row = NULL;
	i = 0;

	for (iter = net->servlist; iter; iter = iter->next)
	{
		ircserver *serv;

		serv = iter->data;
		row = servlist_server_row_new (serv);
		gtk_list_box_append (GTK_LIST_BOX (list), row);

		if (prefer && serv == prefer)
			selected_row = GTK_LIST_BOX_ROW (row);
		else if (!prefer && i == net->selected)
			selected_row = GTK_LIST_BOX_ROW (row);

		i++;
	}

	if (!selected_row)
	{
		row = gtk_widget_get_first_child (list);
		if (row)
			selected_row = GTK_LIST_BOX_ROW (row);
	}
	if (selected_row)
		gtk_list_box_select_row (GTK_LIST_BOX (list), selected_row);
}

static void
servlist_commands_populate (ircnet *net, GtkWidget *list, commandentry *prefer)
{
	GSList *iter;
	GtkListBoxRow *selected_row;
	GtkWidget *row;

	if (!net || !list)
		return;

	servlist_list_clear (list);
	selected_row = NULL;

	for (iter = net->commandlist; iter; iter = iter->next)
	{
		commandentry *entry_data;

		entry_data = iter->data;
		row = servlist_command_row_new (entry_data);
		gtk_list_box_append (GTK_LIST_BOX (list), row);

		if (prefer && entry_data == prefer)
			selected_row = GTK_LIST_BOX_ROW (row);
	}

	if (!selected_row)
	{
		row = gtk_widget_get_first_child (list);
		if (row)
			selected_row = GTK_LIST_BOX_ROW (row);
	}
	if (selected_row)
		gtk_list_box_select_row (GTK_LIST_BOX (list), selected_row);
}

static void
servlist_channels_populate (ircnet *net, GtkWidget *list, favchannel *prefer)
{
	GSList *iter;
	GtkListBoxRow *selected_row;
	GtkWidget *row;

	if (!net || !list)
		return;

	servlist_list_clear (list);
	selected_row = NULL;

	for (iter = net->favchanlist; iter; iter = iter->next)
	{
		favchannel *fav;

		fav = iter->data;
		row = servlist_channel_row_new (fav);
		gtk_list_box_append (GTK_LIST_BOX (list), row);

		if (prefer && fav == prefer)
			selected_row = GTK_LIST_BOX_ROW (row);
	}

	if (!selected_row)
	{
		row = gtk_widget_get_first_child (list);
		if (row)
			selected_row = GTK_LIST_BOX_ROW (row);
	}
	if (selected_row)
		gtk_list_box_select_row (GTK_LIST_BOX (list), selected_row);
}

static void
servlist_start_editing (GtkListBox *list)
{
	GtkListBoxRow *row;
	GtkWidget *entry;

	if (!list)
		return;

	row = gtk_list_box_get_selected_row (list);
	if (!row)
		return;

	entry = g_object_get_data (G_OBJECT (row), "hc-entry-primary");
	if (entry)
		gtk_widget_grab_focus (entry);
}

static void
servlist_addserver (void)
{
	ircserver *serv;

	if (!selected_net)
		return;

	serv = servlist_server_add (selected_net, DEFAULT_SERVER);
	servlist_servers_populate (selected_net, edit_lists[SERVER_TREE], serv);
	servlist_start_editing (GTK_LIST_BOX (edit_lists[SERVER_TREE]));
}

static void
servlist_addcommand (void)
{
	commandentry *entry_data;

	if (!selected_net)
		return;

	entry_data = servlist_command_add (selected_net, "ECHO hello");
	servlist_commands_populate (selected_net, edit_lists[CMD_TREE], entry_data);
	servlist_start_editing (GTK_LIST_BOX (edit_lists[CMD_TREE]));
}

static void
servlist_addchannel (void)
{
	favchannel *fav;
	GSList *last;

	if (!selected_net)
		return;

	servlist_favchan_add (selected_net, "#channel");
	last = g_slist_last (selected_net->favchanlist);
	fav = last ? last->data : NULL;
	servlist_channels_populate (selected_net, edit_lists[CHANNEL_TREE], fav);
	servlist_start_editing (GTK_LIST_BOX (edit_lists[CHANNEL_TREE]));
}

static void
servlist_addbutton_cb (GtkWidget *item, gpointer userdata)
{
	guint page;

	(void) item;
	(void) userdata;

	if (!edit_notebook)
		return;

	page = servlist_edit_get_current_page ();
	switch (page)
	{
	case SERVER_TREE:
		servlist_addserver ();
		break;
	case CHANNEL_TREE:
		servlist_addchannel ();
		break;
	case CMD_TREE:
		servlist_addcommand ();
		break;
	default:
		break;
	}
}

static void
servlist_deleteserver_cb (void)
{
	GtkListBoxRow *row;
	ircserver *serv;

	if (!selected_net || !edit_lists[SERVER_TREE])
		return;
	if (g_slist_length (selected_net->servlist) < 2)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (edit_lists[SERVER_TREE]));
	if (!row)
		return;

	serv = g_object_get_data (G_OBJECT (row), "hc-server");
	if (!serv)
		return;

	servlist_server_remove (selected_net, serv);
	selected_serv = NULL;
	servlist_servers_populate (selected_net, edit_lists[SERVER_TREE], NULL);
}

static void
servlist_deletecommand_cb (void)
{
	GtkListBoxRow *row;
	commandentry *entry_data;

	if (!selected_net || !edit_lists[CMD_TREE])
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (edit_lists[CMD_TREE]));
	if (!row)
		return;

	entry_data = g_object_get_data (G_OBJECT (row), "hc-command");
	if (!entry_data)
		return;

	servlist_command_remove (selected_net, entry_data);
	selected_cmd = NULL;
	servlist_commands_populate (selected_net, edit_lists[CMD_TREE], NULL);
}

static void
servlist_deletechannel_cb (void)
{
	GtkListBoxRow *row;
	favchannel *fav;

	if (!selected_net || !edit_lists[CHANNEL_TREE])
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (edit_lists[CHANNEL_TREE]));
	if (!row)
		return;

	fav = g_object_get_data (G_OBJECT (row), "hc-channel");
	if (!fav)
		return;

	servlist_favchan_remove (selected_net, fav);
	selected_chan = NULL;
	servlist_channels_populate (selected_net, edit_lists[CHANNEL_TREE], NULL);
}

static void
servlist_deletebutton_cb (GtkWidget *item, gpointer userdata)
{
	guint page;

	(void) item;
	(void) userdata;

	if (!edit_notebook)
		return;

	page = servlist_edit_get_current_page ();
	switch (page)
	{
	case SERVER_TREE:
		servlist_deleteserver_cb ();
		break;
	case CHANNEL_TREE:
		servlist_deletechannel_cb ();
		break;
	case CMD_TREE:
		servlist_deletecommand_cb ();
		break;
	default:
		break;
	}
}

static void
servlist_editbutton_cb (GtkWidget *item, gpointer userdata)
{
	guint page;

	(void) item;
	(void) userdata;

	if (!edit_notebook)
		return;

	page = servlist_edit_get_current_page ();
	if (page < N_TREES)
		servlist_start_editing (GTK_LIST_BOX (edit_lists[page]));
}

static gboolean
servlist_keypress_cb (GtkEventControllerKey *controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	gpointer userdata)
{
	int delta;
	guint page;

	(void) controller;
	(void) keycode;

	if (!selected_net || !edit_notebook)
		return FALSE;
	if (!(state & GDK_SHIFT_MASK))
		return FALSE;

	delta = 0;
	if (keyval == GDK_KEY_Up)
		delta = -1;
	else if (keyval == GDK_KEY_Down)
		delta = 1;
	if (!delta)
		return FALSE;

	(void) userdata;
	page = servlist_edit_get_current_page ();
	switch (page)
	{
	case SERVER_TREE:
		if (selected_serv)
		{
			selected_net->servlist = servlist_move_item (selected_net->servlist, selected_serv, delta);
			servlist_servers_populate (selected_net, edit_lists[SERVER_TREE], selected_serv);
		}
		break;
	case CHANNEL_TREE:
		if (selected_chan)
		{
			selected_net->favchanlist =
				servlist_move_item (selected_net->favchanlist, selected_chan, delta);
			servlist_channels_populate (selected_net, edit_lists[CHANNEL_TREE], selected_chan);
		}
		break;
	case CMD_TREE:
		if (selected_cmd)
		{
			selected_net->commandlist =
				servlist_move_item (selected_net->commandlist, selected_cmd, delta);
			servlist_commands_populate (selected_net, edit_lists[CMD_TREE], selected_cmd);
		}
		break;
	default:
		break;
	}

	return TRUE;
}

static void
servlist_toggle_global_user (gboolean sensitive)
{
	if (!edit_entry_nick || !edit_entry_nick2 || !edit_entry_user || !edit_entry_real)
		return;

	gtk_widget_set_sensitive (edit_entry_nick, sensitive);
	gtk_widget_set_sensitive (edit_label_nick, sensitive);
	gtk_widget_set_sensitive (edit_entry_nick2, sensitive);
	gtk_widget_set_sensitive (edit_label_nick2, sensitive);
	gtk_widget_set_sensitive (edit_entry_user, sensitive);
	gtk_widget_set_sensitive (edit_label_user, sensitive);
	gtk_widget_set_sensitive (edit_entry_real, sensitive);
	gtk_widget_set_sensitive (edit_label_real, sensitive);
}

static void
servlist_check_cb (GtkWidget *check, gpointer num_p)
{
	int num;
	guint32 mask;
	gboolean active;

	if (!selected_net)
		return;

	num = GPOINTER_TO_INT (num_p);
	mask = (guint32) (1 << num);
	active = gtk_check_button_get_active (GTK_CHECK_BUTTON (check));

	if (mask == FLAG_CYCLE || mask == FLAG_USE_PROXY)
	{
		/* reversed for compatibility with old semantics */
		if (active)
			selected_net->flags &= ~mask;
		else
			selected_net->flags |= mask;
	}
	else
	{
		if (active)
			selected_net->flags |= mask;
		else
			selected_net->flags &= ~mask;
	}

	if (mask == FLAG_USE_GLOBAL)
		servlist_toggle_global_user (!active);
}

static GtkWidget *
servlist_create_check (int num, int state, GtkWidget *grid, int row, int col, const char *labeltext)
{
	GtkWidget *check;

	check = gtk_check_button_new_with_label (labeltext ? labeltext : "");
	gtk_check_button_set_active (GTK_CHECK_BUTTON (check), state ? TRUE : FALSE);
	g_signal_connect (check, "toggled", G_CALLBACK (servlist_check_cb), GINT_TO_POINTER (num));
	gtk_grid_attach (GTK_GRID (grid), check, col, row, 2, 1);

	return check;
}

static GtkWidget *
servlist_create_entry (GtkWidget *grid, const char *labeltext, int row,
	const char *def, GtkWidget **label_ret, const char *tip)
{
	GtkWidget *label;
	GtkWidget *entry;

	label = gtk_label_new (labeltext ? labeltext : "");
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	if (label_ret)
		*label_ret = label;
	gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

	entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (entry), def ? def : "");
	if (tip)
		gtk_widget_set_tooltip_text (entry, tip);
	gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 1, 1);

	return entry;
}

static void
servlist_combo_cb (GtkEditable *editable, gpointer userdata)
{
	(void) userdata;

	if (!selected_net)
		return;

	g_free (selected_net->encoding);
	selected_net->encoding = g_strdup (gtk_editable_get_text (editable));
}

static void
servlist_charset_selected_cb (GObject *object, GParamSpec *pspec, gpointer userdata)
{
	GtkDropDown *dd;
	guint index;

	(void) pspec;
	(void) userdata;

	dd = GTK_DROP_DOWN (object);
	index = gtk_drop_down_get_selected (dd);
	if (index == GTK_INVALID_LIST_POSITION)
		return;
	if (!edit_entry_charset)
		return;
	if (!pages[index])
		return;

	gtk_editable_set_text (GTK_EDITABLE (edit_entry_charset), pages[index]);
}

static GtkWidget *
servlist_create_charsetcombo (void)
{
	GtkWidget *box;
	GtkWidget *entry;
	GtkWidget *dd;
	guint i;
	guint selected;

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	entry = gtk_entry_new ();
	gtk_widget_set_hexpand (entry, TRUE);
	gtk_editable_set_text (GTK_EDITABLE (entry),
		selected_net && selected_net->encoding ? selected_net->encoding : pages[0]);
	g_signal_connect (entry, "changed", G_CALLBACK (servlist_combo_cb), NULL);
	gtk_box_append (GTK_BOX (box), entry);
	edit_entry_charset = entry;

	dd = gtk_drop_down_new_from_strings (pages);
	selected = 0;
	for (i = 0; pages[i]; i++)
	{
		if (selected_net && selected_net->encoding && g_strcmp0 (selected_net->encoding, pages[i]) == 0)
		{
			selected = i;
			break;
		}
	}
	gtk_drop_down_set_selected (GTK_DROP_DOWN (dd), selected);
	g_signal_connect (dd, "notify::selected", G_CALLBACK (servlist_charset_selected_cb), NULL);
	gtk_box_append (GTK_BOX (box), dd);

	return box;
}

static void
servlist_logintypecombo_cb (GObject *object, GParamSpec *pspec, gpointer userdata)
{
	GtkDropDown *dd;
	guint index;
	int length;

	(void) pspec;
	(void) userdata;

	if (!selected_net)
		return;

	dd = GTK_DROP_DOWN (object);
	index = gtk_drop_down_get_selected (dd);
	if (index == GTK_INVALID_LIST_POSITION)
		return;
	length = (int) (sizeof (login_types_conf) / sizeof (login_types_conf[0]));
	if ((int) index >= length)
		return;

	selected_net->logintype = login_types_conf[index];

	if (login_types_conf[index] == LOGIN_CUSTOM && edit_notebook)
		servlist_edit_set_current_page (CMD_TREE);

	if (edit_entry_pass)
	{
		if (login_types_conf[index] == LOGIN_SASLEXTERNAL)
			gtk_widget_set_sensitive (edit_entry_pass, FALSE);
		else
			gtk_widget_set_sensitive (edit_entry_pass, TRUE);
	}
}

static GtkWidget *
servlist_create_logintypecombo (void)
{
	GtkWidget *dd;

	dd = gtk_drop_down_new_from_strings (login_types);
	gtk_drop_down_set_selected (GTK_DROP_DOWN (dd),
		servlist_get_login_desc_index (selected_net ? selected_net->logintype : 0));
	gtk_widget_set_tooltip_text (dd,
		_("The way you identify yourself to the server. For custom login methods use connect commands."));
	g_signal_connect (dd, "notify::selected", G_CALLBACK (servlist_logintypecombo_cb), NULL);

	return dd;
}

static void
servlist_cleanup_servers (ircnet *net)
{
	GSList *list;
	GSList *next;

	if (!net)
		return;

	for (list = net->servlist; list; list = next)
	{
		ircserver *serv;
		char *clean;

		next = list->next;
		serv = list->data;
		if (!serv)
			continue;

		clean = servlist_sanitize_hostname (serv->hostname);
		g_free (serv->hostname);
		serv->hostname = clean;

		if (!serv->hostname || serv->hostname[0] == 0)
			servlist_server_remove (net, serv);
	}

	if (!net->servlist)
		servlist_server_add (net, DEFAULT_SERVER);

	if (net->selected < 0 || net->selected >= g_slist_length (net->servlist))
		net->selected = 0;
}

static void
servlist_cleanup_commands (ircnet *net)
{
	GSList *list;
	GSList *next;

	if (!net)
		return;

	for (list = net->commandlist; list; list = next)
	{
		commandentry *entry_data;
		char *clean;

		next = list->next;
		entry_data = list->data;
		if (!entry_data)
			continue;

		clean = servlist_sanitize_command (entry_data->command);
		g_free (entry_data->command);
		entry_data->command = clean;

		if (!entry_data->command || entry_data->command[0] == 0)
			servlist_command_remove (net, entry_data);
	}
}

static void
servlist_cleanup_channels (ircnet *net)
{
	GSList *list;
	GSList *next;

	if (!net)
		return;

	for (list = net->favchanlist; list; list = next)
	{
		favchannel *fav;
		char *name;

		next = list->next;
		fav = list->data;
		if (!fav)
			continue;

		name = g_strdup (fav->name ? fav->name : "");
		g_strstrip (name);
		g_free (fav->name);
		fav->name = name;

		if (!fav->name || fav->name[0] == 0)
		{
			servlist_favchan_remove (net, fav);
			continue;
		}

		if (fav->key && fav->key[0] == 0)
		{
			g_free (fav->key);
			fav->key = NULL;
		}
	}
}

static void
servlist_edit_update (ircnet *net)
{
	const char *name;
	guint login_index;
	int length;

	if (!net)
		return;

	name = gtk_editable_get_text (GTK_EDITABLE (edit_entry_netname));
	if (name && name[0])
	{
		g_free (net->name);
		net->name = g_strdup (name);
	}

	servlist_update_from_entry (&net->nick, edit_entry_nick);
	servlist_update_from_entry (&net->nick2, edit_entry_nick2);
	servlist_update_from_entry (&net->user, edit_entry_user);
	servlist_update_from_entry (&net->real, edit_entry_real);
	servlist_update_from_entry (&net->pass, edit_entry_pass);
	servlist_update_from_entry (&net->encoding, edit_entry_charset);

	if (edit_entry_charset && (!net->encoding || !net->encoding[0]))
	{
		g_free (net->encoding);
		net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
	}

	if (edit_notebook)
		netedit_active_tab = servlist_edit_get_current_page ();

	login_index = 0;
	if (edit_notebook)
	{
		GtkWidget *grid;
		GtkWidget *login_dd;

		grid = g_object_get_data (G_OBJECT (edit_notebook), "hc-options-grid");
		login_dd = grid ? g_object_get_data (G_OBJECT (grid), "hc-login-dd") : NULL;
		if (login_dd)
			login_index = gtk_drop_down_get_selected (GTK_DROP_DOWN (login_dd));
	}
	length = (int) (sizeof (login_types_conf) / sizeof (login_types_conf[0]));
	if ((int) login_index >= 0 && (int) login_index < length)
		net->logintype = login_types_conf[login_index];

	servlist_cleanup_servers (net);
	servlist_cleanup_channels (net);
	servlist_cleanup_commands (net);
}

static gboolean
servlist_edit_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) userdata;

	if (window)
	{
		int width;
		int height;

		width = gtk_widget_get_width (GTK_WIDGET (window));
		height = gtk_widget_get_height (GTK_WIDGET (window));
		if (width > 0)
			netedit_win_width = width;
		if (height > 0)
			netedit_win_height = height;
	}

	if (selected_net)
		servlist_edit_update (selected_net);
	if (networks_list)
		servlist_networks_populate (networks_list, network_list);

	edit_win = NULL;
	memset (edit_lists, 0, sizeof (edit_lists));
	edit_notebook = NULL;
	edit_entry_netname = NULL;
	edit_entry_nick = NULL;
	edit_entry_nick2 = NULL;
	edit_entry_user = NULL;
	edit_entry_real = NULL;
	edit_entry_pass = NULL;
	edit_entry_charset = NULL;
	edit_label_nick = NULL;
	edit_label_nick2 = NULL;
	edit_label_user = NULL;
	edit_label_real = NULL;
	selected_serv = NULL;
	selected_cmd = NULL;
	selected_chan = NULL;

	return FALSE;
}

static void
servlist_edit_close_cb (GtkWidget *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (edit_win)
		gtk_window_close (GTK_WINDOW (edit_win));
}

static GtkWidget *
servlist_open_edit (GtkWidget *parent, ircnet *net)
{
	GtkWidget *window;
	GtkWidget *pages_slot;
	GtkWidget *pages_widget;
	GtkWidget *scroll;
	GtkWidget *list;
	GtkWidget *stack_box;
	GtkWidget *switcher;
	GtkWidget *add_button;
	GtkWidget *remove_button;
	GtkWidget *change_button;
	GtkWidget *cancel_button;
	GtkWidget *button;
	GtkWidget *options_grid;
	GtkWidget *check;
	GtkWidget *label;
	GtkWidget *row_box;
	GtkBuilder *builder;
	char *title;
	int width;
	int height;

	builder = fe_gtk4_builder_new_from_resource (SERVLIST_EDIT_UI_PATH);
	window = fe_gtk4_builder_get_widget (builder, "servlist_edit_window", GTK_TYPE_WINDOW);
	edit_entry_netname = fe_gtk4_builder_get_widget (builder, "servlist_edit_name_entry", GTK_TYPE_ENTRY);
	pages_slot = fe_gtk4_builder_get_widget (builder, "servlist_edit_pages_slot", GTK_TYPE_BOX);
	options_grid = fe_gtk4_builder_get_widget (builder, "servlist_edit_options_grid", GTK_TYPE_GRID);
	add_button = fe_gtk4_builder_get_widget (builder, "servlist_edit_add_button", GTK_TYPE_BUTTON);
	remove_button = fe_gtk4_builder_get_widget (builder, "servlist_edit_remove_button", GTK_TYPE_BUTTON);
	change_button = fe_gtk4_builder_get_widget (builder, "servlist_edit_change_button", GTK_TYPE_BUTTON);
	cancel_button = fe_gtk4_builder_get_widget (builder, "servlist_edit_cancel_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (window);
	g_object_unref (builder);

	title = g_strdup_printf (_("Edit %s - %s"), net->name ? net->name : _("Network"), PACKAGE_NAME);
	gtk_window_set_title (GTK_WINDOW (window), title);
	g_free (title);
	width = netedit_win_width > 0 ? netedit_win_width : 820;
	height = netedit_win_height > 0 ? netedit_win_height : 680;
	gtk_window_set_default_size (GTK_WINDOW (window), width, height);
	gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (parent));
	gtk_window_set_modal (GTK_WINDOW (window), TRUE);
	gtk_editable_set_text (GTK_EDITABLE (edit_entry_netname), net->name ? net->name : "");

	stack_box = pages_slot;
	gtk_widget_set_hexpand (stack_box, TRUE);
	gtk_widget_set_vexpand (stack_box, TRUE);

	switcher = adw_view_switcher_new ();
	adw_view_switcher_set_policy (ADW_VIEW_SWITCHER (switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
	gtk_widget_set_halign (switcher, GTK_ALIGN_START);
	gtk_box_append (GTK_BOX (stack_box), switcher);

	pages_widget = adw_view_stack_new ();
	edit_notebook = pages_widget;
	gtk_widget_set_hexpand (pages_widget, TRUE);
	gtk_widget_set_vexpand (pages_widget, TRUE);
	adw_view_switcher_set_stack (ADW_VIEW_SWITCHER (switcher), ADW_VIEW_STACK (pages_widget));
	gtk_box_append (GTK_BOX (stack_box), pages_widget);

	scroll = gtk_scrolled_window_new ();
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list);
	adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (pages_widget), scroll,
		edit_page_names[SERVER_TREE], _("Servers"), "network-server-symbolic");
	g_signal_connect (list, "row-selected", G_CALLBACK (servlist_server_row_cb), NULL);
	{
		GtkEventController *controller = gtk_event_controller_key_new ();
		g_signal_connect (controller, "key-pressed", G_CALLBACK (servlist_keypress_cb), NULL);
		gtk_widget_add_controller (list, controller);
	}
	edit_lists[SERVER_TREE] = list;

	scroll = gtk_scrolled_window_new ();
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list);
	adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (pages_widget), scroll,
		edit_page_names[CHANNEL_TREE], _("Autojoin channels"), "view-list-symbolic");
	g_signal_connect (list, "row-selected", G_CALLBACK (servlist_channel_row_cb), NULL);
	{
		GtkEventController *controller = gtk_event_controller_key_new ();
		g_signal_connect (controller, "key-pressed", G_CALLBACK (servlist_keypress_cb), NULL);
		gtk_widget_add_controller (list, controller);
	}
	edit_lists[CHANNEL_TREE] = list;

	scroll = gtk_scrolled_window_new ();
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list);
	adw_view_stack_add_titled_with_icon (ADW_VIEW_STACK (pages_widget), scroll,
		edit_page_names[CMD_TREE], _("Connect commands"), "applications-engineering-symbolic");
	g_signal_connect (list, "row-selected", G_CALLBACK (servlist_command_row_cb), NULL);
	{
		GtkEventController *controller = gtk_event_controller_key_new ();
		g_signal_connect (controller, "key-pressed", G_CALLBACK (servlist_keypress_cb), NULL);
		gtk_widget_add_controller (list, controller);
	}
	edit_lists[CMD_TREE] = list;

	g_signal_connect (add_button, "clicked", G_CALLBACK (servlist_addbutton_cb), NULL);
	g_signal_connect (remove_button, "clicked", G_CALLBACK (servlist_deletebutton_cb), NULL);
	g_signal_connect (change_button, "clicked", G_CALLBACK (servlist_editbutton_cb), NULL);
	g_object_set_data (G_OBJECT (edit_notebook), "hc-options-grid", options_grid);

	check = servlist_create_check (0, !(net->flags & FLAG_CYCLE), options_grid, 0, 0,
		_("Connect to selected server only"));
	gtk_widget_set_tooltip_text (check,
		_("Don't cycle through all the servers when the connection fails."));
	servlist_create_check (3, net->flags & FLAG_AUTO_CONNECT, options_grid, 1, 0,
		_("Connect to this network automatically"));
	servlist_create_check (4, !(net->flags & FLAG_USE_PROXY), options_grid, 2, 0,
		_("Bypass proxy server"));
	check = servlist_create_check (2, net->flags & FLAG_USE_SSL, options_grid, 3, 0,
		_("Use SSL for all the servers on this network"));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (check, FALSE);
#endif
	check = servlist_create_check (5, net->flags & FLAG_ALLOW_INVALID, options_grid, 4, 0,
		_("Accept invalid SSL certificates"));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (check, FALSE);
#endif
	servlist_create_check (1, net->flags & FLAG_USE_GLOBAL, options_grid, 5, 0,
		_("Use global user information"));

	edit_entry_nick = servlist_create_entry (options_grid, _("Nick name:"), 6,
		net->nick, &edit_label_nick, NULL);
	edit_entry_nick2 = servlist_create_entry (options_grid, _("Second choice:"), 7,
		net->nick2, &edit_label_nick2, NULL);
	edit_entry_real = servlist_create_entry (options_grid, _("Real name:"), 8,
		net->real, &edit_label_real, NULL);
	edit_entry_user = servlist_create_entry (options_grid, _("User name:"), 9,
		net->user, &edit_label_user, NULL);

	label = gtk_label_new (_("Login method:"));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_grid_attach (GTK_GRID (options_grid), label, 0, 10, 1, 1);
	row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	button = servlist_create_logintypecombo ();
	gtk_box_append (GTK_BOX (row_box), button);
	gtk_grid_attach (GTK_GRID (options_grid), row_box, 1, 10, 1, 1);
	g_object_set_data (G_OBJECT (options_grid), "hc-login-dd", button);

	edit_entry_pass = servlist_create_entry (options_grid, _("Password:"), 11,
		net->pass, NULL,
		_("Password used for login. If in doubt, leave blank."));
	gtk_entry_set_visibility (GTK_ENTRY (edit_entry_pass), FALSE);
	if (net->logintype == LOGIN_SASLEXTERNAL)
		gtk_widget_set_sensitive (edit_entry_pass, FALSE);

	label = gtk_label_new (_("Character set:"));
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_grid_attach (GTK_GRID (options_grid), label, 0, 12, 1, 1);
	row_box = servlist_create_charsetcombo ();
	gtk_grid_attach (GTK_GRID (options_grid), row_box, 1, 12, 1, 1);
	g_signal_connect (cancel_button, "clicked", G_CALLBACK (servlist_edit_close_cb), NULL);

	if (net->flags & FLAG_USE_GLOBAL)
		servlist_toggle_global_user (FALSE);

	servlist_servers_populate (net, edit_lists[SERVER_TREE], NULL);
	servlist_channels_populate (net, edit_lists[CHANNEL_TREE], NULL);
	servlist_commands_populate (net, edit_lists[CMD_TREE], NULL);

	servlist_edit_set_current_page (netedit_active_tab);
	g_signal_connect (window, "close-request", G_CALLBACK (servlist_edit_close_request_cb), NULL);

	return window;
}

static void
servlist_edit_cb (GtkWidget *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (!selected_net || !serverlist_win)
		return;

	if (edit_win)
	{
		gtk_window_present (GTK_WINDOW (edit_win));
		return;
	}

	edit_win = servlist_open_edit (serverlist_win, selected_net);
	if (edit_win)
		gtk_window_present (GTK_WINDOW (edit_win));
}

void
fe_serverlist_open (session *sess)
{
	GtkWidget *placeholder;
	GtkWidget *button;
	GtkWidget *button_box;
	GtkWidget *add_button;
	GtkWidget *remove_button;
	GtkWidget *edit_button;
	GtkWidget *sort_button;
	GtkWidget *favor_button;
	GtkWidget *cancel_button;
	GtkWidget *header;
	GtkBuilder *builder;
	int width;
	int height;

	if (serverlist_win)
	{
		gtk_window_present (GTK_WINDOW (serverlist_win));
		return;
	}

	servlist_sess = sess;

	builder = fe_gtk4_builder_new_from_resource (SERVLIST_UI_PATH);
	serverlist_win = fe_gtk4_builder_get_widget (builder, "servlist_window", GTK_TYPE_WINDOW);
	entry_nick1 = fe_gtk4_builder_get_widget (builder, "servlist_entry_nick1", GTK_TYPE_ENTRY);
	entry_nick2 = fe_gtk4_builder_get_widget (builder, "servlist_entry_nick2", GTK_TYPE_ENTRY);
	entry_nick3 = fe_gtk4_builder_get_widget (builder, "servlist_entry_nick3", GTK_TYPE_ENTRY);
	entry_guser = fe_gtk4_builder_get_widget (builder, "servlist_entry_guser", GTK_TYPE_ENTRY);
	networks_search_entry = fe_gtk4_builder_get_widget (builder, "servlist_networks_search_entry", GTK_TYPE_SEARCH_ENTRY);
	networks_list = fe_gtk4_builder_get_widget (builder, "servlist_networks_list", GTK_TYPE_LIST_BOX);
	checkbutton_skip = fe_gtk4_builder_get_widget (builder, "servlist_check_skip", GTK_TYPE_CHECK_BUTTON);
	checkbutton_fav = fe_gtk4_builder_get_widget (builder, "servlist_check_fav", GTK_TYPE_CHECK_BUTTON);
	button_connect = fe_gtk4_builder_get_widget (builder, "servlist_connect_button", GTK_TYPE_BUTTON);
	cancel_button = fe_gtk4_builder_get_widget (builder, "servlist_cancel_button", GTK_TYPE_BUTTON);
	button_box = fe_gtk4_builder_get_widget (builder, "servlist_networks_button_box", GTK_TYPE_BOX);
	add_button = fe_gtk4_builder_get_widget (builder, "servlist_add_button", GTK_TYPE_BUTTON);
	remove_button = fe_gtk4_builder_get_widget (builder, "servlist_remove_button", GTK_TYPE_BUTTON);
	edit_button = fe_gtk4_builder_get_widget (builder, "servlist_edit_button", GTK_TYPE_BUTTON);
	sort_button = fe_gtk4_builder_get_widget (builder, "servlist_sort_button", GTK_TYPE_BUTTON);
	favor_button = fe_gtk4_builder_get_widget (builder, "servlist_favor_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (serverlist_win);
	g_object_unref (builder);

	gtk_window_set_title (GTK_WINDOW (serverlist_win), _("Network List"));
	width = netlist_win_width > 0 ? netlist_win_width : 760;
	height = netlist_win_height > 0 ? netlist_win_height : 700;
	gtk_window_set_default_size (GTK_WINDOW (serverlist_win), width, height);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (serverlist_win), GTK_WINDOW (main_window));

	gtk_widget_set_visible (button_box, FALSE);

	header = adw_header_bar_new ();
	adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (header), TRUE);
	adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (header), TRUE);
	gtk_window_set_titlebar (GTK_WINDOW (serverlist_win), header);

	button = gtk_button_new_from_icon_name ("list-add-symbolic");
	gtk_widget_set_tooltip_text (button, _("Add Network"));
	g_signal_connect (button, "clicked", G_CALLBACK (servlist_addnet_cb), NULL);
	adw_header_bar_pack_start (ADW_HEADER_BAR (header), button);

	button = gtk_button_new_from_icon_name ("list-remove-symbolic");
	gtk_widget_set_tooltip_text (button, _("Remove Network"));
	g_signal_connect (button, "clicked", G_CALLBACK (servlist_deletenet_cb), NULL);
	adw_header_bar_pack_start (ADW_HEADER_BAR (header), button);

	button = gtk_button_new_from_icon_name ("document-edit-symbolic");
	gtk_widget_set_tooltip_text (button, _("Edit Network"));
	g_signal_connect (button, "clicked", G_CALLBACK (servlist_edit_cb), NULL);
	adw_header_bar_pack_start (ADW_HEADER_BAR (header), button);

	button = gtk_button_new_from_icon_name ("view-sort-ascending-symbolic");
	gtk_widget_set_tooltip_text (button, _("Sort Networks"));
	g_signal_connect (button, "clicked", G_CALLBACK (servlist_sort_cb), NULL);
	adw_header_bar_pack_end (ADW_HEADER_BAR (header), button);

	button = gtk_button_new_from_icon_name ("star-new-symbolic");
	gtk_widget_set_tooltip_text (button, _("Toggle Favorite"));
	g_signal_connect (button, "clicked", G_CALLBACK (servlist_favor_cb), NULL);
	adw_header_bar_pack_end (ADW_HEADER_BAR (header), button);

	gtk_editable_set_text (GTK_EDITABLE (entry_nick1), prefs.hex_irc_nick1);
	gtk_editable_set_text (GTK_EDITABLE (entry_nick2), prefs.hex_irc_nick2);
	gtk_editable_set_text (GTK_EDITABLE (entry_nick3), prefs.hex_irc_nick3);
	gtk_editable_set_text (GTK_EDITABLE (entry_guser), prefs.hex_irc_user_name);
	gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (networks_search_entry), _("Search networks"));
	gtk_search_entry_set_key_capture_widget (GTK_SEARCH_ENTRY (networks_search_entry), serverlist_win);

	g_signal_connect (networks_search_entry, "changed",
		G_CALLBACK (servlist_network_search_changed_cb), NULL);

	gtk_list_box_set_filter_func (GTK_LIST_BOX (networks_list),
		servlist_network_filter_cb, NULL, NULL);
	placeholder = gtk_label_new (_("No matching networks"));
	gtk_widget_add_css_class (placeholder, "dim-label");
	gtk_list_box_set_placeholder (GTK_LIST_BOX (networks_list), placeholder);
	g_signal_connect (networks_list, "row-selected", G_CALLBACK (servlist_network_row_cb), NULL);
	{
		GtkEventController *controller = gtk_event_controller_key_new ();
		g_signal_connect (controller, "key-pressed", G_CALLBACK (servlist_net_keypress_cb), NULL);
		gtk_widget_add_controller (networks_list, controller);
	}

	g_signal_connect (add_button, "clicked", G_CALLBACK (servlist_addnet_cb), NULL);
	g_signal_connect (remove_button, "clicked", G_CALLBACK (servlist_deletenet_cb), NULL);
	g_signal_connect (edit_button, "clicked", G_CALLBACK (servlist_edit_cb), NULL);
	g_signal_connect (sort_button, "clicked", G_CALLBACK (servlist_sort_cb), NULL);
	g_signal_connect (favor_button, "clicked", G_CALLBACK (servlist_favor_cb), NULL);

	gtk_check_button_set_active (GTK_CHECK_BUTTON (checkbutton_skip), prefs.hex_gui_slist_skip);
	g_signal_connect (checkbutton_skip, "toggled", G_CALLBACK (no_servlist), NULL);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (checkbutton_fav), prefs.hex_gui_slist_fav);
	g_signal_connect (checkbutton_fav, "toggled", G_CALLBACK (fav_servlist), NULL);

	g_signal_connect (cancel_button, "clicked", G_CALLBACK (servlist_close_clicked_cb), NULL);
	g_signal_connect (button_connect, "clicked", G_CALLBACK (servlist_connect_cb), NULL);

	g_signal_connect (entry_guser, "changed", G_CALLBACK (servlist_username_changed_cb), NULL);
	g_signal_connect (entry_nick1, "changed", G_CALLBACK (servlist_nick_changed_cb), NULL);
	g_signal_connect (entry_nick2, "changed", G_CALLBACK (servlist_nick_changed_cb), NULL);

	servlist_networks_populate (networks_list, network_list);
	servlist_validate_user_entries ();

	g_signal_connect (serverlist_win, "close-request", G_CALLBACK (servlist_close_request_cb), NULL);
	gtk_window_present (GTK_WINDOW (serverlist_win));
}

void
fe_gtk4_servlistgui_cleanup (void)
{
	if (edit_win)
		gtk_window_destroy (GTK_WINDOW (edit_win));
	if (serverlist_win)
		gtk_window_destroy (GTK_WINDOW (serverlist_win));

	edit_win = NULL;
	serverlist_win = NULL;
	networks_list = NULL;
	networks_search_entry = NULL;
	g_free (networks_filter_casefold);
	networks_filter_casefold = NULL;
	entry_nick1 = NULL;
	entry_nick2 = NULL;
	entry_nick3 = NULL;
	entry_guser = NULL;
	checkbutton_skip = NULL;
	checkbutton_fav = NULL;
	button_connect = NULL;
	selected_net = NULL;
	selected_serv = NULL;
	selected_cmd = NULL;
	selected_chan = NULL;
	servlist_sess = NULL;
	memset (edit_lists, 0, sizeof (edit_lists));
	edit_notebook = NULL;
	edit_entry_netname = NULL;
	edit_entry_nick = NULL;
	edit_entry_nick2 = NULL;
	edit_entry_user = NULL;
	edit_entry_real = NULL;
	edit_entry_pass = NULL;
	edit_entry_charset = NULL;
	edit_label_nick = NULL;
	edit_label_nick2 = NULL;
	edit_label_user = NULL;
	edit_label_real = NULL;
}

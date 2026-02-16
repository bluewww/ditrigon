/* HexChat GTK4 server list window */
#include "fe-gtk4.h"

#include <string.h>
#include <adwaita.h>

#ifdef USE_OPENSSL
#define DEFAULT_SERVER "newserver/6697"
#else
#define DEFAULT_SERVER "newserver/6667"
#endif

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

static AdwEntryRow *entry_nick1;
static AdwEntryRow *entry_nick2;
static AdwEntryRow *entry_nick3;
static AdwEntryRow *entry_guser;

static AdwSwitchRow *checkbutton_skip;
static GtkWidget *checkbutton_fav;
static GtkWidget *button_connect;
static GtkWidget *button_add_net;
static GtkWidget *button_remove_net;
static GtkWidget *button_edit_net;
static GtkWidget *button_favor_net;

static GtkWidget *edit_win;
static GtkWidget *edit_lists[N_TREES];
static AdwEntryRow *edit_row_netname;
static AdwEntryRow *edit_row_nick;
static AdwEntryRow *edit_row_nick2;
static AdwEntryRow *edit_row_user;
static AdwEntryRow *edit_row_real;
static AdwPasswordEntryRow *edit_row_pass;
static AdwComboRow *edit_row_login;
static AdwEntryRow *edit_row_charset;
static GtkDropDown *edit_charset_dropdown;
static AdwSwitchRow *edit_row_use_global;
static AdwSwitchRow *edit_row_connect_selected;
static AdwSwitchRow *edit_row_auto_connect;
static AdwSwitchRow *edit_row_bypass_proxy;
static AdwSwitchRow *edit_row_use_ssl;
static AdwSwitchRow *edit_row_allow_invalid;

static ircnet *selected_net;
static ircserver *selected_serv;
static commandentry *selected_cmd;
static favchannel *selected_chan;
static session *servlist_sess;

static int netlist_win_width;
static int netlist_win_height;
static int netedit_win_width;
static int netedit_win_height;

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


static void servlist_networks_populate (GtkWidget *list, GSList *netlist);
static GtkWidget *servlist_open_edit (GtkWidget *parent, ircnet *net);
static GSList *servlist_move_item (GSList *list, gpointer item, int delta);
static void servlist_validate_user_entries (void);

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

static const char *
servlist_network_primary_server (ircnet *net)
{
	GSList *node;
	int index;
	int len;
	ircserver *serv;

	if (!net || !net->servlist)
		return _("No servers configured");

	len = g_slist_length (net->servlist);
	if (len <= 0)
		return _("No servers configured");

	index = CLAMP (net->selected, 0, len - 1);
	node = g_slist_nth (net->servlist, index);
	if (!node)
		node = net->servlist;
	if (!node)
		return _("No servers configured");

	serv = node->data;
	if (!serv || !serv->hostname || !serv->hostname[0])
		return _("No servers configured");

	return serv->hostname;
}

static const char *
servlist_network_connection_state (ircnet *net)
{
	GSList *list;

	if (!net)
		return _("Disconnected");

	for (list = serv_list; list; list = list->next)
	{
		server *serv;

		serv = list->data;
		if (!serv || serv->network != net)
			continue;
		if (serv->connected)
			return _("Connected");
		if (serv->connecting)
			return _("Connecting");
	}

	return _("Disconnected");
}

static void
servlist_update_actions (void)
{
	gboolean has_selected;
	gboolean favorite;

	has_selected = selected_net != NULL;
	favorite = has_selected && ((selected_net->flags & FLAG_FAVORITE) != 0);

	if (button_remove_net)
		gtk_widget_set_sensitive (button_remove_net, has_selected);
	if (button_edit_net)
		gtk_widget_set_sensitive (button_edit_net, has_selected);
	if (button_favor_net)
	{
		gtk_widget_set_sensitive (button_favor_net, has_selected);
		if (has_selected)
		{
			gtk_button_set_icon_name (GTK_BUTTON (button_favor_net),
				favorite ? "starred-symbolic" : "star-new-symbolic");
			gtk_widget_set_tooltip_text (button_favor_net,
				favorite ? _("Remove Favorite") : _("Favorite Network"));
		}
		else
		{
			gtk_button_set_icon_name (GTK_BUTTON (button_favor_net), "star-new-symbolic");
			gtk_widget_set_tooltip_text (button_favor_net, _("Favorite Network"));
		}
	}

	servlist_validate_user_entries ();
}

static GtkWidget *
servlist_network_row_new (ircnet *net)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *labels_box;
	GtkWidget *title;
	GtkWidget *subtitle;
	GtkWidget *star;
	char *subtitle_text;
	char *tooltip_text;
	const char *name;

	row = gtk_list_box_row_new ();

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start (box, 8);
	gtk_widget_set_margin_end (box, 8);
	gtk_widget_set_margin_top (box, 5);
	gtk_widget_set_margin_bottom (box, 5);

	labels_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 1);
	gtk_widget_set_hexpand (labels_box, TRUE);

	name = net && net->name ? net->name : _("New Network");
	title = gtk_label_new (name);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);
	gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
	gtk_label_set_single_line_mode (GTK_LABEL (title), TRUE);
	gtk_widget_add_css_class (title, "heading");
	gtk_box_append (GTK_BOX (labels_box), title);

	subtitle_text = g_strdup_printf ("%s - %s",
		servlist_network_primary_server (net),
		servlist_network_connection_state (net));
	subtitle = gtk_label_new (subtitle_text);
	g_free (subtitle_text);
	gtk_label_set_xalign (GTK_LABEL (subtitle), 0.0f);
	gtk_label_set_ellipsize (GTK_LABEL (subtitle), PANGO_ELLIPSIZE_END);
	gtk_label_set_single_line_mode (GTK_LABEL (subtitle), TRUE);
	gtk_widget_add_css_class (subtitle, "dim-label");
	gtk_box_append (GTK_BOX (labels_box), subtitle);

	gtk_box_append (GTK_BOX (box), labels_box);

	star = gtk_image_new_from_icon_name ("starred-symbolic");
	gtk_widget_set_visible (star, net && (net->flags & FLAG_FAVORITE));
	gtk_widget_set_valign (star, GTK_ALIGN_CENTER);
	gtk_box_append (GTK_BOX (box), star);

	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
	tooltip_text = g_strdup_printf ("%s\n%s",
		name, servlist_network_primary_server (net));
	gtk_widget_set_tooltip_text (row, tooltip_text);
	g_free (tooltip_text);
	g_object_set_data (G_OBJECT (row), "hc-network", net);

	return row;
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
	{
		servlist_update_actions ();
		return;
	}

	selected_net = g_object_get_data (G_OBJECT (row), "hc-network");
	if (!selected_net)
	{
		servlist_update_actions ();
		return;
	}

	pos = g_slist_index (network_list, selected_net);
	if (pos >= 0)
		prefs.hex_gui_slist_select = pos;

	servlist_update_actions ();
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
		GtkWidget *row;

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

		row = servlist_network_row_new (net);
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

	servlist_update_actions ();
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

	ok = selected_net != NULL;
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
servlist_sort_cb (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
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
no_servlist (AdwSwitchRow *row, GParamSpec *pspec, gpointer userdata)
{
	(void) pspec;
	(void) userdata;
	prefs.hex_gui_slist_skip = adw_switch_row_get_active (row) ? TRUE : FALSE;
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
	button_add_net = NULL;
	button_remove_net = NULL;
	button_edit_net = NULL;
	button_favor_net = NULL;
	selected_net = NULL;
	servlist_sess = NULL;

	if (sess_list == NULL)
		hexchat_exit ();

	return FALSE;
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
	int page;

	(void) item;

	page = GPOINTER_TO_INT (userdata);
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
	int page;

	(void) item;

	page = GPOINTER_TO_INT (userdata);
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
	int page;

	(void) item;

	page = GPOINTER_TO_INT (userdata);
	if (page >= 0 && page < N_TREES)
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
	int page;

	(void) controller;
	(void) keycode;

	if (!selected_net)
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

	page = GPOINTER_TO_INT (userdata);
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
	const char *text;

	if (!net)
		return;

	/* Network name */
	if (edit_row_netname)
	{
		text = gtk_editable_get_text (GTK_EDITABLE (edit_row_netname));
		if (text && text[0])
		{
			g_free (net->name);
			net->name = g_strdup (text);
		}
	}

	/* Nick names, user, and real name */
	if (edit_row_nick)
	{
		text = gtk_editable_get_text (GTK_EDITABLE (edit_row_nick));
		g_free (net->nick);
		net->nick = (text && text[0]) ? g_strdup (text) : NULL;
	}

	if (edit_row_nick2)
	{
		text = gtk_editable_get_text (GTK_EDITABLE (edit_row_nick2));
		g_free (net->nick2);
		net->nick2 = (text && text[0]) ? g_strdup (text) : NULL;
	}

	if (edit_row_user)
	{
		text = gtk_editable_get_text (GTK_EDITABLE (edit_row_user));
		g_free (net->user);
		net->user = (text && text[0]) ? g_strdup (text) : NULL;
	}

	if (edit_row_real)
	{
		text = gtk_editable_get_text (GTK_EDITABLE (edit_row_real));
		g_free (net->real);
		net->real = (text && text[0]) ? g_strdup (text) : NULL;
	}

	/* Password */
	if (edit_row_pass)
	{
		text = gtk_editable_get_text (GTK_EDITABLE (edit_row_pass));
		g_free (net->pass);
		net->pass = (text && text[0]) ? g_strdup (text) : NULL;
	}

	/* Character encoding */
	if (edit_row_charset)
	{
		text = gtk_editable_get_text (GTK_EDITABLE (edit_row_charset));
		g_free (net->encoding);
		if (text && text[0])
			net->encoding = g_strdup (text);
		else
			net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
	}

	/* Login type - already updated by signal handler */
	/* Flags - already updated by signal handlers */

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
	edit_row_netname = NULL;
	edit_row_nick = NULL;
	edit_row_nick2 = NULL;
	edit_row_user = NULL;
	edit_row_real = NULL;
	edit_row_pass = NULL;
	edit_row_login = NULL;
	edit_row_charset = NULL;
	edit_charset_dropdown = NULL;
	edit_row_use_global = NULL;
	edit_row_connect_selected = NULL;
	edit_row_auto_connect = NULL;
	edit_row_bypass_proxy = NULL;
	edit_row_use_ssl = NULL;
	edit_row_allow_invalid = NULL;
	selected_serv = NULL;
	selected_cmd = NULL;
	selected_chan = NULL;

	return FALSE;
}


static void
servlist_edit_switch_toggled_cb (AdwSwitchRow *row, GParamSpec *pspec, gpointer user_data)
{
	int flag_num;
	guint32 mask;
	gboolean active;

	(void) pspec;

	if (!selected_net)
		return;

	flag_num = GPOINTER_TO_INT (user_data);
	mask = (guint32) (1 << flag_num);
	active = adw_switch_row_get_active (row);

	/* FLAG_CYCLE and FLAG_USE_PROXY have inverted UI semantics */
	if (mask == FLAG_CYCLE || mask == FLAG_USE_PROXY)
	{
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
}

static void
servlist_edit_global_user_toggled_cb (AdwSwitchRow *row, GParamSpec *pspec, gpointer user_data)
{
	gboolean active;
	gboolean sensitive;

	(void) pspec;
	(void) user_data;

	if (!selected_net)
		return;

	active = adw_switch_row_get_active (row);
	sensitive = !active;

	if (active)
		selected_net->flags |= FLAG_USE_GLOBAL;
	else
		selected_net->flags &= ~FLAG_USE_GLOBAL;

	if (edit_row_nick)
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_nick), sensitive);
	if (edit_row_nick2)
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_nick2), sensitive);
	if (edit_row_user)
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_user), sensitive);
	if (edit_row_real)
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_real), sensitive);
}

static void
servlist_edit_login_selected_cb (AdwComboRow *row, GParamSpec *pspec, gpointer user_data)
{
	guint index;
	int length;

	(void) pspec;
	(void) user_data;

	if (!selected_net)
		return;

	index = adw_combo_row_get_selected (row);
	if (index == GTK_INVALID_LIST_POSITION)
		return;

	length = (int) (sizeof (login_types_conf) / sizeof (login_types_conf[0]));
	if ((int) index >= length)
		return;

	selected_net->logintype = login_types_conf[index];

	/* Disable password field for SASL EXTERNAL (cert-based auth) */
	if (edit_row_pass)
	{
		if (login_types_conf[index] == LOGIN_SASLEXTERNAL)
			gtk_widget_set_sensitive (GTK_WIDGET (edit_row_pass), FALSE);
		else
			gtk_widget_set_sensitive (GTK_WIDGET (edit_row_pass), TRUE);
	}
}

static void
servlist_edit_charset_entry_changed_cb (AdwEntryRow *row, gpointer user_data)
{
	const char *text;

	(void) user_data;

	if (!selected_net)
		return;

	text = gtk_editable_get_text (GTK_EDITABLE (row));
	g_free (selected_net->encoding);
	selected_net->encoding = g_strdup (text && text[0] ? text : IRC_DEFAULT_CHARSET);
}

static void
servlist_edit_charset_dropdown_selected_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	GtkDropDown *dd;
	guint index;
	GtkStringObject *str_obj;
	const char *text;

	(void) pspec;
	(void) user_data;

	dd = GTK_DROP_DOWN (object);
	index = gtk_drop_down_get_selected (dd);
	if (index == GTK_INVALID_LIST_POSITION)
		return;

	str_obj = GTK_STRING_OBJECT (gtk_drop_down_get_selected_item (dd));
	if (!str_obj)
		return;

	text = gtk_string_object_get_string (str_obj);
	if (!edit_row_charset || !text)
		return;

	gtk_editable_set_text (GTK_EDITABLE (edit_row_charset), text);
}

static GtkWidget *
servlist_open_edit (GtkWidget *parent, ircnet *net)
{
	GtkWidget *window;
	AdwPreferencesGroup *group;
	GtkWidget *list;
	GtkWidget *add_button, *remove_button, *edit_button;
	GtkBuilder *builder;
	GtkStringList *string_list;
	guint i, selected_index;
	char *title;
	int width, height;

	builder = fe_gtk4_builder_new_from_resource (SERVLIST_EDIT_UI_PATH);
	window = fe_gtk4_builder_get_widget (builder, "servlist_edit_window", ADW_TYPE_PREFERENCES_WINDOW);

	/* Get the preference groups */
	group = fe_gtk4_builder_get_widget (builder, "group_identity", ADW_TYPE_PREFERENCES_GROUP);

	/* Network Identity Group */
	edit_row_netname = ADW_ENTRY_ROW (adw_entry_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_netname), _("Network name"));
	gtk_editable_set_text (GTK_EDITABLE (edit_row_netname), net->name ? net->name : "");
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_netname));

	/* Connection Options Group */
	group = fe_gtk4_builder_get_widget (builder, "group_connection", ADW_TYPE_PREFERENCES_GROUP);

	edit_row_connect_selected = ADW_SWITCH_ROW (adw_switch_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_connect_selected), _("Connect to selected server only"));
	adw_action_row_set_subtitle (ADW_ACTION_ROW (edit_row_connect_selected), _("Don't cycle through servers when connection fails"));
	adw_switch_row_set_active (edit_row_connect_selected, !(net->flags & FLAG_CYCLE));
	g_signal_connect (edit_row_connect_selected, "notify::active", G_CALLBACK (servlist_edit_switch_toggled_cb), GINT_TO_POINTER (0));
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_connect_selected));

	edit_row_auto_connect = ADW_SWITCH_ROW (adw_switch_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_auto_connect), _("Auto-connect on startup"));
	adw_switch_row_set_active (edit_row_auto_connect, net->flags & FLAG_AUTO_CONNECT);
	g_signal_connect (edit_row_auto_connect, "notify::active", G_CALLBACK (servlist_edit_switch_toggled_cb), GINT_TO_POINTER (3));
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_auto_connect));

	edit_row_bypass_proxy = ADW_SWITCH_ROW (adw_switch_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_bypass_proxy), _("Bypass proxy server"));
	adw_switch_row_set_active (edit_row_bypass_proxy, !(net->flags & FLAG_USE_PROXY));
	g_signal_connect (edit_row_bypass_proxy, "notify::active", G_CALLBACK (servlist_edit_switch_toggled_cb), GINT_TO_POINTER (4));
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_bypass_proxy));

	/* Security Group */
	group = fe_gtk4_builder_get_widget (builder, "group_security", ADW_TYPE_PREFERENCES_GROUP);

	edit_row_use_ssl = ADW_SWITCH_ROW (adw_switch_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_use_ssl), _("Use SSL for all servers"));
	adw_switch_row_set_active (edit_row_use_ssl, net->flags & FLAG_USE_SSL);
	g_signal_connect (edit_row_use_ssl, "notify::active", G_CALLBACK (servlist_edit_switch_toggled_cb), GINT_TO_POINTER (2));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (GTK_WIDGET (edit_row_use_ssl), FALSE);
#endif
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_use_ssl));

	edit_row_allow_invalid = ADW_SWITCH_ROW (adw_switch_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_allow_invalid), _("Accept invalid SSL certificates"));
	adw_action_row_set_subtitle (ADW_ACTION_ROW (edit_row_allow_invalid), _("Only enable if you trust the server"));
	adw_switch_row_set_active (edit_row_allow_invalid, net->flags & FLAG_ALLOW_INVALID);
	g_signal_connect (edit_row_allow_invalid, "notify::active", G_CALLBACK (servlist_edit_switch_toggled_cb), GINT_TO_POINTER (5));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (GTK_WIDGET (edit_row_allow_invalid), FALSE);
#endif
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_allow_invalid));

	/* Authentication Group */
	group = fe_gtk4_builder_get_widget (builder, "group_auth", ADW_TYPE_PREFERENCES_GROUP);

	string_list = gtk_string_list_new (login_types);
	edit_row_login = ADW_COMBO_ROW (adw_combo_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_login), _("Login method"));
	adw_action_row_set_subtitle (ADW_ACTION_ROW (edit_row_login), _("How you identify to the server"));
	adw_combo_row_set_model (edit_row_login, G_LIST_MODEL (string_list));
	adw_combo_row_set_selected (edit_row_login, servlist_get_login_desc_index (net->logintype));
	g_signal_connect (edit_row_login, "notify::selected", G_CALLBACK (servlist_edit_login_selected_cb), NULL);
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_login));

	edit_row_pass = ADW_PASSWORD_ENTRY_ROW (adw_password_entry_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_pass), _("Password"));
	gtk_editable_set_text (GTK_EDITABLE (edit_row_pass), net->pass ? net->pass : "");
	if (net->logintype == LOGIN_SASLEXTERNAL)
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_pass), FALSE);
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_pass));

	/* User Information Group */
	group = fe_gtk4_builder_get_widget (builder, "group_user_info", ADW_TYPE_PREFERENCES_GROUP);

	edit_row_use_global = ADW_SWITCH_ROW (adw_switch_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_use_global), _("Use global user information"));
	adw_switch_row_set_active (edit_row_use_global, net->flags & FLAG_USE_GLOBAL);
	g_signal_connect (edit_row_use_global, "notify::active", G_CALLBACK (servlist_edit_global_user_toggled_cb), NULL);
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_use_global));

	edit_row_nick = ADW_ENTRY_ROW (adw_entry_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_nick), _("Nick name"));
	gtk_editable_set_text (GTK_EDITABLE (edit_row_nick), net->nick ? net->nick : "");
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_nick));

	edit_row_nick2 = ADW_ENTRY_ROW (adw_entry_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_nick2), _("Second choice"));
	gtk_editable_set_text (GTK_EDITABLE (edit_row_nick2), net->nick2 ? net->nick2 : "");
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_nick2));

	edit_row_real = ADW_ENTRY_ROW (adw_entry_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_real), _("Real name"));
	gtk_editable_set_text (GTK_EDITABLE (edit_row_real), net->real ? net->real : "");
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_real));

	edit_row_user = ADW_ENTRY_ROW (adw_entry_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_user), _("User name"));
	gtk_editable_set_text (GTK_EDITABLE (edit_row_user), net->user ? net->user : "");
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_user));

	/* Set initial sensitivity based on use_global flag */
	if (net->flags & FLAG_USE_GLOBAL)
	{
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_nick), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_nick2), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_user), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (edit_row_real), FALSE);
	}

	/* Advanced Group */
	group = fe_gtk4_builder_get_widget (builder, "group_advanced", ADW_TYPE_PREFERENCES_GROUP);

	edit_row_charset = ADW_ENTRY_ROW (adw_entry_row_new ());
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (edit_row_charset), _("Character encoding"));
	gtk_editable_set_text (GTK_EDITABLE (edit_row_charset), net->encoding ? net->encoding : IRC_DEFAULT_CHARSET);
	g_signal_connect (edit_row_charset, "changed", G_CALLBACK (servlist_edit_charset_entry_changed_cb), NULL);

	/* Add dropdown as suffix */
	string_list = gtk_string_list_new (pages);
	edit_charset_dropdown = GTK_DROP_DOWN (gtk_drop_down_new (G_LIST_MODEL (string_list), NULL));
	selected_index = 0;
	for (i = 0; pages[i]; i++)
	{
		if (net->encoding && g_strcmp0 (net->encoding, pages[i]) == 0)
		{
			selected_index = i;
			break;
		}
	}
	gtk_drop_down_set_selected (edit_charset_dropdown, selected_index);
	g_signal_connect (edit_charset_dropdown, "notify::selected", G_CALLBACK (servlist_edit_charset_dropdown_selected_cb), NULL);
	adw_entry_row_add_suffix (edit_row_charset, GTK_WIDGET (edit_charset_dropdown));
	adw_preferences_group_add (group, GTK_WIDGET (edit_row_charset));

	/* Get list widgets and buttons from UI */
	list = fe_gtk4_builder_get_widget (builder, "servers_list", GTK_TYPE_LIST_BOX);
	edit_lists[SERVER_TREE] = list;
	add_button = fe_gtk4_builder_get_widget (builder, "servers_add_button", GTK_TYPE_BUTTON);
	remove_button = fe_gtk4_builder_get_widget (builder, "servers_remove_button", GTK_TYPE_BUTTON);
	edit_button = fe_gtk4_builder_get_widget (builder, "servers_edit_button", GTK_TYPE_BUTTON);
	g_signal_connect (list, "row-selected", G_CALLBACK (servlist_server_row_cb), NULL);
	g_signal_connect (add_button, "clicked", G_CALLBACK (servlist_addbutton_cb), GINT_TO_POINTER (SERVER_TREE));
	g_signal_connect (remove_button, "clicked", G_CALLBACK (servlist_deletebutton_cb), GINT_TO_POINTER (SERVER_TREE));
	g_signal_connect (edit_button, "clicked", G_CALLBACK (servlist_editbutton_cb), GINT_TO_POINTER (SERVER_TREE));
	{
		GtkEventController *controller = gtk_event_controller_key_new ();
		g_signal_connect (controller, "key-pressed", G_CALLBACK (servlist_keypress_cb), GINT_TO_POINTER (SERVER_TREE));
		gtk_widget_add_controller (list, controller);
	}

	list = fe_gtk4_builder_get_widget (builder, "channels_list", GTK_TYPE_LIST_BOX);
	edit_lists[CHANNEL_TREE] = list;
	add_button = fe_gtk4_builder_get_widget (builder, "channels_add_button", GTK_TYPE_BUTTON);
	remove_button = fe_gtk4_builder_get_widget (builder, "channels_remove_button", GTK_TYPE_BUTTON);
	edit_button = fe_gtk4_builder_get_widget (builder, "channels_edit_button", GTK_TYPE_BUTTON);
	g_signal_connect (list, "row-selected", G_CALLBACK (servlist_channel_row_cb), NULL);
	g_signal_connect (add_button, "clicked", G_CALLBACK (servlist_addbutton_cb), GINT_TO_POINTER (CHANNEL_TREE));
	g_signal_connect (remove_button, "clicked", G_CALLBACK (servlist_deletebutton_cb), GINT_TO_POINTER (CHANNEL_TREE));
	g_signal_connect (edit_button, "clicked", G_CALLBACK (servlist_editbutton_cb), GINT_TO_POINTER (CHANNEL_TREE));
	{
		GtkEventController *controller = gtk_event_controller_key_new ();
		g_signal_connect (controller, "key-pressed", G_CALLBACK (servlist_keypress_cb), GINT_TO_POINTER (CHANNEL_TREE));
		gtk_widget_add_controller (list, controller);
	}

	list = fe_gtk4_builder_get_widget (builder, "commands_list", GTK_TYPE_LIST_BOX);
	edit_lists[CMD_TREE] = list;
	add_button = fe_gtk4_builder_get_widget (builder, "commands_add_button", GTK_TYPE_BUTTON);
	remove_button = fe_gtk4_builder_get_widget (builder, "commands_remove_button", GTK_TYPE_BUTTON);
	edit_button = fe_gtk4_builder_get_widget (builder, "commands_edit_button", GTK_TYPE_BUTTON);
	g_signal_connect (list, "row-selected", G_CALLBACK (servlist_command_row_cb), NULL);
	g_signal_connect (add_button, "clicked", G_CALLBACK (servlist_addbutton_cb), GINT_TO_POINTER (CMD_TREE));
	g_signal_connect (remove_button, "clicked", G_CALLBACK (servlist_deletebutton_cb), GINT_TO_POINTER (CMD_TREE));
	g_signal_connect (edit_button, "clicked", G_CALLBACK (servlist_editbutton_cb), GINT_TO_POINTER (CMD_TREE));
	{
		GtkEventController *controller = gtk_event_controller_key_new ();
		g_signal_connect (controller, "key-pressed", G_CALLBACK (servlist_keypress_cb), GINT_TO_POINTER (CMD_TREE));
		gtk_widget_add_controller (list, controller);
	}

	g_object_ref_sink (window);
	g_object_unref (builder);

	title = g_strdup_printf (_("Edit %s - %s"), net->name ? net->name : _("Network"), PACKAGE_NAME);
	gtk_window_set_title (GTK_WINDOW (window), title);
	g_free (title);
	width = netedit_win_width > 0 ? netedit_win_width : 680;
	height = netedit_win_height > 0 ? netedit_win_height : 600;
	gtk_window_set_default_size (GTK_WINDOW (window), width, height);
	gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (parent));
	gtk_window_set_modal (GTK_WINDOW (window), TRUE);

	/* Populate the lists */
	servlist_servers_populate (net, edit_lists[SERVER_TREE], NULL);
	servlist_channels_populate (net, edit_lists[CHANNEL_TREE], NULL);
	servlist_commands_populate (net, edit_lists[CMD_TREE], NULL);

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
	GtkBuilder *builder;
	GSimpleActionGroup *actions;
	GSimpleAction *sort_action;
	int width;
	int height;

	if (serverlist_win)
	{
		gtk_window_present (GTK_WINDOW (serverlist_win));
		return;
	}

	servlist_sess = sess;

	builder = fe_gtk4_builder_new_from_resource (SERVLIST_UI_PATH);
	serverlist_win = fe_gtk4_builder_get_widget (builder, "servlist_window", ADW_TYPE_WINDOW);
	entry_nick1 = ADW_ENTRY_ROW (fe_gtk4_builder_get_widget (builder, "servlist_entry_nick1", ADW_TYPE_ENTRY_ROW));
	entry_nick2 = ADW_ENTRY_ROW (fe_gtk4_builder_get_widget (builder, "servlist_entry_nick2", ADW_TYPE_ENTRY_ROW));
	entry_nick3 = ADW_ENTRY_ROW (fe_gtk4_builder_get_widget (builder, "servlist_entry_nick3", ADW_TYPE_ENTRY_ROW));
	entry_guser = ADW_ENTRY_ROW (fe_gtk4_builder_get_widget (builder, "servlist_entry_guser", ADW_TYPE_ENTRY_ROW));
	networks_search_entry = fe_gtk4_builder_get_widget (builder, "servlist_networks_search_entry", GTK_TYPE_SEARCH_ENTRY);
	networks_list = fe_gtk4_builder_get_widget (builder, "servlist_networks_list", GTK_TYPE_LIST_BOX);
	checkbutton_skip = ADW_SWITCH_ROW (fe_gtk4_builder_get_widget (builder, "servlist_check_skip", ADW_TYPE_SWITCH_ROW));
	checkbutton_fav = fe_gtk4_builder_get_widget (builder, "servlist_check_fav", GTK_TYPE_CHECK_BUTTON);
	button_connect = fe_gtk4_builder_get_widget (builder, "servlist_connect_button", GTK_TYPE_BUTTON);
	GtkWidget *button_cancel = fe_gtk4_builder_get_widget (builder, "servlist_cancel_button", GTK_TYPE_BUTTON);
	button_add_net = fe_gtk4_builder_get_widget (builder, "servlist_add_button", GTK_TYPE_BUTTON);
	button_remove_net = fe_gtk4_builder_get_widget (builder, "servlist_remove_button", GTK_TYPE_BUTTON);
	button_edit_net = fe_gtk4_builder_get_widget (builder, "servlist_edit_button", GTK_TYPE_BUTTON);
	button_favor_net = fe_gtk4_builder_get_widget (builder, "servlist_favor_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (serverlist_win);
	g_object_unref (builder);

	gtk_window_set_title (GTK_WINDOW (serverlist_win), _("Network List"));
	width = netlist_win_width > 0 ? netlist_win_width : 760;
	height = netlist_win_height > 0 ? netlist_win_height : 700;
	gtk_window_set_default_size (GTK_WINDOW (serverlist_win), width, height);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (serverlist_win), GTK_WINDOW (main_window));

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

	g_signal_connect (button_add_net, "clicked", G_CALLBACK (servlist_addnet_cb), NULL);
	g_signal_connect (button_remove_net, "clicked", G_CALLBACK (servlist_deletenet_cb), NULL);
	g_signal_connect (button_edit_net, "clicked", G_CALLBACK (servlist_edit_cb), NULL);
	g_signal_connect (button_favor_net, "clicked", G_CALLBACK (servlist_favor_cb), NULL);

	/* Create action group and add sort action for the menu */
	actions = g_simple_action_group_new ();
	sort_action = g_simple_action_new ("sort-networks", NULL);
	g_signal_connect (sort_action, "activate", G_CALLBACK (servlist_sort_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (actions), G_ACTION (sort_action));
	gtk_widget_insert_action_group (serverlist_win, "app", G_ACTION_GROUP (actions));

	adw_switch_row_set_active (checkbutton_skip, prefs.hex_gui_slist_skip);
	g_signal_connect (checkbutton_skip, "notify::active", G_CALLBACK (no_servlist), NULL);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (checkbutton_fav), prefs.hex_gui_slist_fav);
	g_signal_connect (checkbutton_fav, "toggled", G_CALLBACK (fav_servlist), NULL);

	g_signal_connect_swapped (button_cancel, "clicked", G_CALLBACK (gtk_window_close), serverlist_win);
	g_signal_connect (button_connect, "clicked", G_CALLBACK (servlist_connect_cb), NULL);

	g_signal_connect (entry_guser, "changed", G_CALLBACK (servlist_username_changed_cb), NULL);
	g_signal_connect (entry_nick1, "changed", G_CALLBACK (servlist_nick_changed_cb), NULL);
	g_signal_connect (entry_nick2, "changed", G_CALLBACK (servlist_nick_changed_cb), NULL);

	servlist_networks_populate (networks_list, network_list);
	servlist_update_actions ();

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
	button_add_net = NULL;
	button_remove_net = NULL;
	button_edit_net = NULL;
	button_favor_net = NULL;
	selected_net = NULL;
	selected_serv = NULL;
	selected_cmd = NULL;
	selected_chan = NULL;
	servlist_sess = NULL;
	memset (edit_lists, 0, sizeof (edit_lists));
	edit_row_netname = NULL;
	edit_row_nick = NULL;
	edit_row_nick2 = NULL;
	edit_row_user = NULL;
	edit_row_real = NULL;
	edit_row_pass = NULL;
	edit_row_login = NULL;
	edit_row_charset = NULL;
	edit_charset_dropdown = NULL;
	edit_row_use_global = NULL;
	edit_row_connect_selected = NULL;
	edit_row_auto_connect = NULL;
	edit_row_bypass_proxy = NULL;
	edit_row_use_ssl = NULL;
	edit_row_allow_invalid = NULL;
}

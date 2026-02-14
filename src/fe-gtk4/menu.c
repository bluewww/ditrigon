/* HexChat GTK4 menu model and window actions */
#include "fe-gtk4.h"

#define usercommands_help  _("User Commands - Special codes:\n\n" \
	"%c  =  current channel\n" \
	"%e  =  current network name\n" \
	"%m  =  machine info\n" \
	"%n  =  your nick\n" \
	"%t  =  time/date\n" \
	"%v  =  HexChat version\n" \
	"%2  =  word 2\n" \
	"%3  =  word 3\n" \
	"&2  =  word 2 to the end of line\n" \
	"&3  =  word 3 to the end of line\n\n" \
	"eg:\n" \
	"/cmd john hello\n\n" \
	"%2 would be \"john\"\n" \
	"&2 would be \"john hello\".")

#define ulbutton_help       _("Userlist Buttons - Special codes:\n\n" \
	"%a  =  all selected nicks\n" \
	"%c  =  current channel\n" \
	"%e  =  current network name\n" \
	"%h  =  selected nick's hostname\n" \
	"%m  =  machine info\n" \
	"%n  =  your nick\n" \
	"%s  =  selected nick\n" \
	"%t  =  time/date\n" \
	"%u  =  selected users account")

#define dlgbutton_help      _("Dialog Buttons - Special codes:\n\n" \
	"%a  =  all selected nicks\n" \
	"%c  =  current channel\n" \
	"%e  =  current network name\n" \
	"%h  =  selected nick's hostname\n" \
	"%m  =  machine info\n" \
	"%n  =  your nick\n" \
	"%s  =  selected nick\n" \
	"%t  =  time/date\n" \
	"%u  =  selected users account")

#define ctcp_help          _("CTCP Replies - Special codes:\n\n" \
	"%d  =  data (the whole ctcp)\n" \
	"%e  =  current network name\n" \
	"%m  =  machine info\n" \
	"%s  =  nick who sent the ctcp\n" \
	"%t  =  time/date\n" \
	"%2  =  word 2\n" \
	"%3  =  word 3\n" \
	"&2  =  word 2 to the end of line\n" \
	"&3  =  word 3 to the end of line\n\n")

#define url_help           _("URL Handlers - Special codes:\n\n" \
	"%s  =  the URL string\n\n" \
	"Putting a ! in front of the command\n" \
	"indicates it should be sent to a\n" \
	"shell instead of HexChat")

typedef struct
{
	char *path;
	char *label;
	char *cmd;
	char *ucmd;
	char *group;
	char *action_name;
	gint16 root_offset;
	gint32 pos;
	gboolean state;
	gboolean enable;
	gboolean is_main;
} HcDynamicMenuItem;

static GHashTable *dynamic_menu_items;
static GPtrArray *dynamic_action_names;
static guint dynamic_action_id;
static char *strip_mnemonic (const char *label);

static char *
strip_mnemonic (const char *label)
{
	GString *out;

	if (!label || !label[0])
		return g_strdup ("");

	out = g_string_sized_new (strlen (label));
	while (*label)
	{
		if (*label != '_')
			g_string_append_c (out, *label);
		label++;
	}

	return g_string_free (out, FALSE);
}

static void
dynamic_menu_item_free (gpointer data)
{
	HcDynamicMenuItem *item;

	item = data;
	if (!item)
		return;

	g_free (item->path);
	g_free (item->label);
	g_free (item->cmd);
	g_free (item->ucmd);
	g_free (item->group);
	g_free (item->action_name);
	g_free (item);
}

static char *
menu_entry_key (const char *path, const char *label)
{
	return g_strdup_printf ("%s\x1f%s", path ? path : "", label ? label : "");
}

static char *
menu_strip_markup (const char *label)
{
	char *text;

	if (!label)
		return NULL;

	text = NULL;
	if (!pango_parse_markup (label, -1, 0, NULL, &text, NULL, NULL))
		return NULL;
	return text;
}

static const char *
menu_item_path (const HcDynamicMenuItem *item)
{
	const char *path;

	if (!item || !item->path)
		return "";

	path = item->path;
	if (item->root_offset > 0)
	{
		gsize len;

		len = strlen (item->path);
		if ((gsize) item->root_offset >= len)
			return "";
		path = item->path + item->root_offset;
	}

	while (*path == '/')
		path++;

	return path;
}

static GMenu *
menu_ensure_path (GMenu *root, GHashTable *submenus, const char *path)
{
	GMenu *parent;
	GString *key_buf;
	const char *segment;

	parent = root;
	if (!path || !path[0])
		return parent;

	key_buf = g_string_new ("");
	segment = path;
	while (segment && segment[0])
	{
		const char *slash;
		gsize seg_len;

		slash = strchr (segment, '/');
		seg_len = slash ? (gsize) (slash - segment) : strlen (segment);

		if (seg_len > 0)
		{
			char *display;
			char *lookup_key;
			char *raw_segment;
			GMenu *submenu;

			raw_segment = g_strndup (segment, seg_len);
			if (key_buf->len > 0)
				g_string_append_c (key_buf, '/');
			g_string_append_len (key_buf, raw_segment, seg_len);

			lookup_key = g_strdup (key_buf->str);
			submenu = g_hash_table_lookup (submenus, lookup_key);
			if (!submenu)
			{
				display = strip_mnemonic (raw_segment);
				submenu = g_menu_new ();
				g_menu_append_submenu (parent, display, G_MENU_MODEL (submenu));
				g_hash_table_insert (submenus, lookup_key, submenu);
				g_free (display);
			}
			else
			{
				g_free (lookup_key);
			}

			parent = submenu;
			g_free (raw_segment);
		}

		if (!slash)
			break;
		segment = slash + 1;
	}

	g_string_free (key_buf, TRUE);
	return parent;
}

static session *
menu_target_session (void)
{
	if (current_sess && is_session (current_sess))
		return current_sess;
	if (current_tab && is_session (current_tab))
		return current_tab;
	if (sess_list)
		return sess_list->data;
	return NULL;
}

static void
dynamic_menu_set_state (HcDynamicMenuItem *item, gboolean state)
{
	GAction *action;

	if (!item)
		return;

	item->state = state ? TRUE : FALSE;

	if (!window_actions || !item->action_name)
		return;

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), item->action_name);
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (item->state));
}

static void
dynamic_menu_select_group_item (HcDynamicMenuItem *selected)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	const char *selected_path;

	if (!selected || !selected->group || !selected->group[0] || !dynamic_menu_items)
		return;

	selected_path = menu_item_path (selected);
	g_hash_table_iter_init (&iter, dynamic_menu_items);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		HcDynamicMenuItem *item = value;

		if (!item->group || g_strcmp0 (item->group, selected->group) != 0)
			continue;
		if (g_strcmp0 (menu_item_path (item), selected_path) != 0)
			continue;

		dynamic_menu_set_state (item, item == selected);
	}
}

static void
dynamic_menu_run_command (const char *cmd)
{
	char *line;
	session *sess;

	if (!cmd || !cmd[0])
		return;

	sess = menu_target_session ();
	if (!sess)
		return;

	line = g_strdup (cmd);
	handle_command (sess, line, FALSE);
	g_free (line);
}

static void
dynamic_menu_action_cb (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	const char *cmd;
	HcDynamicMenuItem *item;

	item = userdata;
	if (!item)
		return;

	cmd = item->cmd;
	if (item->group && item->group[0])
	{
		dynamic_menu_select_group_item (item);
	}
	else if (item->ucmd && item->ucmd[0])
	{
		gboolean next_state = !item->state;

		dynamic_menu_set_state (item, next_state);
		cmd = next_state ? item->cmd : item->ucmd;
	}

	(void) action;
	(void) parameter;

	dynamic_menu_run_command (cmd);
}

static gint
menu_item_pos_value (const HcDynamicMenuItem *item)
{
	if (!item)
		return G_MAXINT;
	if (item->pos == 0xffff)
		return G_MAXINT;
	if (item->pos < 0)
		return (G_MAXINT / 2) + item->pos;
	return item->pos;
}

static gint
menu_item_sort_cb (gconstpointer a, gconstpointer b)
{
	const HcDynamicMenuItem *ia = a;
	const HcDynamicMenuItem *ib = b;
	int cmp;

	cmp = g_strcmp0 (menu_item_path (ia), menu_item_path (ib));
	if (cmp != 0)
		return cmp;

	if (menu_item_pos_value (ia) < menu_item_pos_value (ib))
		return -1;
	if (menu_item_pos_value (ia) > menu_item_pos_value (ib))
		return 1;

	return g_strcmp0 (ia->label, ib->label);
}

static session *
window_target_session (void)
{
	return fe_gtk4_window_target_session ();
}

static void
win_action_quit (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;
	hexchat_exit ();
}

static void
win_action_show (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;
	if (main_window)
	{
		gtk_widget_set_visible (main_window, TRUE);
		gtk_window_present (GTK_WINDOW (main_window));
		fe_gtk4_tray_menu_emit_layout_updated (0);
	}
}

static void
win_action_hide (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;
	if (main_window)
	{
		gtk_widget_set_visible (main_window, FALSE);
		fe_gtk4_tray_menu_emit_layout_updated (0);
	}
}

static void
win_action_clear_log (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_text_clear (current_tab, 0);
}

static void
win_action_iconify (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;
	if (main_window)
		gtk_window_minimize (GTK_WINDOW (main_window));
}

static void
win_action_network_list (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (sess)
		fe_serverlist_open (sess);
}

static void
win_action_new_server (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	new_ircwindow (NULL, NULL, SESS_SERVER, 1);
}

static void
win_action_new_channel (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;
	server *serv;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (!sess || !sess->server)
		return;

	serv = sess->server;
	new_ircwindow (serv, NULL, SESS_CHANNEL, 1);
}

static void
win_action_new_server_window (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	int old_tabs;

	(void) action;
	(void) parameter;
	(void) userdata;

	old_tabs = prefs.hex_gui_tab_chans;
	prefs.hex_gui_tab_chans = 0;
	new_ircwindow (NULL, NULL, SESS_SERVER, 0);
	prefs.hex_gui_tab_chans = old_tabs;
}

static void
win_action_new_channel_window (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;
	int old_tabs;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (!sess || !sess->server)
		return;

	old_tabs = prefs.hex_gui_tab_chans;
	prefs.hex_gui_tab_chans = 0;
	new_ircwindow (sess->server, NULL, SESS_CHANNEL, 0);
	prefs.hex_gui_tab_chans = old_tabs;
}

static void
win_action_detach (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (sess)
		handle_command (sess, "GUI DETACH", FALSE);
}

static void
win_action_close (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (sess)
		handle_command (sess, "CLOSE", FALSE);
}

static void
win_action_toggle_menubar (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	gboolean visible;

	(void) action;
	(void) parameter;
	(void) userdata;

	visible = !fe_gtk4_maingui_get_menubar_visible ();
	fe_gtk4_maingui_set_menubar_visible (visible);
	save_config ();
}

static void
win_action_toggle_topicbar (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	gboolean visible;

	(void) action;
	(void) parameter;
	(void) userdata;

	visible = !fe_gtk4_topicbar_get_visible ();
	fe_gtk4_topicbar_set_visible (visible);
	save_config ();
}

static void
win_action_toggle_userlist (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	gboolean visible;

	(void) action;
	(void) parameter;
	(void) userdata;

	visible = !fe_gtk4_userlist_get_visible ();
	fe_gtk4_userlist_set_visible (visible);
	save_config ();
}

static void
win_action_toggle_userlist_buttons (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	prefs.hex_gui_ulist_buttons = prefs.hex_gui_ulist_buttons ? 0 : 1;
	save_config ();
}

static void
win_action_toggle_mode_buttons (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	prefs.hex_gui_mode_buttons = prefs.hex_gui_mode_buttons ? 0 : 1;
	save_config ();
}

static void
win_action_layout_tabs (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_gtk4_chanview_set_layout (0);
	save_config ();
}

static void
win_action_layout_tree (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_gtk4_chanview_set_layout (2);
	save_config ();
}

static void
win_action_metres_off (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	prefs.hex_gui_lagometer = 0;
	prefs.hex_gui_throttlemeter = 0;
	hexchat_reinit_timers ();
	save_config ();
}

static void
win_action_metres_graph (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	prefs.hex_gui_lagometer = 1;
	prefs.hex_gui_throttlemeter = 1;
	hexchat_reinit_timers ();
	save_config ();
}

static void
win_action_metres_text (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	prefs.hex_gui_lagometer = 2;
	prefs.hex_gui_throttlemeter = 2;
	hexchat_reinit_timers ();
	save_config ();
}

static void
win_action_metres_both (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	prefs.hex_gui_lagometer = 3;
	prefs.hex_gui_throttlemeter = 3;
	hexchat_reinit_timers ();
	save_config ();
}

static void
win_action_toggle_fullscreen (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	gboolean fullscreen;

	(void) action;
	(void) parameter;
	(void) userdata;

	fullscreen = !fe_gtk4_maingui_get_fullscreen ();
	fe_gtk4_maingui_set_fullscreen (fullscreen);
	save_config ();
}

static void
win_action_server_disconnect (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (sess)
		handle_command (sess, "DISCON", FALSE);
}

static void
win_action_server_reconnect (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (!sess)
		return;

	if (sess->server && sess->server->hostname[0])
		handle_command (sess, "RECONNECT", FALSE);
	else
		fe_serverlist_open (sess);
}

static void
win_action_server_join (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	if (!command_entry)
		return;

	gtk_editable_set_text (GTK_EDITABLE (command_entry), "/JOIN #");
	gtk_editable_set_position (GTK_EDITABLE (command_entry), -1);
	gtk_widget_grab_focus (command_entry);
}

static void
win_action_server_list (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (sess && sess->server)
		chanlist_opengui (sess->server, FALSE);
}

static void
win_action_server_away (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (!sess || !sess->server)
		return;

	handle_command (sess, sess->server->is_away ? "BACK" : "AWAY", FALSE);
}

static void
win_action_preferences (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	setup_open ();
}

static void
win_action_settings_auto_replace (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("Replace - %s"), PACKAGE_NAME);
	editlist_gui_open (_("Text"), _("Replace with"), replace_list, buf,
		"replace", "replace.conf", NULL);
}

static void
win_action_settings_usermenu (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("User menu - %s"), PACKAGE_NAME);
	editlist_gui_open (NULL, NULL, usermenu_list, buf,
		"usermenu", "usermenu.conf", NULL);
}

static void
win_action_settings_ctcp_replies (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("CTCP Replies - %s"), PACKAGE_NAME);
	editlist_gui_open (NULL, NULL, ctcp_list, buf,
		"ctcpreply", "ctcpreply.conf", (char *) ctcp_help);
}

static void
win_action_settings_dialog_buttons (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("Dialog buttons - %s"), PACKAGE_NAME);
	editlist_gui_open (NULL, NULL, dlgbutton_list, buf,
		"dlgbuttons", "dlgbuttons.conf", (char *) dlgbutton_help);
}

static void
win_action_settings_keyboard_shortcuts (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;
	key_dialog_show ();
}

static void
win_action_settings_text_events (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;
	pevent_dialog_show ();
}

static void
win_action_settings_url_handlers (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("URL Handlers - %s"), PACKAGE_NAME);
	editlist_gui_open (NULL, NULL, urlhandler_list, buf,
		"urlhandlers", "urlhandlers.conf", (char *) url_help);
}

static void
win_action_settings_user_commands (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("User Defined Commands - %s"), PACKAGE_NAME);
	editlist_gui_open (NULL, NULL, command_list, buf,
		"commands", "commands.conf", (char *) usercommands_help);
}

static void
win_action_settings_userlist_buttons (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("Userlist buttons - %s"), PACKAGE_NAME);
	editlist_gui_open (NULL, NULL, button_list, buf,
		"buttons", "buttons.conf", (char *) ulbutton_help);
}

static void
win_action_settings_userlist_popup (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	char buf[128];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (buf, sizeof (buf), _("Userlist Popup menu -  %s"), PACKAGE_NAME);
	editlist_gui_open (NULL, NULL, popup_list, buf,
		"popup", "popup.conf", (char *) ulbutton_help);
}

static void
win_action_load_plugin (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	plugingui_load ();
}

static void
win_action_plugin_list (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	plugingui_open ();
}

static void
win_action_window_ban_list (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (sess)
		banlist_opengui (sess);
}

static void
win_action_window_char_chart (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	ascii_open ();
}

static void
win_action_window_dcc_chat (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_dcc_open_chat_win (FALSE);
}

static void
win_action_window_dcc_files (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_dcc_open_recv_win (FALSE);
	fe_dcc_open_send_win (FALSE);
}

static void
win_action_window_friends (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	notify_opengui ();
}

static void
win_action_window_ignore (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	ignore_gui_open ();
}

static void
win_action_window_rawlog (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (!sess || !sess->server)
		return;

	open_rawlog (sess->server);
}

static void
win_action_window_urlgrab (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	url_opengui ();
}

static void
win_action_window_reset_marker (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_message (_("Marker line support is not implemented in GTK4 yet."), FE_MSG_INFO);
}

static void
win_action_window_move_to_marker (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_message (_("Marker line support is not implemented in GTK4 yet."), FE_MSG_INFO);
}

static void
win_action_window_copy_selection (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	GdkClipboard *clipboard;

	(void) action;
	(void) parameter;
	(void) userdata;

	if (!log_view || !log_buffer)
		return;

	clipboard = gtk_widget_get_clipboard (log_view);
	if (!clipboard)
		return;

	gtk_text_buffer_copy_clipboard (log_buffer, clipboard);
}

typedef struct
{
	char *text;
} HcSaveTextData;

static void
save_text_data_free (HcSaveTextData *data)
{
	if (!data)
		return;

	g_free (data->text);
	g_free (data);
}

static void
win_action_window_save_text_cb (void *userdata, char *file)
{
	HcSaveTextData *data;
	GError *error;

	data = userdata;
	if (!data)
		return;

	if (!file || !file[0])
	{
		save_text_data_free (data);
		return;
	}

	error = NULL;
	if (!g_file_set_contents (file, data->text ? data->text : "", -1, &error))
	{
		if (error)
		{
			fe_message (error->message, FE_MSG_ERROR);
			g_error_free (error);
		}
	}

	save_text_data_free (data);
}

static void
win_action_window_save_text (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	HcSaveTextData *data;
	session *sess;

	(void) action;
	(void) parameter;
	(void) userdata;

	sess = window_target_session ();
	if (!sess)
		return;

	data = g_new0 (HcSaveTextData, 1);
	data->text = g_strdup (fe_gtk4_xtext_get_session_text (sess));
	fe_get_file (_("Select an output filename"), NULL,
		win_action_window_save_text_cb, data, FRF_WRITE);
}

static void
win_action_window_search (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_gtk4_xtext_search_prompt ();
}

static void
win_action_window_search_next (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_gtk4_xtext_search_next ();
}

static void
win_action_window_search_prev (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_gtk4_xtext_search_prev ();
}

static void
win_action_help_contents (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	(void) action;
	(void) parameter;
	(void) userdata;

	fe_open_url ("https://hexchat.readthedocs.io/");
}

static void
win_action_help_about (GSimpleAction *action, GVariant *parameter, gpointer userdata)
{
	GtkAlertDialog *dialog;
	char detail[256];

	(void) action;
	(void) parameter;
	(void) userdata;

	g_snprintf (detail, sizeof (detail), "%s %s\n%s", PACKAGE_NAME, PACKAGE_VERSION,
		_("GTK4 staged frontend"));
	dialog = gtk_alert_dialog_new (PACKAGE_NAME);
	gtk_alert_dialog_set_detail (dialog, detail);
	gtk_alert_dialog_show (dialog, GTK_WINDOW (main_window));
	g_object_unref (dialog);
}

static void
remove_dynamic_actions (void)
{
	guint i;

	if (!window_actions || !dynamic_action_names)
		return;

	for (i = 0; i < dynamic_action_names->len; i++)
	{
		const char *name = g_ptr_array_index (dynamic_action_names, i);
		g_action_map_remove_action (G_ACTION_MAP (window_actions), name);
	}
	g_ptr_array_set_size (dynamic_action_names, 0);
}

void
fe_gtk4_menu_sync_actions (void)
{
	GAction *action;
	session *sess;
	gboolean have_sess;
	gboolean have_server;
	gboolean connected;

	if (!window_actions)
		return;

	sess = window_target_session ();
	have_sess = sess && is_session (sess);
	have_server = have_sess && sess->server;
	connected = have_server && sess->server->connected;

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "close");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_sess);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "detach");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_sess);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "new-channel");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_server);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "new-channel-window");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_server);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "server-disconnect");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), connected);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "server-reconnect");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_server);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "server-join");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_server);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "server-list");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_server);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "server-away");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), connected);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "window-ban-list");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_sess);

	action = g_action_map_lookup_action (G_ACTION_MAP (window_actions), "window-rawlog");
	if (action && G_IS_SIMPLE_ACTION (action))
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), have_server);
}

void
fe_gtk4_rebuild_menu_bar (void)
{
	GMenu *root;
	GMenu *hexchat_menu;
	GMenu *new_menu;
	GMenu *view_menu;
	GMenu *chan_switch_menu;
	GMenu *meter_menu;
	GMenu *server_menu;
	GMenu *usermenu_menu;
	GMenu *settings_menu;
	GMenu *window_menu;
	GMenu *search_menu;
	GMenu *help_menu;
	GHashTable *submenus;
	GList *values;
	GList *cur;
	GSimpleAction *action;

	if (!main_box || !window_actions || !dynamic_menu_items)
		return;

	root = g_menu_new ();

	hexchat_menu = g_menu_new ();
	g_menu_append (hexchat_menu, _("Network List"), "win.network-list");
	new_menu = g_menu_new ();
	g_menu_append (new_menu, _("Server Tab"), "win.new-server");
	g_menu_append (new_menu, _("Channel Tab"), "win.new-channel");
	g_menu_append (new_menu, _("Server Window"), "win.new-server-window");
	g_menu_append (new_menu, _("Channel Window"), "win.new-channel-window");
	g_menu_append_submenu (hexchat_menu, _("New"), G_MENU_MODEL (new_menu));
	g_object_unref (new_menu);
	g_menu_append (hexchat_menu, _("Load Plugin or Script..."), "win.load-plugin");
	g_menu_append (hexchat_menu, _("Detach"), "win.detach");
	g_menu_append (hexchat_menu, _("Close"), "win.close");
	g_menu_append (hexchat_menu, _("Plugins and Scripts"), "win.plugin-list");
	g_menu_append (hexchat_menu, _("Quit"), "win.quit");
	g_menu_append_submenu (root, _("HexChat"), G_MENU_MODEL (hexchat_menu));
	g_object_unref (hexchat_menu);

	view_menu = g_menu_new ();
	g_menu_append (view_menu, _("Menu Bar"), "win.toggle-menubar");
	g_menu_append (view_menu, _("Topic Bar"), "win.toggle-topicbar");
	g_menu_append (view_menu, _("User List"), "win.toggle-userlist");
	g_menu_append (view_menu, _("User List Buttons"), "win.toggle-userlist-buttons");
	g_menu_append (view_menu, _("Mode Buttons"), "win.toggle-mode-buttons");

	chan_switch_menu = g_menu_new ();
	g_menu_append (chan_switch_menu, _("Tabs"), "win.layout-tabs");
	g_menu_append (chan_switch_menu, _("Tree"), "win.layout-tree");
	g_menu_append_submenu (view_menu, _("Channel Switcher"), G_MENU_MODEL (chan_switch_menu));
	g_object_unref (chan_switch_menu);

	meter_menu = g_menu_new ();
	g_menu_append (meter_menu, _("Off"), "win.metres-off");
	g_menu_append (meter_menu, _("Graph"), "win.metres-graph");
	g_menu_append (meter_menu, _("Text"), "win.metres-text");
	g_menu_append (meter_menu, _("Both"), "win.metres-both");
	g_menu_append_submenu (view_menu, _("Network Meters"), G_MENU_MODEL (meter_menu));
	g_object_unref (meter_menu);

	g_menu_append (view_menu, _("Fullscreen"), "win.toggle-fullscreen");
	g_menu_append_submenu (root, _("View"), G_MENU_MODEL (view_menu));
	g_object_unref (view_menu);

	server_menu = g_menu_new ();
	g_menu_append (server_menu, _("Disconnect"), "win.server-disconnect");
	g_menu_append (server_menu, _("Reconnect"), "win.server-reconnect");
	g_menu_append (server_menu, _("Join a Channel..."), "win.server-join");
	g_menu_append (server_menu, _("Channel List"), "win.server-list");
	g_menu_append (server_menu, _("Marked Away"), "win.server-away");
	g_menu_append_submenu (root, _("Server"), G_MENU_MODEL (server_menu));
	g_object_unref (server_menu);

	usermenu_menu = g_menu_new ();
	g_menu_append (usermenu_menu, _("Edit This Menu..."), "win.settings-usermenu");
	g_menu_append_submenu (root, _("Usermenu"), G_MENU_MODEL (usermenu_menu));
	g_object_unref (usermenu_menu);

	settings_menu = g_menu_new ();
	g_menu_append (settings_menu, _("Preferences"), "win.preferences");
	g_menu_append (settings_menu, _("Auto Replace"), "win.settings-auto-replace");
	g_menu_append (settings_menu, _("CTCP Replies"), "win.settings-ctcp-replies");
	g_menu_append (settings_menu, _("Dialog Buttons"), "win.settings-dialog-buttons");
	g_menu_append (settings_menu, _("Keyboard Shortcuts"), "win.settings-keyboard-shortcuts");
	g_menu_append (settings_menu, _("Text Events"), "win.settings-text-events");
	g_menu_append (settings_menu, _("URL Handlers"), "win.settings-url-handlers");
	g_menu_append (settings_menu, _("User Commands"), "win.settings-user-commands");
	g_menu_append (settings_menu, _("User List Buttons"), "win.settings-userlist-buttons");
	g_menu_append (settings_menu, _("User List Popup"), "win.settings-userlist-popup");
	g_menu_append_submenu (root, _("Settings"), G_MENU_MODEL (settings_menu));
	g_object_unref (settings_menu);

	window_menu = g_menu_new ();
	g_menu_append (window_menu, _("Show Window"), "win.show-window");
	g_menu_append (window_menu, _("Hide Window"), "win.hide-window");
	g_menu_append (window_menu, _("Ban List"), "win.window-ban-list");
	g_menu_append (window_menu, _("Character Chart"), "win.window-char-chart");
	g_menu_append (window_menu, _("Direct Chat"), "win.window-dcc-chat");
	g_menu_append (window_menu, _("File Transfers"), "win.window-dcc-files");
	g_menu_append (window_menu, _("Friends List"), "win.window-friends");
	g_menu_append (window_menu, _("Ignore List"), "win.window-ignore");
	g_menu_append (window_menu, _("Plugins and Scripts"), "win.plugin-list");
	g_menu_append (window_menu, _("Raw Log"), "win.window-rawlog");
	g_menu_append (window_menu, _("URL Grabber"), "win.window-urlgrab");
	g_menu_append (window_menu, _("Reset Marker Line"), "win.window-reset-marker");
	g_menu_append (window_menu, _("Move to Marker Line"), "win.window-move-marker");
	g_menu_append (window_menu, _("Copy Selection"), "win.window-copy-selection");
	g_menu_append (window_menu, _("Clear Text"), "win.clear-log");
	g_menu_append (window_menu, _("Save Text..."), "win.window-save-text");
	search_menu = g_menu_new ();
	g_menu_append (search_menu, _("Search Text..."), "win.window-search");
	g_menu_append (search_menu, _("Search Next"), "win.window-search-next");
	g_menu_append (search_menu, _("Search Previous"), "win.window-search-prev");
	g_menu_append_submenu (window_menu, _("Search"), G_MENU_MODEL (search_menu));
	g_object_unref (search_menu);
	g_menu_append_submenu (root, _("Window"), G_MENU_MODEL (window_menu));
	g_object_unref (window_menu);

	help_menu = g_menu_new ();
	g_menu_append (help_menu, _("Contents"), "win.help-contents");
	g_menu_append (help_menu, _("About"), "win.help-about");
	g_menu_append_submenu (root, _("Help"), G_MENU_MODEL (help_menu));
	g_object_unref (help_menu);

	remove_dynamic_actions ();
	submenus = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	values = g_hash_table_get_values (dynamic_menu_items);
	values = g_list_sort (values, menu_item_sort_cb);
	for (cur = values; cur; cur = cur->next)
	{
		char *detailed;
		char *display_label;
		HcDynamicMenuItem *item;
		GMenu *parent;
		const char *path;

		item = cur->data;
		if (!item->is_main)
			continue;

		path = menu_item_path (item);
		parent = menu_ensure_path (root, submenus, path);
		if (!parent)
			continue;

		if (!item->label || !item->label[0])
		{
			/* Separators are represented by section boundaries in GMenu. */
			GMenu *section = g_menu_new ();
			g_menu_append_section (parent, NULL, G_MENU_MODEL (section));
			g_object_unref (section);
			continue;
		}

		if ((!item->cmd || !item->cmd[0]) &&
			 (!item->ucmd || !item->ucmd[0]) &&
			 (!item->group || !item->group[0]))
		{
			char *subpath;

			if (path && path[0])
				subpath = g_strdup_printf ("%s/%s", path, item->label);
			else
				subpath = g_strdup (item->label);
			menu_ensure_path (root, submenus, subpath);
			g_free (subpath);
			continue;
		}

		if ((item->ucmd && item->ucmd[0]) || (item->group && item->group[0]))
			action = g_simple_action_new_stateful (item->action_name, NULL,
				g_variant_new_boolean (item->state));
		else
			action = g_simple_action_new (item->action_name, NULL);

		g_simple_action_set_enabled (action, item->enable);
		g_signal_connect (action, "activate", G_CALLBACK (dynamic_menu_action_cb), item);
		g_action_map_add_action (G_ACTION_MAP (window_actions), G_ACTION (action));
		g_object_unref (action);
		g_ptr_array_add (dynamic_action_names, g_strdup (item->action_name));

		detailed = g_strdup_printf ("win.%s", item->action_name);
		display_label = strip_mnemonic (item->label);
		g_menu_append (parent, display_label, detailed);
		g_free (display_label);
		g_free (detailed);
	}

	g_list_free (values);
	g_hash_table_unref (submenus);

	if (menu_bar)
	{
		fe_gtk4_adw_detach_menu_bar (menu_bar);
		menu_bar = NULL;
	}

	menu_bar = gtk_popover_menu_bar_new_from_model (G_MENU_MODEL (root));
	fe_gtk4_adw_attach_menu_bar (menu_bar);
	g_object_unref (root);
	fe_gtk4_menu_sync_actions ();
}


void
fe_menu_del (menu_entry *me)
{
	char *key;

	if (!dynamic_menu_items || !me || !me->is_main)
		return;

	key = menu_entry_key (me->path, me->label);
	g_hash_table_remove (dynamic_menu_items, key);
	g_free (key);
	fe_gtk4_rebuild_menu_bar ();
}

char *
fe_menu_add (menu_entry *me)
{
	char *clean_label;
	const char *label_for_key;
	HcDynamicMenuItem *item;
	char *key;

	if (!me)
		return NULL;

	clean_label = NULL;
	if (me->markup)
		clean_label = menu_strip_markup (me->label);
	label_for_key = clean_label ? clean_label : me->label;

	if (!dynamic_menu_items || !me->is_main)
		return clean_label;

	key = menu_entry_key (me->path, label_for_key);
	item = g_hash_table_lookup (dynamic_menu_items, key);
	if (!item)
	{
		item = g_new0 (HcDynamicMenuItem, 1);
		item->path = g_strdup (me->path);
		item->label = g_strdup (label_for_key);
		item->cmd = g_strdup (me->cmd);
		item->ucmd = g_strdup (me->ucmd);
		item->group = g_strdup (me->group);
		item->state = me->state;
		item->pos = me->pos;
		item->root_offset = me->root_offset;
		item->enable = me->enable;
		item->is_main = me->is_main;
		item->action_name = g_strdup_printf ("menu-item-%u", ++dynamic_action_id);
		g_hash_table_insert (dynamic_menu_items, g_strdup (key), item);
	}
	else
	{
		g_free (item->path);
		g_free (item->label);
		g_free (item->cmd);
		g_free (item->ucmd);
		g_free (item->group);
		item->path = g_strdup (me->path);
		item->label = g_strdup (label_for_key);
		item->cmd = g_strdup (me->cmd);
		item->ucmd = g_strdup (me->ucmd);
		item->group = g_strdup (me->group);
		item->state = me->state;
		item->pos = me->pos;
		item->root_offset = me->root_offset;
		item->enable = me->enable;
		item->is_main = me->is_main;
	}

	g_free (key);
	fe_gtk4_rebuild_menu_bar ();
	return clean_label;
}

void
fe_menu_update (menu_entry *me)
{
	char *key;
	HcDynamicMenuItem *item;

	if (!dynamic_menu_items || !me || !me->is_main)
		return;

	key = menu_entry_key (me->path, me->label);
	item = g_hash_table_lookup (dynamic_menu_items, key);
	if (item)
	{
		g_free (item->cmd);
		g_free (item->ucmd);
		g_free (item->group);
		item->cmd = g_strdup (me->cmd);
		item->ucmd = g_strdup (me->ucmd);
		item->group = g_strdup (me->group);
		item->state = me->state;
		item->pos = me->pos;
		item->root_offset = me->root_offset;
		item->enable = me->enable;
	}
	g_free (key);
	fe_gtk4_rebuild_menu_bar ();
}

void
fe_gtk4_menu_register_actions (void)
{
	static const GActionEntry win_actions[] =
	{
		{ "quit", win_action_quit, NULL, NULL, NULL },
		{ "network-list", win_action_network_list, NULL, NULL, NULL },
		{ "new-server", win_action_new_server, NULL, NULL, NULL },
		{ "new-channel", win_action_new_channel, NULL, NULL, NULL },
		{ "new-server-window", win_action_new_server_window, NULL, NULL, NULL },
		{ "new-channel-window", win_action_new_channel_window, NULL, NULL, NULL },
		{ "detach", win_action_detach, NULL, NULL, NULL },
		{ "close", win_action_close, NULL, NULL, NULL },
		{ "load-plugin", win_action_load_plugin, NULL, NULL, NULL },
		{ "plugin-list", win_action_plugin_list, NULL, NULL, NULL },
		{ "toggle-menubar", win_action_toggle_menubar, NULL, NULL, NULL },
		{ "toggle-topicbar", win_action_toggle_topicbar, NULL, NULL, NULL },
		{ "toggle-userlist", win_action_toggle_userlist, NULL, NULL, NULL },
		{ "toggle-userlist-buttons", win_action_toggle_userlist_buttons, NULL, NULL, NULL },
		{ "toggle-mode-buttons", win_action_toggle_mode_buttons, NULL, NULL, NULL },
		{ "layout-tabs", win_action_layout_tabs, NULL, NULL, NULL },
		{ "layout-tree", win_action_layout_tree, NULL, NULL, NULL },
		{ "metres-off", win_action_metres_off, NULL, NULL, NULL },
		{ "metres-graph", win_action_metres_graph, NULL, NULL, NULL },
		{ "metres-text", win_action_metres_text, NULL, NULL, NULL },
		{ "metres-both", win_action_metres_both, NULL, NULL, NULL },
		{ "toggle-fullscreen", win_action_toggle_fullscreen, NULL, NULL, NULL },
		{ "show-window", win_action_show, NULL, NULL, NULL },
		{ "hide-window", win_action_hide, NULL, NULL, NULL },
		{ "iconify", win_action_iconify, NULL, NULL, NULL },
		{ "clear-log", win_action_clear_log, NULL, NULL, NULL },
		{ "server-disconnect", win_action_server_disconnect, NULL, NULL, NULL },
		{ "server-reconnect", win_action_server_reconnect, NULL, NULL, NULL },
		{ "server-join", win_action_server_join, NULL, NULL, NULL },
		{ "server-list", win_action_server_list, NULL, NULL, NULL },
		{ "server-away", win_action_server_away, NULL, NULL, NULL },
		{ "preferences", win_action_preferences, NULL, NULL, NULL },
		{ "settings-usermenu", win_action_settings_usermenu, NULL, NULL, NULL },
		{ "settings-auto-replace", win_action_settings_auto_replace, NULL, NULL, NULL },
		{ "settings-ctcp-replies", win_action_settings_ctcp_replies, NULL, NULL, NULL },
		{ "settings-dialog-buttons", win_action_settings_dialog_buttons, NULL, NULL, NULL },
		{ "settings-keyboard-shortcuts", win_action_settings_keyboard_shortcuts, NULL, NULL, NULL },
		{ "settings-text-events", win_action_settings_text_events, NULL, NULL, NULL },
		{ "settings-url-handlers", win_action_settings_url_handlers, NULL, NULL, NULL },
		{ "settings-user-commands", win_action_settings_user_commands, NULL, NULL, NULL },
		{ "settings-userlist-buttons", win_action_settings_userlist_buttons, NULL, NULL, NULL },
		{ "settings-userlist-popup", win_action_settings_userlist_popup, NULL, NULL, NULL },
		{ "window-ban-list", win_action_window_ban_list, NULL, NULL, NULL },
		{ "window-char-chart", win_action_window_char_chart, NULL, NULL, NULL },
		{ "window-dcc-chat", win_action_window_dcc_chat, NULL, NULL, NULL },
		{ "window-dcc-files", win_action_window_dcc_files, NULL, NULL, NULL },
		{ "window-friends", win_action_window_friends, NULL, NULL, NULL },
		{ "window-ignore", win_action_window_ignore, NULL, NULL, NULL },
		{ "window-rawlog", win_action_window_rawlog, NULL, NULL, NULL },
		{ "window-urlgrab", win_action_window_urlgrab, NULL, NULL, NULL },
		{ "window-reset-marker", win_action_window_reset_marker, NULL, NULL, NULL },
		{ "window-move-marker", win_action_window_move_to_marker, NULL, NULL, NULL },
		{ "window-copy-selection", win_action_window_copy_selection, NULL, NULL, NULL },
		{ "window-save-text", win_action_window_save_text, NULL, NULL, NULL },
		{ "window-search", win_action_window_search, NULL, NULL, NULL },
		{ "window-search-next", win_action_window_search_next, NULL, NULL, NULL },
		{ "window-search-prev", win_action_window_search_prev, NULL, NULL, NULL },
		{ "help-contents", win_action_help_contents, NULL, NULL, NULL },
		{ "help-about", win_action_help_about, NULL, NULL, NULL },
	};

	if (!window_actions)
		return;

	g_action_map_add_action_entries (G_ACTION_MAP (window_actions), win_actions,
		G_N_ELEMENTS (win_actions), NULL);
}

void
fe_gtk4_menu_init (void)
{
	if (!dynamic_menu_items)
		dynamic_menu_items = g_hash_table_new_full (g_str_hash, g_str_equal,
			g_free, dynamic_menu_item_free);
	if (!dynamic_action_names)
		dynamic_action_names = g_ptr_array_new_with_free_func (g_free);
}

void
fe_gtk4_menu_cleanup (void)
{
	if (dynamic_menu_items)
	{
		g_hash_table_unref (dynamic_menu_items);
		dynamic_menu_items = NULL;
	}

	if (dynamic_action_names)
	{
		g_ptr_array_unref (dynamic_action_names);
		dynamic_action_names = NULL;
	}

	dynamic_action_id = 0;
	fe_gtk4_ascii_cleanup ();
	fe_gtk4_banlist_cleanup ();
	fe_gtk4_dccgui_cleanup ();
	fe_gtk4_ignoregui_cleanup ();
	fe_gtk4_notifygui_cleanup ();
	fe_gtk4_rawlog_cleanup ();
	fe_gtk4_urlgrab_cleanup ();
	fe_gtk4_joind_cleanup ();
	fe_gtk4_setup_cleanup ();
	fe_gtk4_servlistgui_cleanup ();
	fe_gtk4_chanlist_cleanup ();
}

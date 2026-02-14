/* HexChat GTK4 setup/preferences window */
#include "fe-gtk4.h"
#include "sexy-spell-entry.h"
#ifdef USE_LIBADWAITA
#include <adwaita.h>
#endif

enum
{
	ST_END,
	ST_TOGGLE,
	ST_ENTRY,
	ST_FONT,
	ST_NUMBER,
	ST_MENU,
	ST_HEADER,
	ST_LABEL
};

enum
{
	SET_FLAG_NONE = 0,
	SET_FLAG_INVERT = 1 << 0,
	SET_FLAG_PASSWORD = 1 << 1
};

typedef struct
{
	int type;
	const char *label;
	int offset;
	const char *tooltip;
	const char *const *list;
	int extra;
	int flags;
	void (*apply)(void);
} setting;

typedef struct
{
	const char *title;
	const setting *settings;
} setting_page;

#define setup_get_str(set) (((char *)&prefs) + (set)->offset)
#define setup_get_int(set) (*(((int *)&prefs) + (set)->offset))
#define setup_set_int(set, value) (*(((int *)&prefs) + (set)->offset) = (value))

static GtkWidget *prefs_window;

static void
setup_apply_menu_visibility (void)
{
	if (menu_bar)
		gtk_widget_set_visible (menu_bar, prefs.hex_gui_hide_menu ? FALSE : TRUE);
}

static void
setup_apply_input_spell (void)
{
	if (command_entry)
		sexy_spell_entry_set_checked (SEXY_SPELL_ENTRY (command_entry), prefs.hex_gui_input_spell);
}

static void
setup_apply_input_attr (void)
{
	if (command_entry)
		sexy_spell_entry_set_parse_attributes (SEXY_SPELL_ENTRY (command_entry), prefs.hex_gui_input_attr);
}

static void
setup_apply_input_style (void)
{
	fe_gtk4_maingui_apply_input_font ();
}

static void
setup_apply_text_font (void)
{
	fe_gtk4_xtext_apply_prefs ();
	fe_gtk4_maingui_apply_input_font ();
}

static const char *const tabcompmenu[] =
{
	N_("A-Z"),
	N_("Last-spoke order"),
	NULL
};

static const char *const lagmenutext[] =
{
	N_("Off"),
	N_("Graphical"),
	N_("Text"),
	N_("Both"),
	NULL
};

static const char *const ulmenutext[] =
{
	N_("A-Z, ops first"),
	N_("A-Z"),
	N_("Z-A, ops last"),
	N_("Z-A"),
	N_("Unsorted"),
	NULL
};

static const char *const cspos[] =
{
	N_("Left (upper)"),
	N_("Left (lower)"),
	N_("Right (upper)"),
	N_("Right (lower)"),
	N_("Top"),
	N_("Bottom"),
	N_("Hidden"),
	NULL
};

static const char *const ulpos[] =
{
	N_("Left (upper)"),
	N_("Left (lower)"),
	N_("Right (upper)"),
	N_("Right (lower)"),
	NULL
};

static const char *const tabwin[] =
{
	N_("Windows"),
	N_("Tabs"),
	NULL
};

static const char *const focusnewtabsmenu[] =
{
	N_("Never"),
	N_("Always"),
	N_("Only requested tabs"),
	NULL
};

static const char *const noticeposmenu[] =
{
	N_("Automatic"),
	N_("In an extra tab"),
	N_("In the front tab"),
	NULL
};

static const char *const proxytypes[] =
{
	N_("(Disabled)"),
	N_("Wingate"),
	N_("SOCKS4"),
	N_("SOCKS5"),
	N_("HTTP"),
	N_("Auto"),
	NULL
};

static const char *const proxyuse[] =
{
	N_("All connections"),
	N_("IRC server only"),
	N_("DCC only"),
	NULL
};

static const char *const dccaccept[] =
{
	N_("Ask for confirmation"),
	N_("Ask for download folder"),
	N_("Save without interaction"),
	NULL
};

static const char *const bantypemenu[] =
{
	N_("*!*@*.host"),
	N_("*!*@domain"),
	N_("*!*user@*.host"),
	N_("*!*user@domain"),
	NULL
};

static const setting appearance_settings[] =
{
	{ST_HEADER, N_("General")},
	{ST_TOGGLE, N_("Show menu bar"), P_OFFINTNL (hex_gui_hide_menu), NULL, NULL, 0,
		SET_FLAG_INVERT, setup_apply_menu_visibility},
	{ST_FONT, N_("Font:"), P_OFFSETNL (hex_text_font), NULL, NULL, sizeof (prefs.hex_text_font),
		SET_FLAG_NONE, setup_apply_text_font},

	{ST_HEADER, N_("Text Box")},
	{ST_TOGGLE, N_("Colored nick names"), P_OFFINTNL (hex_text_color_nicks)},
	{ST_TOGGLE, N_("Indent nick names"), P_OFFINTNL (hex_text_indent)},
	{ST_TOGGLE, N_("Show marker line"), P_OFFINTNL (hex_text_show_marker)},
	{ST_ENTRY, N_("Background image:"), P_OFFSETNL (hex_text_background), NULL, NULL, sizeof (prefs.hex_text_background)},

	{ST_HEADER, N_("Timestamps")},
	{ST_TOGGLE, N_("Enable timestamps"), P_OFFINTNL (hex_stamp_text)},
	{ST_ENTRY, N_("Timestamp format:"), P_OFFSETNL (hex_stamp_text_format), NULL, NULL, sizeof (prefs.hex_stamp_text_format)},

	{ST_HEADER, N_("Title Bar")},
	{ST_TOGGLE, N_("Show channel modes"), P_OFFINTNL (hex_gui_win_modes)},
	{ST_TOGGLE, N_("Show number of users"), P_OFFINTNL (hex_gui_win_ucount)},
	{ST_TOGGLE, N_("Show nickname"), P_OFFINTNL (hex_gui_win_nick)},
	{ST_END}
};

static const setting inputbox_settings[] =
{
	{ST_HEADER, N_("Input Box")},
	{ST_TOGGLE, N_("Use the text box font and colors"), P_OFFINTNL (hex_gui_input_style), NULL, NULL, 0,
		SET_FLAG_NONE, setup_apply_input_style},
	{ST_TOGGLE, N_("Render colors and attributes"), P_OFFINTNL (hex_gui_input_attr), NULL, NULL, 0,
		SET_FLAG_NONE, setup_apply_input_attr},
	{ST_TOGGLE, N_("Show nick box"), P_OFFINTNL (hex_gui_input_nick)},
	{ST_TOGGLE, N_("Show user mode icon in nick box"), P_OFFINTNL (hex_gui_input_icon)},
	{ST_TOGGLE, N_("Spell checking"), P_OFFINTNL (hex_gui_input_spell), NULL, NULL, 0,
		SET_FLAG_NONE, setup_apply_input_spell},
	{ST_ENTRY, N_("Dictionaries to use:"), P_OFFSETNL (hex_text_spell_langs), NULL, NULL,
		sizeof (prefs.hex_text_spell_langs)},
	{ST_LABEL, N_("Use language codes. Separate multiple entries with commas.")},

	{ST_HEADER, N_("Nick Completion")},
	{ST_ENTRY, N_("Nick completion suffix:"), P_OFFSETNL (hex_completion_suffix), NULL, NULL,
		sizeof (prefs.hex_completion_suffix)},
	{ST_MENU, N_("Nick completion sorted:"), P_OFFINTNL (hex_completion_sort), NULL, tabcompmenu, 0},
	{ST_NUMBER, N_("Nick completion amount:"), P_OFFINTNL (hex_completion_amount),
		N_("Threshold of nicks to start listing instead of completing"), NULL, 1000},
	{ST_END}
};

static const setting userlist_settings[] =
{
	{ST_HEADER, N_("User List")},
	{ST_TOGGLE, N_("Show hostnames in user list"), P_OFFINTNL (hex_gui_ulist_show_hosts)},
	{ST_TOGGLE, N_("Use the Text box font and colors"), P_OFFINTNL (hex_gui_ulist_style)},
	{ST_TOGGLE, N_("Show icons for user modes"), P_OFFINTNL (hex_gui_ulist_icons)},
	{ST_TOGGLE, N_("Color nicknames in userlist"), P_OFFINTNL (hex_gui_ulist_color)},
	{ST_TOGGLE, N_("Show user count in channels"), P_OFFINTNL (hex_gui_ulist_count)},
	{ST_MENU, N_("User list sorted by:"), P_OFFINTNL (hex_gui_ulist_sort), NULL, ulmenutext, 0},
	{ST_MENU, N_("Show user list at:"), P_OFFINTNL (hex_gui_ulist_pos), NULL, ulpos, 1},

	{ST_HEADER, N_("Away Tracking")},
	{ST_TOGGLE, N_("Track away status"), P_OFFINTNL (hex_away_track)},
	{ST_NUMBER, N_("On channels smaller than:"), P_OFFINTNL (hex_away_size_max), NULL, NULL, 10000},

	{ST_HEADER, N_("Action Upon Double Click")},
	{ST_ENTRY, N_("Execute command:"), P_OFFSETNL (hex_gui_ulist_doubleclick), NULL, NULL,
		sizeof (prefs.hex_gui_ulist_doubleclick)},

	{ST_HEADER, N_("Extra Gadgets")},
	{ST_MENU, N_("Lag meter:"), P_OFFINTNL (hex_gui_lagometer), NULL, lagmenutext, 0},
	{ST_MENU, N_("Throttle meter:"), P_OFFINTNL (hex_gui_throttlemeter), NULL, lagmenutext, 0},
	{ST_END}
};

static const setting tabs_settings[] =
{
	{ST_HEADER, N_("Channel Switcher")},
	{ST_TOGGLE, N_("Open an extra tab for server messages"), P_OFFINTNL (hex_gui_tab_server)},
	{ST_TOGGLE, N_("Open private messages in new tabs"), P_OFFINTNL (hex_gui_autoopen_dialog)},
	{ST_TOGGLE, N_("Sort tabs in alphabetical order"), P_OFFINTNL (hex_gui_tab_sort)},
	{ST_TOGGLE, N_("Show icons in the channel tree"), P_OFFINTNL (hex_gui_tab_icons)},
	{ST_TOGGLE, N_("Show dotted lines in the channel tree"), P_OFFINTNL (hex_gui_tab_dots)},
	{ST_TOGGLE, N_("Scroll mouse-wheel to change tabs"), P_OFFINTNL (hex_gui_tab_scrollchans)},
	{ST_TOGGLE, N_("Middle click to close tab"), P_OFFINTNL (hex_gui_tab_middleclose)},
	{ST_TOGGLE, N_("Smaller text"), P_OFFINTNL (hex_gui_tab_small)},
	{ST_MENU, N_("Focus new tabs:"), P_OFFINTNL (hex_gui_tab_newtofront), NULL, focusnewtabsmenu, 0},
	{ST_MENU, N_("Placement of notices:"), P_OFFINTNL (hex_irc_notice_pos), NULL, noticeposmenu, 0},
	{ST_MENU, N_("Show channel switcher at:"), P_OFFINTNL (hex_gui_tab_pos), NULL, cspos, 1},
	{ST_NUMBER, N_("Shorten tab labels to:"), P_OFFINTNL (hex_gui_tab_trunc), NULL, NULL, 99},

	{ST_HEADER, N_("Tabs or Windows")},
	{ST_MENU, N_("Open channels in:"), P_OFFINTNL (hex_gui_tab_chans), NULL, tabwin, 0},
	{ST_MENU, N_("Open dialogs in:"), P_OFFINTNL (hex_gui_tab_dialogs), NULL, tabwin, 0},
	{ST_MENU, N_("Open utilities in:"), P_OFFINTNL (hex_gui_tab_utils), NULL, tabwin, 0},
	{ST_END}
};

static const setting general_settings[] =
{
	{ST_HEADER, N_("Default Messages")},
	{ST_ENTRY, N_("Quit:"), P_OFFSETNL (hex_irc_quit_reason), NULL, NULL, sizeof (prefs.hex_irc_quit_reason)},
	{ST_ENTRY, N_("Leave channel:"), P_OFFSETNL (hex_irc_part_reason), NULL, NULL, sizeof (prefs.hex_irc_part_reason)},
	{ST_ENTRY, N_("Away:"), P_OFFSETNL (hex_away_reason), NULL, NULL, sizeof (prefs.hex_away_reason)},

	{ST_HEADER, N_("Away")},
	{ST_TOGGLE, N_("Show away once"), P_OFFINTNL (hex_away_show_once)},
	{ST_TOGGLE, N_("Automatically unmark away"), P_OFFINTNL (hex_away_auto_unmark)},

	{ST_HEADER, N_("Miscellaneous")},
	{ST_TOGGLE, N_("Display MODEs in raw form"), P_OFFINTNL (hex_irc_raw_modes)},
	{ST_TOGGLE, N_("WHOIS on notify"), P_OFFINTNL (hex_notify_whois_online)},
	{ST_TOGGLE, N_("Hide join and part messages"), P_OFFINTNL (hex_irc_conf_mode)},
	{ST_TOGGLE, N_("Hide nick change messages"), P_OFFINTNL (hex_irc_hide_nickchange)},
	{ST_END}
};

static const setting alerts_settings[] =
{
	{ST_HEADER, N_("Alerts")},
	{ST_TOGGLE, N_("Notify on channel messages"), P_OFFINTNL (hex_input_balloon_chans)},
	{ST_TOGGLE, N_("Notify on private messages"), P_OFFINTNL (hex_input_balloon_priv)},
	{ST_TOGGLE, N_("Notify on highlighted messages"), P_OFFINTNL (hex_input_balloon_hilight)},
	{ST_TOGGLE, N_("Blink tray icon on channel messages"), P_OFFINTNL (hex_input_tray_chans)},
	{ST_TOGGLE, N_("Blink tray icon on private messages"), P_OFFINTNL (hex_input_tray_priv)},
	{ST_TOGGLE, N_("Blink tray icon on highlighted messages"), P_OFFINTNL (hex_input_tray_hilight)},
	{ST_TOGGLE, N_("Omit alerts while away"), P_OFFINTNL (hex_away_omit_alerts)},
	{ST_TOGGLE, N_("Omit alerts while focused"), P_OFFINTNL (hex_gui_focus_omitalerts)},

	{ST_HEADER, N_("Tray Behavior")},
	{ST_TOGGLE, N_("Enable system tray icon"), P_OFFINTNL (hex_gui_tray)},
	{ST_TOGGLE, N_("Minimize to tray"), P_OFFINTNL (hex_gui_tray_minimize)},
	{ST_TOGGLE, N_("Close to tray"), P_OFFINTNL (hex_gui_tray_close)},
	{ST_TOGGLE, N_("Automatically mark away/back"), P_OFFINTNL (hex_gui_tray_away)},
	{ST_TOGGLE, N_("Only show notifications when hidden"), P_OFFINTNL (hex_gui_tray_quiet)},

	{ST_HEADER, N_("Highlighted Messages")},
	{ST_ENTRY, N_("Extra words to highlight:"), P_OFFSETNL (hex_irc_extra_hilight), NULL, NULL,
		sizeof (prefs.hex_irc_extra_hilight)},
	{ST_ENTRY, N_("Nick names not to highlight:"), P_OFFSETNL (hex_irc_no_hilight), NULL, NULL,
		sizeof (prefs.hex_irc_no_hilight)},
	{ST_ENTRY, N_("Nick names to always highlight:"), P_OFFSETNL (hex_irc_nick_hilight), NULL, NULL,
		sizeof (prefs.hex_irc_nick_hilight)},
	{ST_LABEL, N_("Separate multiple words with commas. Wildcards are accepted.")},
	{ST_END}
};

static const setting logging_settings[] =
{
	{ST_HEADER, N_("Logging")},
	{ST_TOGGLE, N_("Display scrollback from previous session"), P_OFFINTNL (hex_text_replay)},
	{ST_NUMBER, N_("Scrollback lines:"), P_OFFINTNL (hex_text_max_lines), NULL, NULL, 100000},
	{ST_TOGGLE, N_("Enable logging of conversations"), P_OFFINTNL (hex_irc_logging)},
	{ST_ENTRY, N_("Log filename:"), P_OFFSETNL (hex_irc_logmask), NULL, NULL, sizeof (prefs.hex_irc_logmask)},
	{ST_LABEL, N_("%s=Server %c=Channel %n=Network.")},

	{ST_HEADER, N_("Timestamps")},
	{ST_TOGGLE, N_("Insert timestamps in logs"), P_OFFINTNL (hex_stamp_log)},
	{ST_ENTRY, N_("Log timestamp format:"), P_OFFSETNL (hex_stamp_log_format), NULL, NULL,
		sizeof (prefs.hex_stamp_log_format)},
	{ST_LABEL, N_("See the strftime manpage for details.")},

	{ST_HEADER, N_("URLs")},
	{ST_TOGGLE, N_("Enable logging of URLs"), P_OFFINTNL (hex_url_logging)},
	{ST_TOGGLE, N_("Enable URL grabber"), P_OFFINTNL (hex_url_grabber)},
	{ST_NUMBER, N_("Maximum number of URLs to grab:"), P_OFFINTNL (hex_url_grabber_limit), NULL, NULL, 9999},
	{ST_END}
};

static const setting advanced_settings[] =
{
	{ST_HEADER, N_("Auto Copy Behavior")},
	{ST_TOGGLE, N_("Automatically copy selected text"), P_OFFINTNL (hex_text_autocopy_text)},
	{ST_TOGGLE, N_("Automatically include timestamps"), P_OFFINTNL (hex_text_autocopy_stamp)},
	{ST_TOGGLE, N_("Automatically include color information"), P_OFFINTNL (hex_text_autocopy_color)},

	{ST_HEADER, N_("Miscellaneous")},
	{ST_ENTRY, N_("Real name:"), P_OFFSETNL (hex_irc_real_name), NULL, NULL, sizeof (prefs.hex_irc_real_name)},
	{ST_TOGGLE, N_("Display lists in compact mode"), P_OFFINTNL (hex_gui_compact)},
	{ST_TOGGLE, N_("Use server time if supported"), P_OFFINTNL (hex_irc_cap_server_time)},
	{ST_TOGGLE, N_("Automatically reconnect to servers"), P_OFFINTNL (hex_net_auto_reconnect)},
	{ST_NUMBER, N_("Auto reconnect delay:"), P_OFFINTNL (hex_net_reconnect_delay), NULL, NULL, 9999},
	{ST_NUMBER, N_("Auto join delay:"), P_OFFINTNL (hex_irc_join_delay), NULL, NULL, 9999},
	{ST_MENU, N_("Ban Type:"), P_OFFINTNL (hex_irc_ban_type), NULL, bantypemenu, 0},
	{ST_END}
};

static const setting network_settings[] =
{
	{ST_HEADER, N_("Your Address")},
	{ST_ENTRY, N_("Bind to:"), P_OFFSETNL (hex_net_bind_host), NULL, NULL, sizeof (prefs.hex_net_bind_host)},
	{ST_LABEL, N_("Only useful for computers with multiple addresses.")},

	{ST_HEADER, N_("File Transfers")},
	{ST_TOGGLE, N_("Get my address from the IRC server"), P_OFFINTNL (hex_dcc_ip_from_server)},
	{ST_ENTRY, N_("DCC IP address:"), P_OFFSETNL (hex_dcc_ip), NULL, NULL, sizeof (prefs.hex_dcc_ip)},
	{ST_NUMBER, N_("First DCC listen port:"), P_OFFINTNL (hex_dcc_port_first), NULL, NULL, 65535},
	{ST_NUMBER, N_("Last DCC listen port:"), P_OFFINTNL (hex_dcc_port_last), NULL, NULL, 65535},

	{ST_HEADER, N_("Proxy Server")},
	{ST_ENTRY, N_("Hostname:"), P_OFFSETNL (hex_net_proxy_host), NULL, NULL, sizeof (prefs.hex_net_proxy_host)},
	{ST_NUMBER, N_("Port:"), P_OFFINTNL (hex_net_proxy_port), NULL, NULL, 65535},
	{ST_MENU, N_("Type:"), P_OFFINTNL (hex_net_proxy_type), NULL, proxytypes, 0},
	{ST_MENU, N_("Use proxy for:"), P_OFFINTNL (hex_net_proxy_use), NULL, proxyuse, 0},

	{ST_HEADER, N_("Proxy Authentication")},
	{ST_TOGGLE, N_("Use authentication"), P_OFFINTNL (hex_net_proxy_auth)},
	{ST_ENTRY, N_("Username:"), P_OFFSETNL (hex_net_proxy_user), NULL, NULL, sizeof (prefs.hex_net_proxy_user)},
	{ST_ENTRY, N_("Password:"), P_OFFSETNL (hex_net_proxy_pass), NULL, NULL,
		sizeof (prefs.hex_net_proxy_pass), SET_FLAG_PASSWORD},
	{ST_END}
};

static const setting filexfer_settings[] =
{
	{ST_HEADER, N_("Files and Directories")},
	{ST_MENU, N_("Auto accept file offers:"), P_OFFINTNL (hex_dcc_auto_recv), NULL, dccaccept, 0},
	{ST_ENTRY, N_("Download files to:"), P_OFFSETNL (hex_dcc_dir), NULL, NULL, sizeof (prefs.hex_dcc_dir)},
	{ST_ENTRY, N_("Move completed files to:"), P_OFFSETNL (hex_dcc_completed_dir), NULL, NULL,
		sizeof (prefs.hex_dcc_completed_dir)},
	{ST_TOGGLE, N_("Save nick name in filenames"), P_OFFINTNL (hex_dcc_save_nick)},

	{ST_HEADER, N_("Auto Open DCC Windows")},
	{ST_TOGGLE, N_("Send window"), P_OFFINTNL (hex_gui_autoopen_send)},
	{ST_TOGGLE, N_("Receive window"), P_OFFINTNL (hex_gui_autoopen_recv)},
	{ST_TOGGLE, N_("Chat window"), P_OFFINTNL (hex_gui_autoopen_chat)},

	{ST_HEADER, N_("Maximum File Transfer Speeds (Bytes per Second)")},
	{ST_NUMBER, N_("One upload:"), P_OFFINTNL (hex_dcc_max_send_cps), NULL, NULL, 10000000},
	{ST_NUMBER, N_("One download:"), P_OFFINTNL (hex_dcc_max_get_cps), NULL, NULL, 10000000},
	{ST_NUMBER, N_("All uploads combined:"), P_OFFINTNL (hex_dcc_global_max_send_cps), NULL, NULL, 10000000},
	{ST_NUMBER, N_("All downloads combined:"), P_OFFINTNL (hex_dcc_global_max_get_cps), NULL, NULL, 10000000},
	{ST_END}
};

static const setting identd_settings[] =
{
	{ST_HEADER, N_("Identd Server")},
	{ST_TOGGLE, N_("Enabled"), P_OFFINTNL (hex_identd_server)},
	{ST_NUMBER, N_("Port:"), P_OFFINTNL (hex_identd_port), NULL, NULL, 65535},
	{ST_END}
};

static const setting_page setting_pages[] =
{
	{N_("Appearance"), appearance_settings},
	{N_("Input box"), inputbox_settings},
	{N_("User list"), userlist_settings},
	{N_("Channel switcher"), tabs_settings},
	{N_("General"), general_settings},
	{N_("Alerts"), alerts_settings},
	{N_("Logging"), logging_settings},
	{N_("Advanced"), advanced_settings},
	{N_("Network setup"), network_settings},
	{N_("File transfers"), filexfer_settings},
	{N_("Identd"), identd_settings},
	{NULL, NULL}
};

#ifdef USE_LIBADWAITA
static const char *const setting_page_icons[] =
{
	"preferences-desktop-appearance-symbolic",    /* Appearance */
	"input-keyboard-symbolic",                    /* Input box */
	"system-users-symbolic",                      /* User list */
	"view-list-symbolic",                         /* Channel switcher */
	"preferences-system-symbolic",                /* General */
	"preferences-system-notifications-symbolic",  /* Alerts */
	"text-x-generic-symbolic",                    /* Logging */
	"applications-engineering-symbolic",          /* Advanced */
	"preferences-system-network-symbolic",        /* Network setup */
	"folder-download-symbolic",                   /* File transfers */
	"dialog-password-symbolic"                    /* Identd */
};
#endif

#ifndef USE_LIBADWAITA
static void
setup_toggle_cb (GtkCheckButton *check, gpointer userdata)
{
	const setting *set;
	int value;

	set = userdata;
	value = gtk_check_button_get_active (check) ? 1 : 0;
	if (set->flags & SET_FLAG_INVERT)
		value = value ? 0 : 1;

	setup_set_int (set, value);
	if (set->apply)
		set->apply ();
}
#endif

#ifdef USE_LIBADWAITA
static void
setup_switch_row_cb (GObject *object, GParamSpec *pspec, gpointer userdata)
{
	const setting *set;
	gboolean active;
	int value;

	(void) pspec;

	set = userdata;
	active = adw_switch_row_get_active (ADW_SWITCH_ROW (object));
	value = active ? 1 : 0;
	if (set->flags & SET_FLAG_INVERT)
		value = value ? 0 : 1;

	setup_set_int (set, value);
	if (set->apply)
		set->apply ();
}
#endif

static void
setup_entry_cb (GtkEditable *editable, gpointer userdata)
{
	const setting *set;
	const char *text;

	set = userdata;
	text = gtk_editable_get_text (editable);
	if (!text)
		text = "";

	if (set->extra > 0)
		g_strlcpy (setup_get_str (set), text, (gsize) set->extra);

	if (set->apply)
		set->apply ();
}

typedef struct
{
	GtkWidget *entry;
	GtkFontDialog *dialog;
} SetupFontRequest;

typedef struct
{
	GtkWidget *entry;
} SetupFontControl;

static void
setup_font_request_free (SetupFontRequest *req)
{
	if (!req)
		return;

	g_clear_object (&req->dialog);
	g_clear_object (&req->entry);
	g_free (req);
}

static void
setup_font_choose_finish_cb (GObject *source, GAsyncResult *result, gpointer userdata)
{
	SetupFontRequest *req;
	PangoFontDescription *font_desc;
	char *font_name;
	GError *error;

	req = userdata;
	error = NULL;
	font_desc = gtk_font_dialog_choose_font_finish (GTK_FONT_DIALOG (source), result, &error);
	if (!font_desc)
	{
		g_clear_error (&error);
		setup_font_request_free (req);
		return;
	}

	font_name = pango_font_description_to_string (font_desc);
	gtk_editable_set_text (GTK_EDITABLE (req->entry), font_name ? font_name : "");
	g_free (font_name);
	pango_font_description_free (font_desc);
	setup_font_request_free (req);
}

static void
setup_font_browse_cb (GtkButton *button, gpointer userdata)
{
	SetupFontControl *control;
	SetupFontRequest *req;
	PangoFontDescription *initial;
	const char *text;

	(void) userdata;

	control = g_object_get_data (G_OBJECT (button), "setup-font-control");
	if (!control || !control->entry)
		return;

	initial = NULL;
	text = gtk_editable_get_text (GTK_EDITABLE (control->entry));
	if (text && text[0])
		initial = pango_font_description_from_string (text);

	req = g_new0 (SetupFontRequest, 1);
	req->entry = g_object_ref (control->entry);
	req->dialog = gtk_font_dialog_new ();
	gtk_font_dialog_set_title (req->dialog, _("Select font"));
	gtk_font_dialog_set_modal (req->dialog, TRUE);
	gtk_font_dialog_choose_font (req->dialog,
		prefs_window ? GTK_WINDOW (prefs_window) : NULL,
		initial,
		NULL,
		setup_font_choose_finish_cb,
		req);

	if (initial)
		pango_font_description_free (initial);
}

static void
setup_spin_cb (GtkSpinButton *spin, gpointer userdata)
{
	const setting *set;

	set = userdata;
	setup_set_int (set, gtk_spin_button_get_value_as_int (spin));
	if (set->apply)
		set->apply ();
}

#ifndef USE_LIBADWAITA
static void
setup_menu_cb (GObject *object, GParamSpec *pspec, gpointer userdata)
{
	const setting *set;
	GtkDropDown *dd;
	guint n;

	(void) pspec;

	set = userdata;
	dd = GTK_DROP_DOWN (object);
	n = gtk_drop_down_get_selected (dd);
	if (n == GTK_INVALID_LIST_POSITION)
		return;

	setup_set_int (set, (int) n + set->extra);
	if (set->apply)
		set->apply ();
}
#endif

#ifdef USE_LIBADWAITA
static void
setup_combo_row_cb (GObject *object, GParamSpec *pspec, gpointer userdata)
{
	const setting *set;
	guint n;

	(void) pspec;

	set = userdata;
	n = adw_combo_row_get_selected (ADW_COMBO_ROW (object));
	if (n == GTK_INVALID_LIST_POSITION)
		return;

	setup_set_int (set, (int) n + set->extra);
	if (set->apply)
		set->apply ();
}
#endif

#ifndef USE_LIBADWAITA
static GtkWidget *
setup_translated_dropdown_new (const setting *set)
{
	GtkWidget *dd;
	guint count;
	guint i;
	char **items;
	int selected;

	count = 0;
	while (set->list && set->list[count])
		count++;

	items = g_new0 (char *, count + 1);
	for (i = 0; i < count; i++)
		items[i] = g_strdup (_(set->list[i]));

	dd = gtk_drop_down_new_from_strings ((const char *const *) items);

	for (i = 0; i < count; i++)
		g_free (items[i]);
	g_free (items);

	selected = setup_get_int (set) - set->extra;
	if (selected < 0 || selected >= (int) count)
		selected = 0;
	gtk_drop_down_set_selected (GTK_DROP_DOWN (dd), (guint) selected);
	g_signal_connect (dd, "notify::selected", G_CALLBACK (setup_menu_cb), (gpointer) set);

	return dd;
}
#endif

#ifdef USE_LIBADWAITA
static GtkWidget *
setup_translated_combo_row_new (const setting *set)
{
	GtkStringList *items;
	GtkWidget *row;
	guint count;
	guint i;
	int selected;

	items = gtk_string_list_new (NULL);
	count = 0;
	while (set->list && set->list[count])
		count++;

	for (i = 0; i < count; i++)
		gtk_string_list_append (items, _(set->list[i]));

	row = adw_combo_row_new ();
	adw_combo_row_set_model (ADW_COMBO_ROW (row), G_LIST_MODEL (items));
	g_object_unref (items);
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set->label));

	selected = setup_get_int (set) - set->extra;
	if (selected < 0 || selected >= (int) count)
		selected = 0;
	adw_combo_row_set_selected (ADW_COMBO_ROW (row), (guint) selected);
	g_signal_connect (row, "notify::selected", G_CALLBACK (setup_combo_row_cb), (gpointer) set);

	if (set->tooltip)
		gtk_widget_set_tooltip_text (row, _(set->tooltip));

	return row;
}
#endif

static gboolean
setup_is_menu_visibility_setting (const setting *set)
{
#ifdef USE_LIBADWAITA
	if (set && set->apply == setup_apply_menu_visibility)
		return TRUE;
#else
	(void) set;
#endif
	return FALSE;
}

#ifdef USE_LIBADWAITA
static GtkWidget *
setup_ensure_adw_group (GtkWidget *page, GtkWidget *group)
{
	if (group)
		return group;

	group = adw_preferences_group_new ();
	adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));
	return group;
}

static GtkWidget *
setup_create_page_adw (const setting *set)
{
	GtkWidget *page;
	GtkWidget *group;
	GtkWidget *row;
	GtkWidget *control;
	int i;

	page = adw_preferences_page_new ();
	group = NULL;

	for (i = 0; set[i].type != ST_END; i++)
	{
		if (setup_is_menu_visibility_setting (&set[i]))
			continue;

		switch (set[i].type)
		{
		case ST_HEADER:
			group = adw_preferences_group_new ();
			adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), _(set[i].label));
			adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));
			break;
		case ST_LABEL:
			group = setup_ensure_adw_group (page, group);
			row = adw_action_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "");
			adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _(set[i].label));
			gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
			gtk_widget_add_css_class (row, "dim-label");
			adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
			break;
		case ST_TOGGLE:
			group = setup_ensure_adw_group (page, group);
			row = adw_switch_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
			adw_switch_row_set_active (ADW_SWITCH_ROW (row),
				(set[i].flags & SET_FLAG_INVERT) ? (setup_get_int (&set[i]) == 0) : (setup_get_int (&set[i]) != 0));
			g_signal_connect (row, "notify::active", G_CALLBACK (setup_switch_row_cb), (gpointer) &set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
			break;
		case ST_ENTRY:
			group = setup_ensure_adw_group (page, group);
			if (set[i].flags & SET_FLAG_PASSWORD)
			{
				GtkWidget *pass;

				row = adw_action_row_new ();
				adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
				gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);

				pass = gtk_password_entry_new ();
				gtk_widget_set_hexpand (pass, TRUE);
				gtk_editable_set_text (GTK_EDITABLE (pass), setup_get_str (&set[i]));
				g_signal_connect (pass, "changed", G_CALLBACK (setup_entry_cb), (gpointer) &set[i]);
				adw_action_row_add_suffix (ADW_ACTION_ROW (row), pass);
				adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), pass);
			}
			else
			{
				row = adw_entry_row_new ();
				adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
				gtk_editable_set_text (GTK_EDITABLE (row), setup_get_str (&set[i]));
				g_signal_connect (row, "changed", G_CALLBACK (setup_entry_cb), (gpointer) &set[i]);
			}
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
			break;
		case ST_FONT:
		{
			GtkWidget *button;
			SetupFontControl *font_control;

			group = setup_ensure_adw_group (page, group);
			row = adw_entry_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
			gtk_editable_set_text (GTK_EDITABLE (row), setup_get_str (&set[i]));
			g_signal_connect (row, "changed", G_CALLBACK (setup_entry_cb), (gpointer) &set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));

			button = gtk_button_new_from_icon_name ("font-x-generic-symbolic");
			gtk_widget_set_tooltip_text (button, _("Browse fonts"));
			adw_entry_row_add_suffix (ADW_ENTRY_ROW (row), button);

			font_control = g_new0 (SetupFontControl, 1);
			font_control->entry = row;
			g_object_set_data_full (G_OBJECT (button), "setup-font-control", font_control, g_free);
			g_signal_connect (button, "clicked", G_CALLBACK (setup_font_browse_cb), NULL);

			adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
			break;
		}
		case ST_NUMBER:
			group = setup_ensure_adw_group (page, group);
			row = adw_action_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
			control = gtk_spin_button_new_with_range (0, set[i].extra > 0 ? set[i].extra : 999999, 1);
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (control), setup_get_int (&set[i]));
			g_signal_connect (control, "value-changed", G_CALLBACK (setup_spin_cb), (gpointer) &set[i]);
			adw_action_row_add_suffix (ADW_ACTION_ROW (row), control);
			adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), control);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
			break;
		case ST_MENU:
			group = setup_ensure_adw_group (page, group);
			row = setup_translated_combo_row_new (&set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
			break;
		default:
			break;
		}
	}

	return page;
}
#else
static GtkWidget *
setup_make_label (const char *text, gboolean header)
{
	GtkWidget *label;
	char *markup;

	label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (label), TRUE);

	if (header)
	{
		markup = g_strdup_printf ("<b>%s</b>", text ? text : "");
		gtk_label_set_markup (GTK_LABEL (label), markup);
		g_free (markup);
		gtk_widget_set_margin_top (label, 10);
		gtk_widget_set_margin_bottom (label, 2);
	}
	else
	{
		markup = g_strdup_printf ("<span size=\"small\">%s</span>", text ? text : "");
		gtk_label_set_markup (GTK_LABEL (label), markup);
		g_free (markup);
		gtk_widget_add_css_class (label, "dim-label");
	}

	return label;
}

static GtkWidget *
setup_create_page (const setting *set)
{
	GtkWidget *box;
	GtkWidget *grid;
	GtkWidget *row;
	GtkWidget *label;
	GtkWidget *control;
	int i;
	int row_idx;

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start (box, 10);
	gtk_widget_set_margin_end (box, 10);
	gtk_widget_set_margin_top (box, 6);
	gtk_widget_set_margin_bottom (box, 10);

	grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_box_append (GTK_BOX (box), grid);

	row_idx = 0;
	for (i = 0; set[i].type != ST_END; i++)
	{
		if (setup_is_menu_visibility_setting (&set[i]))
			continue;

		switch (set[i].type)
		{
		case ST_HEADER:
			row = setup_make_label (_(set[i].label), TRUE);
			gtk_grid_attach (GTK_GRID (grid), row, 0, row_idx++, 2, 1);
			break;
		case ST_LABEL:
			row = setup_make_label (_(set[i].label), FALSE);
			gtk_grid_attach (GTK_GRID (grid), row, 0, row_idx++, 2, 1);
			break;
		case ST_TOGGLE:
			control = gtk_check_button_new_with_label (_(set[i].label));
			gtk_check_button_set_active (GTK_CHECK_BUTTON (control),
				(set[i].flags & SET_FLAG_INVERT) ? (setup_get_int (&set[i]) == 0) : (setup_get_int (&set[i]) != 0));
			g_signal_connect (control, "toggled", G_CALLBACK (setup_toggle_cb), (gpointer) &set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (control, _(set[i].tooltip));
			gtk_grid_attach (GTK_GRID (grid), control, 0, row_idx++, 2, 1);
			break;
		case ST_ENTRY:
			label = gtk_label_new (_(set[i].label));
			gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
			gtk_grid_attach (GTK_GRID (grid), label, 0, row_idx, 1, 1);

			control = gtk_entry_new ();
			gtk_widget_set_hexpand (control, TRUE);
			gtk_editable_set_text (GTK_EDITABLE (control), setup_get_str (&set[i]));
			if (set[i].flags & SET_FLAG_PASSWORD)
				gtk_entry_set_visibility (GTK_ENTRY (control), FALSE);
			g_signal_connect (control, "changed", G_CALLBACK (setup_entry_cb), (gpointer) &set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (control, _(set[i].tooltip));
			gtk_grid_attach (GTK_GRID (grid), control, 1, row_idx++, 1, 1);
			break;
		case ST_FONT:
		{
			GtkWidget *row_box;
			GtkWidget *button;
			SetupFontControl *font_control;

			label = gtk_label_new (_(set[i].label));
			gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
			gtk_grid_attach (GTK_GRID (grid), label, 0, row_idx, 1, 1);

			row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
			gtk_widget_set_hexpand (row_box, TRUE);
			gtk_grid_attach (GTK_GRID (grid), row_box, 1, row_idx++, 1, 1);

			control = gtk_entry_new ();
			gtk_widget_set_hexpand (control, TRUE);
			gtk_editable_set_text (GTK_EDITABLE (control), setup_get_str (&set[i]));
			g_signal_connect (control, "changed", G_CALLBACK (setup_entry_cb), (gpointer) &set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (control, _(set[i].tooltip));
			gtk_box_append (GTK_BOX (row_box), control);

			button = gtk_button_new_with_label (_("Browse..."));
			gtk_box_append (GTK_BOX (row_box), button);

			font_control = g_new0 (SetupFontControl, 1);
			font_control->entry = control;
			g_object_set_data_full (G_OBJECT (button), "setup-font-control", font_control, g_free);
			g_signal_connect (button, "clicked", G_CALLBACK (setup_font_browse_cb), NULL);
			break;
		}
		case ST_NUMBER:
			label = gtk_label_new (_(set[i].label));
			gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
			gtk_grid_attach (GTK_GRID (grid), label, 0, row_idx, 1, 1);

			control = gtk_spin_button_new_with_range (0, set[i].extra > 0 ? set[i].extra : 999999, 1);
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (control), setup_get_int (&set[i]));
			g_signal_connect (control, "value-changed", G_CALLBACK (setup_spin_cb), (gpointer) &set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (control, _(set[i].tooltip));
			gtk_grid_attach (GTK_GRID (grid), control, 1, row_idx++, 1, 1);
			break;
		case ST_MENU:
			label = gtk_label_new (_(set[i].label));
			gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
			gtk_grid_attach (GTK_GRID (grid), label, 0, row_idx, 1, 1);

			control = setup_translated_dropdown_new (&set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (control, _(set[i].tooltip));
			gtk_grid_attach (GTK_GRID (grid), control, 1, row_idx++, 1, 1);
			break;
		default:
			break;
		}
	}

	return box;
}
#endif

static gboolean
prefs_window_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	save_config ();
	prefs_window = NULL;
	return FALSE;
}

void
setup_open (void)
{
#ifndef USE_LIBADWAITA
	GtkWidget *root;
	GtkWidget *content;
	GtkWidget *sidebar;
	GtkWidget *stack;
	GtkWidget *bottom;
	GtkWidget *button;
#endif
	GtkWidget *page;
#ifndef USE_LIBADWAITA
	GtkWidget *scroller;
	char name[32];
#endif
	int i;

	if (prefs_window)
	{
		gtk_window_present (GTK_WINDOW (prefs_window));
		return;
	}

#ifdef USE_LIBADWAITA
	prefs_window = adw_preferences_window_new ();
	gtk_window_set_title (GTK_WINDOW (prefs_window), _("Preferences"));
	gtk_window_set_default_size (GTK_WINDOW (prefs_window), 920, 680);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (prefs_window), GTK_WINDOW (main_window));

	adw_preferences_window_set_search_enabled (ADW_PREFERENCES_WINDOW (prefs_window), TRUE);

	for (i = 0; setting_pages[i].title; i++)
	{
		const char *icon_name;

		page = setup_create_page_adw (setting_pages[i].settings);
		adw_preferences_page_set_title (ADW_PREFERENCES_PAGE (page), _(setting_pages[i].title));
		icon_name = (i < (int) G_N_ELEMENTS (setting_page_icons)) ? setting_page_icons[i] : NULL;
		if (icon_name && icon_name[0])
			adw_preferences_page_set_icon_name (ADW_PREFERENCES_PAGE (page), icon_name);
		adw_preferences_window_add (ADW_PREFERENCES_WINDOW (prefs_window), ADW_PREFERENCES_PAGE (page));
	}
#else
	prefs_window = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (prefs_window), _("Preferences"));
	gtk_window_set_default_size (GTK_WINDOW (prefs_window), 920, 680);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (prefs_window), GTK_WINDOW (main_window));

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (root, 10);
	gtk_widget_set_margin_end (root, 10);
	gtk_widget_set_margin_top (root, 10);
	gtk_widget_set_margin_bottom (root, 10);
	gtk_window_set_child (GTK_WINDOW (prefs_window), root);

	content = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_hexpand (content, TRUE);
	gtk_widget_set_vexpand (content, TRUE);
	gtk_box_append (GTK_BOX (root), content);

	stack = gtk_stack_new ();
	gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_SLIDE_UP_DOWN);

	sidebar = gtk_stack_sidebar_new ();
	gtk_stack_sidebar_set_stack (GTK_STACK_SIDEBAR (sidebar), GTK_STACK (stack));
	gtk_widget_set_size_request (sidebar, 220, -1);
	gtk_box_append (GTK_BOX (content), sidebar);
	gtk_box_append (GTK_BOX (content), stack);

	for (i = 0; setting_pages[i].title; i++)
	{
		g_snprintf (name, sizeof (name), "page%d", i);
		page = setup_create_page (setting_pages[i].settings);
		scroller = gtk_scrolled_window_new ();
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), page);
		gtk_stack_add_titled (GTK_STACK (stack), scroller, name, _(setting_pages[i].title));
	}

	bottom = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (bottom, GTK_ALIGN_END);
	gtk_box_append (GTK_BOX (root), bottom);

	button = gtk_button_new_with_label (_("Close"));
	g_signal_connect_swapped (button, "clicked", G_CALLBACK (gtk_window_close), prefs_window);
	gtk_box_append (GTK_BOX (bottom), button);
#endif

	g_signal_connect (prefs_window, "close-request",
		G_CALLBACK (prefs_window_close_request_cb), NULL);

	setup_apply_menu_visibility ();
	setup_apply_input_spell ();
	setup_apply_input_attr ();
	setup_apply_input_style ();
	setup_apply_text_font ();

	gtk_window_present (GTK_WINDOW (prefs_window));
}

void
fe_gtk4_setup_open (void)
{
	setup_open ();
}

void
fe_gtk4_setup_cleanup (void)
{
	if (!prefs_window)
		return;

	gtk_window_destroy (GTK_WINDOW (prefs_window));
	prefs_window = NULL;
}

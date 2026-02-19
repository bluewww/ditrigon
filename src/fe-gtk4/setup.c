/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 setup/preferences window */

#include "fe-gtk4.h"
#include "sexy-spell-entry.h"
#include <adwaita.h>

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

#define SETUP_UI_BASE "/org/ditrigon/ui/gtk4/setup"

static GtkWidget *
setup_ui_widget_new (const char *resource_path, const char *object_id)
{
	GtkBuilder *builder;
	GtkWidget *widget;

	builder = fe_gtk4_builder_new_from_resource (resource_path);
	widget = fe_gtk4_builder_get_widget (builder, object_id, GTK_TYPE_WIDGET);
	g_object_ref_sink (widget);
	g_object_unref (builder);
	return widget;
}

static GtkWidget *
setup_ui_window_new (void)
{
	GtkWidget *window;

	window = setup_ui_widget_new (SETUP_UI_BASE "/preferences-window.ui", "setup_prefs_window");
	return window;
}

static GtkWidget *
setup_ui_page_new (void)
{
	GtkWidget *page;

	page = setup_ui_widget_new (SETUP_UI_BASE "/page.ui", "setup_page");
	return page;
}

static GtkWidget *
setup_ui_group_new (void)
{
	GtkWidget *group;

	group = setup_ui_widget_new (SETUP_UI_BASE "/group.ui", "setup_group");
	return group;
}

static GtkWidget *
setup_ui_switch_row_new (void)
{
	GtkWidget *row;

	row = setup_ui_widget_new (SETUP_UI_BASE "/rows/switch-row.ui", "setup_switch_row");
	return row;
}

static GtkWidget *
setup_ui_entry_row_new (void)
{
	GtkWidget *row;

	row = setup_ui_widget_new (SETUP_UI_BASE "/rows/entry-row.ui", "setup_entry_row");
	return row;
}

static GtkWidget *
setup_ui_action_row_new (void)
{
	GtkWidget *row;

	row = setup_ui_widget_new (SETUP_UI_BASE "/rows/action-row.ui", "setup_action_row");
	return row;
}

static GtkWidget *
setup_ui_combo_row_new (void)
{
	GtkWidget *row;

	row = setup_ui_widget_new (SETUP_UI_BASE "/rows/combo-row.ui", "setup_combo_row");
	return row;
}

static void
setup_adw_page_add_group_owned (GtkWidget *page, GtkWidget *group)
{
	if (!page || !group)
		return;

	adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));
	g_object_unref (group);
}

static void
setup_adw_group_add_row_owned (GtkWidget *group, GtkWidget *row)
{
	if (!group || !row)
		return;

	adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
	g_object_unref (row);
}

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

/* static const char *const lagmenutext[] = */
/* { */
/* 	N_("Off"), */
/* 	N_("Graphical"), */
/* 	N_("Text"), */
/* 	N_("Both"), */
/* 	NULL */
/* }; */

static const char *const ulmenutext[] =
{
	N_("A-Z, ops first"),
	N_("A-Z"),
	N_("Z-A, ops last"),
	N_("Z-A"),
	N_("Unsorted"),
	NULL
};

/* static const char *const cspos[] = */
/* { */
/* 	N_("Left (upper)"), */
/* 	N_("Left (lower)"), */
/* 	N_("Right (upper)"), */
/* 	N_("Right (lower)"), */
/* 	N_("Top"), */
/* 	N_("Bottom"), */
/* 	N_("Hidden"), */
/* 	NULL */
/* }; */

/* static const char *const ulpos[] = */
/* { */
/* 	N_("Left (upper)"), */
/* 	N_("Left (lower)"), */
/* 	N_("Right (upper)"), */
/* 	N_("Right (lower)"), */
/* 	NULL */
/* }; */

/* static const char *const tabwin[] = */
/* { */
/* 	N_("Windows"), */
/* 	N_("Tabs"), */
/* 	NULL */
/* }; */

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
	/* {ST_ENTRY, N_("Background image:"), P_OFFSETNL (hex_text_background), NULL, NULL, sizeof (prefs.hex_text_background)}, */

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
	/* {ST_MENU, N_("Show user list at:"), P_OFFINTNL (hex_gui_ulist_pos), NULL, ulpos, 1}, */

	{ST_HEADER, N_("Away Tracking")},
	{ST_TOGGLE, N_("Track away status"), P_OFFINTNL (hex_away_track)},
	{ST_NUMBER, N_("On channels smaller than:"), P_OFFINTNL (hex_away_size_max), NULL, NULL, 10000},

	{ST_HEADER, N_("Action Upon Double Click")},
	{ST_ENTRY, N_("Execute command:"), P_OFFSETNL (hex_gui_ulist_doubleclick), NULL, NULL,
		sizeof (prefs.hex_gui_ulist_doubleclick)},

	/* {ST_HEADER, N_("Extra Gadgets")}, */
	/* {ST_MENU, N_("Lag meter:"), P_OFFINTNL (hex_gui_lagometer), NULL, lagmenutext, 0}, */
	/* {ST_MENU, N_("Throttle meter:"), P_OFFINTNL (hex_gui_throttlemeter), NULL, lagmenutext, 0}, */
	{ST_END}
};

static const setting tabs_settings[] =
{
	{ST_HEADER, N_("Channel Switcher")},
	{ST_TOGGLE, N_("Open an extra tab for server messages"), P_OFFINTNL (hex_gui_tab_server)},
	{ST_TOGGLE, N_("Open private messages in new tabs"), P_OFFINTNL (hex_gui_autoopen_dialog)},
	{ST_TOGGLE, N_("Sort tabs in alphabetical order"), P_OFFINTNL (hex_gui_tab_sort)},
	/* {ST_TOGGLE, N_("Show icons in the channel tree"), P_OFFINTNL (hex_gui_tab_icons)}, */
	/* {ST_TOGGLE, N_("Show dotted lines in the channel tree"), P_OFFINTNL (hex_gui_tab_dots)}, */
	/* {ST_TOGGLE, N_("Scroll mouse-wheel to change tabs"), P_OFFINTNL (hex_gui_tab_scrollchans)}, */
	/* {ST_TOGGLE, N_("Middle click to close tab"), P_OFFINTNL (hex_gui_tab_middleclose)}, */
	/* {ST_TOGGLE, N_("Smaller text"), P_OFFINTNL (hex_gui_tab_small)}, */
	{ST_MENU, N_("Focus new tabs"), P_OFFINTNL (hex_gui_tab_newtofront), NULL, focusnewtabsmenu, 0},
	{ST_MENU, N_("Placement of notices"), P_OFFINTNL (hex_irc_notice_pos), NULL, noticeposmenu, 0},
	/* {ST_MENU, N_("Show channel switcher at:"), P_OFFINTNL (hex_gui_tab_pos), NULL, cspos, 1}, */
	/* {ST_NUMBER, N_("Shorten tab labels to"), P_OFFINTNL (hex_gui_tab_trunc), NULL, NULL, 99}, */

	/* {ST_HEADER, N_("Tabs or Windows")}, */
	/* {ST_MENU, N_("Open channels in:"), P_OFFINTNL (hex_gui_tab_chans), NULL, tabwin, 0}, */
	/* {ST_MENU, N_("Open dialogs in:"), P_OFFINTNL (hex_gui_tab_dialogs), NULL, tabwin, 0}, */
	/* {ST_MENU, N_("Open utilities in:"), P_OFFINTNL (hex_gui_tab_utils), NULL, tabwin, 0}, */
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
	/* {ST_TOGGLE, N_("Blink tray icon on channel messages"), P_OFFINTNL (hex_input_tray_chans)}, */
	/* {ST_TOGGLE, N_("Blink tray icon on private messages"), P_OFFINTNL (hex_input_tray_priv)}, */
	/* {ST_TOGGLE, N_("Blink tray icon on highlighted messages"), P_OFFINTNL (hex_input_tray_hilight)}, */
	{ST_TOGGLE, N_("Omit alerts while away"), P_OFFINTNL (hex_away_omit_alerts)},
	{ST_TOGGLE, N_("Omit alerts while focused"), P_OFFINTNL (hex_gui_focus_omitalerts)},

	/* {ST_HEADER, N_("Tray Behavior")}, */
	/* {ST_TOGGLE, N_("Enable system tray icon"), P_OFFINTNL (hex_gui_tray)}, */
	/* {ST_TOGGLE, N_("Minimize to tray"), P_OFFINTNL (hex_gui_tray_minimize)}, */
	/* {ST_TOGGLE, N_("Close to tray"), P_OFFINTNL (hex_gui_tray_close)}, */
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
	/* {ST_HEADER, N_("Auto Copy Behavior")}, */
	/* {ST_TOGGLE, N_("Automatically copy selected text"), P_OFFINTNL (hex_text_autocopy_text)}, */
	/* {ST_TOGGLE, N_("Automatically include timestamps"), P_OFFINTNL (hex_text_autocopy_stamp)}, */
	/* {ST_TOGGLE, N_("Automatically include color information"), P_OFFINTNL (hex_text_autocopy_color)}, */

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
	const setting *set;
	GtkWidget *switch_row;
	GtkWidget *font_row;
	GtkWidget *font_value_label;
	char *custom_font;
	gboolean use_default;
	gboolean use_default_initialized;
	gboolean updating;
} SetupAdwFontControl;

typedef struct
{
	GtkWidget *row;
	GtkFontDialog *dialog;
} SetupAdwFontRequest;


static gboolean
setup_adw_font_is_default (const char *font_name)
{
	const char *default_font;

	default_font = fe_get_default_font ();
	if (!default_font || !default_font[0])
		return !font_name || !font_name[0];

	if (!font_name || !font_name[0])
		return TRUE;

	return g_strcmp0 (font_name, default_font) == 0;
}

static void
setup_adw_font_control_free (gpointer data)
{
	SetupAdwFontControl *control;

	control = data;
	if (!control)
		return;

	g_free (control->custom_font);
	g_free (control);
}

static void
setup_adw_font_request_free (SetupAdwFontRequest *req)
{
	if (!req)
		return;

	g_clear_object (&req->dialog);
	g_clear_object (&req->row);
	g_free (req);
}

static void
setup_adw_font_sync (SetupAdwFontControl *control)
{
	const char *font_name;
	const char *default_font;
	gboolean use_default;

	if (!control || !control->set)
		return;

	font_name = setup_get_str (control->set);
	default_font = fe_get_default_font ();
	if (!control->use_default_initialized)
	{
		control->use_default = setup_adw_font_is_default (font_name);
		control->use_default_initialized = TRUE;
	}
	use_default = control->use_default;

	control->updating = TRUE;
	adw_switch_row_set_active (ADW_SWITCH_ROW (control->switch_row), use_default);
	gtk_widget_set_sensitive (control->font_row, !use_default);
	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (control->font_row), !use_default);
	gtk_label_set_text (GTK_LABEL (control->font_value_label),
		use_default ? (default_font && default_font[0] ? default_font : _("System Default"))
		            : (font_name && font_name[0] ? font_name : _("System Default")));
	control->updating = FALSE;

	if (!use_default && font_name && font_name[0])
	{
		g_free (control->custom_font);
		control->custom_font = g_strdup (font_name);
	}
}

static void
setup_adw_font_choose_finish_cb (GObject *source, GAsyncResult *result, gpointer userdata)
{
	SetupAdwFontRequest *req;
	SetupAdwFontControl *control;
	PangoFontDescription *font_desc;
	char *font_name;
	GError *error;

	req = userdata;
	error = NULL;
	font_desc = gtk_font_dialog_choose_font_finish (GTK_FONT_DIALOG (source), result, &error);
	if (!font_desc)
	{
		g_clear_error (&error);
		setup_adw_font_request_free (req);
		return;
	}

	control = g_object_get_data (G_OBJECT (req->row), "setup-adw-font-control");
	if (!control || !control->set)
	{
		pango_font_description_free (font_desc);
		setup_adw_font_request_free (req);
		return;
	}

	font_name = pango_font_description_to_string (font_desc);
	g_strlcpy (setup_get_str (control->set), font_name ? font_name : "",
		(gsize) control->set->extra);
	if (font_name && font_name[0])
	{
		g_free (control->custom_font);
		control->custom_font = g_strdup (font_name);
	}
	control->use_default = FALSE;
	control->use_default_initialized = TRUE;
	g_free (font_name);
	pango_font_description_free (font_desc);

	if (control->set->apply)
		control->set->apply ();
	setup_adw_font_sync (control);
	setup_adw_font_request_free (req);
}

static void
setup_adw_font_row_activated_cb (GtkListBoxRow *row, gpointer userdata)
{
	SetupAdwFontControl *control;
	SetupAdwFontRequest *req;
	PangoFontDescription *initial;
	const char *text;

	(void) userdata;

	control = g_object_get_data (G_OBJECT (row), "setup-adw-font-control");
	if (!control || !control->set || control->updating)
		return;

	if (adw_switch_row_get_active (ADW_SWITCH_ROW (control->switch_row)))
		return;

	initial = NULL;
	text = setup_get_str (control->set);
	if (text && text[0])
		initial = pango_font_description_from_string (text);

	req = g_new0 (SetupAdwFontRequest, 1);
	req->row = GTK_WIDGET (g_object_ref (row));
	req->dialog = gtk_font_dialog_new ();
	gtk_font_dialog_set_title (req->dialog, _("Select font"));
	gtk_font_dialog_set_modal (req->dialog, TRUE);
	gtk_font_dialog_choose_font (req->dialog,
		prefs_window ? GTK_WINDOW (gtk_widget_get_root (prefs_window)) : NULL,
		initial,
		NULL,
		setup_adw_font_choose_finish_cb,
		req);

	if (initial)
		pango_font_description_free (initial);
}

static void
setup_adw_font_use_default_cb (GObject *object, GParamSpec *pspec, gpointer userdata)
{
	SetupAdwFontControl *control;
	const char *default_font;
	const char *current;
	const char *font_name;
	gboolean use_default;

	(void) pspec;
	(void) userdata;

	control = g_object_get_data (G_OBJECT (object), "setup-adw-font-control");
	if (!control || !control->set || control->updating)
		return;

	default_font = fe_get_default_font ();
	if (!default_font || !default_font[0])
		default_font = "Monospace 10";

	current = setup_get_str (control->set);
	use_default = adw_switch_row_get_active (ADW_SWITCH_ROW (control->switch_row));
	control->use_default = use_default;
	control->use_default_initialized = TRUE;
	if (use_default)
	{
		if (current && current[0] && !setup_adw_font_is_default (current))
		{
			g_free (control->custom_font);
			control->custom_font = g_strdup (current);
		}
		g_strlcpy (setup_get_str (control->set), default_font, (gsize) control->set->extra);
	}
	else
	{
		font_name = (control->custom_font && control->custom_font[0]) ?
			control->custom_font :
			(current && current[0] ? current : default_font);
		g_strlcpy (setup_get_str (control->set), font_name, (gsize) control->set->extra);
	}

	if (control->set->apply)
		control->set->apply ();
	setup_adw_font_sync (control);
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

	row = setup_ui_combo_row_new ();
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

static gboolean
setup_is_menu_visibility_setting (const setting *set)
{
	if (set && set->apply == setup_apply_menu_visibility)
		return TRUE;
	return FALSE;
}

static GtkWidget *
setup_ensure_adw_group (GtkWidget *page, GtkWidget *group)
{
	if (group)
		return group;

	group = setup_ui_group_new ();
	setup_adw_page_add_group_owned (page, group);
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

	page = setup_ui_page_new ();
	group = NULL;

	for (i = 0; set[i].type != ST_END; i++)
	{
		if (setup_is_menu_visibility_setting (&set[i]))
			continue;

		switch (set[i].type)
		{
		case ST_HEADER:
			group = setup_ui_group_new ();
			adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), _(set[i].label));
			setup_adw_page_add_group_owned (page, group);
			break;
		case ST_LABEL:
			group = setup_ensure_adw_group (page, group);
			row = setup_ui_action_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), "");
			adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _(set[i].label));
			gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
			gtk_widget_add_css_class (row, "dim-label");
			setup_adw_group_add_row_owned (group, row);
			break;
		case ST_TOGGLE:
			group = setup_ensure_adw_group (page, group);
			row = setup_ui_switch_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
			adw_switch_row_set_active (ADW_SWITCH_ROW (row),
				(set[i].flags & SET_FLAG_INVERT) ? (setup_get_int (&set[i]) == 0) : (setup_get_int (&set[i]) != 0));
			g_signal_connect (row, "notify::active", G_CALLBACK (setup_switch_row_cb), (gpointer) &set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			setup_adw_group_add_row_owned (group, row);
			break;
		case ST_ENTRY:
			group = setup_ensure_adw_group (page, group);
			if (set[i].flags & SET_FLAG_PASSWORD)
			{
				GtkWidget *pass;

				row = setup_ui_action_row_new ();
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
				row = setup_ui_entry_row_new ();
				adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
				gtk_editable_set_text (GTK_EDITABLE (row), setup_get_str (&set[i]));
				g_signal_connect (row, "changed", G_CALLBACK (setup_entry_cb), (gpointer) &set[i]);
			}
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			setup_adw_group_add_row_owned (group, row);
			break;
		case ST_FONT:
		{
			GtkWidget *value_label;
			GtkWidget *arrow;
			SetupAdwFontControl *font_control;

			group = setup_ensure_adw_group (page, group);
			control = setup_ui_switch_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (control), _("Use System Font"));
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (control, _(set[i].tooltip));
			setup_adw_group_add_row_owned (group, control);

			row = setup_ui_action_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _("Custom Font"));
			gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);

			value_label = gtk_label_new ("");
			gtk_label_set_xalign (GTK_LABEL (value_label), 1.0f);
			gtk_widget_add_css_class (value_label, "dim-label");
			adw_action_row_add_suffix (ADW_ACTION_ROW (row), value_label);

			arrow = gtk_image_new_from_icon_name ("go-next-symbolic");
			gtk_widget_add_css_class (arrow, "dim-label");
			adw_action_row_add_suffix (ADW_ACTION_ROW (row), arrow);

			font_control = g_new0 (SetupAdwFontControl, 1);
			font_control->set = &set[i];
			font_control->switch_row = control;
			font_control->font_row = row;
			font_control->font_value_label = value_label;
			g_object_set_data (G_OBJECT (control), "setup-adw-font-control", font_control);
			g_object_set_data_full (G_OBJECT (row), "setup-adw-font-control",
				font_control, setup_adw_font_control_free);
			g_signal_connect (control, "notify::active",
				G_CALLBACK (setup_adw_font_use_default_cb), NULL);
			g_signal_connect (row, "activated",
				G_CALLBACK (setup_adw_font_row_activated_cb), NULL);
			setup_adw_font_sync (font_control);
			setup_adw_group_add_row_owned (group, row);
			break;
		}
		case ST_NUMBER:
			group = setup_ensure_adw_group (page, group);
			row = setup_ui_action_row_new ();
			adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _(set[i].label));
			control = gtk_spin_button_new_with_range (0, set[i].extra > 0 ? set[i].extra : 999999, 1);
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (control), setup_get_int (&set[i]));
			g_signal_connect (control, "value-changed", G_CALLBACK (setup_spin_cb), (gpointer) &set[i]);
			adw_action_row_add_suffix (ADW_ACTION_ROW (row), control);
			adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), control);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			setup_adw_group_add_row_owned (group, row);
			break;
		case ST_MENU:
			group = setup_ensure_adw_group (page, group);
			row = setup_translated_combo_row_new (&set[i]);
			if (set[i].tooltip)
				gtk_widget_set_tooltip_text (row, _(set[i].tooltip));
			setup_adw_group_add_row_owned (group, row);
			break;
		default:
			break;
		}
	}

	return page;
}

static void
prefs_window_closed_cb (AdwDialog *dialog, gpointer userdata)
{
	(void) dialog;
	(void) userdata;

	save_config ();
	prefs_window = NULL;
}

void
setup_open (void)
{
	GtkWidget *page;
	int i;

	if (prefs_window)
	{
		adw_dialog_present (ADW_DIALOG (prefs_window), main_window);
		return;
	}

	prefs_window = setup_ui_window_new ();

	adw_preferences_dialog_set_search_enabled (ADW_PREFERENCES_DIALOG (prefs_window), TRUE);

	for (i = 0; setting_pages[i].title; i++)
	{
		const char *icon_name;

		page = setup_create_page_adw (setting_pages[i].settings);
		adw_preferences_page_set_title (ADW_PREFERENCES_PAGE (page), _(setting_pages[i].title));
		icon_name = (i < (int) G_N_ELEMENTS (setting_page_icons)) ? setting_page_icons[i] : NULL;
		if (icon_name && icon_name[0])
			adw_preferences_page_set_icon_name (ADW_PREFERENCES_PAGE (page), icon_name);
		adw_preferences_dialog_add (ADW_PREFERENCES_DIALOG (prefs_window), ADW_PREFERENCES_PAGE (page));
		g_object_unref (page);
	}

	g_signal_connect (prefs_window, "closed",
		G_CALLBACK (prefs_window_closed_cb), NULL);

	setup_apply_menu_visibility ();
	setup_apply_input_spell ();
	setup_apply_input_attr ();
	setup_apply_input_style ();
	setup_apply_text_font ();

	adw_dialog_present (ADW_DIALOG (prefs_window), main_window);
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

	adw_dialog_force_close (ADW_DIALOG (prefs_window));
	prefs_window = NULL;
}

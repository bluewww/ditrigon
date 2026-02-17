/* HexChat GTK4 tree-style channel view */
#include "fe-gtk4.h"
#include "../common/chanopt.h"
#include "../common/text.h"

typedef struct _HcChanNode
{
	GObject parent_instance;
	char *label;
	session *sess;
	server *serv;
	GListStore *children;
	GListStore *owner;
} HcChanNode;

typedef struct _HcChanNodeClass
{
	GObjectClass parent_class;
} HcChanNodeClass;

#define HC_TYPE_CHAN_NODE (hc_chan_node_get_type ())
GType hc_chan_node_get_type (void);
G_DEFINE_TYPE (HcChanNode, hc_chan_node, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_LABEL,
	PROP_LAST,
};

static GParamSpec *chan_node_props[PROP_LAST];

static GHashTable *tree_session_nodes;
static GHashTable *tree_server_nodes;
static GHashTable *tree_unread_counts;
static GListStore *tree_root_nodes;
static GtkTreeListModel *tree_model;
static GtkSingleSelection *tree_selection;
static GtkWidget *tree_scroller;
static GtkWidget *tree_view;
static gboolean tree_select_syncing;
static gboolean tree_css_loaded;

static session *tree_ctx_sess;
static GtkWidget *tree_ctx_popover;

static gboolean tree_group_by_server (void);
static const char *tree_server_state_text (server *serv);

static void
tree_load_css (void)
{
	GtkCssProvider *provider;
	GdkDisplay *display;

	if (tree_css_loaded)
		return;

	display = gdk_display_get_default ();
	if (!display)
		return;

	provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_string (provider,
		".hc-tree-row { border-radius: 8px; min-height: 28px; padding: 1px 2px; }"
		".hc-tree-label { padding: 1px 0; }"
		".hc-tree-subtitle { padding: 0; font-size: 0.82em; }"
		".hc-tree-icon { opacity: 0.72; }"
		".hc-tree-section { border-radius: 0; min-height: 22px; padding: 10px 4px 2px 4px; }"
		".hc-tree-section-label { color: alpha(@theme_fg_color, 0.70); font-size: 0.83em; font-weight: 700; }"
		".hc-tree-badge { min-width: 16px; padding: 0 6px; border-radius: 999px; margin: 0 4px 0 4px; font-size: 0.85em; font-weight: 700; }"
		".hc-tree-server { color: alpha(@theme_fg_color, 0.90); font-weight: 600; }"
		".hc-tree-data { color: @theme_fg_color; font-weight: 600; }"
		".hc-tree-msg { color: @theme_fg_color; font-weight: 600; }"
		".hc-tree-hilight { color: @theme_fg_color; font-weight: 700; }"
		".hc-tree-badge-data { background-color: alpha(@theme_fg_color, 0.18); color: @theme_fg_color; }"
		".hc-tree-badge-msg { background-color: alpha(@accent_bg_color, 0.25); color: @accent_fg_color; }"
		".hc-tree-badge-hilight { background-color: #e01b24; color: #ffffff; }"
		".hc-tree-disconnected { color: alpha(@theme_fg_color, 0.62); }");
	gtk_style_context_add_provider_for_display (display,
		GTK_STYLE_PROVIDER (provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (provider);
	tree_css_loaded = TRUE;
}

static const char *
tree_node_icon_name (HcChanNode *node)
{
	session *sess;

	if (!node)
		return "chat-message-new-symbolic";

	sess = node->sess;
	if (!sess)
	{
		if (node->children)
			return "network-workgroup-symbolic";
		return "chat-message-new-symbolic";
	}

	switch (sess->type)
	{
	case SESS_SERVER:
		return "network-server-symbolic";
	case SESS_CHANNEL:
		return "chat-message-new-symbolic";
	case SESS_DIALOG:
		return "user-available-symbolic";
	case SESS_NOTICES:
	case SESS_SNOTICES:
		return "dialog-information-symbolic";
	default:
		return "applications-system-symbolic";
	}
}

static void
tree_clear_state_classes (GtkWidget *row_box, GtkWidget *label)
{
	static const char *const classes[] =
	{
		"hc-tree-section",
		"hc-tree-server",
		"hc-tree-data",
		"hc-tree-msg",
		"hc-tree-hilight",
		"hc-tree-disconnected"
	};
	static const char *const label_classes[] =
	{
		"hc-tree-section-label",
		"hc-tree-server",
		"hc-tree-data",
		"hc-tree-msg",
		"hc-tree-hilight",
		"hc-tree-disconnected"
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (classes); i++)
	{
		if (row_box)
			gtk_widget_remove_css_class (row_box, classes[i]);
	}

	for (i = 0; i < G_N_ELEMENTS (label_classes); i++)
	{
		if (label)
			gtk_widget_remove_css_class (label, label_classes[i]);
	}
}

static void
tree_clear_badge_classes (GtkWidget *badge)
{
	static const char *const classes[] =
	{
		"hc-tree-badge-data",
		"hc-tree-badge-msg",
		"hc-tree-badge-hilight"
	};
	guint i;

	if (!badge)
		return;

	for (i = 0; i < G_N_ELEMENTS (classes); i++)
		gtk_widget_remove_css_class (badge, classes[i]);
}

static int
tree_unread_count_get (session *sess)
{
	if (!tree_unread_counts || !sess)
		return 0;

	return GPOINTER_TO_INT (g_hash_table_lookup (tree_unread_counts, sess));
}

static void
tree_unread_count_set (session *sess, int count)
{
	if (!tree_unread_counts || !sess)
		return;

	if (count <= 0)
	{
		g_hash_table_remove (tree_unread_counts, sess);
		return;
	}

	if (count > 999)
		count = 999;

	g_hash_table_replace (tree_unread_counts, sess, GINT_TO_POINTER (count));
}

static void
tree_unread_count_bump (session *sess)
{
	int count;

	if (!sess)
		return;

	count = tree_unread_count_get (sess);
	if (count < 999)
		count++;
	tree_unread_count_set (sess, count);
}

static void
tree_apply_state_classes (GtkWidget *row_box, GtkWidget *label, HcChanNode *node)
{
	session *sess;

	sess = node ? node->sess : NULL;
	tree_clear_state_classes (row_box, label);

	if (sess && sess->type == SESS_SERVER)
	{
		if (row_box)
			gtk_widget_add_css_class (row_box, "hc-tree-server");
		if (label)
			gtk_widget_add_css_class (label, "hc-tree-server");
	}

	if (sess && sess->server && !sess->server->connected)
	{
		if (row_box)
			gtk_widget_add_css_class (row_box, "hc-tree-disconnected");
		if (label)
			gtk_widget_add_css_class (label, "hc-tree-disconnected");
	}

	if (!sess || sess == current_tab)
		return;

	if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
	{
		if (row_box)
			gtk_widget_add_css_class (row_box, "hc-tree-hilight");
		if (label)
			gtk_widget_add_css_class (label, "hc-tree-hilight");
		return;
	}
	if (sess->tab_state & TAB_STATE_NEW_MSG)
	{
		if (row_box)
			gtk_widget_add_css_class (row_box, "hc-tree-msg");
		if (label)
			gtk_widget_add_css_class (label, "hc-tree-msg");
		return;
	}
	if (sess->tab_state & TAB_STATE_NEW_DATA)
	{
		if (row_box)
			gtk_widget_add_css_class (row_box, "hc-tree-data");
		if (label)
			gtk_widget_add_css_class (label, "hc-tree-data");
	}
}

static gboolean
tree_node_is_server_entry (HcChanNode *node)
{
	if (!node)
		return FALSE;

	return node->children != NULL;
}

static void
tree_update_list_item (GtkListItem *list_item, HcChanNode *node)
{
	GtkWidget *expander;
	GtkWidget *row_box;
	GtkWidget *icon;
	GtkWidget *label;
	GtkWidget *subtitle;
	GtkWidget *badge;
	session *sess;
	gboolean show_badge;
	gboolean server_entry;
	gboolean server_session_entry;
	int unread_count;
	char *tooltip_text;
	char unread_text[8];

	if (!list_item)
		return;

	expander = gtk_list_item_get_child (list_item);
	row_box = g_object_get_data (G_OBJECT (list_item), "hc-tree-row-box");
	icon = g_object_get_data (G_OBJECT (list_item), "hc-tree-icon");
	label = g_object_get_data (G_OBJECT (list_item), "hc-tree-label");
	subtitle = g_object_get_data (G_OBJECT (list_item), "hc-tree-subtitle");
	badge = g_object_get_data (G_OBJECT (list_item), "hc-tree-badge");
	sess = node ? node->sess : NULL;
	server_entry = tree_node_is_server_entry (node);
	server_session_entry = (sess && sess->type == SESS_SERVER && !server_entry);
	tooltip_text = NULL;

	if (row_box)
	{
		gtk_widget_set_margin_start (row_box, server_entry ? 0 : 8);
		gtk_widget_set_margin_end (row_box, 0);
	}

	if (label)
	{
		gtk_label_set_text (GTK_LABEL (label), node && node->label ? node->label : "");
	}

	if (subtitle)
	{
		if (server_session_entry)
		{
			const char *state_text = tree_server_state_text (sess->server);

			gtk_label_set_text (GTK_LABEL (subtitle), state_text);
			gtk_widget_set_visible (subtitle, TRUE);
			if (node && node->label && node->label[0])
				tooltip_text = g_strdup_printf ("%s (%s)", node->label, state_text);
		}
		else
		{
			gtk_label_set_text (GTK_LABEL (subtitle), "");
			gtk_widget_set_visible (subtitle, FALSE);
		}
	}

	if (!tooltip_text && node && node->label && node->label[0])
		tooltip_text = g_strdup (node->label);

	if (label)
		gtk_widget_set_tooltip_text (label, tooltip_text);

	if (icon)
	{
		if (prefs.hex_gui_tab_icons != 0 &&
			sess &&
			(sess->type == SESS_DIALOG || sess->type == SESS_NOTICES || sess->type == SESS_SNOTICES) &&
			!server_entry)
		{
			gtk_image_set_from_icon_name (GTK_IMAGE (icon), tree_node_icon_name (node));
			gtk_widget_set_visible (icon, TRUE);
		}
		else
		{
			gtk_widget_set_visible (icon, FALSE);
		}
	}

	if (expander)
	{
		gtk_tree_expander_set_hide_expander (GTK_TREE_EXPANDER (expander), TRUE);
		gtk_widget_set_tooltip_text (expander, tooltip_text);
	}

	if (badge)
	{
		tree_clear_badge_classes (badge);
		show_badge = FALSE;
		unread_count = tree_unread_count_get (sess);

		if (sess && sess != current_tab && !server_entry && unread_count > 0)
		{
			if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
			{
				gtk_widget_add_css_class (badge, "hc-tree-badge-hilight");
				show_badge = TRUE;
			}
			else if (sess->tab_state & TAB_STATE_NEW_MSG)
			{
				gtk_widget_add_css_class (badge, "hc-tree-badge-msg");
				show_badge = TRUE;
			}
			else if (sess->tab_state & TAB_STATE_NEW_DATA)
			{
				gtk_widget_add_css_class (badge, "hc-tree-badge-data");
				show_badge = TRUE;
			}
			else
			{
				gtk_widget_add_css_class (badge, "hc-tree-badge-data");
				show_badge = TRUE;
			}
		}

		if (show_badge)
		{
			if (unread_count > 99)
				g_strlcpy (unread_text, "99+", sizeof unread_text);
			else
				g_snprintf (unread_text, sizeof unread_text, "%d", unread_count);
			gtk_label_set_text (GTK_LABEL (badge), unread_text);
		}
		else
		{
			gtk_label_set_text (GTK_LABEL (badge), "");
		}

		gtk_widget_set_visible (badge, show_badge);
	}

	tree_apply_state_classes (row_box, label, node);

	if (server_entry)
	{
		if (row_box)
			gtk_widget_add_css_class (row_box, "hc-tree-section");
		if (label)
			gtk_widget_add_css_class (label, "hc-tree-section-label");
	}

	g_free (tooltip_text);
}

static gboolean
tree_toggle_initial (guint8 setting, int global_value)
{
	if (setting == SET_DEFAULT)
		return global_value != 0;
	return setting == SET_ON;
}

static void
tree_ctx_close_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) action;
	(void) param;
	(void) userdata;

	printf("tree_ctx_close_cb()\n");
	if (tree_ctx_sess)
	    printf("\ttree_ctx_sess=%p, is_session=%d\n", tree_ctx_sess, is_session (tree_ctx_sess));
	else
	    printf("\ttree_ctx_sess=NULL");


	if (tree_ctx_sess && is_session (tree_ctx_sess))
		fe_close_window (tree_ctx_sess);
	if (tree_ctx_popover)
		gtk_popover_popdown (GTK_POPOVER (tree_ctx_popover));
}

static void
tree_ctx_disconnect_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) action;
	(void) param;
	(void) userdata;

	if (tree_ctx_sess && is_session (tree_ctx_sess))
		handle_command (tree_ctx_sess, "DISCON", FALSE);
	if (tree_ctx_popover)
		gtk_popover_popdown (GTK_POPOVER (tree_ctx_popover));
}

static void
tree_ctx_reconnect_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) action;
	(void) param;
	(void) userdata;

	if (tree_ctx_sess && is_session (tree_ctx_sess))
		handle_command (tree_ctx_sess, "RECONNECT", FALSE);
	if (tree_ctx_popover)
		gtk_popover_popdown (GTK_POPOVER (tree_ctx_popover));
}

static void
tree_ctx_chanlist_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) action;
	(void) param;
	(void) userdata;

	if (tree_ctx_sess && is_session (tree_ctx_sess) && tree_ctx_sess->server)
		chanlist_opengui (tree_ctx_sess->server, TRUE);
	if (tree_ctx_popover)
		gtk_popover_popdown (GTK_POPOVER (tree_ctx_popover));
}

static void
tree_ctx_toggle_setting (GSimpleAction *action, guint8 *setting, gboolean is_logging)
{
	GVariant *state;
	gboolean active;
	guint8 old_value;

	if (!tree_ctx_sess || !is_session (tree_ctx_sess) || !setting)
		return;

	state = g_action_get_state (G_ACTION (action));
	active = !g_variant_get_boolean (state);
	g_variant_unref (state);

	old_value = *setting;
	*setting = active ? SET_ON : SET_OFF;

	if (is_logging && old_value != *setting)
		log_open_or_close (tree_ctx_sess);

	g_simple_action_set_state (action, g_variant_new_boolean (active));
	chanopt_save (tree_ctx_sess);
	chanopt_save_all (FALSE);
}

static void
tree_ctx_balloon_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) param;
	(void) userdata;
	if (tree_ctx_sess && is_session (tree_ctx_sess))
		tree_ctx_toggle_setting (action, &tree_ctx_sess->alert_balloon, FALSE);
}

static void
tree_ctx_beep_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) param;
	(void) userdata;
	if (tree_ctx_sess && is_session (tree_ctx_sess))
		tree_ctx_toggle_setting (action, &tree_ctx_sess->alert_beep, FALSE);
}

static void
tree_ctx_tray_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) param;
	(void) userdata;
	if (tree_ctx_sess && is_session (tree_ctx_sess))
		tree_ctx_toggle_setting (action, &tree_ctx_sess->alert_tray, FALSE);
}

static void
tree_ctx_taskbar_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) param;
	(void) userdata;
	if (tree_ctx_sess && is_session (tree_ctx_sess))
		tree_ctx_toggle_setting (action, &tree_ctx_sess->alert_taskbar, FALSE);
}

static void
tree_ctx_logging_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) param;
	(void) userdata;
	if (tree_ctx_sess && is_session (tree_ctx_sess))
		tree_ctx_toggle_setting (action, &tree_ctx_sess->text_logging, TRUE);
}

static void
tree_ctx_scrollback_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	(void) param;
	(void) userdata;
	if (tree_ctx_sess && is_session (tree_ctx_sess))
		tree_ctx_toggle_setting (action, &tree_ctx_sess->text_scrollback, FALSE);
}

static void
tree_ctx_autoconn_cb (GSimpleAction *action, GVariant *param, gpointer userdata)
{
	GVariant *state;
	ircnet *net;
	gboolean active;

	(void) param;
	(void) userdata;

	if (!tree_ctx_sess || !is_session (tree_ctx_sess) ||
		!tree_ctx_sess->server || !tree_ctx_sess->server->network)
		return;

	state = g_action_get_state (G_ACTION (action));
	active = !g_variant_get_boolean (state);
	g_variant_unref (state);

	net = (ircnet *) tree_ctx_sess->server->network;
	if (active)
		net->flags |= FLAG_AUTO_CONNECT;
	else
		net->flags &= ~FLAG_AUTO_CONNECT;

	g_simple_action_set_state (action, g_variant_new_boolean (active));
	servlist_save ();
}

static void
tree_show_context_menu (GtkWidget *parent, double x, double y, session *sess)
{
	GSimpleActionGroup *group;
	GMenu *menu;
	GMenu *section;
	GSimpleAction *action;
	gboolean is_priv;
	int global_balloon, global_beep, global_tray, global_taskbar;
	GdkRectangle rect;

	if (!parent || !sess || !is_session (sess))
		return;

	/* Clean up previous popover if still around */
	if (tree_ctx_popover)
	{
		gtk_widget_unparent (tree_ctx_popover);
		tree_ctx_popover = NULL;
	}

	tree_ctx_sess = sess;

	group = g_simple_action_group_new ();

	/* Regular actions */
	action = g_simple_action_new ("close", NULL);
	g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_close_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
	g_object_unref (action);

	if (sess->server)
	{
		if (sess->server->connected)
		{
			action = g_simple_action_new ("disconnect", NULL);
			g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_disconnect_cb), NULL);
			g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
			g_object_unref (action);
		}
		else
		{
			action = g_simple_action_new ("reconnect", NULL);
			g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_reconnect_cb), NULL);
			g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
			g_object_unref (action);
		}

		action = g_simple_action_new ("channel-list", NULL);
		g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_chanlist_cb), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);
	}

	/* Toggle actions */
	is_priv = sess->type == SESS_DIALOG;
	global_balloon = is_priv ? prefs.hex_input_balloon_priv : prefs.hex_input_balloon_chans;
	global_beep = is_priv ? prefs.hex_input_beep_priv : prefs.hex_input_beep_chans;
	global_tray = is_priv ? prefs.hex_input_tray_priv : prefs.hex_input_tray_chans;
	global_taskbar = is_priv ? prefs.hex_input_flash_priv : prefs.hex_input_flash_chans;

	action = g_simple_action_new_stateful ("show-notifications", NULL,
		g_variant_new_boolean (tree_toggle_initial (sess->alert_balloon, global_balloon)));
	g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_balloon_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new_stateful ("beep-on-message", NULL,
		g_variant_new_boolean (tree_toggle_initial (sess->alert_beep, global_beep)));
	g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_beep_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new_stateful ("blink-tray", NULL,
		g_variant_new_boolean (tree_toggle_initial (sess->alert_tray, global_tray)));
	g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_tray_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new_stateful ("blink-taskbar", NULL,
		g_variant_new_boolean (tree_toggle_initial (sess->alert_taskbar, global_taskbar)));
	g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_taskbar_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new_stateful ("log-to-disk", NULL,
		g_variant_new_boolean (tree_toggle_initial (sess->text_logging, prefs.hex_irc_logging)));
	g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_logging_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
	g_object_unref (action);

	action = g_simple_action_new_stateful ("reload-scrollback", NULL,
		g_variant_new_boolean (tree_toggle_initial (sess->text_scrollback, prefs.hex_text_replay)));
	g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_scrollback_cb), NULL);
	g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
	g_object_unref (action);

	if (sess->type == SESS_SERVER && sess->server && sess->server->network)
	{
		action = g_simple_action_new_stateful ("auto-connect", NULL,
			g_variant_new_boolean ((((ircnet *) sess->server->network)->flags & FLAG_AUTO_CONNECT) != 0));
		g_signal_connect (action, "activate", G_CALLBACK (tree_ctx_autoconn_cb), NULL);
		g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
		g_object_unref (action);
	}


	gtk_widget_insert_action_group (parent, "chan", G_ACTION_GROUP (group));

	/* Build the menu model */
	menu = g_menu_new ();

	section = g_menu_new ();
	g_menu_append (section, _("Close"), "chan.close");
	if (sess->server)
	{
		if (sess->server->connected)
			g_menu_append (section, _("Disconnect"), "chan.disconnect");
		else
			g_menu_append (section, _("Reconnect"), "chan.reconnect");
		g_menu_append (section, _("Channel List"), "chan.channel-list");
	}
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	g_menu_append (section, _("Show Notifications"), "chan.show-notifications");
	g_menu_append (section, _("Beep on Message"), "chan.beep-on-message");
	g_menu_append (section, _("Blink Tray Icon"), "chan.blink-tray");
	g_menu_append (section, _("Blink Task Bar"), "chan.blink-taskbar");
	g_menu_append_section (menu, _("Notifications"), G_MENU_MODEL (section));
	g_object_unref (section);

	section = g_menu_new ();
	g_menu_append (section, _("Log to Disk"), "chan.log-to-disk");
	g_menu_append (section, _("Reload Scrollback"), "chan.reload-scrollback");
	if (sess->type == SESS_SERVER && sess->server && sess->server->network)
		g_menu_append (section, _("Auto-Connect"), "chan.auto-connect");
	g_menu_append_section (menu, _("Session Settings"), G_MENU_MODEL (section));
	g_object_unref (section);

	/* Create the popover menu */
	tree_ctx_popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
	g_object_add_weak_pointer (G_OBJECT (tree_ctx_popover),
		(gpointer *) &tree_ctx_popover);
	gtk_widget_set_parent (tree_ctx_popover, parent);

	g_object_set_data_full (G_OBJECT (tree_ctx_popover), "chan-actions",
		g_object_ref (group), g_object_unref);

	rect.x = (int) x;
	rect.y = (int) y;
	rect.width = 1;
	rect.height = 1;
	gtk_popover_set_pointing_to (GTK_POPOVER (tree_ctx_popover), &rect);
	gtk_popover_popup (GTK_POPOVER (tree_ctx_popover));

	g_object_unref (menu);
	g_object_unref (group);
}

static void
tree_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	guint pos;
	GtkTreeListRow *row;
	HcChanNode *node;
	session *sess;

	(void) user_data;

	if (n_press != 1 || !tree_selection || !tree_model || !tree_view)
		return;

	pos = gtk_single_selection_get_selected (tree_selection);
	if (pos == GTK_INVALID_LIST_POSITION)
		return;

	row = g_list_model_get_item (G_LIST_MODEL (tree_model), pos);
	if (!row)
		return;

	node = (HcChanNode *) gtk_tree_list_row_get_item (row);
	sess = node ? node->sess : NULL;
	if ((!sess || !is_session (sess)) && node && node->serv &&
		node->serv->server_session && is_session (node->serv->server_session))
		sess = node->serv->server_session;

	if (sess && is_session (sess))
	{
		tree_show_context_menu (tree_view, x, y, sess);
		gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	}

	g_object_unref (row);
}

static void
hc_chan_node_set_label (HcChanNode *node, const char *label)
{
	if (g_strcmp0 (node->label, label) == 0)
		return;

	g_free (node->label);
	node->label = g_strdup (label ? label : "");
	g_object_notify_by_pspec (G_OBJECT (node), chan_node_props[PROP_LABEL]);
}

static void
hc_chan_node_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	HcChanNode *node;

	node = (HcChanNode *) object;

	switch (prop_id)
	{
	case PROP_LABEL:
		g_value_set_string (value, node->label);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
hc_chan_node_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	HcChanNode *node;

	node = (HcChanNode *) object;

	switch (prop_id)
	{
	case PROP_LABEL:
		hc_chan_node_set_label (node, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
hc_chan_node_finalize (GObject *object)
{
	HcChanNode *node;

	node = (HcChanNode *) object;
	g_free (node->label);
	g_clear_object (&node->children);

	G_OBJECT_CLASS (hc_chan_node_parent_class)->finalize (object);
}

static void
hc_chan_node_class_init (HcChanNodeClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = hc_chan_node_get_property;
	object_class->set_property = hc_chan_node_set_property;
	object_class->finalize = hc_chan_node_finalize;

	chan_node_props[PROP_LABEL] = g_param_spec_string (
		"label", "Label", "Display label", "",
		G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
	g_object_class_install_properties (object_class, PROP_LAST, chan_node_props);
}

static void
hc_chan_node_init (HcChanNode *node)
{
	node->label = g_strdup ("");
}

static HcChanNode *
hc_chan_node_new (const char *label, session *sess, server *serv, gboolean with_children)
{
	HcChanNode *node;

	node = g_object_new (HC_TYPE_CHAN_NODE, "label", label ? label : "", NULL);
	node->sess = sess;
	node->serv = serv;
	if (with_children)
		node->children = g_list_store_new (HC_TYPE_CHAN_NODE);

	return node;
}

static void
tree_store_append_node (GListStore *store, HcChanNode *node)
{
	if (!store || !node)
		return;

	node->owner = store;
	g_list_store_append (store, node);
}

static gboolean
tree_store_remove_node (GListStore *store, HcChanNode *node)
{
	guint i;
	guint n_items;

	if (!store || !node)
		return FALSE;

	n_items = g_list_model_get_n_items (G_LIST_MODEL (store));
	for (i = 0; i < n_items; i++)
	{
		HcChanNode *item;

		item = g_list_model_get_item (G_LIST_MODEL (store), i);
		if (item == node)
		{
			g_list_store_remove (store, i);
			if (node->owner == store)
				node->owner = NULL;
			g_object_unref (item);
			return TRUE;
		}

		g_object_unref (item);
	}

	return FALSE;
}

static HcChanNode *
tree_session_lookup (session *sess)
{
	if (!tree_session_nodes || !sess)
		return NULL;

	return g_hash_table_lookup (tree_session_nodes, sess);
}

static void
tree_session_store (session *sess, HcChanNode *node)
{
	if (!tree_session_nodes || !sess || !node)
		return;

	g_hash_table_replace (tree_session_nodes, sess, g_object_ref (node));
}

static void
tree_refresh_node (HcChanNode *node)
{
	if (!node)
		return;

	g_object_notify_by_pspec (G_OBJECT (node), chan_node_props[PROP_LABEL]);
}

static HcChanNode *
tree_server_lookup (server *serv)
{
	if (!tree_server_nodes || !serv)
		return NULL;

	return g_hash_table_lookup (tree_server_nodes, serv);
}

static void
tree_server_store (server *serv, HcChanNode *node)
{
	if (!tree_server_nodes || !serv || !node)
		return;

	g_hash_table_replace (tree_server_nodes, serv, g_object_ref (node));
}

static char *
tree_server_label (server *serv)
{
	const char *network;

	if (!serv)
		return g_strdup (_("Server"));

	network = server_get_network (serv, TRUE);
	if (!network || !network[0])
		network = serv->servername[0] ? serv->servername : _("Server");

	return g_strdup (network);
}

static const char *
tree_server_state_text (server *serv)
{
	if (!serv)
		return _("Disconnected");

	if (serv->connected)
		return _("Connected");

	if (serv->connecting)
		return _("Connecting");

	return _("Disconnected");
}

static char *
tree_server_session_label (server *serv)
{
	const char *target;

	if (!serv)
		return g_strdup (_("Server"));

	target = serv->servername[0] ? serv->servername : serv->hostname;
	if (!target || !target[0])
		target = server_get_network (serv, TRUE);
	if (!target || !target[0])
		target = _("Server");

	return g_strdup (target);
}

static char *
tree_session_label (session *sess)
{
	char *base;
	const char *network;

	if (!sess)
		return g_strdup (_("<none>"));

	if (sess->type == SESS_SERVER)
	{
		if (tree_group_by_server ())
			return tree_server_session_label (sess->server);

		network = server_get_network (sess->server, TRUE);
		if (!network || !network[0])
			network = sess->server && sess->server->servername[0] ? sess->server->servername : _("Server");
		base = g_strdup (network);
	}
	else if (sess->type == SESS_CHANNEL)
	{
		if (sess->channel[0])
			base = g_strdup (sess->channel);
		else if (sess->waitchannel[0])
			base = g_strdup (sess->waitchannel);
		else if (sess->server)
		{
			network = server_get_network (sess->server, TRUE);
			if (!network || !network[0])
				network = sess->server->servername[0] ? sess->server->servername : _("Server");
			base = g_strdup (network);
		}
		else
			base = g_strdup (_("Channel"));
	}
	else if (sess->channel[0])
		base = g_strdup (sess->channel);
	else if (sess->type == SESS_DIALOG)
		base = g_strdup (_("Dialog"));
	else if (sess->type == SESS_NOTICES || sess->type == SESS_SNOTICES)
		base = g_strdup (_("Notices"));
	else
		base = g_strdup (_("Session"));

	return base;
}

static gboolean
tree_model_find_position_for_node (HcChanNode *node, guint *position)
{
	guint i;
	guint n_items;

	if (!tree_model || !node)
		return FALSE;

	n_items = g_list_model_get_n_items (G_LIST_MODEL (tree_model));
	for (i = 0; i < n_items; i++)
	{
		GtkTreeListRow *row;
		HcChanNode *item;

		row = g_list_model_get_item (G_LIST_MODEL (tree_model), i);
		if (!row)
			continue;

		item = (HcChanNode *) gtk_tree_list_row_get_item (row);
		if (item == node)
		{
			if (position)
				*position = i;
			g_object_unref (row);
			return TRUE;
		}

		g_object_unref (row);
	}

	return FALSE;
}

static void
tree_expand_node (HcChanNode *node)
{
	guint position;
	GtkTreeListRow *row;

	if (!tree_model_find_position_for_node (node, &position))
		return;

	row = g_list_model_get_item (G_LIST_MODEL (tree_model), position);
	if (!row)
		return;

	if (gtk_tree_list_row_is_expandable (row) && !gtk_tree_list_row_get_expanded (row))
		gtk_tree_list_row_set_expanded (row, TRUE);

	g_object_unref (row);
}

static GListModel *
tree_create_child_model_cb (gpointer item, gpointer user_data)
{
	HcChanNode *node;

	(void) user_data;

	node = (HcChanNode *) item;
	if (!node || !node->children)
		return NULL;

	return G_LIST_MODEL (g_object_ref (node->children));
}

static HcChanNode *
tree_ensure_server_node (server *serv)
{
	HcChanNode *node;
	char *label;

	node = tree_server_lookup (serv);
	if (node)
		return node;

	label = tree_server_label (serv);
	node = hc_chan_node_new (label, NULL, serv, TRUE);
	g_free (label);

	tree_store_append_node (tree_root_nodes, node);
	tree_server_store (serv, node);

	g_object_unref (node);
	return tree_server_lookup (serv);
}

static gboolean
tree_group_by_server (void)
{
	return prefs.hex_gui_tab_server != 0;
}

static void
tree_update_selected_session (void)
{
	guint pos;
	GtkTreeListRow *row;
	HcChanNode *node;

	/* printf("selecting tree stuff\n"); */
	if (tree_select_syncing || !tree_selection || !tree_model)
		return;

	pos = gtk_single_selection_get_selected (tree_selection);
	if (pos == GTK_INVALID_LIST_POSITION)
		return;

	row = g_list_model_get_item (G_LIST_MODEL (tree_model), pos);
	if (!row)
		return;

	node = (HcChanNode *) gtk_tree_list_row_get_item (row);
	if (node && node->sess && is_session (node->sess))
		fe_set_channel (node->sess);

	g_object_unref (row);
}

static void
tree_selection_notify_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	(void) object;
	(void) pspec;
	(void) user_data;
	tree_update_selected_session ();
}

static void
tree_children_changed_cb (GListModel *model, guint position, guint removed, guint added, gpointer user_data)
{
	GtkListItem *list_item;
	GtkTreeListRow *row;
	HcChanNode *node;

	(void) model;
	(void) position;
	(void) removed;
	(void) added;

	list_item = GTK_LIST_ITEM (user_data);
	row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item));
	node = row ? (HcChanNode *) gtk_tree_list_row_get_item (row) : NULL;
	tree_update_list_item (list_item, node);
}

static void
tree_label_notify_cb (HcChanNode *node, GParamSpec *pspec, gpointer user_data)
{
	GtkListItem *list_item;

	(void) pspec;

	list_item = GTK_LIST_ITEM (user_data);
	tree_update_list_item (list_item, node);
}

static void
tree_factory_setup_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *expander;
	GtkWidget *row_box;
	GtkWidget *icon;
	GtkWidget *label_box;
	GtkWidget *label;
	GtkWidget *subtitle;
	GtkWidget *badge;

	(void) factory;
	(void) user_data;

	expander = gtk_tree_expander_new ();
	gtk_tree_expander_set_indent_for_depth (GTK_TREE_EXPANDER (expander), FALSE);
	gtk_tree_expander_set_indent_for_icon (GTK_TREE_EXPANDER (expander), FALSE);
	gtk_tree_expander_set_hide_expander (GTK_TREE_EXPANDER (expander), TRUE);
	row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_add_css_class (row_box, "hc-tree-row");
	gtk_widget_set_hexpand (row_box, TRUE);

	icon = gtk_image_new_from_icon_name ("chat-message-new-symbolic");
	gtk_widget_set_size_request (icon, 16, 16);
	gtk_widget_add_css_class (icon, "hc-tree-icon");
	gtk_widget_set_valign (icon, GTK_ALIGN_CENTER);
	gtk_box_append (GTK_BOX (row_box), icon);

	label_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_hexpand (label_box, TRUE);
	gtk_widget_set_halign (label_box, GTK_ALIGN_FILL);
	gtk_widget_set_valign (label_box, GTK_ALIGN_CENTER);
	gtk_box_append (GTK_BOX (row_box), label_box);

	label = gtk_label_new ("");
	gtk_widget_add_css_class (label, "hc-tree-label");
	gtk_widget_set_hexpand (label, TRUE);
	gtk_widget_set_halign (label, GTK_ALIGN_FILL);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_label_set_single_line_mode (GTK_LABEL (label), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_box_append (GTK_BOX (label_box), label);

	subtitle = gtk_label_new ("");
	gtk_widget_add_css_class (subtitle, "hc-tree-subtitle");
	gtk_widget_add_css_class (subtitle, "dim-label");
	gtk_widget_set_halign (subtitle, GTK_ALIGN_FILL);
	gtk_label_set_xalign (GTK_LABEL (subtitle), 0.0f);
	gtk_label_set_single_line_mode (GTK_LABEL (subtitle), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (subtitle), PANGO_ELLIPSIZE_END);
	gtk_widget_set_visible (subtitle, FALSE);
	gtk_box_append (GTK_BOX (label_box), subtitle);

	badge = gtk_label_new (NULL);
	gtk_widget_add_css_class (badge, "hc-tree-badge");
	gtk_widget_set_valign (badge, GTK_ALIGN_CENTER);
	gtk_widget_set_visible (badge, FALSE);
	gtk_box_append (GTK_BOX (row_box), badge);

	gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), row_box);
	gtk_list_item_set_child (list_item, expander);

	g_object_set_data (G_OBJECT (list_item), "hc-tree-row-box", row_box);
	g_object_set_data (G_OBJECT (list_item), "hc-tree-icon", icon);
	g_object_set_data (G_OBJECT (list_item), "hc-tree-label", label);
	g_object_set_data (G_OBJECT (list_item), "hc-tree-subtitle", subtitle);
	g_object_set_data (G_OBJECT (list_item), "hc-tree-badge", badge);
}

static void
tree_factory_bind_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkTreeListRow *row;
	HcChanNode *node;
	GtkWidget *expander;

	(void) factory;
	(void) user_data;

	row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item));
	node = row ? (HcChanNode *) gtk_tree_list_row_get_item (row) : NULL;

	expander = gtk_list_item_get_child (list_item);
	if (!expander)
		return;

	gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), row);
	tree_update_list_item (list_item, node);

	if (node)
	{
		g_signal_connect (node, "notify::label", G_CALLBACK (tree_label_notify_cb), list_item);
		if (node->children)
			g_signal_connect (node->children, "items-changed",
				G_CALLBACK (tree_children_changed_cb), list_item);
	}
}

static void
tree_factory_unbind_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkTreeListRow *row;
	HcChanNode *node;
	GtkWidget *expander;
	GtkWidget *row_box;
	GtkWidget *label;
	GtkWidget *subtitle;
	GtkWidget *icon;
	GtkWidget *badge;

	(void) factory;
	(void) user_data;

	row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item));
	node = row ? (HcChanNode *) gtk_tree_list_row_get_item (row) : NULL;

	if (node)
	{
		g_signal_handlers_disconnect_by_data (node, list_item);
		if (node->children)
			g_signal_handlers_disconnect_by_data (node->children, list_item);
	}

	expander = gtk_list_item_get_child (list_item);
	if (!expander)
		return;

	row_box = g_object_get_data (G_OBJECT (list_item), "hc-tree-row-box");
	label = g_object_get_data (G_OBJECT (list_item), "hc-tree-label");
	subtitle = g_object_get_data (G_OBJECT (list_item), "hc-tree-subtitle");
	icon = g_object_get_data (G_OBJECT (list_item), "hc-tree-icon");
	badge = g_object_get_data (G_OBJECT (list_item), "hc-tree-badge");

	gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), NULL);
	if (row_box)
		tree_clear_state_classes (row_box, label);
	if (label)
	{
		gtk_label_set_text (GTK_LABEL (label), "");
		gtk_widget_set_tooltip_text (label, NULL);
	}
	if (subtitle)
	{
		gtk_label_set_text (GTK_LABEL (subtitle), "");
		gtk_widget_set_visible (subtitle, FALSE);
	}
	if (icon)
		gtk_image_set_from_icon_name (GTK_IMAGE (icon), "chat-message-new-symbolic");
	if (badge)
	{
		tree_clear_badge_classes (badge);
		gtk_label_set_text (GTK_LABEL (badge), "");
		gtk_widget_set_visible (badge, FALSE);
	}
	gtk_widget_set_tooltip_text (expander, NULL);
}

void
fe_gtk4_chanview_tree_init (void)
{
	if (!tree_session_nodes)
		tree_session_nodes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			NULL, g_object_unref);
	if (!tree_server_nodes)
		tree_server_nodes = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			NULL, g_object_unref);
	if (!tree_unread_counts)
		tree_unread_counts = g_hash_table_new (g_direct_hash, g_direct_equal);

	tree_select_syncing = FALSE;
}

void
fe_gtk4_chanview_tree_cleanup (void)
{
	if (tree_session_nodes)
	{
		g_hash_table_unref (tree_session_nodes);
		tree_session_nodes = NULL;
	}
	if (tree_server_nodes)
	{
		g_hash_table_unref (tree_server_nodes);
		tree_server_nodes = NULL;
	}
	if (tree_unread_counts)
	{
		g_hash_table_unref (tree_unread_counts);
		tree_unread_counts = NULL;
	}

	if (tree_ctx_popover)
	{
		gtk_widget_unparent (tree_ctx_popover);
		tree_ctx_popover = NULL;
	}
	tree_ctx_sess = NULL;

	g_clear_object (&tree_selection);
	g_clear_object (&tree_model);
	g_clear_object (&tree_root_nodes);

	tree_scroller = NULL;
	tree_view = NULL;
	tree_select_syncing = FALSE;
}

GtkWidget *
fe_gtk4_chanview_tree_create_widget (void)
{
	GtkListItemFactory *factory;

	if (!tree_scroller)
	{
		tree_load_css ();
		tree_scroller = gtk_scrolled_window_new ();
		gtk_widget_set_hexpand (tree_scroller, FALSE);
		gtk_widget_set_vexpand (tree_scroller, TRUE);
		gtk_scrolled_window_set_has_frame (GTK_SCROLLED_WINDOW (tree_scroller), FALSE);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (tree_scroller),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		tree_root_nodes = g_list_store_new (HC_TYPE_CHAN_NODE);
		tree_model = gtk_tree_list_model_new (
			G_LIST_MODEL (g_object_ref (tree_root_nodes)),
			FALSE, TRUE, tree_create_child_model_cb, NULL, NULL);
		tree_selection = gtk_single_selection_new (
			G_LIST_MODEL (g_object_ref (tree_model)));
		gtk_single_selection_set_autoselect (tree_selection, FALSE);
		gtk_single_selection_set_can_unselect (tree_selection, TRUE);

		factory = gtk_signal_list_item_factory_new ();
		g_signal_connect (factory, "setup", G_CALLBACK (tree_factory_setup_cb), NULL);
		g_signal_connect (factory, "bind", G_CALLBACK (tree_factory_bind_cb), NULL);
		g_signal_connect (factory, "unbind", G_CALLBACK (tree_factory_unbind_cb), NULL);

		tree_view = gtk_list_view_new (
			GTK_SELECTION_MODEL (g_object_ref (tree_selection)),
			GTK_LIST_ITEM_FACTORY (g_object_ref (factory)));
		gtk_list_view_set_single_click_activate (GTK_LIST_VIEW (tree_view), FALSE);
		gtk_list_view_set_show_separators (GTK_LIST_VIEW (tree_view), FALSE);
		gtk_widget_add_css_class (tree_view, "navigation-sidebar");
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (tree_scroller), tree_view);
		g_object_unref (factory);

		g_signal_connect (tree_selection, "notify::selected",
			G_CALLBACK (tree_selection_notify_cb), NULL);
		{
			GtkGesture *gesture = gtk_gesture_click_new ();
			gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
			g_signal_connect (gesture, "pressed", G_CALLBACK (tree_right_click_cb), NULL);
			gtk_widget_add_controller (tree_view, GTK_EVENT_CONTROLLER (gesture));
		}
	}

	session_scroller = tree_scroller;
	session_list = tree_view;

	return tree_scroller;
}

void
fe_gtk4_chanview_tree_update_label (session *sess)
{
	HcChanNode *node;
	HcChanNode *server_node;
	char *label;
	gboolean grouped;

	if (!sess)
		return;

	grouped = tree_group_by_server ();
	node = tree_session_lookup (sess);
	if (!node)
		return;

	label = tree_session_label (sess);

	hc_chan_node_set_label (node, label);
	g_free (label);
	tree_refresh_node (node);

	if (!grouped || !sess->server)
		return;

	server_node = tree_server_lookup (sess->server);
	if (!server_node)
		return;

	label = tree_server_label (sess->server);
	hc_chan_node_set_label (server_node, label);
	g_free (label);
	if (server_node != node)
		tree_refresh_node (server_node);
}

void
fe_gtk4_chanview_tree_add (session *sess)
{
	HcChanNode *node;
	HcChanNode *server_node;
	GListStore *store;
	char *label;
	gboolean grouped;

	if (!tree_root_nodes || !sess)
		return;

	node = tree_session_lookup (sess);
	if (node)
	{
		fe_gtk4_chanview_tree_update_label (sess);
		return;
	}

	grouped = tree_group_by_server ();
	if (!grouped)
	{
		label = tree_session_label (sess);
		node = hc_chan_node_new (label, sess, sess->server, FALSE);
		g_free (label);
		tree_store_append_node (tree_root_nodes, node);
		tree_session_store (sess, node);
		g_object_unref (node);
		return;
	}

	if (sess->type == SESS_SERVER && sess->server)
	{
		server_node = tree_ensure_server_node (sess->server);
		if (!server_node)
			return;

		label = tree_session_label (sess);
		node = hc_chan_node_new (label, sess, sess->server, FALSE);
		g_free (label);
		tree_store_append_node (server_node->children, node);
		tree_session_store (sess, node);
		g_object_unref (node);
		return;
	}

	server_node = NULL;
	store = tree_root_nodes;
	if (sess->server)
	{
		server_node = tree_ensure_server_node (sess->server);
		if (server_node && server_node->children)
			store = server_node->children;
	}

	label = tree_session_label (sess);
	node = hc_chan_node_new (label, sess, sess->server, FALSE);
	g_free (label);
	tree_store_append_node (store, node);
	tree_session_store (sess, node);
	g_object_unref (node);

	if (server_node)
		tree_expand_node (server_node);
}

void
fe_gtk4_chanview_tree_remove (session *sess)
{
	HcChanNode *node;
	HcChanNode *server_node;
	gboolean grouped;
	guint n_children;

	if (!tree_root_nodes || !tree_session_nodes || !sess)
		return;

	if (tree_unread_counts)
		g_hash_table_remove (tree_unread_counts, sess);

	node = tree_session_lookup (sess);
	if (!node)
	{
		g_hash_table_remove (tree_session_nodes, sess);
		return;
	}

	if (node->owner)
		tree_store_remove_node (node->owner, node);
	g_hash_table_remove (tree_session_nodes, sess);

	grouped = tree_group_by_server ();
	if (!grouped || !sess->server)
		return;

	server_node = tree_server_lookup (sess->server);
	if (!server_node || server_node == node)
		return;

	n_children = server_node->children ?
		g_list_model_get_n_items (G_LIST_MODEL (server_node->children)) : 0;
	if (n_children > 0)
		return;

	if (server_node->owner)
		tree_store_remove_node (server_node->owner, server_node);
	g_hash_table_remove (tree_server_nodes, sess->server);
}

void
fe_gtk4_chanview_tree_select (session *sess)
{
	HcChanNode *node;
	HcChanNode *server_node;
	guint pos;
	gboolean grouped;

	if (!tree_selection || !tree_model || !sess)
		return;

	tree_unread_count_set (sess, 0);

	node = tree_session_lookup (sess);
	if (!node)
		return;

	grouped = tree_group_by_server ();
	if (grouped && sess->server)
	{
		server_node = tree_server_lookup (sess->server);
		if (server_node && server_node != node)
			tree_expand_node (server_node);
	}

	if (!tree_model_find_position_for_node (node, &pos))
		return;

	tree_select_syncing = TRUE;
	gtk_single_selection_set_selected (tree_selection, pos);
	tree_select_syncing = FALSE;
}

void
fe_gtk4_chanview_tree_note_activity (session *sess, int color)
{
	if (!sess || !is_session (sess))
		return;

	if (color == FE_COLOR_NONE || sess == current_tab)
	{
		tree_unread_count_set (sess, 0);
		return;
	}

	switch (color)
	{
	case FE_COLOR_NEW_DATA:
	case FE_COLOR_NEW_MSG:
	case FE_COLOR_NEW_HILIGHT:
		tree_unread_count_bump (sess);
		break;
	default:
		break;
	}
}

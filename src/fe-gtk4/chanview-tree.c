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
static GListStore *tree_root_nodes;
static GtkTreeListModel *tree_model;
static GtkSingleSelection *tree_selection;
static GtkWidget *tree_scroller;
static GtkWidget *tree_view;
static gboolean tree_select_syncing;

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

static gboolean tree_group_by_server (void);

static gboolean
tree_node_has_children (HcChanNode *node)
{
	if (!node || !node->children)
		return FALSE;

	return g_list_model_get_n_items (G_LIST_MODEL (node->children)) > 0;
}

static session *
tree_popover_get_session (GtkWidget *popover)
{
	session *sess;

	sess = g_object_get_data (G_OBJECT (popover), "hc-session");
	if (!sess || !is_session (sess))
		return NULL;

	return sess;
}

static void
tree_popover_close_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tree_popover_get_session (popover);
	if (sess)
		fe_close_window (sess);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tree_popover_detach_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tree_popover_get_session (popover);
	if (sess)
		handle_command (sess, "GUI DETACH", FALSE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tree_popover_disconnect_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tree_popover_get_session (popover);
	if (sess)
		handle_command (sess, "DISCON", FALSE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tree_popover_reconnect_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tree_popover_get_session (popover);
	if (sess)
		handle_command (sess, "RECONNECT", FALSE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tree_popover_chanlist_cb (GtkWidget *button, gpointer userdata)
{
	GtkWidget *popover;
	session *sess;

	(void) button;
	popover = GTK_WIDGET (userdata);
	sess = tree_popover_get_session (popover);
	if (sess && sess->server)
		chanlist_opengui (sess->server, TRUE);
	gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
tree_popover_session_toggle_cb (GtkCheckButton *check, gpointer userdata)
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
tree_popover_autoconn_cb (GtkCheckButton *check, gpointer userdata)
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
tree_popover_add_action (GtkWidget *box, const char *label, GCallback cb, GtkWidget *popover)
{
	GtkWidget *button;

	button = gtk_button_new_with_mnemonic (label);
	gtk_widget_set_halign (button, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (button, TRUE);
	g_signal_connect (button, "clicked", cb, popover);
	gtk_box_append (GTK_BOX (box), button);
	return button;
}

static GtkWidget *
tree_popover_add_toggle (GtkWidget *box, const char *label, gboolean active, GCallback cb,
	gpointer data, GClosureNotify destroy_data)
{
	GtkWidget *check;

	check = gtk_check_button_new_with_mnemonic (label);
	gtk_check_button_set_active (GTK_CHECK_BUTTON (check), active);
	g_signal_connect_data (check, "toggled", cb, data, destroy_data, 0);
	gtk_box_append (GTK_BOX (box), check);
	return check;
}

static void
tree_popover_add_section_title (GtkWidget *box, const char *title)
{
	GtkWidget *label;

	label = gtk_label_new (title);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_widget_add_css_class (label, "heading");
	gtk_box_append (GTK_BOX (box), label);
}

static gboolean
tree_toggle_initial (guint8 setting, int global_value)
{
	if (setting == SET_DEFAULT)
		return global_value != 0;
	return setting == SET_ON;
}

static void
tree_show_context_menu (GtkWidget *parent, double x, double y, session *sess)
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
	gtk_popover_set_child (GTK_POPOVER (popover), box);

	tree_popover_add_action (box, _("_Close"), G_CALLBACK (tree_popover_close_cb), popover);
	tree_popover_add_action (box, _("_Detach"), G_CALLBACK (tree_popover_detach_cb), popover);

	if (sess->server)
	{
		tree_popover_add_action (box,
			sess->server->connected ? _("_Disconnect") : _("_Reconnect"),
			sess->server->connected ? G_CALLBACK (tree_popover_disconnect_cb) : G_CALLBACK (tree_popover_reconnect_cb),
			popover);
		tree_popover_add_action (box, _("Channel _List"), G_CALLBACK (tree_popover_chanlist_cb), popover);

		if (sess->type == SESS_SERVER && sess->server->network)
		{
			HcServerToggleData *auto_data;
			gboolean active;

			auto_data = g_new0 (HcServerToggleData, 1);
			auto_data->popover = popover;
			auto_data->serv = sess->server;
			active = (((ircnet *) sess->server->network)->flags & FLAG_AUTO_CONNECT) != 0;
			tree_popover_add_toggle (box, _("_Auto-Connect"), active,
				G_CALLBACK (tree_popover_autoconn_cb), auto_data, (GClosureNotify) g_free);
		}
	}

	gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
	tree_popover_add_section_title (box, _("Extra Alerts"));

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
	tree_popover_add_toggle (box, _("Show Notifications"),
		tree_toggle_initial (sess->alert_balloon, alert_balloon),
		G_CALLBACK (tree_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->alert_beep;
	tree_popover_add_toggle (box, _("Beep on _Message"),
		tree_toggle_initial (sess->alert_beep, alert_beep),
		G_CALLBACK (tree_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->alert_tray;
	tree_popover_add_toggle (box, _("Blink Tray _Icon"),
		tree_toggle_initial (sess->alert_tray, alert_tray),
		G_CALLBACK (tree_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->alert_taskbar;
	tree_popover_add_toggle (box, _("Blink Task _Bar"),
		tree_toggle_initial (sess->alert_taskbar, alert_taskbar),
		G_CALLBACK (tree_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));
	tree_popover_add_section_title (box, _("Settings"));

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->text_logging;
	toggle_data->logging_setting = TRUE;
	tree_popover_add_toggle (box, _("_Log to Disk"),
		tree_toggle_initial (sess->text_logging, prefs.hex_irc_logging),
		G_CALLBACK (tree_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	toggle_data = g_new0 (HcSessionToggleData, 1);
	toggle_data->popover = popover;
	toggle_data->sess = sess;
	toggle_data->setting = &sess->text_scrollback;
	tree_popover_add_toggle (box, _("_Reload Scrollback"),
		tree_toggle_initial (sess->text_scrollback, prefs.hex_text_replay),
		G_CALLBACK (tree_popover_session_toggle_cb), toggle_data, (GClosureNotify) g_free);

	rect.x = (int) x;
	rect.y = (int) y;
	rect.width = 1;
	rect.height = 1;
	gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);
	gtk_popover_popup (GTK_POPOVER (popover));
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
		return g_strdup (_("[Server]"));

	network = server_get_network (serv, TRUE);
	if (!network || !network[0])
		network = serv->servername[0] ? serv->servername : _("Server");

	return g_strdup_printf ("[%s]", network);
}

static char *
tree_session_label (session *sess)
{
	char *base;
	char *full;
	const char *prefix;
	const char *network;

	if (!sess)
		return g_strdup (_("<none>"));

	if (sess->type == SESS_SERVER)
	{
		network = server_get_network (sess->server, TRUE);
		if (!network || !network[0])
			network = sess->server && sess->server->servername[0] ? sess->server->servername : _("Server");
		base = g_strdup_printf ("[%s]", network);
	}
	else if (!sess->channel[0] && sess->server &&
		sess->type != SESS_DIALOG &&
		sess->type != SESS_NOTICES &&
		sess->type != SESS_SNOTICES)
	{
		network = server_get_network (sess->server, TRUE);
		if (!network || !network[0])
			network = sess->server->servername[0] ? sess->server->servername : _("Server");
		base = g_strdup_printf ("[%s]", network);
	}
	else if (sess->channel[0])
		base = g_strdup (sess->channel);
	else if (sess->type == SESS_DIALOG)
		base = g_strdup (_("Dialog"));
	else if (sess->type == SESS_NOTICES || sess->type == SESS_SNOTICES)
		base = g_strdup (_("Notices"));
	else
		base = g_strdup (_("Session"));

	if (!sess || sess == current_tab)
		prefix = "";
	else if (sess->tab_state & TAB_STATE_NEW_HILIGHT)
		prefix = "[!] ";
	else if (sess->tab_state & TAB_STATE_NEW_MSG)
		prefix = "[*] ";
	else if (sess->tab_state & TAB_STATE_NEW_DATA)
		prefix = "[+] ";
	else
		prefix = "";

	full = g_strconcat (prefix, base, NULL);
	g_free (base);
	return full;
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
	session *server_sess;
	char *label;

	node = tree_server_lookup (serv);
	if (node)
		return node;

	label = tree_server_label (serv);
	node = hc_chan_node_new (label, NULL, serv, TRUE);
	g_free (label);

	tree_store_append_node (tree_root_nodes, node);
	tree_server_store (serv, node);

	server_sess = serv->server_session;
	if (server_sess && is_session (server_sess))
	{
		char *sess_label;

		node->sess = server_sess;
		sess_label = tree_session_label (server_sess);
		hc_chan_node_set_label (node, sess_label);
		g_free (sess_label);
		tree_session_store (server_sess, node);
	}

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
tree_activate_cb (GtkListView *list, guint position, gpointer user_data)
{
	GtkTreeListRow *row;
	HcChanNode *node;

	(void) list;
	(void) user_data;

	if (!tree_model)
		return;

	row = g_list_model_get_item (G_LIST_MODEL (tree_model), position);
	if (!row)
		return;

	node = (HcChanNode *) gtk_tree_list_row_get_item (row);
	if (gtk_tree_list_row_is_expandable (row) && tree_node_has_children (node))
	{
		gboolean expanded;

		expanded = gtk_tree_list_row_get_expanded (row);
		gtk_tree_list_row_set_expanded (row, !expanded);
	}

	g_object_unref (row);
}

static void
tree_children_changed_cb (GListModel *model, guint position, guint removed, guint added, gpointer user_data)
{
	GtkListItem *list_item;
	GtkWidget *expander;
	GtkTreeListRow *row;
	HcChanNode *node;

	(void) model;
	(void) position;
	(void) removed;
	(void) added;

	list_item = GTK_LIST_ITEM (user_data);
	expander = gtk_list_item_get_child (list_item);
	if (!expander)
		return;

	row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item));
	node = row ? (HcChanNode *) gtk_tree_list_row_get_item (row) : NULL;
	gtk_tree_expander_set_hide_expander (GTK_TREE_EXPANDER (expander),
		tree_node_has_children (node) ? FALSE : TRUE);
}

static void
tree_label_notify_cb (HcChanNode *node, GParamSpec *pspec, gpointer user_data)
{
	GtkListItem *list_item;
	GtkWidget *expander;
	GtkWidget *label;

	(void) node;
	(void) pspec;

	list_item = GTK_LIST_ITEM (user_data);
	expander = gtk_list_item_get_child (list_item);
	if (!expander)
		return;

	label = gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander));
	if (label)
		gtk_label_set_text (GTK_LABEL (label), node && node->label ? node->label : "");
}

static void
tree_factory_setup_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *expander;
	GtkWidget *label;

	(void) factory;
	(void) user_data;

	expander = gtk_tree_expander_new ();
	label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), label);
	gtk_list_item_set_child (list_item, expander);
}

static void
tree_factory_bind_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkTreeListRow *row;
	HcChanNode *node;
	GtkWidget *expander;
	GtkWidget *label;

	(void) factory;
	(void) user_data;

	row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item));
	node = row ? (HcChanNode *) gtk_tree_list_row_get_item (row) : NULL;

	expander = gtk_list_item_get_child (list_item);
	if (!expander)
		return;

	gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), row);
	gtk_tree_expander_set_hide_expander (GTK_TREE_EXPANDER (expander),
		tree_node_has_children (node) ? FALSE : TRUE);
	label = gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander));
	if (label)
		gtk_label_set_text (GTK_LABEL (label), node && node->label ? node->label : "");

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
	GtkWidget *label;

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

	gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), NULL);
	label = gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander));
	if (label)
		gtk_label_set_text (GTK_LABEL (label), "");
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
		tree_scroller = gtk_scrolled_window_new ();
		gtk_widget_set_hexpand (tree_scroller, FALSE);
		gtk_widget_set_vexpand (tree_scroller, TRUE);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (tree_scroller),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		tree_root_nodes = g_list_store_new (HC_TYPE_CHAN_NODE);
		tree_model = gtk_tree_list_model_new (
			G_LIST_MODEL (g_object_ref (tree_root_nodes)),
			FALSE, FALSE, tree_create_child_model_cb, NULL, NULL);
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
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (tree_scroller), tree_view);
		g_object_unref (factory);

		g_signal_connect (tree_selection, "notify::selected",
			G_CALLBACK (tree_selection_notify_cb), NULL);
		g_signal_connect (tree_view, "activate", G_CALLBACK (tree_activate_cb), NULL);
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
	char *label;

	if (!sess)
		return;

	node = tree_session_lookup (sess);
	if (!node && tree_group_by_server () && sess->type == SESS_SERVER && sess->server)
		node = tree_server_lookup (sess->server);
	if (!node)
		return;

	if (sess->type == SESS_SERVER && node->sess == NULL)
		label = tree_server_label (sess->server);
	else
		label = tree_session_label (sess);

	hc_chan_node_set_label (node, label);
	g_free (label);
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

		server_node->sess = sess;
		label = tree_session_label (sess);
		hc_chan_node_set_label (server_node, label);
		g_free (label);
		tree_session_store (sess, server_node);
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
	gboolean has_children;
	char *label;
	guint n_children;

	if (!tree_root_nodes || !tree_session_nodes || !sess)
		return;

	node = tree_session_lookup (sess);
	if (!node)
	{
		g_hash_table_remove (tree_session_nodes, sess);
		return;
	}

	grouped = tree_group_by_server ();
	if (grouped && sess->type == SESS_SERVER && sess->server)
	{
		server_node = tree_server_lookup (sess->server);
		if (server_node == node)
		{
			has_children = server_node->children &&
				g_list_model_get_n_items (G_LIST_MODEL (server_node->children)) > 0;
			if (has_children)
			{
				server_node->sess = NULL;
				label = tree_server_label (sess->server);
				hc_chan_node_set_label (server_node, label);
				g_free (label);
				g_hash_table_remove (tree_session_nodes, sess);
				return;
			}

			if (server_node->owner)
				tree_store_remove_node (server_node->owner, server_node);
			g_hash_table_remove (tree_session_nodes, sess);
			g_hash_table_remove (tree_server_nodes, sess->server);
			return;
		}
	}

	if (node->owner)
		tree_store_remove_node (node->owner, node);
	g_hash_table_remove (tree_session_nodes, sess);

	if (!grouped || !sess->server)
		return;

	server_node = tree_server_lookup (sess->server);
	if (!server_node || server_node == node)
		return;

	n_children = server_node->children ?
		g_list_model_get_n_items (G_LIST_MODEL (server_node->children)) : 0;
	if (server_node->sess || n_children > 0)
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

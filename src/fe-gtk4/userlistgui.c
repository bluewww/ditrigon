/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 user list */

#include "fe-gtk4.h"
#include <adwaita.h>

#include "../common/text.h"
#include "../common/userlist.h"

typedef struct _HcUserItem
{
	GObject parent_instance;
	struct User *user;
	char *display;
	char *host;
	char prefix;
} HcUserItem;

typedef struct _HcUserItemClass
{
	GObjectClass parent_class;
} HcUserItemClass;

#define HC_TYPE_USER_ITEM (hc_user_item_get_type ())
GType hc_user_item_get_type (void);
G_DEFINE_TYPE (HcUserItem, hc_user_item, G_TYPE_OBJECT)

static GtkWidget *userlist_panel;
static GtkWidget *userlist_revealer;
static GtkWidget *userlist_info_label;
static GtkWidget *userlist_search_entry;
static GtkWidget *userlist_scroller;
static GtkWidget *userlist_view;
static GtkWidget *userlist_empty_page;
static GHashTable *session_stores;     /* session * -> GListStore * */
static GtkCustomFilter *userlist_filter;
static GtkFilterListModel *userlist_filter_model;
static GtkMultiSelection *userlist_selection;
static session *userlist_session;
static gboolean userlist_select_syncing;
static GtkCssProvider *userlist_css_provider;
static gboolean userlist_pending_hide;
static char *userlist_filter_folded;

static void userlist_update_info_label (session *sess);

static GListStore *
get_session_store (session *sess)
{
	if (!session_stores || !sess)
		return NULL;
	return g_hash_table_lookup (session_stores, sess);
}

static GListStore *
get_active_store (void)
{
	return get_session_store (userlist_session);
}

static gboolean
userlist_session_supports_members (session *sess)
{
	if (!sess)
		return FALSE;

	return sess->type == SESS_CHANNEL || sess->type == SESS_DIALOG;
}

static void
userlist_update_surface_for_session (session *sess)
{
	gboolean supports_members;
	const char *title;
	const char *description;

	supports_members = userlist_session_supports_members (sess);

	if (userlist_info_label)
		gtk_widget_set_visible (userlist_info_label, supports_members);
	if (userlist_search_entry)
		gtk_widget_set_visible (userlist_search_entry, supports_members);
	if (userlist_scroller)
		gtk_widget_set_visible (userlist_scroller, supports_members);
	if (userlist_empty_page)
		gtk_widget_set_visible (userlist_empty_page, !supports_members);

	if (!userlist_empty_page || !ADW_IS_STATUS_PAGE (userlist_empty_page))
		return;

	if (!sess)
	{
		title = _("No Conversation Selected");
		description = _("Select a channel or private conversation to view members.");
	}
	else if (sess->type == SESS_SERVER)
	{
		title = _("No User List for Server");
		description = _("Join a channel to view its members.");
	}
	else
	{
		title = _("No User List Available");
		description = _("Switch to a channel or private conversation to view members.");
	}

	adw_status_page_set_icon_name (ADW_STATUS_PAGE (userlist_empty_page), "system-users-symbolic");
	adw_status_page_set_title (ADW_STATUS_PAGE (userlist_empty_page), title);
	adw_status_page_set_description (ADW_STATUS_PAGE (userlist_empty_page), description);
}

static const char *
userlist_role_css_class (char prefix)
{
	switch (prefix)
	{
	case '@':
	case '&':
	case '~':
	case '!':
		return "hc-user-role-op";
	case '%':
		return "hc-user-role-halfop";
	case '+':
		return "hc-user-role-voice";
	default:
		return NULL;
	}
}

static void
userlist_install_css (void)
{
	GdkDisplay *display;
	const char *css;

	if (userlist_css_provider)
		return;

	display = gdk_display_get_default ();
	if (!display)
		return;

	css =
		".hc-user-row { min-height: 30px; padding: 2px 4px; border-radius: 8px; }\n"
		".hc-user-host { font-size: 0.83em; }\n"
		".hc-user-role-badge { min-width: 18px; padding: 0 6px; border-radius: 999px; font-size: 0.78em; font-weight: 700; }\n"
		".hc-user-role-op-badge { background-color: alpha(#157915, 0.28); color: #8fe48f; }\n"
		".hc-user-role-halfop-badge { background-color: alpha(#856117, 0.32); color: #f0cf8e; }\n"
		".hc-user-role-voice-badge { background-color: alpha(#451984, 0.32); color: #c7a2fa; }\n"
		".hc-user-role-op { color: #157915; }\n"
		".hc-user-role-halfop { color: #856117; }\n"
		".hc-user-role-voice { color: #451984; }\n"
		".hc-userlist-empty label { font-size: 0.8em; }\n"
		".hc-userlist-empty image { -gtk-icon-size: 52px; }\n";

	userlist_css_provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_string (userlist_css_provider, css);
	gtk_style_context_add_provider_for_display (display,
		GTK_STYLE_PROVIDER (userlist_css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
userlist_set_presence_icon (GtkWidget *image, gboolean away)
{
	if (!image)
		return;

	gtk_image_set_from_icon_name (GTK_IMAGE (image),
		away ? "user-away-symbolic" : "user-available-symbolic");
	gtk_widget_remove_css_class (image, "dim-label");
	if (away)
		gtk_widget_add_css_class (image, "dim-label");
}

static const char *
userlist_role_badge_class (char prefix)
{
	switch (prefix)
	{
	case '@':
	case '&':
	case '~':
	case '!':
		return "hc-user-role-op-badge";
	case '%':
		return "hc-user-role-halfop-badge";
	case '+':
		return "hc-user-role-voice-badge";
	default:
		return NULL;
	}
}

static const char *
userlist_role_badge_text (char prefix)
{
	switch (prefix)
	{
	case '@':
	case '&':
	case '~':
	case '!':
		return "@";
	case '%':
		return "%";
	case '+':
		return "+";
	default:
		return NULL;
	}
}

static void
userlist_set_role_badge (GtkWidget *badge, char prefix)
{
	const char *text;
	const char *klass;

	if (!badge)
		return;

	gtk_widget_remove_css_class (badge, "hc-user-role-op-badge");
	gtk_widget_remove_css_class (badge, "hc-user-role-halfop-badge");
	gtk_widget_remove_css_class (badge, "hc-user-role-voice-badge");

	text = userlist_role_badge_text (prefix);
	klass = userlist_role_badge_class (prefix);
	if (!text || !klass)
	{
		gtk_label_set_text (GTK_LABEL (badge), "");
		gtk_widget_set_visible (badge, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (badge), text);
	gtk_widget_add_css_class (badge, klass);
	gtk_widget_set_visible (badge, TRUE);
}

static GListModel *
userlist_visible_model (void)
{
	if (userlist_filter_model)
		return G_LIST_MODEL (userlist_filter_model);
	return NULL;
}

static gboolean
userlist_filter_active (void)
{
	return userlist_filter_folded && userlist_filter_folded[0];
}

static gboolean
userlist_filter_match_cb (gpointer item, gpointer user_data)
{
	HcUserItem *user_item;
	char *nick_folded;
	char *host_folded;
	gboolean match;

	(void) user_data;

	if (!userlist_filter_folded || !userlist_filter_folded[0])
		return TRUE;

	user_item = (HcUserItem *) item;
	if (!user_item)
		return FALSE;

	nick_folded = g_utf8_casefold (user_item->display ? user_item->display : "", -1);
	host_folded = g_utf8_casefold (user_item->host ? user_item->host : "", -1);
	match = (g_strstr_len (nick_folded, -1, userlist_filter_folded) != NULL) ||
		(g_strstr_len (host_folded, -1, userlist_filter_folded) != NULL);
	g_free (nick_folded);
	g_free (host_folded);
	return match;
}

static gboolean
userlist_find_visible_position_by_user (struct User *user, guint *position)
{
	GListModel *model;
	guint i;
	guint n_items;

	if (!user)
		return FALSE;

	model = userlist_visible_model ();
	if (!model)
		return FALSE;

	n_items = g_list_model_get_n_items (model);
	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;

		item = g_list_model_get_item (model, i);
		if (!item)
			continue;
		if (item->user == user)
		{
			if (position)
				*position = i;
			g_object_unref (item);
			return TRUE;
		}
		g_object_unref (item);
	}

	return FALSE;
}

static void
userlist_sync_visible_selection_from_users (void)
{
	GListModel *model;
	guint i;
	guint n_items;

	if (!userlist_selection)
		return;

	model = userlist_visible_model ();
	if (!model)
		return;

	userlist_select_syncing = TRUE;
	gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (userlist_selection));
	n_items = g_list_model_get_n_items (model);
	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;

		item = g_list_model_get_item (model, i);
		if (!item || !item->user)
		{
			if (item)
				g_object_unref (item);
			continue;
		}
		if (item->user->selected)
			gtk_selection_model_select_item (GTK_SELECTION_MODEL (userlist_selection), i, TRUE);
		g_object_unref (item);
	}
	userlist_select_syncing = FALSE;
}

static void
userlist_search_changed_cb (GtkEditable *editable, gpointer user_data)
{
	const char *text;
	char *folded;

	(void) user_data;

	text = gtk_editable_get_text (editable);
	folded = g_utf8_casefold (text ? text : "", -1);
	g_strstrip (folded);

	g_free (userlist_filter_folded);
	if (folded[0])
		userlist_filter_folded = folded;
	else
	{
		g_free (folded);
		userlist_filter_folded = NULL;
	}

	if (userlist_filter)
		gtk_filter_changed (GTK_FILTER (userlist_filter), GTK_FILTER_CHANGE_DIFFERENT);

	userlist_sync_visible_selection_from_users ();
	userlist_update_info_label (userlist_session);
}

static void
userlist_revealer_child_revealed_cb (GtkRevealer *revealer, GParamSpec *pspec, gpointer user_data)
{
	(void) pspec;
	(void) user_data;

	if (!revealer)
		return;

	if (userlist_pending_hide &&
		!gtk_revealer_get_reveal_child (revealer) &&
		!gtk_revealer_get_child_revealed (revealer))
	{
		gtk_widget_set_visible (GTK_WIDGET (revealer), FALSE);
		userlist_pending_hide = FALSE;
	}
}

static char *
user_display_text (struct User *user)
{
	const char *nick;
	char prefix[2];

	if (!user)
		return g_strdup ("");

	nick = user->nick;
	if (!nick)
		nick = "";

	if (prefs.hex_gui_ulist_icons)
		return g_strdup (nick);

	prefix[0] = user->prefix[0];
	prefix[1] = 0;
	if (prefix[0] == 0 || prefix[0] == ' ')
		return g_strdup (nick);

	return g_strconcat (prefix, nick, NULL);
}

static char *
user_markup_text (struct User *user, const char *display)
{
	char *escaped;
	char *markup;
	int nick_color;
	unsigned int red;
	unsigned int green;
	unsigned int blue;

	escaped = g_markup_escape_text (display ? display : "", -1);
	nick_color = 0;
	if (user)
	{
		if (prefs.hex_away_track && user->away)
			nick_color = COL_AWAY;
		else if (prefs.hex_gui_ulist_color)
			nick_color = text_color_of (user->nick);
	}

	if (nick_color > 0)
	{
		red = colors[nick_color].red >> 8;
		green = colors[nick_color].green >> 8;
		blue = colors[nick_color].blue >> 8;
		markup = g_strdup_printf ("<span foreground=\"#%02x%02x%02x\">%s</span>",
			red, green, blue, escaped);
		g_free (escaped);
		return markup;
	}

	return escaped;
}

static void
hc_user_item_finalize (GObject *object)
{
	HcUserItem *item;

	item = (HcUserItem *) object;
	g_free (item->display);
	g_free (item->host);

	G_OBJECT_CLASS (hc_user_item_parent_class)->finalize (object);
}

static void
hc_user_item_class_init (HcUserItemClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hc_user_item_finalize;
}

static void
hc_user_item_init (HcUserItem *item)
{
	(void) item;
}

static HcUserItem *
hc_user_item_new (session *sess, struct User *user)
{
	HcUserItem *item;

	(void) sess;
	item = g_object_new (HC_TYPE_USER_ITEM, NULL);
	item->user = user;
	item->display = user_display_text (user);
	item->host = g_strdup (user && user->hostname ? user->hostname : "");
	item->prefix = (user && user->prefix[0]) ? user->prefix[0] : 0;

	return item;
}

static int
user_sort_compare (session *sess, struct User *left, struct User *right)
{
	int mode;

	if (!left || !right)
		return 0;

	if (!sess || !sess->server)
		return g_ascii_strcasecmp (left->nick, right->nick);

	mode = prefs.hex_gui_ulist_sort;
	switch (mode)
	{
	case 0:
		return nick_cmp_az_ops (sess->server, left, right);
	case 1:
		return nick_cmp_alpha (left, right, sess->server);
	case 2:
		return -nick_cmp_az_ops (sess->server, left, right);
	case 3:
		return -nick_cmp_alpha (left, right, sess->server);
	default:
		return 0;
	}
}

static guint
store_n_items (GListStore *store)
{
	if (!store)
		return 0;
	return g_list_model_get_n_items (G_LIST_MODEL (store));
}

static void
store_clear_all (GListStore *store)
{
	guint n_items;

	if (!store)
		return;

	n_items = store_n_items (store);
	if (n_items > 0)
		g_list_store_splice (store, 0, n_items, NULL, 0);
}

static gboolean
store_find_position_by_user (GListStore *store, struct User *user, guint *position)
{
	guint i;
	guint n_items;

	if (!store || !user)
		return FALSE;

	n_items = store_n_items (store);
	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;

		item = g_list_model_get_item (G_LIST_MODEL (store), i);
		if (!item)
			continue;
		if (item->user == user)
		{
			if (position)
				*position = i;
			g_object_unref (item);
			return TRUE;
		}
		g_object_unref (item);
	}

	return FALSE;
}

static gboolean
store_remove_row_by_user (GListStore *store, struct User *user, gboolean *was_selected)
{
	guint position;
	guint visible_position;
	gboolean selected;
	gboolean is_active;

	if (!store || !user)
		return FALSE;

	if (!store_find_position_by_user (store, user, &position))
		return FALSE;

	selected = user->selected ? TRUE : FALSE;
	is_active = (store == get_active_store ());
	if (is_active && userlist_selection &&
		userlist_find_visible_position_by_user (user, &visible_position))
	{
		selected = gtk_selection_model_is_selected (
			GTK_SELECTION_MODEL (userlist_selection), visible_position);
	}
	if (was_selected)
		*was_selected = selected;
	g_list_store_remove (store, position);
	return TRUE;
}

static guint
store_find_insert_position (GListStore *store, session *sess, struct User *user)
{
	guint i;
	guint n_items;
	int mode;

	if (!store || !user)
		return 0;

	mode = prefs.hex_gui_ulist_sort;
	n_items = store_n_items (store);
	if (mode < 0 || mode > 3)
		return n_items;

	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;
		int cmp;

		item = g_list_model_get_item (G_LIST_MODEL (store), i);
		if (!item)
			continue;

		cmp = user_sort_compare (sess, user, item->user);
		g_object_unref (item);
		if (cmp < 0)
			return i;
	}

	return n_items;
}

static void
store_insert_row (GListStore *store, session *sess, struct User *user, gboolean selected)
{
	HcUserItem *item;
	guint position;
	guint visible_position;
	gboolean is_active;

	if (!store || !user)
		return;

	item = hc_user_item_new (sess, user);
	position = store_find_insert_position (store, sess, user);
	g_list_store_insert (store, position, item);
	g_object_unref (item);

	is_active = (store == get_active_store ());
	if (selected && is_active && userlist_selection &&
		userlist_find_visible_position_by_user (user, &visible_position))
	{
		gtk_selection_model_select_item (
			GTK_SELECTION_MODEL (userlist_selection), visible_position, TRUE);
	}
}

static void
store_row_upsert (GListStore *store, session *sess, struct User *user, gboolean force_selected)
{
	gboolean selected;

	if (!sess || !user || !store)
		return;

	selected = force_selected ? TRUE : user->selected;
	store_remove_row_by_user (store, user, &selected);
	store_insert_row (store, sess, user, selected);
}

static GListStore *
get_or_create_session_store (session *sess)
{
	GListStore *store;

	if (!sess)
		return NULL;

	if (!session_stores)
		session_stores = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);

	store = g_hash_table_lookup (session_stores, sess);
	if (store)
		return store;

	store = g_list_store_new (HC_TYPE_USER_ITEM);
	g_hash_table_insert (session_stores, sess, store);

	/* Populate from the session's existing user tree */
	if (userlist_session_supports_members (sess))
	{
		GSList *list;

		list = userlist_flat_list (sess);
		for (; list; list = list->next)
		{
			struct User *user = list->data;
			if (user)
				store_insert_row (store, sess, user, user->selected ? TRUE : FALSE);
		}
		g_slist_free (list);
	}

	return store;
}

static void
userlist_update_info_label (session *sess)
{
	GListModel *model;
	char tbuf[256];
	int shown;

	if (!userlist_info_label)
		return;

	if (!sess || !userlist_session_supports_members (sess))
	{
		gtk_label_set_text (GTK_LABEL (userlist_info_label), "");
		return;
	}

	model = userlist_visible_model ();
	shown = model ? (int) g_list_model_get_n_items (model) : sess->total;

	if (userlist_filter_active ())
	{
		g_snprintf (tbuf, sizeof (tbuf),
			_("Users: %d/%d, <span foreground=\"#157915\">%d@</span>,"
			  "<span foreground=\"#856117\">%d%%</span>,"
			  "<span foreground=\"#451984\">%d+</span>"),
			shown, sess->total, sess->ops, sess->hops, sess->voices);
	}
	else
	{
		g_snprintf (tbuf, sizeof (tbuf),
			_("Users: %d, <span foreground=\"#157915\">%d@</span>,"
			  "<span foreground=\"#856117\">%d%%</span>,"
			  "<span foreground=\"#451984\">%d+</span>"),
			sess->total, sess->ops, sess->hops, sess->voices);
	}
	tbuf[sizeof (tbuf) - 1] = 0;
	gtk_label_set_markup (GTK_LABEL (userlist_info_label), tbuf);
}

static void
userlist_mark_all (session *sess, gboolean selected)
{
	GSList *list;

	if (!sess)
		return;

	list = userlist_flat_list (sess);
	for (; list; list = list->next)
	{
		struct User *user;

		user = list->data;
		if (!user)
			continue;
		user->selected = selected ? 1 : 0;
	}
	g_slist_free (list);
}

static void
userlist_rebuild_for_session (session *sess)
{
	GListStore *store;

	userlist_session = sess;
	userlist_update_surface_for_session (sess);

	if (!userlist_filter_model)
		return;

	userlist_select_syncing = TRUE;
	if (userlist_selection)
		gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (userlist_selection));
	userlist_select_syncing = FALSE;

	if (!sess || !userlist_session_supports_members (sess))
	{
		gtk_filter_list_model_set_model (userlist_filter_model, NULL);
		userlist_update_info_label (NULL);
		return;
	}

	store = get_or_create_session_store (sess);

	/* O(1) model swap instead of O(n^2) rebuild */
	gtk_filter_list_model_set_model (userlist_filter_model,
		G_LIST_MODEL (store));

	if (userlist_filter)
		gtk_filter_changed (GTK_FILTER (userlist_filter), GTK_FILTER_CHANGE_DIFFERENT);

	userlist_sync_visible_selection_from_users ();
	userlist_update_info_label (sess);
}

static void
userlist_apply_row (GtkListItem *list_item, HcUserItem *item)
{
	GtkWidget *presence_image;
	GtkWidget *name_label;
	GtkWidget *host_label;
	GtkWidget *badge_label;
	char *markup;
	const char *host_text;
	const char *role_class;

	presence_image = g_object_get_data (G_OBJECT (list_item), "hc-user-presence");
	name_label = g_object_get_data (G_OBJECT (list_item), "hc-user-name");
	host_label = g_object_get_data (G_OBJECT (list_item), "hc-user-host");
	badge_label = g_object_get_data (G_OBJECT (list_item), "hc-user-badge");
	if (!presence_image || !name_label || !host_label || !badge_label)
		return;

	if (!item)
	{
		userlist_set_presence_icon (presence_image, FALSE);
		gtk_label_set_text (GTK_LABEL (name_label), "");
		gtk_label_set_text (GTK_LABEL (host_label), "");
		gtk_widget_set_visible (host_label, FALSE);
		userlist_set_role_badge (badge_label, 0);
		gtk_widget_set_tooltip_text (GTK_WIDGET (name_label), NULL);
		return;
	}

	userlist_set_presence_icon (presence_image, item->user && item->user->away);

	markup = user_markup_text (item->user, item->display);
	gtk_label_set_markup (GTK_LABEL (name_label), markup);
	g_free (markup);

	gtk_widget_remove_css_class (name_label, "hc-user-role-op");
	gtk_widget_remove_css_class (name_label, "hc-user-role-halfop");
	gtk_widget_remove_css_class (name_label, "hc-user-role-voice");
	role_class = userlist_role_css_class (item->prefix);
	if (role_class)
		gtk_widget_add_css_class (name_label, role_class);

	if (prefs.hex_gui_ulist_show_hosts && item->host && item->host[0])
		host_text = item->host;
	else if (prefs.hex_away_track && item->user && item->user->away)
		host_text = _("Away");
	else
		host_text = NULL;

	if (host_text)
	{
		gtk_label_set_text (GTK_LABEL (host_label), host_text);
		gtk_widget_set_visible (host_label, TRUE);
	}
	else
	{
		gtk_label_set_text (GTK_LABEL (host_label), "");
		gtk_widget_set_visible (host_label, FALSE);
	}
	gtk_widget_set_tooltip_text (GTK_WIDGET (name_label),
		(item->host && item->host[0]) ? item->host : NULL);
	userlist_set_role_badge (badge_label, item->prefix);
}

static void
userlist_row_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);

static void
userlist_factory_setup_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *row;
	GtkWidget *presence_image;
	GtkWidget *content_box;
	GtkWidget *name_label;
	GtkWidget *host_label;
	GtkWidget *badge_label;
	GtkGesture *gesture;

	(void) factory;
	(void) user_data;

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_add_css_class (row, "hc-user-row");
	gtk_widget_set_margin_start (row, 6);
	gtk_widget_set_margin_end (row, 6);
	gtk_widget_set_margin_top (row, 2);
	gtk_widget_set_margin_bottom (row, 2);

	presence_image = gtk_image_new ();
	gtk_widget_set_valign (presence_image, GTK_ALIGN_CENTER);
	gtk_box_append (GTK_BOX (row), presence_image);

	content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_hexpand (content_box, TRUE);
	gtk_widget_set_valign (content_box, GTK_ALIGN_CENTER);
	gtk_box_append (GTK_BOX (row), content_box);

	name_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (name_label), 0.0f);
	gtk_label_set_ellipsize (GTK_LABEL (name_label), PANGO_ELLIPSIZE_END);
	gtk_box_append (GTK_BOX (content_box), name_label);

	host_label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (host_label), 0.0f);
	gtk_label_set_ellipsize (GTK_LABEL (host_label), PANGO_ELLIPSIZE_END);
	gtk_widget_add_css_class (host_label, "dim-label");
	gtk_widget_add_css_class (host_label, "caption");
	gtk_widget_add_css_class (host_label, "hc-user-host");
	gtk_box_append (GTK_BOX (content_box), host_label);

	badge_label = gtk_label_new ("");
	gtk_widget_set_valign (badge_label, GTK_ALIGN_CENTER);
	gtk_widget_add_css_class (badge_label, "hc-user-role-badge");
	gtk_widget_set_visible (badge_label, FALSE);
	gtk_box_append (GTK_BOX (row), badge_label);

	g_object_set_data (G_OBJECT (list_item), "hc-user-presence", presence_image);
	g_object_set_data (G_OBJECT (list_item), "hc-user-name", name_label);
	g_object_set_data (G_OBJECT (list_item), "hc-user-host", host_label);
	g_object_set_data (G_OBJECT (list_item), "hc-user-badge", badge_label);
	gesture = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
	g_signal_connect (gesture, "pressed",
		G_CALLBACK (userlist_row_right_click_cb), list_item);
	gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (gesture));
	gtk_list_item_set_child (list_item, row);
}

static void
userlist_factory_bind_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	HcUserItem *item;

	(void) factory;
	(void) user_data;

	item = (HcUserItem *) gtk_list_item_get_item (list_item);
	userlist_apply_row (list_item, item);
}

static void
userlist_factory_unbind_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	(void) factory;
	(void) user_data;

	userlist_apply_row (list_item, NULL);
}

static void
userlist_selection_changed_cb (GtkSelectionModel *model, guint position, guint n_items, gpointer user_data)
{
	(void) model;
	(void) position;
	(void) n_items;
	(void) user_data;

	if (userlist_select_syncing || !userlist_session)
		return;

	fe_userlist_set_selected (userlist_session);
}

static void
userlist_activate_cb (GtkListView *list, guint position, gpointer user_data)
{
	GListModel *model;
	HcUserItem *item;
	char *cmd;

	(void) list;
	(void) user_data;

	model = userlist_visible_model ();
	if (!prefs.hex_gui_ulist_doubleclick[0] || !current_sess || !model)
		return;

	item = g_list_model_get_item (model, position);
	if (!item || !item->user)
	{
		if (item)
			g_object_unref (item);
		return;
	}

	{
		const char *tmpl;
		const char *nick;
		const char *p;
		const char *next;
		GString *out;

		tmpl = prefs.hex_gui_ulist_doubleclick;
		nick = item->user->nick;
		out = g_string_sized_new (strlen (tmpl) + strlen (nick) + 16);
		p = tmpl;
		while ((next = strstr (p, "%s")) != NULL)
		{
			g_string_append_len (out, p, (gssize) (next - p));
			g_string_append (out, nick);
			p = next + 2;
		}
		g_string_append (out, p);
		cmd = g_string_free (out, FALSE);
	}

	if (cmd && cmd[0])
		handle_command (current_sess, cmd, FALSE);
	g_free (cmd);
	g_object_unref (item);
}

static void
userlist_row_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
	GtkListItem *list_item;
	HcUserItem *item;
	guint position;
	GtkWidget *widget;

	if (n_press != 1 || !userlist_session || !is_session (userlist_session))
		return;

	list_item = GTK_LIST_ITEM (user_data);
	item = (HcUserItem *) gtk_list_item_get_item (list_item);
	if (!item || !item->user || !item->user->nick[0])
		return;

	position = gtk_list_item_get_position (list_item);
	if (userlist_selection && position != GTK_INVALID_LIST_POSITION &&
		!gtk_selection_model_is_selected (GTK_SELECTION_MODEL (userlist_selection), position))
	{
		userlist_select_syncing = TRUE;
		gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (userlist_selection));
		gtk_selection_model_select_item (GTK_SELECTION_MODEL (userlist_selection), position, TRUE);
		userlist_select_syncing = FALSE;
		fe_userlist_set_selected (userlist_session);
	}

	widget = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
	fe_gtk4_menu_show_nickmenu (widget, x, y, userlist_session, item->user->nick);
	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

void
fe_gtk4_userlist_init (void)
{
	userlist_select_syncing = FALSE;
	userlist_pending_hide = FALSE;
}

void
fe_gtk4_userlist_cleanup (void)
{
	g_clear_pointer (&session_stores, g_hash_table_destroy);
	g_clear_object (&userlist_filter_model);
	g_clear_object (&userlist_filter);
	g_clear_object (&userlist_selection);
	g_clear_object (&userlist_css_provider);
	g_free (userlist_filter_folded);
	userlist_filter_folded = NULL;
	userlist_panel = NULL;
	userlist_revealer = NULL;
	userlist_info_label = NULL;
	userlist_search_entry = NULL;
	userlist_scroller = NULL;
	userlist_view = NULL;
	userlist_empty_page = NULL;
	userlist_session = NULL;
	userlist_select_syncing = FALSE;
	userlist_pending_hide = FALSE;
}

GtkWidget *
fe_gtk4_userlist_create_widget (void)
{
	GtkListItemFactory *factory;
	int right_size;

	right_size = MAX (prefs.hex_gui_pane_right_size, prefs.hex_gui_pane_right_size_min);
	if (right_size <= 0)
		right_size = 150;

	userlist_install_css ();

	if (!userlist_revealer)
	{
		userlist_revealer = gtk_revealer_new ();
		gtk_widget_set_hexpand (userlist_revealer, FALSE);
		gtk_widget_set_vexpand (userlist_revealer, TRUE);
		gtk_revealer_set_transition_type (GTK_REVEALER (userlist_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
		gtk_revealer_set_transition_duration (GTK_REVEALER (userlist_revealer), 250);
		gtk_widget_set_visible (userlist_revealer, TRUE);
		g_signal_connect (userlist_revealer, "notify::child-revealed",
			G_CALLBACK (userlist_revealer_child_revealed_cb), NULL);
	}

	if (!userlist_panel)
	{
		userlist_panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_set_size_request (userlist_panel, right_size, -1);

		userlist_info_label = gtk_label_new (NULL);
		gtk_label_set_xalign (GTK_LABEL (userlist_info_label), 0.0f);
		gtk_widget_set_margin_start (userlist_info_label, 8);
		gtk_widget_set_margin_end (userlist_info_label, 8);
		gtk_widget_set_margin_top (userlist_info_label, 8);
		gtk_widget_set_margin_bottom (userlist_info_label, 4);
		gtk_label_set_use_markup (GTK_LABEL (userlist_info_label), TRUE);
		gtk_box_append (GTK_BOX (userlist_panel), userlist_info_label);

		userlist_search_entry = gtk_search_entry_new ();
		gtk_widget_set_margin_start (userlist_search_entry, 8);
		gtk_widget_set_margin_end (userlist_search_entry, 8);
		gtk_widget_set_margin_top (userlist_search_entry, 2);
		gtk_widget_set_margin_bottom (userlist_search_entry, 8);
		gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (userlist_search_entry), _("Search Users"));
		gtk_editable_set_text (GTK_EDITABLE (userlist_search_entry), "");
		gtk_box_append (GTK_BOX (userlist_panel), userlist_search_entry);

		userlist_scroller = gtk_scrolled_window_new ();
		gtk_widget_set_hexpand (userlist_scroller, FALSE);
		gtk_widget_set_vexpand (userlist_scroller, TRUE);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (userlist_scroller),
			GTK_POLICY_NEVER,
			GTK_POLICY_AUTOMATIC);
		gtk_box_append (GTK_BOX (userlist_panel), userlist_scroller);

		userlist_empty_page = adw_status_page_new ();
		gtk_widget_add_css_class (userlist_empty_page, "hc-userlist-empty");
		gtk_widget_set_hexpand (userlist_empty_page, TRUE);
		gtk_widget_set_vexpand (userlist_empty_page, TRUE);
		gtk_widget_set_margin_start (userlist_empty_page, 8);
		gtk_widget_set_margin_end (userlist_empty_page, 8);
		gtk_widget_set_margin_bottom (userlist_empty_page, 8);
		gtk_box_append (GTK_BOX (userlist_panel), userlist_empty_page);

		userlist_filter = gtk_custom_filter_new (userlist_filter_match_cb, NULL, NULL);
		userlist_filter_model = gtk_filter_list_model_new (
			NULL,
			GTK_FILTER (g_object_ref (userlist_filter)));
		userlist_selection = gtk_multi_selection_new (
			G_LIST_MODEL (g_object_ref (userlist_filter_model)));

		factory = gtk_signal_list_item_factory_new ();
		g_signal_connect (factory, "setup", G_CALLBACK (userlist_factory_setup_cb), NULL);
		g_signal_connect (factory, "bind", G_CALLBACK (userlist_factory_bind_cb), NULL);
		g_signal_connect (factory, "unbind", G_CALLBACK (userlist_factory_unbind_cb), NULL);

		userlist_view = gtk_list_view_new (
			GTK_SELECTION_MODEL (g_object_ref (userlist_selection)),
			GTK_LIST_ITEM_FACTORY (g_object_ref (factory)));
		gtk_list_view_set_single_click_activate (GTK_LIST_VIEW (userlist_view), FALSE);
		gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (userlist_scroller), userlist_view);
		g_object_unref (factory);

		g_signal_connect (userlist_selection, "selection-changed",
			G_CALLBACK (userlist_selection_changed_cb), NULL);
		g_signal_connect (userlist_view, "activate", G_CALLBACK (userlist_activate_cb), NULL);
		g_signal_connect (userlist_search_entry, "changed",
			G_CALLBACK (userlist_search_changed_cb), NULL);

		gtk_revealer_set_child (GTK_REVEALER (userlist_revealer), userlist_panel);
	}
	else
	{
		gtk_widget_set_size_request (userlist_panel, right_size, -1);
	}

	fe_gtk4_userlist_set_visible (prefs.hex_gui_ulist_hide ? FALSE : TRUE);
	userlist_update_surface_for_session (userlist_session);

	return userlist_revealer;
}

void
fe_gtk4_userlist_show (session *sess)
{
	userlist_rebuild_for_session (sess);
}

void
fe_gtk4_userlist_remove_session (session *sess)
{
	if (!sess || !session_stores)
		return;

	if (sess == userlist_session)
	{
		userlist_session = NULL;
		if (userlist_filter_model)
			gtk_filter_list_model_set_model (userlist_filter_model, NULL);
	}

	g_hash_table_remove (session_stores, sess);
}

void
fe_gtk4_userlist_set_visible (gboolean visible)
{
	prefs.hex_gui_ulist_hide = visible ? 0 : 1;
	if (userlist_revealer)
	{
		if (visible)
		{
			userlist_pending_hide = FALSE;
			gtk_widget_set_visible (userlist_revealer, TRUE);
			gtk_revealer_set_reveal_child (GTK_REVEALER (userlist_revealer), TRUE);
		}
		else
		{
			userlist_pending_hide = TRUE;
			gtk_revealer_set_reveal_child (GTK_REVEALER (userlist_revealer), FALSE);
		}
	}
	fe_gtk4_maingui_animate_userlist_split (visible ? TRUE : FALSE);
	fe_gtk4_adw_sync_userlist_button (visible ? TRUE : FALSE);
}

gboolean
fe_gtk4_userlist_get_visible (void)
{
	if (userlist_revealer)
		return gtk_revealer_get_reveal_child (GTK_REVEALER (userlist_revealer));

	return prefs.hex_gui_ulist_hide ? FALSE : TRUE;
}

void
fe_userlist_insert (struct session *sess, struct User *newuser, gboolean sel)
{
	GListStore *store;

	if (!newuser)
		return;

	if (sel)
		newuser->selected = 1;

	store = get_session_store (sess);
	if (!store)
		return;

	store_row_upsert (store, sess, newuser, sel);
}

int
fe_userlist_remove (struct session *sess, struct User *user)
{
	GListStore *store;
	gboolean selected;

	if (!user)
		return 0;

	selected = user->selected ? TRUE : FALSE;

	store = get_session_store (sess);
	if (store)
		store_remove_row_by_user (store, user, &selected);

	return selected ? 1 : 0;
}

void
fe_userlist_rehash (struct session *sess, struct User *user)
{
	GListStore *store;

	if (!sess || !user)
		return;

	store = get_session_store (sess);
	if (!store)
		return;

	store_row_upsert (store, sess, user, FALSE);
}

void
fe_userlist_update (session *sess, struct User *user)
{
	fe_userlist_rehash (sess, user);
}

void
fe_userlist_numbers (struct session *sess)
{
	if (!sess)
		return;

	if (sess == userlist_session)
		userlist_update_info_label (sess);

	if (sess == current_tab && prefs.hex_gui_win_ucount)
		fe_set_title (sess);
}

void
fe_userlist_clear (struct session *sess)
{
	GListStore *store;

	if (!sess)
		return;

	store = get_session_store (sess);
	if (!store)
		return;

	if (sess == userlist_session)
	{
		userlist_select_syncing = TRUE;
		if (userlist_selection)
			gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (userlist_selection));
		userlist_select_syncing = FALSE;
	}
	store_clear_all (store);

	if (sess == userlist_session)
		userlist_update_info_label (sess);
}

void
fe_userlist_set_selected (struct session *sess)
{
	GListModel *model;
	GListStore *store;
	guint i;
	guint n_items;

	if (!sess)
		return;

	if (sess != userlist_session || !userlist_selection)
		return;

	store = get_session_store (sess);
	if (!store)
		return;

	model = userlist_visible_model ();
	if (!model)
		return;

	if (userlist_filter_active ())
	{
		n_items = g_list_model_get_n_items (model);
		for (i = 0; i < n_items; i++)
		{
			HcUserItem *item;
			gboolean selected;

			item = g_list_model_get_item (model, i);
			if (!item || !item->user)
			{
				if (item)
					g_object_unref (item);
				continue;
			}

			selected = gtk_selection_model_is_selected (
				GTK_SELECTION_MODEL (userlist_selection), i);
			item->user->selected = selected ? 1 : 0;
			g_object_unref (item);
		}
		return;
	}

	n_items = store_n_items (store);
	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;
		gboolean selected;

		item = g_list_model_get_item (G_LIST_MODEL (store), i);
		if (!item || !item->user)
		{
			if (item)
				g_object_unref (item);
			continue;
		}

		selected = gtk_selection_model_is_selected (GTK_SELECTION_MODEL (userlist_selection), i);
		item->user->selected = selected ? 1 : 0;
		g_object_unref (item);
	}
}

void
fe_uselect (struct session *sess, char *word[], int do_clear, int scroll_to)
{
	int idx;

	(void) scroll_to;

	if (!sess || !word)
		return;

	if (do_clear)
	{
		userlist_mark_all (sess, FALSE);
		if (sess == userlist_session && userlist_selection)
		{
			userlist_select_syncing = TRUE;
			gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (userlist_selection));
			userlist_select_syncing = FALSE;
		}
	}

	for (idx = 0; word[idx] && word[idx][0]; idx++)
	{
		struct User *user;

		user = userlist_find (sess, word[idx]);
		if (!user)
			continue;

		user->selected = 1;
		if (sess == userlist_session && userlist_selection)
		{
			guint visible_position;

			if (userlist_find_visible_position_by_user (user, &visible_position))
			{
				userlist_select_syncing = TRUE;
				gtk_selection_model_select_item (
					GTK_SELECTION_MODEL (userlist_selection), visible_position, TRUE);
				userlist_select_syncing = FALSE;
			}
		}
	}
}

/* HexChat GTK4 user list */
#include "fe-gtk4.h"

#include "../common/text.h"
#include "../common/userlist.h"

typedef struct _HcUserItem
{
	GObject parent_instance;
	struct User *user;
	char *display;
	char *host;
	GdkPixbuf *icon;
} HcUserItem;

typedef struct _HcUserItemClass
{
	GObjectClass parent_class;
} HcUserItemClass;

#define HC_TYPE_USER_ITEM (hc_user_item_get_type ())
GType hc_user_item_get_type (void);
G_DEFINE_TYPE (HcUserItem, hc_user_item, G_TYPE_OBJECT)

static GtkWidget *userlist_panel;
static GtkWidget *userlist_info_label;
static GtkWidget *userlist_scroller;
static GtkWidget *userlist_view;
static GListStore *userlist_store;
static GtkMultiSelection *userlist_selection;
static session *userlist_session;
static gboolean userlist_select_syncing;

static GdkPixbuf *
get_user_icon (server *serv, struct User *user)
{
	char *pre;
	int level;

	if (!serv || !user)
		return NULL;

	switch (user->prefix[0])
	{
	case 0:
		return NULL;
	case '+':
		return pix_ulist_voice;
	case '%':
		return pix_ulist_halfop;
	case '@':
		return pix_ulist_op;
	default:
		break;
	}

	pre = strchr (serv->nick_prefixes, '@');
	if (pre && pre != serv->nick_prefixes)
	{
		pre--;
		level = 0;
		while (1)
		{
			if (pre[0] == user->prefix[0])
			{
				switch (level)
				{
				case 0:
					return pix_ulist_owner;
				case 1:
					return pix_ulist_founder;
				case 2:
					return pix_ulist_netop;
				default:
					break;
				}
				break;
			}
			level++;
			if (pre == serv->nick_prefixes)
				break;
			pre--;
		}
	}

	return NULL;
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
	g_clear_object (&item->icon);

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
	GdkPixbuf *icon;

	item = g_object_new (HC_TYPE_USER_ITEM, NULL);
	item->user = user;
	item->display = user_display_text (user);
	item->host = g_strdup (user && user->hostname ? user->hostname : "");

	icon = NULL;
	if (prefs.hex_gui_ulist_icons && sess && sess->server)
		icon = get_user_icon (sess->server, user);
	if (icon)
		item->icon = g_object_ref (icon);

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
userlist_store_n_items (void)
{
	if (!userlist_store)
		return 0;
	return g_list_model_get_n_items (G_LIST_MODEL (userlist_store));
}

static void
userlist_store_clear_all (void)
{
	guint n_items;

	if (!userlist_store)
		return;

	n_items = userlist_store_n_items ();
	if (n_items > 0)
		g_list_store_splice (userlist_store, 0, n_items, NULL, 0);
}

static gboolean
userlist_find_position_by_user (struct User *user, guint *position)
{
	guint i;
	guint n_items;

	if (!userlist_store || !user)
		return FALSE;

	n_items = userlist_store_n_items ();
	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;

		item = g_list_model_get_item (G_LIST_MODEL (userlist_store), i);
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
userlist_remove_row_by_user (struct User *user, gboolean *was_selected)
{
	guint position;
	gboolean selected;

	if (!userlist_store || !user)
		return FALSE;

	if (!userlist_find_position_by_user (user, &position))
		return FALSE;

	selected = FALSE;
	if (userlist_selection)
		selected = gtk_selection_model_is_selected (GTK_SELECTION_MODEL (userlist_selection), position);
	if (was_selected)
		*was_selected = selected;
	g_list_store_remove (userlist_store, position);
	return TRUE;
}

static guint
userlist_find_insert_position (session *sess, struct User *user)
{
	guint i;
	guint n_items;
	int mode;

	if (!userlist_store || !user)
		return 0;

	mode = prefs.hex_gui_ulist_sort;
	n_items = userlist_store_n_items ();
	if (mode < 0 || mode > 3)
		return n_items;

	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;
		int cmp;

		item = g_list_model_get_item (G_LIST_MODEL (userlist_store), i);
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
userlist_insert_row (session *sess, struct User *user, gboolean selected)
{
	HcUserItem *item;
	guint position;

	if (!userlist_store || !user)
		return;

	item = hc_user_item_new (sess, user);
	position = userlist_find_insert_position (sess, user);
	g_list_store_insert (userlist_store, position, item);
	g_object_unref (item);

	if (selected && userlist_selection)
		gtk_selection_model_select_item (GTK_SELECTION_MODEL (userlist_selection), position, TRUE);
}

static void
userlist_row_upsert (session *sess, struct User *user, gboolean force_selected)
{
	gboolean selected;

	if (!sess || !user || !userlist_store)
		return;

	selected = force_selected ? TRUE : user->selected;
	userlist_remove_row_by_user (user, &selected);
	userlist_insert_row (sess, user, selected);
}

static void
userlist_update_info_label (session *sess)
{
	char tbuf[256];

	if (!userlist_info_label)
		return;

	if (!sess || sess->total <= 0)
	{
		gtk_label_set_text (GTK_LABEL (userlist_info_label), NULL);
		return;
	}

	g_snprintf (tbuf, sizeof (tbuf), _("%d ops, %d total"), sess->ops, sess->total);
	tbuf[sizeof (tbuf) - 1] = 0;
	gtk_label_set_text (GTK_LABEL (userlist_info_label), tbuf);
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
	GSList *list;

	userlist_session = sess;
	if (!userlist_store)
		return;

	userlist_select_syncing = TRUE;
	if (userlist_selection)
		gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (userlist_selection));
	userlist_store_clear_all ();
	userlist_select_syncing = FALSE;

	if (!sess)
	{
		userlist_update_info_label (NULL);
		return;
	}

	list = userlist_flat_list (sess);
	for (; list; list = list->next)
	{
		struct User *user;

		user = list->data;
		if (!user)
			continue;
		userlist_insert_row (sess, user, user->selected ? TRUE : FALSE);
	}
	g_slist_free (list);

	userlist_update_info_label (sess);
}

static void
userlist_apply_row (GtkListItem *list_item, HcUserItem *item)
{
	GtkWidget *image;
	GtkWidget *label;
	char *markup;
	char *host_text;

	image = g_object_get_data (G_OBJECT (list_item), "hc-user-image");
	label = g_object_get_data (G_OBJECT (list_item), "hc-user-label");
	if (!image || !label)
		return;

	if (!item)
	{
		gtk_widget_set_visible (image, FALSE);
		gtk_label_set_text (GTK_LABEL (label), "");
		gtk_widget_set_tooltip_text (GTK_WIDGET (label), NULL);
		return;
	}

	if (item->icon && prefs.hex_gui_ulist_icons)
	{
		GdkTexture *texture;

		texture = gdk_texture_new_for_pixbuf (item->icon);
		gtk_image_set_from_paintable (GTK_IMAGE (image), GDK_PAINTABLE (texture));
		g_object_unref (texture);
		gtk_widget_set_visible (image, TRUE);
	}
	else
	{
		gtk_widget_set_visible (image, FALSE);
	}

	markup = user_markup_text (item->user, item->display);
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);

	if (prefs.hex_gui_ulist_show_hosts && item->host && item->host[0])
		host_text = item->host;
	else
		host_text = NULL;
	gtk_widget_set_tooltip_text (GTK_WIDGET (label), host_text);
}

static void
userlist_row_right_click_cb (GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);

static void
userlist_factory_setup_cb (GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
	GtkWidget *row;
	GtkWidget *image;
	GtkWidget *label;
	GtkGesture *gesture;

	(void) factory;
	(void) user_data;

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	image = gtk_image_new ();
	label = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_box_append (GTK_BOX (row), image);
	gtk_box_append (GTK_BOX (row), label);

	g_object_set_data (G_OBJECT (list_item), "hc-user-image", image);
	g_object_set_data (G_OBJECT (list_item), "hc-user-label", label);
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
	HcUserItem *item;
	char *cmd;

	(void) list;
	(void) user_data;

	if (!prefs.hex_gui_ulist_doubleclick[0] || !current_sess || !userlist_store)
		return;

	item = g_list_model_get_item (G_LIST_MODEL (userlist_store), position);
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
}

void
fe_gtk4_userlist_cleanup (void)
{
	g_clear_object (&userlist_selection);
	g_clear_object (&userlist_store);
	userlist_panel = NULL;
	userlist_info_label = NULL;
	userlist_scroller = NULL;
	userlist_view = NULL;
	userlist_session = NULL;
	userlist_select_syncing = FALSE;
}

GtkWidget *
fe_gtk4_userlist_create_widget (void)
{
	GtkListItemFactory *factory;
	int right_size;

	right_size = MAX (prefs.hex_gui_pane_right_size, prefs.hex_gui_pane_right_size_min);
	if (right_size <= 0)
		right_size = 100;

	if (!userlist_panel)
	{
		userlist_panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
		gtk_widget_set_size_request (userlist_panel, right_size, -1);

		userlist_info_label = gtk_label_new (NULL);
		gtk_label_set_xalign (GTK_LABEL (userlist_info_label), 0.0f);
		gtk_box_append (GTK_BOX (userlist_panel), userlist_info_label);

		userlist_scroller = gtk_scrolled_window_new ();
		gtk_widget_set_hexpand (userlist_scroller, FALSE);
		gtk_widget_set_vexpand (userlist_scroller, TRUE);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (userlist_scroller),
			prefs.hex_gui_ulist_show_hosts ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
			GTK_POLICY_AUTOMATIC);
		gtk_box_append (GTK_BOX (userlist_panel), userlist_scroller);

		userlist_store = g_list_store_new (HC_TYPE_USER_ITEM);
		userlist_selection = gtk_multi_selection_new (
			G_LIST_MODEL (g_object_ref (userlist_store)));

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
	}
	else
	{
		gtk_widget_set_size_request (userlist_panel, right_size, -1);
	}

	if (prefs.hex_gui_ulist_hide)
		gtk_widget_set_visible (userlist_panel, FALSE);
	else
		gtk_widget_set_visible (userlist_panel, TRUE);

	return userlist_panel;
}

void
fe_gtk4_userlist_show (session *sess)
{
	userlist_rebuild_for_session (sess);
}

void
fe_gtk4_userlist_set_visible (gboolean visible)
{
	prefs.hex_gui_ulist_hide = visible ? 0 : 1;
	if (userlist_panel)
		gtk_widget_set_visible (userlist_panel, visible ? TRUE : FALSE);
}

gboolean
fe_gtk4_userlist_get_visible (void)
{
	if (userlist_panel)
		return gtk_widget_get_visible (userlist_panel);

	return prefs.hex_gui_ulist_hide ? FALSE : TRUE;
}

void
fe_userlist_insert (struct session *sess, struct User *newuser, gboolean sel)
{
	if (!newuser)
		return;

	if (sel)
		newuser->selected = 1;

	if (!userlist_store || sess != userlist_session)
		return;

	userlist_row_upsert (sess, newuser, sel);
}

int
fe_userlist_remove (struct session *sess, struct User *user)
{
	gboolean selected;

	if (!user)
		return 0;

	selected = user->selected ? TRUE : FALSE;
	if (userlist_store && sess == userlist_session)
		userlist_remove_row_by_user (user, &selected);

	return selected ? 1 : 0;
}

void
fe_userlist_rehash (struct session *sess, struct User *user)
{
	if (!sess || !user)
		return;

	if (!userlist_store || sess != userlist_session)
		return;

	userlist_row_upsert (sess, user, FALSE);
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
	if (!sess)
		return;

	if (sess != userlist_session || !userlist_store)
		return;

	userlist_select_syncing = TRUE;
	if (userlist_selection)
		gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (userlist_selection));
	userlist_store_clear_all ();
	userlist_select_syncing = FALSE;
	userlist_update_info_label (sess);
}

void
fe_userlist_set_selected (struct session *sess)
{
	guint i;
	guint n_items;

	if (!sess)
		return;

	if (sess != userlist_session || !userlist_store || !userlist_selection)
		return;

	n_items = userlist_store_n_items ();
	for (i = 0; i < n_items; i++)
	{
		HcUserItem *item;
		gboolean selected;

		item = g_list_model_get_item (G_LIST_MODEL (userlist_store), i);
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
			guint position;

			if (userlist_find_position_by_user (user, &position))
			{
				userlist_select_syncing = TRUE;
				gtk_selection_model_select_item (
					GTK_SELECTION_MODEL (userlist_selection), position, TRUE);
				userlist_select_syncing = FALSE;
			}
		}
	}
}

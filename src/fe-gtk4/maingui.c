/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 main window and session UI */

#include "fe-gtk4.h"
#include "sexy-spell-entry.h"
#include "../common/url.h"
#include <adwaita.h>

#ifdef USE_PLUGIN
int notification_plugin_init (void *, char **, char **, char **, char *);
int notification_plugin_deinit (void *);
#endif

GtkWidget *main_window;
GtkWidget *main_box;
GtkWidget *menu_bar;
GtkWidget *content_paned;
GtkWidget *session_scroller;
GtkWidget *session_list;
GtkWidget *log_view;
GtkTextBuffer *log_buffer; /* Points to the currently displayed session's buffer */
GtkWidget *command_entry;
GSimpleActionGroup *window_actions;
static GtkWidget *main_right_paned;
static GtkWidget *main_right_box;
static GtkWidget *main_center_box;
static GtkWidget *entry_row;
static GtkWidget *input_composer_box;
static GtkWidget *input_nick_box;
static GtkWidget *input_nick_button;
static GtkWidget *input_send_button;
static gboolean pane_positions_ready;
static GtkCssProvider *maingui_css_provider;
static guint userlist_split_anim_source;
static gint64 userlist_split_anim_start_us;
static int userlist_split_anim_from;
static int userlist_split_anim_to;
static gboolean userlist_split_animating;
static gboolean left_sidebar_visible = TRUE;
static GtkWidget *main_nav_split;
static AdwNavigationPage *main_nav_sidebar_page;
static AdwNavigationPage *main_nav_content_page;
static GHashTable *disconnect_preserve_servers;

#define GUI_PANE_LEFT_DEFAULT 128
#define GUI_PANE_RIGHT_DEFAULT 220
#define GUI_PANE_RIGHT_MIN 220
#define GUI_PANE_RIGHT_MAX 300
#define GUI_PANE_CENTER_MIN GUI_PANE_LEFT_DEFAULT
#define NAV_SPLIT_COLLAPSE_CONDITION "max-width: 560sp"
#define NAV_SPLIT_COLLAPSE_WIDTH_HINT 560
#define MAINGUI_UI_BASE "/org/hexchat/ui/gtk4/maingui"

static int done_intro;

#ifdef USE_PLUGIN
static int
fe_idle (gpointer data)
{
	session *sess;

	(void) data;

	sess = sess_list ? sess_list->data : NULL;
	if (!sess)
		return G_SOURCE_REMOVE;

	plugin_add (sess, NULL, NULL, notification_plugin_init, notification_plugin_deinit, NULL, FALSE);

	return G_SOURCE_REMOVE;
}
#endif

static gboolean
maingui_disconnect_preserve_clear_idle (gpointer data)
{
	server *serv;

	serv = data;
	if (!serv)
		return G_SOURCE_REMOVE;

	if (disconnect_preserve_servers)
		g_hash_table_remove (disconnect_preserve_servers, serv);

	return G_SOURCE_REMOVE;
}

static void
maingui_mark_disconnect_preserve (server *serv)
{
	GSList *list;

	if (!serv)
		return;

	if (!disconnect_preserve_servers)
		disconnect_preserve_servers = g_hash_table_new (g_direct_hash, g_direct_equal);

	if (!g_hash_table_contains (disconnect_preserve_servers, serv))
	{
		g_hash_table_add (disconnect_preserve_servers, serv);
		g_idle_add (maingui_disconnect_preserve_clear_idle, serv);
	}

	for (list = sess_list; list; list = list->next)
	{
		session *sess = list->data;

		if (!sess || sess->server != serv || sess->type != SESS_CHANNEL)
			continue;

		if (sess->channel[0])
			safe_strcpy (sess->willjoinchannel, sess->channel, CHANLEN);
		else if (sess->waitchannel[0] && !sess->willjoinchannel[0])
			safe_strcpy (sess->willjoinchannel, sess->waitchannel, CHANLEN);
	}
}

static gboolean
maingui_preserve_disconnect_text (const session *sess)
{
	if (!sess || sess->type != SESS_CHANNEL || !sess->server)
		return FALSE;

	if (!sess->waitchannel[0] || !disconnect_preserve_servers)
		return FALSE;

	return g_hash_table_contains (disconnect_preserve_servers, sess->server);
}

static int
maingui_right_pane_min_size (void)
{
	return MAX (prefs.hex_gui_pane_right_size_min, GUI_PANE_RIGHT_MIN);
}

static int
maingui_right_pane_clamp_size (int size)
{
	int min_right;
	int max_right;

	min_right = maingui_right_pane_min_size ();
	max_right = MAX (GUI_PANE_RIGHT_MAX, min_right);

	return CLAMP (size, min_right, max_right);
}

static GtkBuilder *
maingui_ui_builder_new (const char *resource_path)
{
	return fe_gtk4_builder_new_from_resource (resource_path);
}

static GObject *
maingui_ui_get_object_typed (GtkBuilder *builder, const char *id, GType type)
{
	GObject *obj;

	obj = gtk_builder_get_object (builder, id);
	if (!obj)
		g_error ("UI object id '%s' not found", id);

	if (type && !g_type_is_a (G_OBJECT_TYPE (obj), type))
		g_error ("UI object id '%s' has unexpected type '%s'",
			id, G_OBJECT_TYPE_NAME (obj));

	return obj;
}

static GtkWidget *
maingui_ui_get_widget_typed (GtkBuilder *builder, const char *id, GType type)
{
	return GTK_WIDGET (maingui_ui_get_object_typed (builder, id, type));
}

static void
maingui_build_main_box_from_ui (void)
{
	GtkBuilder *builder;
	GtkWidget *box;

	builder = maingui_ui_builder_new (MAINGUI_UI_BASE "/main-box.ui");
	box = maingui_ui_get_widget_typed (builder, "main_box", GTK_TYPE_BOX);
	g_object_ref_sink (box);
	g_object_unref (builder);
	main_box = box;
}

static void
maingui_build_chat_layout_from_ui (GtkWidget **chat_scroll_slot,
	GtkWidget **userlist_slot,
	GtkWidget **command_entry_slot)
{
	GtkBuilder *builder;
	GtkWidget *right_box;

	if (chat_scroll_slot)
		*chat_scroll_slot = NULL;
	if (userlist_slot)
		*userlist_slot = NULL;
	if (command_entry_slot)
		*command_entry_slot = NULL;

	builder = maingui_ui_builder_new (MAINGUI_UI_BASE "/chat-layout.ui");
	right_box = maingui_ui_get_widget_typed (builder, "main_right_box", GTK_TYPE_BOX);
	main_right_paned = maingui_ui_get_widget_typed (builder, "main_right_paned", GTK_TYPE_PANED);
	main_center_box = maingui_ui_get_widget_typed (builder, "main_center_box", GTK_TYPE_BOX);
	entry_row = maingui_ui_get_widget_typed (builder, "entry_row", GTK_TYPE_BOX);
	input_composer_box = maingui_ui_get_widget_typed (builder, "input_composer_box", GTK_TYPE_BOX);
	input_nick_box = maingui_ui_get_widget_typed (builder, "input_nick_box", GTK_TYPE_BOX);
	input_send_button = maingui_ui_get_widget_typed (builder, "input_send_button", GTK_TYPE_BUTTON);

	if (chat_scroll_slot)
		*chat_scroll_slot = maingui_ui_get_widget_typed (builder, "chat_scroll_slot", GTK_TYPE_BOX);
	if (userlist_slot)
		*userlist_slot = maingui_ui_get_widget_typed (builder, "userlist_slot", GTK_TYPE_BOX);
	if (command_entry_slot)
		*command_entry_slot = maingui_ui_get_widget_typed (builder, "command_entry_slot", GTK_TYPE_BOX);

	g_object_ref_sink (right_box);
	g_object_unref (builder);
	main_right_box = right_box;
}

static void
maingui_build_navigation_split_from_ui (GtkWidget **sidebar_header,
	GtkWidget **sidebar_content_slot,
	GtkWidget **content_header,
	GtkWidget **content_content_slot,
	GtkWidget **content_title)
{
	GtkBuilder *builder;
	GtkWidget *split;

	if (sidebar_header)
		*sidebar_header = NULL;
	if (sidebar_content_slot)
		*sidebar_content_slot = NULL;
	if (content_header)
		*content_header = NULL;
	if (content_content_slot)
		*content_content_slot = NULL;
	if (content_title)
		*content_title = NULL;

	builder = maingui_ui_builder_new (MAINGUI_UI_BASE "/adw-nav-split.ui");
	split = maingui_ui_get_widget_typed (builder, "content_paned", ADW_TYPE_OVERLAY_SPLIT_VIEW);
	main_nav_sidebar_page = ADW_NAVIGATION_PAGE (
		maingui_ui_get_object_typed (builder, "main_nav_sidebar_page", ADW_TYPE_NAVIGATION_PAGE));
	main_nav_content_page = ADW_NAVIGATION_PAGE (
		maingui_ui_get_object_typed (builder, "main_nav_content_page", ADW_TYPE_NAVIGATION_PAGE));
	if (sidebar_header)
		*sidebar_header = maingui_ui_get_widget_typed (builder, "sidebar_header", ADW_TYPE_HEADER_BAR);
	if (sidebar_content_slot)
		*sidebar_content_slot = maingui_ui_get_widget_typed (builder, "sidebar_content_slot", GTK_TYPE_BOX);
	if (content_header)
		*content_header = maingui_ui_get_widget_typed (builder, "content_header", ADW_TYPE_HEADER_BAR);
	if (content_content_slot)
		*content_content_slot = maingui_ui_get_widget_typed (builder, "content_content_slot", GTK_TYPE_BOX);
	if (content_title)
		*content_title = maingui_ui_get_widget_typed (builder, "content_title", ADW_TYPE_WINDOW_TITLE);

	g_object_ref_sink (split);
	g_object_unref (builder);
	content_paned = split;
	main_nav_split = split;
}

static void
maingui_install_css (void)
{
	GdkDisplay *display;
	const char *css;

	if (maingui_css_provider)
		return;

	display = gdk_display_get_default ();
	if (!display)
		return;

	css =
		"paned.hc-soft-paned > separator {\n"
		"  background-color: alpha(currentColor, 0.04);\n"
		"  border: none;\n"
		"  box-shadow: none;\n"
		"  min-width: 3px;\n"
		"  background-image: none;\n"
		"}\n"
		".hc-input-row {\n"
		"  background-color: @view_bg_color;\n"
		"  padding: 4px 6px 6px 6px;\n"
		"  border-top: 1px solid alpha(currentColor, 0.04);\n"
		"}\n"
		".hc-input-composer {\n"
		"  border-radius: 10px;\n"
		"  border: 1px solid alpha(currentColor, 0.06);\n"
		"  padding: 2px 6px;\n"
		"  background-color: alpha(currentColor, 0.07);\n"
		"}\n"
		".hc-input-composer:focus-within {\n"
		"  background-color: alpha(currentColor, 0.09);\n"
		"  border-color: alpha(currentColor, 0.12);\n"
		"  box-shadow: none;\n"
		"}\n"
		".hc-input-nick {\n"
		"  border-radius: 10px;\n"
		"  padding: 0 7px;\n"
		"  min-height: 28px;\n"
		"  font-weight: 700;\n"
		"}\n"
		".hc-input-nick:hover {\n"
		"  background-color: alpha(currentColor, 0.08);\n"
		"}\n"
		".hc-command-entry {\n"
		"  min-height: 28px;\n"
		"  box-shadow: none;\n"
		"  outline: none;\n"
		"}\n"
		".hc-command-entry:focus,\n"
		".hc-command-entry:focus-within {\n"
		"  box-shadow: none;\n"
		"  outline: none;\n"
		"}\n"
		".hc-command-entry > text {\n"
		"  background-color: transparent;\n"
		"  box-shadow: none;\n"
		"  outline: none;\n"
		"}\n"
		".hc-command-entry:focus > text,\n"
		".hc-command-entry:focus-within > text {\n"
		"  box-shadow: none;\n"
		"  outline: none;\n"
		"}\n"
		".hc-input-send {\n"
		"  min-width: 28px;\n"
		"  min-height: 28px;\n"
		"}\n"
		".hc-input-send:not(:disabled) {\n"
		"  background-color: alpha(@accent_bg_color, 0.24);\n"
		"  color: @accent_fg_color;\n"
		"}\n"
		".hc-input-send:disabled {\n"
		"  background-color: transparent;\n"
		"  color: alpha(currentColor, 0.45);\n"
		"}\n";

	maingui_css_provider = gtk_css_provider_new ();
	gtk_css_provider_load_from_string (maingui_css_provider, css);
	gtk_style_context_add_provider_for_display (display,
		GTK_STYLE_PROVIDER (maingui_css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
userlist_split_animation_stop (void)
{
	if (userlist_split_anim_source)
	{
		g_source_remove (userlist_split_anim_source);
		userlist_split_anim_source = 0;
	}
	userlist_split_animating = FALSE;
}

static gboolean
userlist_split_animation_cb (gpointer userdata)
{
	double t;
	double eased;
	double inv;
	int pos;
	gint64 now_us;
	const double duration_us = 250000.0;

	(void) userdata;

	if (!main_right_paned)
	{
		userlist_split_animation_stop ();
		return G_SOURCE_REMOVE;
	}

	now_us = g_get_monotonic_time ();
	t = (now_us - userlist_split_anim_start_us) / duration_us;
	if (t >= 1.0)
		t = 1.0;
	if (t < 0.0)
		t = 0.0;
	inv = 1.0 - t;
	eased = 1.0 - (inv * inv * inv);
	pos = userlist_split_anim_from +
		(int) ((userlist_split_anim_to - userlist_split_anim_from) * eased);

	gtk_paned_set_position (GTK_PANED (main_right_paned), pos);

	if (t >= 1.0)
	{
		userlist_split_animation_stop ();
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

static gboolean
maingui_uses_navigation_split (void)
{
	return content_paned &&
		(ADW_IS_OVERLAY_SPLIT_VIEW (content_paned) || ADW_IS_NAVIGATION_SPLIT_VIEW (content_paned));
}

static void
nav_split_state_notify_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
	gboolean visible;
	gboolean collapsed;
	const char *prop_name;
	int width;
	double fraction;

	(void) object;
	(void) pspec;
	(void) user_data;

	if (!main_nav_split)
		return;

	prop_name = pspec ? g_param_spec_get_name (pspec) : NULL;
	width = gtk_widget_get_width (main_nav_split);
	if (ADW_IS_OVERLAY_SPLIT_VIEW (main_nav_split))
	{
		collapsed = adw_overlay_split_view_get_collapsed (ADW_OVERLAY_SPLIT_VIEW (main_nav_split));
		visible = adw_overlay_split_view_get_show_sidebar (ADW_OVERLAY_SPLIT_VIEW (main_nav_split));

		/* On automatic collapse (window gets narrow), keep sidebar hidden.
		 * Overlay mode should be opened only via the explicit sidebar button.
		 */
		if (collapsed && visible && g_strcmp0 (prop_name, "collapsed") == 0)
		{
			adw_overlay_split_view_set_show_sidebar (ADW_OVERLAY_SPLIT_VIEW (main_nav_split), FALSE);
			visible = FALSE;
		}

		/* In wide mode, reflect the persistent sidebar preference. */
		if (!collapsed && (visible != left_sidebar_visible))
		{
			adw_overlay_split_view_set_show_sidebar (ADW_OVERLAY_SPLIT_VIEW (main_nav_split),
				left_sidebar_visible ? TRUE : FALSE);
			visible = left_sidebar_visible;
		}
	}
	else if (ADW_IS_NAVIGATION_SPLIT_VIEW (main_nav_split))
	{
		collapsed = adw_navigation_split_view_get_collapsed (ADW_NAVIGATION_SPLIT_VIEW (main_nav_split));
		if (collapsed)
			visible = !adw_navigation_split_view_get_show_content (ADW_NAVIGATION_SPLIT_VIEW (main_nav_split));
		else
			visible = TRUE;
	}
	else
	{
		return;
	}

	if (pane_positions_ready && !collapsed)
	{
		if (ADW_IS_OVERLAY_SPLIT_VIEW (main_nav_split))
			fraction = adw_overlay_split_view_get_sidebar_width_fraction (ADW_OVERLAY_SPLIT_VIEW (main_nav_split));
		else
			fraction = adw_navigation_split_view_get_sidebar_width_fraction (ADW_NAVIGATION_SPLIT_VIEW (main_nav_split));
		if (width > 0 && fraction > 0.0)
			prefs.hex_gui_pane_left_size = CLAMP ((int) (width * fraction), 1, MAX (1, width - 1));
	}

	if (!ADW_IS_OVERLAY_SPLIT_VIEW (main_nav_split))
		left_sidebar_visible = visible ? TRUE : FALSE;

	fe_gtk4_adw_sync_sidebar_button (visible ? TRUE : FALSE);
}

static void
maingui_install_nav_split_breakpoints (void)
{
	AdwBreakpointCondition *condition;
	AdwBreakpoint *breakpoint;
	GValue value = G_VALUE_INIT;

	if (!main_window || !main_nav_split)
		return;

	condition = adw_breakpoint_condition_parse (NAV_SPLIT_COLLAPSE_CONDITION);
	if (!condition)
		return;

	breakpoint = adw_breakpoint_new (condition);
	if (!breakpoint)
	{
		adw_breakpoint_condition_free (condition);
		return;
	}

	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, TRUE);
	adw_breakpoint_add_setter (breakpoint, G_OBJECT (main_nav_split), "collapsed", &value);
	g_value_unset (&value);

	if (ADW_IS_WINDOW (main_window))
		adw_window_add_breakpoint (ADW_WINDOW (main_window), breakpoint);
	else if (ADW_IS_APPLICATION_WINDOW (main_window))
		adw_application_window_add_breakpoint (ADW_APPLICATION_WINDOW (main_window), breakpoint);
}

static void
entry_update_nick (session *sess)
{
	const char *nick;

	nick = "";
	if (sess && sess->server && sess->server->nick[0])
		nick = sess->server->nick;

	if (input_nick_button)
		gtk_button_set_label (GTK_BUTTON (input_nick_button), nick);
	if (input_nick_box)
		gtk_widget_set_visible (input_nick_box, prefs.hex_gui_input_nick != 0);
}

static void
entry_apply_input_font (void)
{
	PangoFontDescription *desc;
	PangoAttrList *attrs;
	const char *family;
	int size;

	if (!command_entry || !GTK_IS_ENTRY (command_entry))
		return;

	if (!prefs.hex_gui_input_style || !prefs.hex_text_font[0])
	{
		gtk_entry_set_attributes (GTK_ENTRY (command_entry), NULL);
		return;
	}

	desc = pango_font_description_from_string (prefs.hex_text_font);
	if (!desc)
	{
		gtk_entry_set_attributes (GTK_ENTRY (command_entry), NULL);
		return;
	}

	attrs = pango_attr_list_new ();

	family = pango_font_description_get_family (desc);
	if (family && family[0])
	{
		PangoAttribute *attr = pango_attr_family_new (family);
		pango_attr_list_insert (attrs, attr);
	}

	size = pango_font_description_get_size (desc);
	if (size > 0)
	{
		PangoAttribute *attr;

		if (pango_font_description_get_size_is_absolute (desc))
			attr = pango_attr_size_new_absolute (size);
		else
			attr = pango_attr_size_new (size);
		pango_attr_list_insert (attrs, attr);
	}

	if (pango_font_description_get_style (desc) != PANGO_STYLE_NORMAL)
	{
		PangoAttribute *attr = pango_attr_style_new (pango_font_description_get_style (desc));
		pango_attr_list_insert (attrs, attr);
	}

	if (pango_font_description_get_weight (desc) != PANGO_WEIGHT_NORMAL)
	{
		PangoAttribute *attr = pango_attr_weight_new (pango_font_description_get_weight (desc));
		pango_attr_list_insert (attrs, attr);
	}

	gtk_entry_set_attributes (GTK_ENTRY (command_entry), attrs);
	pango_attr_list_unref (attrs);
	pango_font_description_free (desc);
}

void
fe_gtk4_maingui_apply_input_font (void)
{
	entry_apply_input_font ();
}

static gboolean
topic_supports_session_type (session *sess)
{
	if (!sess)
		return FALSE;

	switch (sess->type)
	{
	case SESS_CHANNEL:
	case SESS_DIALOG:
	case SESS_NOTICES:
	case SESS_SNOTICES:
		return TRUE;
	default:
		return FALSE;
	}
}

static void
topic_update_for_session (session *sess)
{
	const char *topic;
	char *topic_clean;
	const char *shown_topic;
	gboolean can_show;

	if (!sess)
		sess = current_tab;

	can_show = prefs.hex_gui_topicbar != 0 && topic_supports_session_type (sess);
	if (!can_show)
	{
		fe_gtk4_adw_set_window_subtitle (NULL);
		return;
	}

	topic = sess->topic ? sess->topic : "";
	topic_clean = NULL;
	shown_topic = topic;
	if (prefs.hex_text_stripcolor_topic && topic[0])
	{
		topic_clean = strip_color (topic, -1, STRIP_COLOR);
		if (topic_clean)
			shown_topic = topic_clean;
	}

	fe_gtk4_adw_set_window_subtitle (shown_topic);

	g_free (topic_clean);
}

void
fe_gtk4_topicbar_set_visible (gboolean visible)
{
	prefs.hex_gui_topicbar = visible ? 1 : 0;
	topic_update_for_session (current_tab);
}

gboolean
fe_gtk4_topicbar_get_visible (void)
{
	return prefs.hex_gui_topicbar ? TRUE : FALSE;
}

void
fe_gtk4_maingui_set_menubar_visible (gboolean visible)
{
	prefs.hex_gui_hide_menu = visible ? 0 : 1;
	if (menu_bar)
		gtk_widget_set_visible (menu_bar, visible ? TRUE : FALSE);
}

gboolean
fe_gtk4_maingui_get_menubar_visible (void)
{
	return prefs.hex_gui_hide_menu ? FALSE : TRUE;
}

void
fe_gtk4_maingui_set_fullscreen (gboolean fullscreen)
{
	prefs.hex_gui_win_fullscreen = fullscreen ? 1 : 0;
	if (!main_window)
		return;

	if (fullscreen)
		gtk_window_fullscreen (GTK_WINDOW (main_window));
	else
		gtk_window_unfullscreen (GTK_WINDOW (main_window));
}

gboolean
fe_gtk4_maingui_get_fullscreen (void)
{
	return prefs.hex_gui_win_fullscreen ? TRUE : FALSE;
}

void
fe_gtk4_maingui_set_sidebar_widget (GtkWidget *sidebar)
{
	if (main_nav_sidebar_page && maingui_uses_navigation_split ())
		adw_navigation_page_set_child (main_nav_sidebar_page, sidebar);
}

void
fe_gtk4_maingui_set_left_sidebar_visible (gboolean visible)
{
	AdwNavigationSplitView *split;

	left_sidebar_visible = visible ? TRUE : FALSE;

	if (!content_paned || !maingui_uses_navigation_split ())
	{
		fe_gtk4_adw_sync_sidebar_button (left_sidebar_visible);
		return;
	}

	if (ADW_IS_OVERLAY_SPLIT_VIEW (content_paned))
	{
		adw_overlay_split_view_set_show_sidebar (ADW_OVERLAY_SPLIT_VIEW (content_paned),
			left_sidebar_visible ? TRUE : FALSE);
		fe_gtk4_adw_sync_sidebar_button (left_sidebar_visible);
		return;
	}

	if (!ADW_IS_NAVIGATION_SPLIT_VIEW (content_paned))
	{
		fe_gtk4_adw_sync_sidebar_button (left_sidebar_visible);
		return;
	}

	split = ADW_NAVIGATION_SPLIT_VIEW (content_paned);
	if (left_sidebar_visible)
	{
		adw_navigation_split_view_set_collapsed (split, FALSE);
		adw_navigation_split_view_set_show_content (split, TRUE);
	}
	else
	{
		adw_navigation_split_view_set_show_content (split, TRUE);
		adw_navigation_split_view_set_collapsed (split, TRUE);
	}

	fe_gtk4_adw_sync_sidebar_button (left_sidebar_visible);
}

gboolean
fe_gtk4_maingui_get_left_sidebar_visible (void)
{
	if (!content_paned || !maingui_uses_navigation_split ())
		return left_sidebar_visible;

	if (ADW_IS_OVERLAY_SPLIT_VIEW (content_paned))
		return adw_overlay_split_view_get_show_sidebar (ADW_OVERLAY_SPLIT_VIEW (content_paned)) ? TRUE : FALSE;

	if (ADW_IS_NAVIGATION_SPLIT_VIEW (content_paned))
	{
		if (adw_navigation_split_view_get_collapsed (ADW_NAVIGATION_SPLIT_VIEW (content_paned)))
			return adw_navigation_split_view_get_show_content (ADW_NAVIGATION_SPLIT_VIEW (content_paned)) ? FALSE : TRUE;
		return TRUE;
	}

	return left_sidebar_visible;
}

static void
nick_change_cb (int cancel, char *text, gpointer userdata)
{
	char buf[256];

	(void) userdata;

	if (cancel || !text || !text[0] || !current_sess)
		return;

	g_snprintf (buf, sizeof (buf), "nick %s", text);
	handle_command (current_sess, buf, FALSE);
}

static void
nick_button_clicked_cb (GtkWidget *button, gpointer userdata)
{
	session *sess;

	(void) button;
	(void) userdata;

	sess = fe_gtk4_window_target_session ();
	if (!sess || !sess->server)
		return;

	fe_get_str (_("Enter new nickname:"), sess->server->nick, nick_change_cb, (void *) 1);
}

static void
right_pane_pos_cb (GtkPaned *pane, GParamSpec *pspec, gpointer user_data)
{
	int width;
	int pos;
	int right_size;
	int wanted_right;

	(void) pspec;
	(void) user_data;

	if (!pane_positions_ready || prefs.hex_gui_ulist_hide || userlist_split_animating)
		return;

	width = gtk_widget_get_width (GTK_WIDGET (pane));
	pos = gtk_paned_get_position (pane);
	if (width <= 0 || pos < 0)
		return;

	wanted_right = width - pos;
	right_size = maingui_right_pane_clamp_size (wanted_right);
	if (right_size != wanted_right)
	{
		pos = CLAMP (width - right_size, 0, MAX (0, width - 1));
		gtk_paned_set_position (pane, pos);
	}
	prefs.hex_gui_pane_right_size = right_size;
}

void
fe_gtk4_maingui_animate_userlist_split (gboolean visible)
{
	int width;
	int pos;
	int right_size;
	int target_pos;

	if (!main_right_paned)
		return;

	width = gtk_widget_get_width (main_right_paned);
	if (width <= 0)
		return;

	pos = gtk_paned_get_position (GTK_PANED (main_right_paned));
	if (pos < 0)
		pos = width;

	right_size = maingui_right_pane_clamp_size (prefs.hex_gui_pane_right_size);
	if (right_size >= width)
		right_size = MIN (width, MAX (maingui_right_pane_min_size (), GUI_PANE_RIGHT_DEFAULT));

	if (visible)
		target_pos = CLAMP (width - right_size, 0, MAX (0, width - 1));
	else
		target_pos = width;

	userlist_split_animation_stop ();
	userlist_split_anim_from = pos;
	userlist_split_anim_to = target_pos;
	userlist_split_anim_start_us = g_get_monotonic_time ();
	userlist_split_animating = TRUE;
	userlist_split_anim_source = g_timeout_add (16, userlist_split_animation_cb, NULL);
}

static gboolean
apply_initial_panes_cb (gpointer userdata)
{
	int content_width;
	int left_size;
	double left_fraction;
	int right_size;
	int width;
	int pos;

	(void) userdata;

	if (!content_paned || !main_right_paned)
		return G_SOURCE_REMOVE;

	pane_positions_ready = FALSE;

	content_width = gtk_widget_get_width (content_paned);
	width = gtk_widget_get_width (main_right_paned);
	if (content_width <= 0 || width <= 0)
		return G_SOURCE_CONTINUE;

	right_size = maingui_right_pane_clamp_size (prefs.hex_gui_pane_right_size);
	if (right_size >= width)
		right_size = MIN (width, MAX (maingui_right_pane_min_size (), GUI_PANE_RIGHT_DEFAULT));

	left_size = prefs.hex_gui_pane_left_size;
	if (left_size <= 0)
		left_size = GUI_PANE_LEFT_DEFAULT;

	if (maingui_uses_navigation_split ())
	{
		left_fraction = (double) left_size / (double) content_width;
		if (ADW_IS_OVERLAY_SPLIT_VIEW (content_paned))
		{
			left_fraction = CLAMP (left_fraction, 0.16, 0.28);
			adw_overlay_split_view_set_sidebar_width_fraction (
				ADW_OVERLAY_SPLIT_VIEW (content_paned), left_fraction);
			adw_overlay_split_view_set_pin_sidebar (ADW_OVERLAY_SPLIT_VIEW (content_paned), TRUE);
		}
		else
		{
			left_fraction = CLAMP (left_fraction, 0.16, 0.38);
			adw_navigation_split_view_set_sidebar_width_fraction (
				ADW_NAVIGATION_SPLIT_VIEW (content_paned), left_fraction);
		}
		fe_gtk4_maingui_set_left_sidebar_visible (left_sidebar_visible);
	}

	pos = width - right_size;
	pos = CLAMP (pos, 0, MAX (0, width - 1));
	gtk_paned_set_position (GTK_PANED (main_right_paned), pos);
	prefs.hex_gui_pane_right_size = width - pos;
	pane_positions_ready = TRUE;

	return G_SOURCE_REMOVE;
}

void
fe_gtk4_maingui_init (void)
{
	pane_positions_ready = FALSE;
	if (!disconnect_preserve_servers)
		disconnect_preserve_servers = g_hash_table_new (g_direct_hash, g_direct_equal);
	fe_gtk4_chanview_init ();
	fe_gtk4_xtext_init ();
	fe_gtk4_userlist_init ();
}

void
fe_gtk4_maingui_cleanup (void)
{
	fe_gtk4_userlist_cleanup ();
	fe_gtk4_xtext_cleanup ();
	fe_gtk4_chanview_cleanup ();
	g_clear_object (&window_actions);
	main_right_paned = NULL;
	main_right_box = NULL;
	main_center_box = NULL;
	main_nav_split = NULL;
	main_nav_sidebar_page = NULL;
	main_nav_content_page = NULL;
	entry_row = NULL;
	input_composer_box = NULL;
	input_nick_box = NULL;
	input_nick_button = NULL;
	input_send_button = NULL;
	pane_positions_ready = FALSE;
	g_clear_object (&maingui_css_provider);
	userlist_split_animation_stop ();
	userlist_split_anim_start_us = 0;
	userlist_split_anim_from = 0;
	userlist_split_anim_to = 0;
	left_sidebar_visible = TRUE;
	g_clear_pointer (&disconnect_preserve_servers, g_hash_table_unref);
}

session *
fe_gtk4_window_target_session (void)
{
	if (current_tab && is_session (current_tab))
		return current_tab;
	if (current_sess && is_session (current_sess))
		return current_sess;
	if (sess_list)
		return sess_list->data;
	return NULL;
}

static void
session_log_show (session *sess)
{
	fe_gtk4_xtext_show_session (sess);
}

static void
session_sidebar_select (session *sess)
{
	fe_gtk4_chanview_select (sess);
}

static void
session_sidebar_add (session *sess)
{
	fe_gtk4_chanview_add (sess);
}

static void
session_sidebar_remove (session *sess)
{
	fe_gtk4_chanview_remove (sess);
}

static void
send_command (const char *cmd)
{
	char *line;

	if (!cmd || !*cmd || !current_tab)
		return;

	line = g_strdup (cmd);
	handle_multiline (current_tab, line, TRUE, FALSE);
	g_free (line);
}

static void
entry_update_send_sensitivity (void)
{
	const char *text;

	if (!input_send_button || !command_entry)
		return;

	text = gtk_editable_get_text (GTK_EDITABLE (command_entry));
	gtk_widget_set_sensitive (input_send_button, (text && text[0]) ? TRUE : FALSE);
}

static void
entry_changed_cb (GtkEditable *editable, gpointer userdata)
{
	(void) editable;
	(void) userdata;
	entry_update_send_sensitivity ();
}

static void
entry_send_cb (GtkWidget *button, gpointer userdata)
{
	const char *text;

	(void) button;
	(void) userdata;

	if (!command_entry)
		return;

	text = gtk_editable_get_text (GTK_EDITABLE (command_entry));
	if (!text || !*text)
		return;

	send_command (text);
	gtk_editable_set_text (GTK_EDITABLE (command_entry), "");
	gtk_widget_grab_focus (command_entry);
	entry_update_send_sensitivity ();
}

static void
entry_activate_cb (GtkEntry *entry, gpointer userdata)
{
	(void) entry;
	entry_send_cb (NULL, userdata);
}

static gboolean
entry_key_pressed_cb (GtkEventControllerKey *controller,
	guint keyval,
	guint keycode,
	GdkModifierType state,
	gpointer userdata)
{
	session *sess;

	(void) controller;
	(void) keycode;
	(void) userdata;

	sess = fe_gtk4_window_target_session ();
	if (!sess)
		return FALSE;

	return key_handle_key_press (command_entry, keyval, state, sess) ? TRUE : FALSE;
}

static gboolean
window_is_maximized (GtkWindow *window)
{
	return window ? gtk_window_is_maximized (window) : FALSE;
}

static gboolean
window_is_fullscreen (GtkWindow *window)
{
	return window ? gtk_window_is_fullscreen (window) : FALSE;
}

static void
window_save_geometry (GtkWindow *window)
{
	int width;
	int height;
	gboolean maximized;
	gboolean fullscreen;

	if (!window)
		return;

	maximized = window_is_maximized (window);
	fullscreen = window_is_fullscreen (window);

	prefs.hex_gui_win_state = maximized ? 1 : 0;
	prefs.hex_gui_win_fullscreen = fullscreen ? 1 : 0;

	if (!prefs.hex_gui_win_save || maximized || fullscreen)
		return;

	width = gtk_widget_get_width (GTK_WIDGET (window));
	height = gtk_widget_get_height (GTK_WIDGET (window));
	if (width > 0)
		prefs.hex_gui_win_width = width;
	if (height > 0)
		prefs.hex_gui_win_height = height;
}

static gboolean
window_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) userdata;
	window_save_geometry (window);
	hexchat_exit ();
	return TRUE;
}

void
fe_gtk4_create_main_window (void)
{
	GtkWidget *scroll;
	GtkWidget *userlist;
	GtkWidget *chat_scroll_slot;
	GtkWidget *userlist_slot;
	GtkWidget *command_entry_slot;
	GtkWidget *sidebar_header = NULL;
	GtkWidget *sidebar_content_slot = NULL;
	GtkWidget *content_header = NULL;
	GtkWidget *content_content_slot = NULL;
	GtkWidget *content_title = NULL;
	GtkWidget *sidebar_button = NULL;
	GtkWidget *userlist_button = NULL;
	GtkWidget *menu_button = NULL;
	GtkWidget *new_item_button = NULL;
	GSList *iter;

	if (main_window)
		return;

	main_window = fe_gtk4_adw_window_new ();
	gtk_window_set_title (GTK_WINDOW (main_window), PACKAGE_NAME);
	if (prefs.hex_gui_win_save)
		gtk_window_set_default_size (GTK_WINDOW (main_window),
			MAX (prefs.hex_gui_win_width, 106),
			MAX (prefs.hex_gui_win_height, 138));
	else
		gtk_window_set_default_size (GTK_WINDOW (main_window), 960, 700);
	maingui_install_css ();

	maingui_build_main_box_from_ui ();
	gtk_box_set_spacing (GTK_BOX (main_box), 0);
	fe_gtk4_adw_window_set_content (main_window, main_box);

	session_scroller = fe_gtk4_chanview_create_widget ();
	chat_scroll_slot = NULL;
	userlist_slot = NULL;
	command_entry_slot = NULL;

	maingui_build_chat_layout_from_ui (&chat_scroll_slot, &userlist_slot, &command_entry_slot);
	maingui_build_navigation_split_from_ui (&sidebar_header, &sidebar_content_slot,
		&content_header, &content_content_slot, &content_title);
	gtk_box_append (GTK_BOX (sidebar_content_slot), session_scroller);
	gtk_box_append (GTK_BOX (content_content_slot), main_right_box);
	gtk_box_append (GTK_BOX (main_box), content_paned);

	new_item_button = fe_gtk4_adw_new_item_button ();
	adw_header_bar_pack_start (ADW_HEADER_BAR (sidebar_header), new_item_button);
	menu_button = gtk_menu_button_new ();
	gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu_button), "open-menu-symbolic");
	gtk_widget_set_tooltip_text (menu_button, _("Main Menu"));
	adw_header_bar_pack_end (ADW_HEADER_BAR (sidebar_header), menu_button);

	sidebar_button = gtk_button_new_from_icon_name ("sidebar-hide-left-symbolic");
	gtk_widget_add_css_class (sidebar_button, "flat");
	gtk_widget_set_tooltip_text (sidebar_button, _("Hide Sidebar"));
	gtk_actionable_set_action_name (GTK_ACTIONABLE (sidebar_button), "win.toggle-sidebar");
	adw_header_bar_pack_start (ADW_HEADER_BAR (content_header), sidebar_button);

	userlist_button = gtk_button_new_from_icon_name ("sidebar-hide-right-symbolic");
	gtk_widget_add_css_class (userlist_button, "flat");
	gtk_widget_set_tooltip_text (userlist_button, _("Hide User List"));
	gtk_actionable_set_action_name (GTK_ACTIONABLE (userlist_button), "win.toggle-userlist");
	adw_header_bar_pack_end (ADW_HEADER_BAR (content_header), userlist_button);

	adw_overlay_split_view_set_sidebar_width_unit (ADW_OVERLAY_SPLIT_VIEW (content_paned), ADW_LENGTH_UNIT_SP);
	adw_overlay_split_view_set_min_sidebar_width (ADW_OVERLAY_SPLIT_VIEW (content_paned), 150.0);
	adw_overlay_split_view_set_max_sidebar_width (ADW_OVERLAY_SPLIT_VIEW (content_paned), 250.0);
	adw_overlay_split_view_set_sidebar_width_fraction (ADW_OVERLAY_SPLIT_VIEW (content_paned), 0.22);
	adw_overlay_split_view_set_pin_sidebar (ADW_OVERLAY_SPLIT_VIEW (content_paned), TRUE);

	fe_gtk4_adw_bind_header_controls (content_title, sidebar_button, userlist_button, menu_button);
	g_signal_connect (content_paned, "notify::collapsed",
		G_CALLBACK (nav_split_state_notify_cb), NULL);
	g_signal_connect (content_paned, "notify::show-sidebar",
		G_CALLBACK (nav_split_state_notify_cb), NULL);
	g_signal_connect (content_paned, "notify::sidebar-width-fraction",
		G_CALLBACK (nav_split_state_notify_cb), NULL);
	maingui_install_nav_split_breakpoints ();

	scroll = fe_gtk4_xtext_create_widget ();
	gtk_box_append (GTK_BOX (chat_scroll_slot), scroll);

	userlist = fe_gtk4_userlist_create_widget ();
	if (userlist_slot)
		gtk_box_append (GTK_BOX (userlist_slot), userlist);
	else
		gtk_paned_set_end_child (GTK_PANED (main_right_paned), userlist);
	gtk_paned_set_resize_start_child (GTK_PANED (main_right_paned), TRUE);
	gtk_paned_set_shrink_start_child (GTK_PANED (main_right_paned), TRUE);
	gtk_paned_set_resize_end_child (GTK_PANED (main_right_paned), FALSE);
	gtk_paned_set_shrink_end_child (GTK_PANED (main_right_paned), TRUE);

	input_nick_button = gtk_button_new_with_label ("");
	gtk_button_set_has_frame (GTK_BUTTON (input_nick_button), FALSE);
	gtk_widget_set_can_focus (input_nick_button, FALSE);
	gtk_widget_add_css_class (input_nick_button, "flat");
	gtk_widget_add_css_class (input_nick_button, "hc-input-nick");
	gtk_widget_set_valign (input_nick_button, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text (input_nick_button, _("Change Nickname"));
	gtk_box_append (GTK_BOX (input_nick_box), input_nick_button);
	g_signal_connect (input_nick_button, "clicked", G_CALLBACK (nick_button_clicked_cb), NULL);

	command_entry = sexy_spell_entry_new ();
	gtk_widget_set_hexpand (command_entry, TRUE);
	gtk_widget_add_css_class (command_entry, "hc-command-entry");
	gtk_entry_set_has_frame (GTK_ENTRY (command_entry), FALSE);
	gtk_entry_set_placeholder_text (GTK_ENTRY (command_entry), _("Type a message"));
	gtk_box_append (GTK_BOX (command_entry_slot), command_entry);
	g_signal_connect (command_entry, "activate", G_CALLBACK (entry_activate_cb), NULL);
	g_signal_connect (command_entry, "changed", G_CALLBACK (entry_changed_cb), NULL);
	{
		GtkEventController *key_controller = gtk_event_controller_key_new ();
		g_signal_connect (key_controller, "key-pressed",
			G_CALLBACK (entry_key_pressed_cb), NULL);
		gtk_widget_add_controller (command_entry, key_controller);
	}
	sexy_spell_entry_set_checked (SEXY_SPELL_ENTRY (command_entry), prefs.hex_gui_input_spell);
	sexy_spell_entry_set_parse_attributes (SEXY_SPELL_ENTRY (command_entry), prefs.hex_gui_input_attr);
	sexy_spell_entry_activate_default_languages (SEXY_SPELL_ENTRY (command_entry));
	entry_apply_input_font ();

	if (!input_send_button)
	{
		input_send_button = gtk_button_new_from_icon_name ("mail-send-symbolic");
		if (input_composer_box)
			gtk_box_append (GTK_BOX (input_composer_box), input_send_button);
		else
			gtk_box_append (GTK_BOX (entry_row), input_send_button);
	}
	gtk_widget_set_tooltip_text (input_send_button, _("Send Message"));
	gtk_widget_add_css_class (input_send_button, "flat");
	gtk_widget_add_css_class (input_send_button, "circular");
	gtk_widget_add_css_class (input_send_button, "hc-input-send");
	gtk_widget_set_valign (input_send_button, GTK_ALIGN_CENTER);
	g_signal_connect (input_send_button, "clicked", G_CALLBACK (entry_send_cb), NULL);
	entry_update_send_sensitivity ();

	entry_update_nick (current_tab);

	window_actions = g_simple_action_group_new ();
	gtk_widget_insert_action_group (main_window, "win", G_ACTION_GROUP (window_actions));
	fe_gtk4_menu_register_actions ();

	g_signal_connect (main_window, "close-request", G_CALLBACK (window_close_request_cb), NULL);
	fe_gtk4_rebuild_menu_bar ();
	fe_gtk4_tray_sync_window ();

	for (iter = sess_list; iter; iter = iter->next)
	{
		session *sess = iter->data;
		if (!sess)
			continue;
		session_sidebar_add (sess);
	}
	if (current_tab && is_session (current_tab))
	{
		session_sidebar_select (current_tab);
		session_log_show (current_tab);
		fe_gtk4_userlist_show (current_tab);
	}
	else
	{
		fe_gtk4_userlist_show (NULL);
	}
	topic_update_for_session (current_tab);

	g_signal_connect (main_right_paned, "notify::position", G_CALLBACK (right_pane_pos_cb), NULL);
	g_idle_add (apply_initial_panes_cb, NULL);

	if (prefs.hex_gui_win_fullscreen)
		gtk_window_fullscreen (GTK_WINDOW (main_window));
	else if (prefs.hex_gui_win_state)
		gtk_window_maximize (GTK_WINDOW (main_window));

	fe_gtk4_menu_sync_actions ();
	fe_gtk4_adw_sync_sidebar_button (left_sidebar_visible);
	gtk_window_present (GTK_WINDOW (main_window));
}

static int
get_stamp_str (time_t tim, char *dest, int size)
{
	return strftime_validated (dest, size, prefs.hex_stamp_text_format, localtime (&tim));
}

void
fe_new_window (struct session *sess, int focus)
{
	char buf[512];
	gboolean select_tab;

	current_sess = sess;

	if (!sess->server->front_session)
		sess->server->front_session = sess;
	if (!sess->server->server_session)
		sess->server->server_session = sess;
	select_tab = (!current_tab || focus) ? TRUE : FALSE;
	if (select_tab)
		current_tab = sess;

	fe_gtk4_create_main_window ();
	session_sidebar_add (sess);
	fe_gtk4_session_sidebar_update_label (sess);
	if (select_tab)
	{
		session_sidebar_select (sess);
		session_log_show (sess);
		fe_gtk4_userlist_show (sess);
		entry_update_nick (sess);
	}
	fe_set_title (sess);
	topic_update_for_session (sess);

	if (focus)
		gtk_window_present (GTK_WINDOW (main_window));

#ifdef USE_PLUGIN
	if (!sess_list->next)
		g_idle_add (fe_idle, NULL);
#endif

	if (done_intro)
		return;
	done_intro = 1;

	g_snprintf (buf, sizeof (buf),
				"HexChat-GTK4 %s\n"
				"Running on %s\n",
				PACKAGE_VERSION,
				get_sys_str (1));
	fe_print_text (sess, buf, 0, FALSE);

	fe_print_text (sess, "Compiled in Features: "
#ifdef USE_PLUGIN
	"Plugin "
#endif
#ifdef ENABLE_NLS
	"NLS "
#endif
#ifdef USE_OPENSSL
	"OpenSSL "
#endif
	"\n\n", 0, FALSE);
}

void
fe_new_server (struct server *serv)
{
}

void
fe_print_text (struct session *sess, char *text, time_t stamp, gboolean no_activity)
{
	GString *line;
	char stampbuf[64];
	const char *cursor;
	const char *nl;
	gsize seglen;
	session *target;
	gboolean use_stamp;

	if (!text)
		return;

	line = g_string_new ("");
	use_stamp = prefs.hex_stamp_text ? TRUE : FALSE;
	if (!stamp)
		stamp = time (NULL);
	if (use_stamp)
		get_stamp_str (stamp, stampbuf, sizeof (stampbuf));

	cursor = text;
	while (cursor && *cursor)
	{
		nl = strchr (cursor, '\n');
		if (nl)
			seglen = (gsize) (nl - cursor);
		else
			seglen = strlen (cursor);

		if (seglen > 0)
		{
			if (use_stamp)
			{
				g_string_append (line, stampbuf);
				g_string_append_c (line, '\t');
			}
			g_string_append_len (line, cursor, seglen);
		}
		g_string_append_c (line, '\n');

		if (!nl)
			break;
		cursor = nl + 1;
	}

	if (line->len == 0)
		g_string_append_c (line, '\n');

	target = sess;
	if (!target)
		target = fe_gtk4_window_target_session ();

	if (target)
		fe_gtk4_xtext_append_for_session (target, line->str);
	else
		fe_gtk4_append_log_text (line->str);

	if (!no_activity && target)
	{
		if (target == current_tab)
			fe_set_tab_color (target, FE_COLOR_NONE);
		else if (target->tab_state & TAB_STATE_NEW_HILIGHT)
			fe_set_tab_color (target, FE_COLOR_NEW_HILIGHT);
		else if (target->tab_state & TAB_STATE_NEW_MSG)
			fe_set_tab_color (target, FE_COLOR_NEW_MSG);
		else
			fe_set_tab_color (target, FE_COLOR_NEW_DATA);
	}

	g_string_free (line, TRUE);
}

void
fe_message (char *msg, int flags)
{
	char *line;

	(void) flags;

	if (!msg)
		return;

	line = g_strdup (msg);
	if (!g_str_has_suffix (line, "\n"))
	{
		char *tmp = g_strconcat (line, "\n", NULL);
		g_free (line);
		line = tmp;
	}

	if (current_tab && is_session (current_tab))
		fe_gtk4_xtext_append_for_session (current_tab, line);
	else
		fe_gtk4_append_log_text (line);

	g_free (line);
}

void
fe_close_window (struct session *sess)
{
	gboolean was_current;
	session *replacement;

	if (!sess)
		return;

	was_current = (sess == current_tab);
	session_sidebar_remove (sess);
	fe_gtk4_userlist_remove_session (sess);
	fe_gtk4_xtext_remove_session (sess);

	if (sess == current_tab)
		current_tab = NULL;
	if (sess == current_sess)
		current_sess = NULL;

	session_free (sess);

	if (!was_current)
		return;

	replacement = NULL;
	if (current_tab && is_session (current_tab))
		replacement = current_tab;
	else if (sess_list)
		replacement = sess_list->data;

	if (replacement)
		fe_set_channel (replacement);
	else
	{
		fe_gtk4_xtext_show_session (NULL);
		fe_gtk4_userlist_show (NULL);
		fe_gtk4_menu_sync_actions ();
	}
}

void
fe_beep (session *sess)
{
	GdkDisplay *display;

	display = gdk_display_get_default ();
	if (display)
		gdk_display_beep (display);
}

void
fe_timeout_remove (int tag)
{
	g_source_remove (tag);
}

int
fe_timeout_add (int interval, void *callback, void *userdata)
{
	return g_timeout_add (interval, (GSourceFunc) callback, userdata);
}

int
fe_timeout_add_seconds (int interval, void *callback, void *userdata)
{
	return g_timeout_add_seconds (interval, (GSourceFunc) callback, userdata);
}

void
fe_input_remove (int tag)
{
	g_source_remove (tag);
}

int
fe_input_add (int sok, int flags, void *func, void *data)
{
	int tag;
	int type;
	GIOChannel *channel;

	type = 0;
	channel = g_io_channel_unix_new (sok);

	if (flags & FIA_READ)
		type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
	if (flags & FIA_WRITE)
		type |= G_IO_OUT | G_IO_ERR;
	if (flags & FIA_EX)
		type |= G_IO_PRI;

	tag = g_io_add_watch (channel, type, (GIOFunc) func, data);
	g_io_channel_unref (channel);
	return tag;
}

void
fe_idle_add (void *func, void *data)
{
	g_idle_add ((GSourceFunc) func, data);
}

void
fe_ctrl_gui (session *sess, fe_gui_action action, int arg)
{
	switch (action)
	{
	case FE_GUI_FOCUS:
		if (sess)
		{
			fe_set_channel (sess);
		}
		if (main_window)
		{
			gtk_window_present (GTK_WINDOW (main_window));
			fe_gtk4_tray_menu_emit_layout_updated (0);
		}
		break;
	case FE_GUI_HIDE:
		if (main_window)
		{
			gtk_widget_set_visible (main_window, FALSE);
			fe_gtk4_tray_menu_emit_layout_updated (0);
		}
		break;
	case FE_GUI_SHOW:
		if (main_window)
		{
			gtk_widget_set_visible (main_window, TRUE);
			fe_gtk4_tray_menu_emit_layout_updated (0);
		}
		break;
	case FE_GUI_MENU:
		if (menu_bar)
			gtk_widget_set_visible (menu_bar, !gtk_widget_get_visible (menu_bar));
		break;
	case FE_GUI_ATTACH:
		/* GTK4 frontend is currently single-window; keep GUI ATTACH/DETACH compatible. */
		if (sess)
			fe_set_channel (sess);
		if (main_window)
			gtk_window_present (GTK_WINDOW (main_window));
		break;
	case FE_GUI_FLASH:
		fe_flash_window (sess);
		break;
	case FE_GUI_ICONIFY:
		if (main_window)
			gtk_window_minimize (GTK_WINDOW (main_window));
		fe_gtk4_tray_menu_emit_layout_updated (0);
		break;
	default:
		break;
	}
}

int
fe_gui_info (session *sess, int info_type)
{
	if (info_type == 0)
	{
		if (!main_window || !gtk_widget_get_visible (main_window))
			return 2;
		if (gtk_window_is_active (GTK_WINDOW (main_window)))
			return 1;
		return 0;
	}

	return -1;
}

void *
fe_gui_info_ptr (session *sess, int info_type)
{
	switch (info_type)
	{
	case 1:
		return main_window;
	default:
		return NULL;
	}
}

char *
fe_get_inputbox_contents (struct session *sess)
{
	if (!command_entry)
		return NULL;

	return (char *) gtk_editable_get_text (GTK_EDITABLE (command_entry));
}

int
fe_get_inputbox_cursor (struct session *sess)
{
	if (!command_entry)
		return 0;

	return gtk_editable_get_position (GTK_EDITABLE (command_entry));
}

void
fe_set_inputbox_contents (struct session *sess, char *text)
{
	if (!command_entry)
		return;

	if (!text)
		text = "";

	gtk_editable_set_text (GTK_EDITABLE (command_entry), text);
}

void
fe_set_inputbox_cursor (struct session *sess, int delta, int pos)
{
	if (!command_entry)
		return;

	if (delta)
		pos += gtk_editable_get_position (GTK_EDITABLE (command_entry));

	gtk_editable_set_position (GTK_EDITABLE (command_entry), pos);
}

void
fe_open_url (const char *url)
{
	int url_type;
	char *uri;
	GError *error;

	if (!url || !*url)
		return;

	url_type = url_check_word (url);
	uri = NULL;

	if (url_type == WORD_PATH)
		uri = g_strconcat ("file://", url, NULL);
	else if (url_type == WORD_HOST6)
	{
		if (*url != '[')
			uri = g_strdup_printf ("http://[%s]", url);
		else
			uri = g_strdup_printf ("http://%s", url);
	}
	else if (strchr (url, ':') == NULL)
		uri = g_strdup_printf ("http://%s", url);

	error = NULL;
	g_app_info_launch_default_for_uri (uri ? uri : url, NULL, &error);
	if (error)
	{
		g_warning ("Unable to open URL '%s': %s", url, error->message);
		g_error_free (error);
	}

	g_free (uri);
}

void
fe_set_topic (struct session *sess, char *topic, char *stripped_topic)
{
	const char *shown_topic;

	shown_topic = prefs.hex_text_stripcolor_topic && stripped_topic ? stripped_topic : topic;
	if (sess == current_tab)
	{
		if (prefs.hex_gui_topicbar && topic_supports_session_type (sess))
			fe_gtk4_adw_set_window_subtitle (shown_topic);
		else
			fe_gtk4_adw_set_window_subtitle (NULL);
	}
}

void
fe_set_tab_color (struct session *sess, tabcolor col)
{
	int col_noflags;
	gboolean allow_override;

	if (!sess)
		return;

	col_noflags = (int) (col & ~FE_COLOR_ALLFLAGS);
	allow_override = (col & FE_COLOR_FLAG_NOOVERRIDE) == 0;

	switch (col_noflags)
	{
	case FE_COLOR_NONE:
		sess->tab_state = TAB_STATE_NONE;
		break;
	case FE_COLOR_NEW_DATA:
		if (allow_override || !((sess->tab_state & TAB_STATE_NEW_MSG) ||
			(sess->tab_state & TAB_STATE_NEW_HILIGHT)))
			sess->tab_state = TAB_STATE_NEW_DATA;
		break;
	case FE_COLOR_NEW_MSG:
		if (allow_override || !(sess->tab_state & TAB_STATE_NEW_HILIGHT))
			sess->tab_state = TAB_STATE_NEW_MSG;
		break;
	case FE_COLOR_NEW_HILIGHT:
		sess->tab_state = TAB_STATE_NEW_HILIGHT;
		break;
	default:
		break;
	}

	fe_gtk4_chanview_note_activity (sess, col_noflags);
	sess->last_tab_state = sess->tab_state;
	fe_gtk4_session_sidebar_update_label (sess);
}

void
fe_flash_window (struct session *sess)
{
	(void) sess;

	if (main_window && !gtk_window_is_active (GTK_WINDOW (main_window)))
		fe_beep (sess);
}

void
fe_update_mode_buttons (struct session *sess, char mode, char sign)
{
}

void
fe_update_channel_key (struct session *sess)
{
	fe_set_title (sess);
}

void
fe_update_channel_limit (struct session *sess)
{
	fe_set_title (sess);
}

void
fe_text_clear (struct session *sess, int lines)
{
	fe_gtk4_xtext_clear_session (sess, lines);
}

void
fe_clear_channel (struct session *sess)
{
	gboolean preserve;

	preserve = maingui_preserve_disconnect_text (sess);
	if (!preserve)
		fe_text_clear (sess, 0);
	if (sess == current_tab)
		topic_update_for_session (sess);
}

void
fe_session_callback (struct session *sess)
{
	if (sess == current_tab)
		fe_set_title (sess);
}

void
fe_server_callback (struct server *serv)
{
	GSList *iter;

	for (iter = sess_list; iter; iter = iter->next)
	{
		session *sess = iter->data;
		if (sess && sess->server == serv)
			fe_gtk4_session_sidebar_update_label (sess);
	}

	if (current_tab && current_tab->server == serv)
		fe_set_title (current_tab);
	fe_gtk4_menu_sync_actions ();
}

void
fe_buttons_update (struct session *sess)
{
}

void
fe_dlgbuttons_update (struct session *sess)
{
}

void
fe_set_channel (struct session *sess)
{
	session *prev;

	if (!sess)
		return;

	prev = current_tab;
	current_sess = sess;
	current_tab = sess;
	if (sess->server)
		sess->server->front_session = sess;
	fe_set_tab_color (sess, FE_COLOR_NONE);
	if (prev && prev != sess)
		fe_gtk4_session_sidebar_update_label (prev);
	session_sidebar_select (sess);
	session_log_show (sess);
	fe_gtk4_userlist_show (sess);
	entry_update_nick (sess);
	topic_update_for_session (sess);
	fe_set_title (sess);
	fe_gtk4_menu_sync_actions ();
}

void
fe_set_title (struct session *sess)
{
	char tbuf[512];
	char title_status[64];
	const char *channel_name;
	const char *display_name;
	const char *header_title;
	const char *network;
	gboolean disconnected;

	if (!main_window)
		return;

	display_name = PACKAGE_NAME;
	header_title = display_name;
	if (!sess)
		sess = current_tab;
	if (sess)
		fe_gtk4_session_sidebar_update_label (sess);
	if (!sess || !sess->server)
	{
		gtk_window_set_title (GTK_WINDOW (main_window), display_name);
		fe_gtk4_adw_set_window_title (display_name);
		topic_update_for_session (sess);
		return;
	}

	if (current_tab && sess != current_tab)
		return;

	network = server_get_network (sess->server, TRUE);
	if (!network || !network[0])
		network = sess->server->servername[0] ? sess->server->servername : _("Unknown");
	channel_name = sess->channel[0] ? sess->channel : sess->waitchannel;
	disconnected = !sess->server->connected;

	switch (sess->type)
	{
	case SESS_DIALOG:
		g_snprintf (tbuf, sizeof (tbuf), "%s %s @ %s - %s",
			_("Dialog with"), channel_name,
			network, display_name);
		header_title = channel_name[0] ? channel_name : network;
		break;
	case SESS_SERVER:
		g_snprintf (tbuf, sizeof (tbuf), "%s%s%s - %s",
			prefs.hex_gui_win_nick && sess->server->nick[0] ? sess->server->nick : "",
			prefs.hex_gui_win_nick ? " @ " : "",
			network, display_name);
		header_title = network;
		break;
	case SESS_CHANNEL:
		g_snprintf (tbuf, sizeof (tbuf), "%s%s%s / %s%s%s%s - %s",
			prefs.hex_gui_win_nick && sess->server->nick[0] ? sess->server->nick : "",
			prefs.hex_gui_win_nick ? " @ " : "",
			network,
			channel_name,
			prefs.hex_gui_win_modes && sess->current_modes ? " (" : "",
			prefs.hex_gui_win_modes && sess->current_modes ? sess->current_modes : "",
			prefs.hex_gui_win_modes && sess->current_modes ? ")" : "",
			display_name);
		if (prefs.hex_gui_win_ucount)
		{
			gsize used = strlen (tbuf);
			if (used < sizeof (tbuf))
				g_snprintf (tbuf + used, sizeof (tbuf) - used, " (%d)", sess->total);
		}
		header_title = channel_name[0] ? channel_name : network;
		break;
	case SESS_NOTICES:
	case SESS_SNOTICES:
		g_snprintf (tbuf, sizeof (tbuf), "%s%s%s (notices) - %s",
			prefs.hex_gui_win_nick && sess->server->nick[0] ? sess->server->nick : "",
			prefs.hex_gui_win_nick ? " @ " : "",
			network, display_name);
		header_title = network;
		break;
	default:
		g_snprintf (tbuf, sizeof (tbuf), "%s", display_name);
		header_title = display_name;
		break;
	}

	if (disconnected && sess->type != SESS_DIALOG)
	{
		gsize used;

		g_snprintf (title_status, sizeof (title_status), " (%s)", _("Disconnected"));
		used = strlen (tbuf);
		if (used < sizeof (tbuf))
			g_strlcpy (tbuf + used, title_status, sizeof (tbuf) - used);
	}

	gtk_window_set_title (GTK_WINDOW (main_window), tbuf);
	fe_gtk4_adw_set_window_title (header_title);
	topic_update_for_session (sess);
}

void
fe_set_nonchannel (struct session *sess, int state)
{
	(void) state;
	if (sess == current_tab)
		fe_set_title (sess);
}

void
fe_set_nick (struct server *serv, char *newnick)
{
	(void) newnick;
	fe_server_callback (serv);
	if (current_tab && current_tab->server == serv)
	{
		entry_update_nick (current_tab);
		fe_set_title (current_tab);
	}
}

void
fe_set_lag (server *serv, long lag)
{
}

void
fe_set_throttle (server *serv)
{
}

void
fe_set_away (server *serv)
{
	if (current_tab && current_tab->server == serv)
		fe_set_title (current_tab);
	fe_gtk4_menu_sync_actions ();
}

void
fe_server_event (server *serv, int type, int arg)
{
	char buf[256];
	const char *network;
	session *target_sess;

	if (!serv)
		return;

	network = server_get_network (serv, TRUE);
	if (!network || !network[0])
		network = serv->servername[0] ? serv->servername : _("Unknown");

	switch (type)
	{
	case FE_SE_CONNECT:
		g_snprintf (buf, sizeof (buf), "* Connected to %s.", network);
		break;
	case FE_SE_LOGGEDIN:
		g_snprintf (buf, sizeof (buf), "* Logged in to %s.", network);
		if (arg == 0)
			joind_open (serv);
		break;
	case FE_SE_DISCONNECT:
		maingui_mark_disconnect_preserve (serv);
		g_snprintf (buf, sizeof (buf), "* Disconnected from %s.", network);
		joind_close (serv);
		break;
	case FE_SE_RECONDELAY:
		g_snprintf (buf, sizeof (buf), "* Reconnecting to %s in %d seconds.", network, arg);
		break;
	case FE_SE_CONNECTING:
		g_snprintf (buf, sizeof (buf), "* Connecting to %s...", network);
		break;
	default:
		return;
	}

	target_sess = serv->server_session ? serv->server_session : current_tab;
	if (target_sess)
		fe_print_text (target_sess, buf, 0, FALSE);
	else
	{
		fe_gtk4_append_log_text (buf);
		fe_gtk4_append_log_text ("\n");
	}

	if (current_tab && current_tab->server == serv)
		fe_set_title (current_tab);
}

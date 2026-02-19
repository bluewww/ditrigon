/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 libadwaita integration */

#include "fe-gtk4.h"
#include <adwaita.h>

static GtkWidget *adw_toolbar_view;
static GtkWidget *adw_menu_button;
static GtkWidget *adw_sidebar_button;
static GtkWidget *adw_userlist_button;
static GtkWidget *adw_title_widget;

static void
adw_new_item_clicked_cb (AdwSplitButton *button, gpointer userdata)
{
	session *sess;

	(void) button;
	(void) userdata;

	sess = fe_gtk4_window_target_session ();
	fe_serverlist_open (sess);
}

GtkWidget *
fe_gtk4_adw_new_item_button (void)
{
	GtkWidget *button;
	GMenu *menu;

	button = adw_split_button_new ();
	adw_split_button_set_icon_name (ADW_SPLIT_BUTTON (button), "list-add-symbolic");
	gtk_widget_set_tooltip_text (button, _("Connect to a Network..."));
	adw_split_button_set_dropdown_tooltip (ADW_SPLIT_BUTTON (button), _("More New Options"));
	g_signal_connect (button, "clicked", G_CALLBACK (adw_new_item_clicked_cb), NULL);

	menu = g_menu_new ();
	g_menu_append (menu, _("Connect to a Network..."), "win.network-list");
	g_menu_append (menu, _("Join a Channel..."), "win.server-join");
	adw_split_button_set_menu_model (ADW_SPLIT_BUTTON (button), G_MENU_MODEL (menu));
	g_object_unref (menu);

	return button;
}

static void
adw_button_set_icon_with_fallback (GtkWidget *button, const char *icon_name, const char *fallback_icon_name)
{
	GdkDisplay *display;
	GtkIconTheme *icon_theme;

	if (!button)
		return;

	display = gdk_display_get_default ();
	if (!display)
	{
		gtk_button_set_icon_name (GTK_BUTTON (button), fallback_icon_name);
		return;
	}

	icon_theme = gtk_icon_theme_get_for_display (display);
	if (icon_theme && icon_name && gtk_icon_theme_has_icon (icon_theme, icon_name))
		gtk_button_set_icon_name (GTK_BUTTON (button), icon_name);
	else
		gtk_button_set_icon_name (GTK_BUTTON (button), fallback_icon_name);
}

gboolean
fe_gtk4_adw_use_hamburger_menu (void)
{
	return TRUE;
}

void
fe_gtk4_adw_init (void)
{
	static gboolean initialized;
	AdwStyleManager *style_manager;

	if (initialized)
		return;

	adw_init ();
	style_manager = adw_style_manager_get_default ();
	adw_style_manager_set_color_scheme (style_manager, ADW_COLOR_SCHEME_DEFAULT);

	initialized = TRUE;
}

GtkApplication *
fe_gtk4_adw_application_new (void)
{
	return GTK_APPLICATION (adw_application_new (NULL, G_APPLICATION_NON_UNIQUE));
}

GtkWidget *
fe_gtk4_adw_window_new (void)
{
	GtkApplication *app;

	app = fe_gtk4_get_application ();

	if (app)
		return adw_application_window_new (app);

	return adw_window_new ();
}

void
fe_gtk4_adw_window_set_content (GtkWidget *window, GtkWidget *content)
{
	adw_title_widget = NULL;
	adw_sidebar_button = NULL;
	adw_userlist_button = NULL;
	adw_menu_button = NULL;
	if (ADW_IS_WINDOW (window))
	{
		adw_toolbar_view = adw_toolbar_view_new ();
		adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (adw_toolbar_view), content);
		adw_toolbar_view_set_extend_content_to_top_edge (ADW_TOOLBAR_VIEW (adw_toolbar_view), FALSE);
		adw_toolbar_view_set_top_bar_style (ADW_TOOLBAR_VIEW (adw_toolbar_view), ADW_TOOLBAR_FLAT);
		adw_window_set_content (ADW_WINDOW (window), adw_toolbar_view);
		return;
	}

	if (ADW_IS_APPLICATION_WINDOW (window))
	{
		adw_toolbar_view = adw_toolbar_view_new ();
		adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (adw_toolbar_view), content);
		adw_toolbar_view_set_extend_content_to_top_edge (ADW_TOOLBAR_VIEW (adw_toolbar_view), FALSE);
		adw_toolbar_view_set_top_bar_style (ADW_TOOLBAR_VIEW (adw_toolbar_view), ADW_TOOLBAR_FLAT);
		adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), adw_toolbar_view);
		return;
	}

	gtk_window_set_child (GTK_WINDOW (window), content);
}

void
fe_gtk4_adw_bind_header_controls (GtkWidget *title_widget,
	GtkWidget *sidebar_button,
	GtkWidget *userlist_button,
	GtkWidget *menu_button)
{
	adw_title_widget = title_widget;
	adw_sidebar_button = sidebar_button;
	adw_userlist_button = userlist_button;
	adw_menu_button = menu_button;
	fe_gtk4_adw_sync_sidebar_button (fe_gtk4_maingui_get_left_sidebar_visible ());
	fe_gtk4_adw_sync_userlist_button (prefs.hex_gui_ulist_hide ? FALSE : TRUE);
}

void
fe_gtk4_adw_sync_sidebar_button (gboolean visible)
{
	if (!adw_sidebar_button)
		return;

	if (visible)
	{
		adw_button_set_icon_with_fallback (adw_sidebar_button,
			"sidebar-hide-left-symbolic", "view-left-pane-symbolic");
		gtk_widget_set_tooltip_text (adw_sidebar_button, _("Hide Sidebar"));
	}
	else
	{
		adw_button_set_icon_with_fallback (adw_sidebar_button,
			"sidebar-show-left-symbolic", "view-left-pane-symbolic");
		gtk_widget_set_tooltip_text (adw_sidebar_button, _("Show Sidebar"));
	}
}

void
fe_gtk4_adw_sync_userlist_button (gboolean visible)
{
	if (!adw_userlist_button)
		return;

	if (visible)
	{
		adw_button_set_icon_with_fallback (adw_userlist_button,
			"sidebar-hide-right-symbolic", "view-right-pane-symbolic");
		gtk_widget_set_tooltip_text (adw_userlist_button, _("Hide User List"));
	}
	else
	{
		adw_button_set_icon_with_fallback (adw_userlist_button,
			"sidebar-show-right-symbolic", "view-right-pane-symbolic");
		gtk_widget_set_tooltip_text (adw_userlist_button, _("Show User List"));
	}
}

void
fe_gtk4_adw_set_window_title (const char *title)
{
	if (adw_title_widget && ADW_IS_WINDOW_TITLE (adw_title_widget))
	{
		adw_window_title_set_title (ADW_WINDOW_TITLE (adw_title_widget),
			(title && title[0]) ? title : PACKAGE_NAME);
	}
}

void
fe_gtk4_adw_set_window_subtitle (const char *subtitle)
{
	if (!adw_title_widget || !ADW_IS_WINDOW_TITLE (adw_title_widget))
		return;

	adw_window_title_set_subtitle (ADW_WINDOW_TITLE (adw_title_widget),
		(subtitle && subtitle[0]) ? subtitle : NULL);
	gtk_widget_set_tooltip_text (adw_title_widget,
		(subtitle && subtitle[0]) ? subtitle : NULL);
}

void
fe_gtk4_adw_attach_menu_bar (GtkWidget *menu_widget)
{
	if (!menu_widget)
		return;

	if (adw_toolbar_view && ADW_IS_TOOLBAR_VIEW (adw_toolbar_view))
	{
		adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (adw_toolbar_view), menu_widget);
		return;
	}

	if (main_box)
		gtk_box_prepend (GTK_BOX (main_box), menu_widget);
}

void
fe_gtk4_adw_detach_menu_bar (GtkWidget *menu_widget)
{
	GtkWidget *parent;

	if (!menu_widget)
		return;

	if (adw_toolbar_view && ADW_IS_TOOLBAR_VIEW (adw_toolbar_view))
	{
		adw_toolbar_view_remove (ADW_TOOLBAR_VIEW (adw_toolbar_view), menu_widget);
		return;
	}

	parent = gtk_widget_get_parent (menu_widget);
	if (parent && parent == main_box)
		gtk_box_remove (GTK_BOX (main_box), menu_widget);
}

void
fe_gtk4_adw_set_menu_model (GMenuModel *model)
{
	if (adw_menu_button)
		gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (adw_menu_button), model);
}

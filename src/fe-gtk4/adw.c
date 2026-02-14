/* HexChat GTK4 libadwaita integration */
#include "fe-gtk4.h"

#ifdef USE_LIBADWAITA
#include <adwaita.h>
#endif

#ifdef USE_LIBADWAITA
static GtkWidget *adw_toolbar_view;
static GtkWidget *adw_header_bar;
static GtkWidget *adw_menu_button;
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

static GtkWidget *
adw_new_item_button_new (void)
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
	g_menu_append (menu, _("New Server Tab"), "win.new-server");
	adw_split_button_set_menu_model (ADW_SPLIT_BUTTON (button), G_MENU_MODEL (menu));
	g_object_unref (menu);

	return button;
}
#endif

gboolean
fe_gtk4_adw_use_hamburger_menu (void)
{
#ifdef USE_LIBADWAITA
	return TRUE;
#else
	return FALSE;
#endif
}

void
fe_gtk4_adw_init (void)
{
#ifdef USE_LIBADWAITA
	static gboolean initialized;
	AdwStyleManager *style_manager;

	if (initialized)
		return;

	adw_init ();
	style_manager = adw_style_manager_get_default ();
	adw_style_manager_set_color_scheme (style_manager, ADW_COLOR_SCHEME_DEFAULT);

	initialized = TRUE;
#endif
}

GtkApplication *
fe_gtk4_adw_application_new (void)
{
#ifdef USE_LIBADWAITA
	return GTK_APPLICATION (adw_application_new (NULL, G_APPLICATION_NON_UNIQUE));
#else
	return gtk_application_new (NULL, G_APPLICATION_NON_UNIQUE);
#endif
}

GtkWidget *
fe_gtk4_adw_window_new (void)
{
	GtkApplication *app;

	app = fe_gtk4_get_application ();

#ifdef USE_LIBADWAITA
	if (app)
		return adw_application_window_new (app);

	return adw_window_new ();
#else
	if (app)
		return gtk_application_window_new (app);

	return gtk_window_new ();
#endif
}

void
fe_gtk4_adw_window_set_content (GtkWidget *window, GtkWidget *content)
{
#ifdef USE_LIBADWAITA
	adw_title_widget = NULL;
	if (ADW_IS_WINDOW (window))
	{
		GtkWidget *title;
		GtkWidget *button;

		adw_toolbar_view = adw_toolbar_view_new ();
		adw_header_bar = adw_header_bar_new ();
		adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		title = adw_window_title_new (PACKAGE_NAME, NULL);
		adw_title_widget = title;
		adw_header_bar_set_title_widget (ADW_HEADER_BAR (adw_header_bar), title);

		button = adw_new_item_button_new ();
		adw_header_bar_pack_start (ADW_HEADER_BAR (adw_header_bar), button);

		button = gtk_button_new_from_icon_name ("emblem-system-symbolic");
		gtk_widget_set_tooltip_text (button, _("Preferences"));
		gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "win.preferences");
		adw_header_bar_pack_end (ADW_HEADER_BAR (adw_header_bar), button);

		adw_menu_button = gtk_menu_button_new ();
		gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (adw_menu_button), "open-menu-symbolic");
		gtk_widget_set_tooltip_text (adw_menu_button, _("Main Menu"));
		adw_header_bar_pack_end (ADW_HEADER_BAR (adw_header_bar), adw_menu_button);

		adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (adw_toolbar_view), adw_header_bar);
		adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (adw_toolbar_view), content);
		adw_toolbar_view_set_top_bar_style (ADW_TOOLBAR_VIEW (adw_toolbar_view), ADW_TOOLBAR_FLAT);
		adw_window_set_content (ADW_WINDOW (window), adw_toolbar_view);
		return;
	}

	if (ADW_IS_APPLICATION_WINDOW (window))
	{
		GtkWidget *title;
		GtkWidget *button;

		adw_toolbar_view = adw_toolbar_view_new ();
		adw_header_bar = adw_header_bar_new ();
		adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		title = adw_window_title_new (PACKAGE_NAME, NULL);
		adw_title_widget = title;
		adw_header_bar_set_title_widget (ADW_HEADER_BAR (adw_header_bar), title);

		button = adw_new_item_button_new ();
		adw_header_bar_pack_start (ADW_HEADER_BAR (adw_header_bar), button);

		button = gtk_button_new_from_icon_name ("emblem-system-symbolic");
		gtk_widget_set_tooltip_text (button, _("Preferences"));
		gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "win.preferences");
		adw_header_bar_pack_end (ADW_HEADER_BAR (adw_header_bar), button);

		adw_menu_button = gtk_menu_button_new ();
		gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (adw_menu_button), "open-menu-symbolic");
		gtk_widget_set_tooltip_text (adw_menu_button, _("Main Menu"));
		adw_header_bar_pack_end (ADW_HEADER_BAR (adw_header_bar), adw_menu_button);

		adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (adw_toolbar_view), adw_header_bar);
		adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (adw_toolbar_view), content);
		adw_toolbar_view_set_top_bar_style (ADW_TOOLBAR_VIEW (adw_toolbar_view), ADW_TOOLBAR_FLAT);
		adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), adw_toolbar_view);
		return;
	}
#endif

	gtk_window_set_child (GTK_WINDOW (window), content);
}

void
fe_gtk4_adw_set_window_title (const char *title)
{
#ifdef USE_LIBADWAITA
	if (adw_title_widget && ADW_IS_WINDOW_TITLE (adw_title_widget))
	{
		adw_window_title_set_title (ADW_WINDOW_TITLE (adw_title_widget),
			(title && title[0]) ? title : PACKAGE_NAME);
		adw_window_title_set_subtitle (ADW_WINDOW_TITLE (adw_title_widget), NULL);
	}
#else
	(void) title;
#endif
}

void
fe_gtk4_adw_attach_menu_bar (GtkWidget *menu_widget)
{
	if (!menu_widget)
		return;

#ifdef USE_LIBADWAITA
	if (adw_toolbar_view && ADW_IS_TOOLBAR_VIEW (adw_toolbar_view))
	{
		adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (adw_toolbar_view), menu_widget);
		return;
	}
#endif

	if (main_box)
		gtk_box_prepend (GTK_BOX (main_box), menu_widget);
}

void
fe_gtk4_adw_detach_menu_bar (GtkWidget *menu_widget)
{
	GtkWidget *parent;

	if (!menu_widget)
		return;

#ifdef USE_LIBADWAITA
	if (adw_toolbar_view && ADW_IS_TOOLBAR_VIEW (adw_toolbar_view))
	{
		adw_toolbar_view_remove (ADW_TOOLBAR_VIEW (adw_toolbar_view), menu_widget);
		return;
	}
#endif

	parent = gtk_widget_get_parent (menu_widget);
	if (parent && parent == main_box)
		gtk_box_remove (GTK_BOX (main_box), menu_widget);
}

void
fe_gtk4_adw_set_menu_model (GMenuModel *model)
{
#ifdef USE_LIBADWAITA
	if (adw_menu_button)
		gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (adw_menu_button), model);
#else
	(void) model;
#endif
}

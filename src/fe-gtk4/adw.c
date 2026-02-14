/* HexChat GTK4 libadwaita integration */
#include "fe-gtk4.h"

#ifdef USE_LIBADWAITA
#include <adwaita.h>
#endif

#ifdef USE_LIBADWAITA
static GtkWidget *adw_toolbar_view;
static GtkWidget *adw_header_bar;
#endif

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
	if (ADW_IS_WINDOW (window))
	{
		adw_toolbar_view = adw_toolbar_view_new ();
		adw_header_bar = adw_header_bar_new ();
		adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (adw_toolbar_view), adw_header_bar);
		adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (adw_toolbar_view), content);
		adw_toolbar_view_set_top_bar_style (ADW_TOOLBAR_VIEW (adw_toolbar_view), ADW_TOOLBAR_RAISED);
		adw_window_set_content (ADW_WINDOW (window), adw_toolbar_view);
		return;
	}

	if (ADW_IS_APPLICATION_WINDOW (window))
	{
		adw_toolbar_view = adw_toolbar_view_new ();
		adw_header_bar = adw_header_bar_new ();
		adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (adw_header_bar), TRUE);
		adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (adw_toolbar_view), adw_header_bar);
		adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (adw_toolbar_view), content);
		adw_toolbar_view_set_top_bar_style (ADW_TOOLBAR_VIEW (adw_toolbar_view), ADW_TOOLBAR_RAISED);
		adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), adw_toolbar_view);
		return;
	}
#endif

	gtk_window_set_child (GTK_WINDOW (window), content);
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

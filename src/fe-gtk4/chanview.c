/* HexChat GTK4 channel view dispatcher */
#include "fe-gtk4.h"
#include <adwaita.h>

typedef enum
{
	HC_CHANVIEW_TABS,
	HC_CHANVIEW_TREE,
} HcChanviewImpl;

static HcChanviewImpl active_impl;

void fe_gtk4_chanview_tabs_init (void);
void fe_gtk4_chanview_tabs_cleanup (void);
GtkWidget *fe_gtk4_chanview_tabs_create_widget (void);
void fe_gtk4_chanview_tabs_add (session *sess);
void fe_gtk4_chanview_tabs_remove (session *sess);
void fe_gtk4_chanview_tabs_select (session *sess);
void fe_gtk4_chanview_tabs_update_label (session *sess);

void fe_gtk4_chanview_tree_init (void);
void fe_gtk4_chanview_tree_cleanup (void);
GtkWidget *fe_gtk4_chanview_tree_create_widget (void);
void fe_gtk4_chanview_tree_add (session *sess);
void fe_gtk4_chanview_tree_remove (session *sess);
void fe_gtk4_chanview_tree_select (session *sess);
void fe_gtk4_chanview_tree_update_label (session *sess);

static HcChanviewImpl
chanview_impl_from_prefs (void)
{
	/* GTK2: 0=tabs, 2=tree (1 is reserved); default to tree for non-zero. */
	if (prefs.hex_gui_tab_layout == 0)
		return HC_CHANVIEW_TABS;

	return HC_CHANVIEW_TREE;
}

void
fe_gtk4_chanview_init (void)
{
	active_impl = chanview_impl_from_prefs ();

	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_init ();
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_init ();
		break;
	}
}

void
fe_gtk4_chanview_cleanup (void)
{
	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_cleanup ();
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_cleanup ();
		break;
	}
}

GtkWidget *
fe_gtk4_chanview_create_widget (void)
{
	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		return fe_gtk4_chanview_tabs_create_widget ();
	case HC_CHANVIEW_TREE:
	default:
		return fe_gtk4_chanview_tree_create_widget ();
	}
}

void
fe_gtk4_session_sidebar_update_label (session *sess)
{
	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_update_label (sess);
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_update_label (sess);
		break;
	}
}

void
fe_gtk4_chanview_add (session *sess)
{
	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_add (sess);
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_add (sess);
		break;
	}
}

void
fe_gtk4_chanview_remove (session *sess)
{
	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_remove (sess);
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_remove (sess);
		break;
	}
}

void
fe_gtk4_chanview_select (session *sess)
{
	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_select (sess);
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_select (sess);
		break;
	}
}

int
fe_gtk4_chanview_get_layout (void)
{
	if (active_impl == HC_CHANVIEW_TABS)
		return 0;

	return 2;
}

void
fe_gtk4_chanview_set_layout (int layout)
{
	HcChanviewImpl next_impl;
	GtkWidget *old_scroller;
	GSList *iter;

	next_impl = (layout == 0) ? HC_CHANVIEW_TABS : HC_CHANVIEW_TREE;
	prefs.hex_gui_tab_layout = (next_impl == HC_CHANVIEW_TABS) ? 0 : 2;

	if (next_impl == active_impl)
		return;

	if (!content_paned)
	{
		active_impl = next_impl;
		return;
	}

	old_scroller = session_scroller;

	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_cleanup ();
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_cleanup ();
		break;
	}

	active_impl = next_impl;
	switch (active_impl)
	{
	case HC_CHANVIEW_TABS:
		fe_gtk4_chanview_tabs_init ();
		break;
	case HC_CHANVIEW_TREE:
	default:
		fe_gtk4_chanview_tree_init ();
		break;
	}

	session_scroller = fe_gtk4_chanview_create_widget ();
	fe_gtk4_maingui_set_sidebar_widget (session_scroller);
	if (GTK_IS_PANED (content_paned))
	{
		if (fe_gtk4_maingui_get_left_sidebar_visible ())
		{
			gtk_widget_set_visible (session_scroller, TRUE);
			gtk_paned_set_position (GTK_PANED (content_paned),
				prefs.hex_gui_pane_left_size > 0 ? prefs.hex_gui_pane_left_size : 128);
		}
		else
		{
			gtk_widget_set_visible (session_scroller, FALSE);
			gtk_paned_set_position (GTK_PANED (content_paned), 0);
		}
	}
	else if (ADW_IS_NAVIGATION_SPLIT_VIEW (content_paned))
	{
		gtk_widget_set_visible (session_scroller, TRUE);
		fe_gtk4_maingui_set_left_sidebar_visible (fe_gtk4_maingui_get_left_sidebar_visible ());
	}

	for (iter = sess_list; iter; iter = iter->next)
	{
		session *sess = iter->data;
		if (!sess)
			continue;
		fe_gtk4_chanview_add (sess);
	}

	if (current_tab && is_session (current_tab))
		fe_gtk4_chanview_select (current_tab);

	if (old_scroller && gtk_widget_get_parent (old_scroller))
		gtk_widget_unparent (old_scroller);
}

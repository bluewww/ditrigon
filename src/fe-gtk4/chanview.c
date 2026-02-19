/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 channel view dispatcher */

#include "fe-gtk4.h"
#include <adwaita.h>

void fe_gtk4_chanview_tree_init (void);
void fe_gtk4_chanview_tree_cleanup (void);
GtkWidget *fe_gtk4_chanview_tree_create_widget (void);
void fe_gtk4_chanview_tree_add (session *sess);
void fe_gtk4_chanview_tree_remove (session *sess);
void fe_gtk4_chanview_tree_select (session *sess);
void fe_gtk4_chanview_tree_update_label (session *sess);
void fe_gtk4_chanview_tree_note_activity (session *sess, int color);

void
fe_gtk4_chanview_init (void)
{
	fe_gtk4_chanview_tree_init ();
}

void
fe_gtk4_chanview_cleanup (void)
{
	fe_gtk4_chanview_tree_cleanup ();
}

GtkWidget *
fe_gtk4_chanview_create_widget (void)
{
	return fe_gtk4_chanview_tree_create_widget ();
}

void
fe_gtk4_session_sidebar_update_label (session *sess)
{
	fe_gtk4_chanview_tree_update_label (sess);
}

void
fe_gtk4_chanview_note_activity (session *sess, int color)
{
	fe_gtk4_chanview_tree_note_activity (sess, color);
}

void
fe_gtk4_chanview_add (session *sess)
{
	fe_gtk4_chanview_tree_add (sess);
}

void
fe_gtk4_chanview_remove (session *sess)
{
	fe_gtk4_chanview_tree_remove (sess);
}

void
fe_gtk4_chanview_select (session *sess)
{
	fe_gtk4_chanview_tree_select (sess);
}

int
fe_gtk4_chanview_get_layout (void)
{
	return 2;
}

void
fe_gtk4_chanview_set_layout (int layout)
{
	(void) layout;
}

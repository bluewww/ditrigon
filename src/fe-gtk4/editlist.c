/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 editable NAME/CMD list dialog */

#include "fe-gtk4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define EDITLIST_UI_PATH "/org/ditrigon/ui/gtk4/dialogs/editlist-window.ui"

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
	GSList *source_list;
	char *file;
	char *title1;
	char *title2;
} HcEditListGui;

static HcEditListGui editlist_gui;

static void editlist_close (void);

static GtkWidget *
editlist_row_new (const char *name, const char *cmd)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *name_entry;
	GtkWidget *cmd_entry;

	row = gtk_list_box_row_new ();

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start (box, 8);
	gtk_widget_set_margin_end (box, 8);
	gtk_widget_set_margin_top (box, 4);
	gtk_widget_set_margin_bottom (box, 4);

	name_entry = gtk_entry_new ();
	gtk_widget_set_hexpand (name_entry, TRUE);
	gtk_editable_set_text (GTK_EDITABLE (name_entry), name ? name : "");
	if (editlist_gui.title1 && editlist_gui.title1[0])
		gtk_entry_set_placeholder_text (GTK_ENTRY (name_entry), editlist_gui.title1);

	cmd_entry = gtk_entry_new ();
	gtk_widget_set_hexpand (cmd_entry, TRUE);
	gtk_editable_set_text (GTK_EDITABLE (cmd_entry), cmd ? cmd : "");
	if (editlist_gui.title2 && editlist_gui.title2[0])
		gtk_entry_set_placeholder_text (GTK_ENTRY (cmd_entry), editlist_gui.title2);

	gtk_box_append (GTK_BOX (box), name_entry);
	gtk_box_append (GTK_BOX (box), cmd_entry);

	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
	g_object_set_data (G_OBJECT (row), "hc-name-entry", name_entry);
	g_object_set_data (G_OBJECT (row), "hc-cmd-entry", cmd_entry);

	return row;
}

static void
editlist_populate_from_list (GSList *list)
{
	struct popup *pop;
	GtkWidget *row;

	if (!editlist_gui.list)
		return;

	for (; list; list = list->next)
	{
		pop = list->data;
		if (!pop)
			continue;

		row = editlist_row_new (pop->name, pop->cmd);
		gtk_list_box_append (GTK_LIST_BOX (editlist_gui.list), row);
	}
}

static GtkListBoxRow *
editlist_selected_row (void)
{
	if (!editlist_gui.list)
		return NULL;

	return gtk_list_box_get_selected_row (GTK_LIST_BOX (editlist_gui.list));
}

static void
editlist_add_cb (GtkButton *button, gpointer userdata)
{
	GtkWidget *row;
	GtkWidget *entry;

	(void) button;
	(void) userdata;

	if (!editlist_gui.list)
		return;

	row = editlist_row_new ("", "");
	gtk_list_box_append (GTK_LIST_BOX (editlist_gui.list), row);
	gtk_list_box_select_row (GTK_LIST_BOX (editlist_gui.list), GTK_LIST_BOX_ROW (row));

	entry = g_object_get_data (G_OBJECT (row), "hc-name-entry");
	if (entry)
		gtk_widget_grab_focus (entry);
}

static void
editlist_delete_cb (GtkButton *button, gpointer userdata)
{
	GtkListBoxRow *row;

	(void) button;
	(void) userdata;

	row = editlist_selected_row ();
	if (!row)
		return;

	gtk_list_box_remove (GTK_LIST_BOX (editlist_gui.list), GTK_WIDGET (row));
}

static void
editlist_move_selected (int delta)
{
	GtkListBoxRow *row;
	GtkWidget *child;
	int pos;
	int target;
	int nrows;

	if (!editlist_gui.list || delta == 0)
		return;

	row = editlist_selected_row ();
	if (!row)
		return;

	pos = gtk_list_box_row_get_index (row);
	nrows = 0;
	for (child = gtk_widget_get_first_child (editlist_gui.list);
		 child;
		 child = gtk_widget_get_next_sibling (child))
	{
		nrows++;
	}
	target = pos + delta;
	if (target < 0 || target >= nrows)
		return;

	g_object_ref (row);
	gtk_list_box_remove (GTK_LIST_BOX (editlist_gui.list), GTK_WIDGET (row));
	gtk_list_box_insert (GTK_LIST_BOX (editlist_gui.list), GTK_WIDGET (row), target);
	gtk_list_box_select_row (GTK_LIST_BOX (editlist_gui.list), row);
	g_object_unref (row);
}

static void
editlist_up_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;
	editlist_move_selected (-1);
}

static void
editlist_down_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;
	editlist_move_selected (1);
}

static void
editlist_reload_source (const char *file)
{
	if (!file || !file[0] || !editlist_gui.source_list)
		return;

	if (editlist_gui.source_list == replace_list)
	{
		list_free (&replace_list);
		list_loadconf ((char *) file, &replace_list, 0);
	}
	else if (editlist_gui.source_list == popup_list)
	{
		list_free (&popup_list);
		list_loadconf ((char *) file, &popup_list, 0);
	}
	else if (editlist_gui.source_list == button_list)
	{
		GSList *list = sess_list;
		session *sess;

		list_free (&button_list);
		list_loadconf ((char *) file, &button_list, 0);
		while (list)
		{
			sess = list->data;
			fe_buttons_update (sess);
			list = list->next;
		}
	}
	else if (editlist_gui.source_list == dlgbutton_list)
	{
		GSList *list = sess_list;
		session *sess;

		list_free (&dlgbutton_list);
		list_loadconf ((char *) file, &dlgbutton_list, 0);
		while (list)
		{
			sess = list->data;
			fe_dlgbuttons_update (sess);
			list = list->next;
		}
	}
	else if (editlist_gui.source_list == ctcp_list)
	{
		list_free (&ctcp_list);
		list_loadconf ((char *) file, &ctcp_list, 0);
	}
	else if (editlist_gui.source_list == command_list)
	{
		list_free (&command_list);
		list_loadconf ((char *) file, &command_list, 0);
	}
	else if (editlist_gui.source_list == usermenu_list)
	{
		list_free (&usermenu_list);
		list_loadconf ((char *) file, &usermenu_list, 0);
		fe_gtk4_rebuild_menu_bar ();
	}
	else
	{
		list_free (&urlhandler_list);
		list_loadconf ((char *) file, &urlhandler_list, 0);
	}
}

static gboolean
editlist_save_to_file (const char *file)
{
	GtkWidget *row;
	int fh;

	if (!file || !file[0] || !editlist_gui.list)
		return FALSE;

	fh = hexchat_open_file ((char *) file, O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh == -1)
		return FALSE;

	for (row = gtk_widget_get_first_child (editlist_gui.list);
		 row;
		 row = gtk_widget_get_next_sibling (row))
	{
		GtkWidget *name_entry;
		GtkWidget *cmd_entry;
		const char *name;
		const char *cmd;
		char *buf;

		name_entry = g_object_get_data (G_OBJECT (row), "hc-name-entry");
		cmd_entry = g_object_get_data (G_OBJECT (row), "hc-cmd-entry");
		if (!name_entry || !cmd_entry)
			continue;

		name = gtk_editable_get_text (GTK_EDITABLE (name_entry));
		cmd = gtk_editable_get_text (GTK_EDITABLE (cmd_entry));

		buf = g_strdup_printf ("NAME %s\nCMD %s\n\n",
			name ? name : "",
			cmd ? cmd : "");
		if (write (fh, buf, strlen (buf)) < 0)
		{
			g_warning ("Failed to write editlist");
			break;
		}
		g_free (buf);
	}

	close (fh);
	return TRUE;
}

static void
editlist_save_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (!editlist_save_to_file (editlist_gui.file))
		return;

	editlist_reload_source (editlist_gui.file);
	editlist_close ();
}

static gboolean
editlist_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;
	editlist_close ();
	return FALSE;
}

static void
editlist_cancel_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;
	editlist_close ();
}

static void
editlist_close (void)
{
	GtkWidget *window;

	window = editlist_gui.window;
	editlist_gui.window = NULL;
	editlist_gui.list = NULL;
	editlist_gui.source_list = NULL;
	g_clear_pointer (&editlist_gui.file, g_free);
	g_clear_pointer (&editlist_gui.title1, g_free);
	g_clear_pointer (&editlist_gui.title2, g_free);

	if (window)
		gtk_window_destroy (GTK_WINDOW (window));
}

void
editlist_gui_open (char *title1, char *title2, GSList *list, char *title, char *wmclass,
	char *file, char *help)
{
	GtkWidget *header;
	GtkWidget *help_label;
	GtkBuilder *builder;
	char *header_text;

	(void) wmclass;

	if (editlist_gui.window)
	{
		gtk_window_present (GTK_WINDOW (editlist_gui.window));
		return;
	}

	editlist_gui.source_list = list;
	editlist_gui.file = g_strdup (file ? file : "");
	editlist_gui.title1 = g_strdup (title1 ? title1 : _("Name"));
	editlist_gui.title2 = g_strdup (title2 ? title2 : _("Command"));

	builder = fe_gtk4_builder_new_from_resource (EDITLIST_UI_PATH);
	{
		GtkWidget *add_button;
		GtkWidget *delete_button;
		GtkWidget *up_button;
		GtkWidget *down_button;
		GtkWidget *cancel_button;
		GtkWidget *save_button;

		editlist_gui.window = fe_gtk4_builder_get_widget (builder, "editlist_window", GTK_TYPE_WINDOW);
		header = fe_gtk4_builder_get_widget (builder, "editlist_header", GTK_TYPE_LABEL);
		editlist_gui.list = fe_gtk4_builder_get_widget (builder, "editlist_list", GTK_TYPE_LIST_BOX);
		help_label = fe_gtk4_builder_get_widget (builder, "editlist_help_label", GTK_TYPE_LABEL);
		add_button = fe_gtk4_builder_get_widget (builder, "editlist_add_button", GTK_TYPE_BUTTON);
		delete_button = fe_gtk4_builder_get_widget (builder, "editlist_delete_button", GTK_TYPE_BUTTON);
		up_button = fe_gtk4_builder_get_widget (builder, "editlist_up_button", GTK_TYPE_BUTTON);
		down_button = fe_gtk4_builder_get_widget (builder, "editlist_down_button", GTK_TYPE_BUTTON);
		cancel_button = fe_gtk4_builder_get_widget (builder, "editlist_cancel_button", GTK_TYPE_BUTTON);
		save_button = fe_gtk4_builder_get_widget (builder, "editlist_save_button", GTK_TYPE_BUTTON);
		g_object_ref_sink (editlist_gui.window);
		g_object_unref (builder);

		gtk_button_set_label (GTK_BUTTON (add_button), _("Add"));
		gtk_button_set_label (GTK_BUTTON (delete_button), _("Delete"));
		gtk_button_set_label (GTK_BUTTON (up_button), _("Up"));
		gtk_button_set_label (GTK_BUTTON (down_button), _("Down"));
		gtk_button_set_label (GTK_BUTTON (cancel_button), _("Cancel"));
		gtk_button_set_label (GTK_BUTTON (save_button), _("Save"));
		g_signal_connect (add_button, "clicked", G_CALLBACK (editlist_add_cb), NULL);
		g_signal_connect (delete_button, "clicked", G_CALLBACK (editlist_delete_cb), NULL);
		g_signal_connect (up_button, "clicked", G_CALLBACK (editlist_up_cb), NULL);
		g_signal_connect (down_button, "clicked", G_CALLBACK (editlist_down_cb), NULL);
		g_signal_connect (cancel_button, "clicked", G_CALLBACK (editlist_cancel_cb), NULL);
		g_signal_connect (save_button, "clicked", G_CALLBACK (editlist_save_cb), NULL);

		header_text = g_strdup_printf ("%s / %s", editlist_gui.title1, editlist_gui.title2);
		gtk_label_set_text (GTK_LABEL (header), header_text);
		g_free (header_text);
		if (help && help[0])
		{
			gtk_label_set_text (GTK_LABEL (help_label), help);
			gtk_widget_set_visible (help_label, TRUE);
		}
		else
		{
			gtk_label_set_text (GTK_LABEL (help_label), "");
			gtk_widget_set_visible (help_label, FALSE);
		}
	}

	gtk_window_set_title (GTK_WINDOW (editlist_gui.window), title ? title : _("Edit List"));
	gtk_window_set_default_size (GTK_WINDOW (editlist_gui.window), 760, 420);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (editlist_gui.window), GTK_WINDOW (main_window));

	g_signal_connect (editlist_gui.window, "close-request",
		G_CALLBACK (editlist_close_request_cb), NULL);
	editlist_populate_from_list (list);
	gtk_window_present (GTK_WINDOW (editlist_gui.window));
}

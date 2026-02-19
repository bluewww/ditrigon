/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 ignore list */

#include "fe-gtk4.h"

#include "../common/ignore.h"

#define IGNORE_UI_PATH "/org/hexchat/ui/gtk4/dialogs/ignore-window.ui"

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
	GtkWidget *stats;
	GtkWidget *remove_button;
	GtkWidget *clear_button;
} HcIgnoreView;

static HcIgnoreView ignore_view;

static gboolean
ignore_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	ignore_save ();
	ignore_view.window = NULL;
	ignore_view.list = NULL;
	ignore_view.stats = NULL;
	ignore_view.remove_button = NULL;
	ignore_view.clear_button = NULL;
	return FALSE;
}

static char *
ignore_flags_text (int type)
{
	GString *flags;

	flags = g_string_new ("");
	if (type & IG_CHAN)
		g_string_append (flags, "CHAN ");
	if (type & IG_PRIV)
		g_string_append (flags, "PRIV ");
	if (type & IG_NOTI)
		g_string_append (flags, "NOTI ");
	if (type & IG_CTCP)
		g_string_append (flags, "CTCP ");
	if (type & IG_DCC)
		g_string_append (flags, "DCC ");
	if (type & IG_INVI)
		g_string_append (flags, "INVI ");
	if (type & IG_UNIG)
		g_string_append (flags, "UNIG ");

	if (flags->len == 0)
		g_string_append (flags, _("(no flags)"));

	return g_string_free (flags, FALSE);
}

static void
ignore_clear_rows (void)
{
	GtkWidget *child;

	if (!ignore_view.list)
		return;

	while ((child = gtk_widget_get_first_child (ignore_view.list)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (ignore_view.list), child);
}

static void
ignore_update_stats (void)
{
	char buf[256];

	if (!ignore_view.stats)
		return;

	g_snprintf (buf, sizeof (buf),
		_ ("Entries: %d | Ignored: Chan %d Priv %d Noti %d CTCP %d Invi %d"),
		g_slist_length (ignore_list), ignored_chan, ignored_priv,
		ignored_noti, ignored_ctcp, ignored_invi);
	gtk_label_set_text (GTK_LABEL (ignore_view.stats), buf);
}

static void
ignore_refresh_rows (void)
{
	GSList *list;

	if (!ignore_view.list)
		return;

	ignore_clear_rows ();

	for (list = ignore_list; list; list = list->next)
	{
		struct ignore *ig;
		GtkWidget *row;
		GtkWidget *box;
		GtkWidget *title;
		GtkWidget *subtitle;
		char *flags;

		ig = list->data;
		if (!ig || !ig->mask)
			continue;

		flags = ignore_flags_text ((int) ig->type);

		title = gtk_label_new (ig->mask);
		gtk_label_set_xalign (GTK_LABEL (title), 0.0f);

		subtitle = gtk_label_new (flags);
		gtk_label_set_xalign (GTK_LABEL (subtitle), 0.0f);
		gtk_label_set_wrap (GTK_LABEL (subtitle), TRUE);
		gtk_widget_add_css_class (subtitle, "dim-label");

		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
		gtk_widget_set_margin_start (box, 10);
		gtk_widget_set_margin_end (box, 10);
		gtk_widget_set_margin_top (box, 6);
		gtk_widget_set_margin_bottom (box, 6);
		gtk_box_append (GTK_BOX (box), title);
		gtk_box_append (GTK_BOX (box), subtitle);

		row = gtk_list_box_row_new ();
		gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
		g_object_set_data_full (G_OBJECT (row), "hc-ignore-mask", g_strdup (ig->mask), g_free);
		gtk_list_box_append (GTK_LIST_BOX (ignore_view.list), row);

		g_free (flags);
	}

	ignore_update_stats ();
}

static void
ignore_selection_changed_cb (GtkListBox *box, gpointer userdata)
{
	GtkListBoxRow *row;

	(void) box;
	(void) userdata;

	if (!ignore_view.list)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (ignore_view.list));
	if (ignore_view.remove_button)
		gtk_widget_set_sensitive (ignore_view.remove_button, row != NULL);
}

static void
ignore_remove_selected_cb (GtkButton *button, gpointer userdata)
{
	GtkListBoxRow *row;
	const char *mask;

	(void) button;
	(void) userdata;

	if (!ignore_view.list)
		return;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (ignore_view.list));
	if (!row)
		return;

	mask = g_object_get_data (G_OBJECT (row), "hc-ignore-mask");
	if (!mask || !mask[0])
		return;

	ignore_del ((char *) mask, NULL);
	ignore_save ();
	ignore_refresh_rows ();
	ignore_selection_changed_cb (GTK_LIST_BOX (ignore_view.list), NULL);
}

static void
ignore_clear_confirm_yes (void *userdata)
{
	(void) userdata;

	while (ignore_list)
	{
		struct ignore *ig = ignore_list->data;

		if (!ig)
			break;
		ignore_del (NULL, ig);
	}

	ignore_save ();
	ignore_refresh_rows ();
	ignore_selection_changed_cb (GTK_LIST_BOX (ignore_view.list), NULL);
}

static void
ignore_clear_confirm_no (void *userdata)
{
	(void) userdata;
}

static void
ignore_clear_all_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	if (!ignore_list)
		return;

	fe_confirm (_("Are you sure you want to remove all ignores?"),
		ignore_clear_confirm_yes, ignore_clear_confirm_no, NULL);
}

static void
ignore_add_entry_cb (int cancel, char *mask, void *userdata)
{
	int flags;

	(void) userdata;

	if (cancel || !mask || !mask[0])
		return;

	if (ignore_exists (mask))
	{
		fe_message (_("That mask already exists."), FE_MSG_ERROR);
		return;
	}

	flags = IG_CHAN | IG_PRIV | IG_NOTI | IG_CTCP | IG_DCC | IG_INVI;
	ignore_add (mask, flags, TRUE);
	ignore_save ();
	ignore_refresh_rows ();
}

static void
ignore_add_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	fe_get_str (_("Enter mask to ignore:"), "nick!userid@host.com",
		ignore_add_entry_cb, NULL);
}

void
ignore_gui_open (void)
{
	GtkWidget *add_button;
	GtkWidget *close_button;
	GtkBuilder *builder;

	if (ignore_view.window)
	{
		ignore_refresh_rows ();
		gtk_window_present (GTK_WINDOW (ignore_view.window));
		return;
	}

	builder = fe_gtk4_builder_new_from_resource (IGNORE_UI_PATH);
	ignore_view.window = fe_gtk4_builder_get_widget (builder, "ignore_window", GTK_TYPE_WINDOW);
	ignore_view.stats = fe_gtk4_builder_get_widget (builder, "ignore_stats", GTK_TYPE_LABEL);
	ignore_view.list = fe_gtk4_builder_get_widget (builder, "ignore_list", GTK_TYPE_LIST_BOX);
	add_button = fe_gtk4_builder_get_widget (builder, "ignore_add_button", GTK_TYPE_BUTTON);
	ignore_view.remove_button = fe_gtk4_builder_get_widget (builder, "ignore_remove_button", GTK_TYPE_BUTTON);
	ignore_view.clear_button = fe_gtk4_builder_get_widget (builder, "ignore_clear_button", GTK_TYPE_BUTTON);
	close_button = fe_gtk4_builder_get_widget (builder, "ignore_close_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (ignore_view.window);
	g_object_unref (builder);

	gtk_button_set_label (GTK_BUTTON (add_button), _("Add..."));
	gtk_button_set_label (GTK_BUTTON (ignore_view.remove_button), _("Remove"));
	gtk_button_set_label (GTK_BUTTON (ignore_view.clear_button), _("Clear"));
	gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));
	g_signal_connect (ignore_view.list, "selected-rows-changed",
		G_CALLBACK (ignore_selection_changed_cb), NULL);
	g_signal_connect (add_button, "clicked", G_CALLBACK (ignore_add_cb), NULL);
	g_signal_connect (ignore_view.remove_button, "clicked",
		G_CALLBACK (ignore_remove_selected_cb), NULL);
	g_signal_connect (ignore_view.clear_button, "clicked",
		G_CALLBACK (ignore_clear_all_cb), NULL);
	g_signal_connect_swapped (close_button, "clicked",
		G_CALLBACK (gtk_window_close), ignore_view.window);
	g_signal_connect (ignore_view.window, "close-request",
		G_CALLBACK (ignore_close_request_cb), NULL);

	gtk_window_set_title (GTK_WINDOW (ignore_view.window), _("Ignore List"));
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (ignore_view.window), GTK_WINDOW (main_window));

	ignore_refresh_rows ();
	ignore_selection_changed_cb (GTK_LIST_BOX (ignore_view.list), NULL);
	gtk_window_present (GTK_WINDOW (ignore_view.window));
}

void
fe_ignore_update (int level)
{
	(void) level;

	if (!ignore_view.window)
		return;

	ignore_refresh_rows ();
}

void
fe_gtk4_ignoregui_cleanup (void)
{
	if (!ignore_view.window)
		return;

	gtk_window_destroy (GTK_WINDOW (ignore_view.window));
	ignore_view.window = NULL;
	ignore_view.list = NULL;
	ignore_view.stats = NULL;
	ignore_view.remove_button = NULL;
	ignore_view.clear_button = NULL;
}

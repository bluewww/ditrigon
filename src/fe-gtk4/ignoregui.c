/* HexChat GTK4 ignore list */
#include "fe-gtk4.h"

#include "../common/ignore.h"

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
	GtkWidget *root;
	GtkWidget *scroll;
	GtkWidget *buttons;
	GtkWidget *add_button;
	GtkWidget *close_button;

	if (ignore_view.window)
	{
		ignore_refresh_rows ();
		gtk_window_present (GTK_WINDOW (ignore_view.window));
		return;
	}

	ignore_view.window = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (ignore_view.window), _("Ignore List"));
	gtk_window_set_default_size (GTK_WINDOW (ignore_view.window), 680, 420);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (ignore_view.window), GTK_WINDOW (main_window));

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (root, 12);
	gtk_widget_set_margin_end (root, 12);
	gtk_widget_set_margin_top (root, 12);
	gtk_widget_set_margin_bottom (root, 12);
	gtk_window_set_child (GTK_WINDOW (ignore_view.window), root);

	ignore_view.stats = gtk_label_new ("");
	gtk_label_set_xalign (GTK_LABEL (ignore_view.stats), 0.0f);
	gtk_box_append (GTK_BOX (root), ignore_view.stats);

	scroll = gtk_scrolled_window_new ();
	gtk_widget_set_hexpand (scroll, TRUE);
	gtk_widget_set_vexpand (scroll, TRUE);
	gtk_box_append (GTK_BOX (root), scroll);

	ignore_view.list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (ignore_view.list), GTK_SELECTION_SINGLE);
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), ignore_view.list);
	g_signal_connect (ignore_view.list, "selected-rows-changed",
		G_CALLBACK (ignore_selection_changed_cb), NULL);

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (buttons, GTK_ALIGN_END);
	gtk_box_append (GTK_BOX (root), buttons);

	add_button = gtk_button_new_with_label (_("Add..."));
	g_signal_connect (add_button, "clicked", G_CALLBACK (ignore_add_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), add_button);

	ignore_view.remove_button = gtk_button_new_with_label (_("Remove"));
	g_signal_connect (ignore_view.remove_button, "clicked",
		G_CALLBACK (ignore_remove_selected_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), ignore_view.remove_button);

	ignore_view.clear_button = gtk_button_new_with_label (_("Clear"));
	g_signal_connect (ignore_view.clear_button, "clicked",
		G_CALLBACK (ignore_clear_all_cb), NULL);
	gtk_box_append (GTK_BOX (buttons), ignore_view.clear_button);

	close_button = gtk_button_new_with_label (_("Close"));
	g_signal_connect_swapped (close_button, "clicked",
		G_CALLBACK (gtk_window_close), ignore_view.window);
	gtk_box_append (GTK_BOX (buttons), close_button);

	g_signal_connect (ignore_view.window, "close-request",
		G_CALLBACK (ignore_close_request_cb), NULL);

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

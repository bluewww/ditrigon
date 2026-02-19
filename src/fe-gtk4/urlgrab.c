/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 URL grabber */

#include "fe-gtk4.h"

#include "../common/url.h"
#include "../common/tree.h"

#define URLGRAB_UI_PATH "/org/ditrigon/ui/gtk4/dialogs/urlgrab-window.ui"

typedef struct
{
	GtkWidget *window;
	GtkWidget *list;
} HcUrlGrabView;

static HcUrlGrabView urlgrab_view;

static gboolean
urlgrab_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;

	urlgrab_view.window = NULL;
	urlgrab_view.list = NULL;
	return FALSE;
}

static void
urlgrab_clear_rows (void)
{
	GtkWidget *child;

	if (!urlgrab_view.list)
		return;

	while ((child = gtk_widget_get_first_child (urlgrab_view.list)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (urlgrab_view.list), child);
}

static void
urlgrab_trim_limit (void)
{
	GtkWidget *child;
	int index;

	if (!urlgrab_view.list || prefs.hex_url_grabber_limit <= 0)
		return;

	index = 0;
	child = gtk_widget_get_first_child (urlgrab_view.list);
	while (child)
	{
		GtkWidget *next = gtk_widget_get_next_sibling (child);

		if (index >= prefs.hex_url_grabber_limit)
			gtk_list_box_remove (GTK_LIST_BOX (urlgrab_view.list), child);
		else
			index++;

		child = next;
	}
}

static void
urlgrab_add_row (const char *url, gboolean prepend)
{
	GtkWidget *row;
	GtkWidget *label;

	if (!urlgrab_view.list || !url)
		return;

	label = gtk_label_new (url);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_label_set_wrap (GTK_LABEL (label), TRUE);
	gtk_widget_set_margin_start (label, 10);
	gtk_widget_set_margin_end (label, 10);
	gtk_widget_set_margin_top (label, 6);
	gtk_widget_set_margin_bottom (label, 6);

	row = gtk_list_box_row_new ();
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
	g_object_set_data_full (G_OBJECT (row), "hc-url", g_strdup (url), g_free);

	if (prepend)
		gtk_list_box_insert (GTK_LIST_BOX (urlgrab_view.list), row, 0);
	else
		gtk_list_box_append (GTK_LIST_BOX (urlgrab_view.list), row);
}

static char *
urlgrab_selected_url_dup (void)
{
	GtkListBoxRow *row;
	const char *url;

	if (!urlgrab_view.list)
		return NULL;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (urlgrab_view.list));
	if (!row)
		return NULL;

	url = g_object_get_data (G_OBJECT (row), "hc-url");
	if (!url || !url[0])
		return NULL;

	return g_strdup (url);
}

static void
urlgrab_row_activated_cb (GtkListBox *box, GtkListBoxRow *row, gpointer userdata)
{
	const char *url;

	(void) box;
	(void) userdata;

	if (!row)
		return;

	url = g_object_get_data (G_OBJECT (row), "hc-url");
	if (!url || !url[0])
		return;

	fe_open_url (url);
}

static void
urlgrab_open_selected_cb (GtkButton *button, gpointer userdata)
{
	char *url;

	(void) button;
	(void) userdata;

	url = urlgrab_selected_url_dup ();
	if (!url)
		return;

	fe_open_url (url);
	g_free (url);
}

static void
urlgrab_copy_selected_cb (GtkButton *button, gpointer userdata)
{
	char *url;
	GdkClipboard *clipboard;
	GdkDisplay *display;

	(void) button;
	(void) userdata;

	url = urlgrab_selected_url_dup ();
	if (!url)
		return;

	display = gdk_display_get_default ();
	if (display)
	{
		clipboard = gdk_display_get_clipboard (display);
		gdk_clipboard_set_text (clipboard, url);
	}

	g_free (url);
}

static void
urlgrab_clear_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	url_clear ();
	urlgrab_clear_rows ();
}

static void
urlgrab_save_cb (void *userdata, char *file)
{
	(void) userdata;

	if (file)
		url_save_tree (file, "w", TRUE);
}

static void
urlgrab_save_clicked_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;

	fe_get_file (_("Select an output filename"), NULL,
		urlgrab_save_cb, NULL, FRF_WRITE);
}

static int
urlgrab_populate_cb (const void *key, void *userdata)
{
	(void) userdata;
	fe_url_add ((const char *) key);
	return TRUE;
}

void
fe_url_add (const char *urltext)
{
	if (!urlgrab_view.window || !urlgrab_view.list || !urltext || !urltext[0])
		return;

	urlgrab_add_row (urltext, TRUE);
	urlgrab_trim_limit ();
}

void
url_opengui (void)
{
	GtkWidget *open_button;
	GtkWidget *copy_button;
	GtkWidget *save_button;
	GtkWidget *clear_button;
	GtkWidget *close_button;
	GtkWidget *hint;
	GtkBuilder *builder;

	if (urlgrab_view.window)
	{
		gtk_window_present (GTK_WINDOW (urlgrab_view.window));
		return;
	}

	builder = fe_gtk4_builder_new_from_resource (URLGRAB_UI_PATH);
	urlgrab_view.window = fe_gtk4_builder_get_widget (builder, "urlgrab_window", GTK_TYPE_WINDOW);
	hint = fe_gtk4_builder_get_widget (builder, "urlgrab_hint", GTK_TYPE_LABEL);
	urlgrab_view.list = fe_gtk4_builder_get_widget (builder, "urlgrab_list", GTK_TYPE_LIST_BOX);
	open_button = fe_gtk4_builder_get_widget (builder, "urlgrab_open_button", GTK_TYPE_BUTTON);
	copy_button = fe_gtk4_builder_get_widget (builder, "urlgrab_copy_button", GTK_TYPE_BUTTON);
	save_button = fe_gtk4_builder_get_widget (builder, "urlgrab_save_button", GTK_TYPE_BUTTON);
	clear_button = fe_gtk4_builder_get_widget (builder, "urlgrab_clear_button", GTK_TYPE_BUTTON);
	close_button = fe_gtk4_builder_get_widget (builder, "urlgrab_close_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (urlgrab_view.window);
	g_object_unref (builder);

	gtk_label_set_text (GTK_LABEL (hint), _("Double-click a URL to open it."));
	gtk_button_set_label (GTK_BUTTON (open_button), _("Open"));
	gtk_button_set_label (GTK_BUTTON (copy_button), _("Copy"));
	gtk_button_set_label (GTK_BUTTON (save_button), _("Save As..."));
	gtk_button_set_label (GTK_BUTTON (clear_button), _("Clear"));
	gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));
	g_signal_connect (urlgrab_view.list, "row-activated",
		G_CALLBACK (urlgrab_row_activated_cb), NULL);
	g_signal_connect (open_button, "clicked", G_CALLBACK (urlgrab_open_selected_cb), NULL);
	g_signal_connect (copy_button, "clicked", G_CALLBACK (urlgrab_copy_selected_cb), NULL);
	g_signal_connect (save_button, "clicked", G_CALLBACK (urlgrab_save_clicked_cb), NULL);
	g_signal_connect (clear_button, "clicked", G_CALLBACK (urlgrab_clear_cb), NULL);
	g_signal_connect_swapped (close_button, "clicked",
		G_CALLBACK (gtk_window_close), urlgrab_view.window);
	g_signal_connect (urlgrab_view.window, "close-request",
		G_CALLBACK (urlgrab_close_request_cb), NULL);

	gtk_window_set_title (GTK_WINDOW (urlgrab_view.window), _("URL Grabber"));
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (urlgrab_view.window), GTK_WINDOW (main_window));

	if (prefs.hex_url_grabber)
		tree_foreach (url_tree, urlgrab_populate_cb, NULL);
	else
		urlgrab_add_row (_("URL Grabber is disabled."), FALSE);

	gtk_window_present (GTK_WINDOW (urlgrab_view.window));
}

void
fe_gtk4_urlgrab_cleanup (void)
{
	if (!urlgrab_view.window)
		return;

	gtk_window_destroy (GTK_WINDOW (urlgrab_view.window));
	urlgrab_view.window = NULL;
	urlgrab_view.list = NULL;
}

/* HexChat GTK4 plugin loading and management */
#include "../common/hexchat.h"
#define PLUGIN_C
typedef struct session hexchat_context;
#include "../common/hexchat-plugin.h"
#include "../common/plugin.h"

#include "fe-gtk4.h"

#define PLUGIN_UI_PATH "/org/hexchat/ui/gtk4/dialogs/plugin-window.ui"

#ifdef USE_PLUGIN
extern GSList *plugin_list;

static GtkWidget *plugin_window;
static GtkWidget *plugin_listbox;

static session *
plugingui_target_session (void)
{
	if (current_sess && is_session (current_sess))
		return current_sess;

	return fe_gtk4_window_target_session ();
}

static gboolean
plugingui_close_request_cb (GtkWindow *window, gpointer userdata)
{
	(void) window;
	(void) userdata;
	plugin_window = NULL;
	plugin_listbox = NULL;
	return FALSE;
}

static void
plugingui_clear_rows (void)
{
	GtkWidget *child;

	if (!plugin_listbox)
		return;

	while ((child = gtk_widget_get_first_child (plugin_listbox)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (plugin_listbox), child);
}

static char *
plugingui_get_selected_value (const char *key)
{
	GtkListBoxRow *row;
	const char *value;

	if (!plugin_listbox)
		return NULL;

	row = gtk_list_box_get_selected_row (GTK_LIST_BOX (plugin_listbox));
	if (!row)
		return NULL;

	value = g_object_get_data (G_OBJECT (row), key);
	if (!value || !value[0])
		return NULL;

	return g_strdup (value);
}

static void
plugingui_add_row (hexchat_plugin *pl)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *title;
	GtkWidget *subtitle;
	char *title_text;
	char *subtitle_text;
	const char *name;
	const char *version;
	const char *file;
	const char *desc;

	if (!plugin_listbox || !pl)
		return;

	name = pl->name ? pl->name : "";
	version = pl->version ? pl->version : "";
	file = pl->filename ? file_part (pl->filename) : "";
	desc = pl->desc ? pl->desc : "";

	title_text = g_strdup_printf ("%s (%s)", name, version);
	subtitle_text = g_strdup_printf ("%s - %s", file, desc);

	title = gtk_label_new (title_text);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0f);
	subtitle = gtk_label_new (subtitle_text);
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
	g_object_set_data_full (G_OBJECT (row), "plugin-name", g_strdup (name), g_free);
	g_object_set_data_full (G_OBJECT (row), "plugin-file", g_strdup (pl->filename ? pl->filename : ""), g_free);
	gtk_list_box_append (GTK_LIST_BOX (plugin_listbox), row);

	g_free (title_text);
	g_free (subtitle_text);
}

static void
plugingui_load_cb (void *userdata, char *file)
{
	session *sess;
	char *cmd;

	sess = userdata;
	if (!file || !file[0])
		return;

	if (!sess || !is_session (sess))
		sess = plugingui_target_session ();
	if (!sess)
		return;

	if (strchr (file, ' '))
		cmd = g_strdup_printf ("LOAD \"%s\"", file);
	else
		cmd = g_strdup_printf ("LOAD %s", file);

	handle_command (sess, cmd, FALSE);
	g_free (cmd);
}

static void
plugingui_load_for_session (session *sess)
{
	char *sub_dir;

	sub_dir = g_build_filename (get_xdir (), "addons", NULL);
	fe_get_file (_("Select a Plugin or Script to load"),
		sub_dir, plugingui_load_cb, sess,
		FRF_FILTERISINITIAL | FRF_EXTENSIONS);
	g_free (sub_dir);
}

static void
plugingui_loadbutton_cb (GtkButton *button, gpointer userdata)
{
	(void) button;
	(void) userdata;
	plugingui_load ();
}

static void
plugingui_unload (GtkButton *button, gpointer userdata)
{
	session *sess;
	char *modname;
	char *file;
	char *cmd;

	(void) button;
	(void) userdata;

	modname = plugingui_get_selected_value ("plugin-name");
	file = plugingui_get_selected_value ("plugin-file");
	if (!modname || !file)
		goto done;

	if (g_str_has_suffix (file, "." PLUGIN_SUFFIX))
	{
		if (plugin_kill (modname, FALSE) == 2)
			fe_message (_("That plugin is refusing to unload.\n"), FE_MSG_ERROR);
	}
	else
	{
		sess = plugingui_target_session ();
		if (sess)
		{
			if (strchr (file, ' '))
				cmd = g_strdup_printf ("UNLOAD \"%s\"", file);
			else
				cmd = g_strdup_printf ("UNLOAD %s", file);
			handle_command (sess, cmd, FALSE);
			g_free (cmd);
		}
	}

done:
	g_free (modname);
	g_free (file);
}

static void
plugingui_reloadbutton_cb (GtkButton *button, gpointer userdata)
{
	session *sess;
	char *file;
	char *cmd;

	(void) button;
	(void) userdata;

	file = plugingui_get_selected_value ("plugin-file");
	if (!file)
		return;

	sess = plugingui_target_session ();
	if (sess)
	{
		if (strchr (file, ' '))
			cmd = g_strdup_printf ("RELOAD \"%s\"", file);
		else
			cmd = g_strdup_printf ("RELOAD %s", file);
		handle_command (sess, cmd, FALSE);
		g_free (cmd);
	}

	g_free (file);
}

#endif

void
plugingui_load (void)
{
#ifdef USE_PLUGIN
	plugingui_load_for_session (current_sess);
#else
	fe_message ((char *) _(PACKAGE_NAME " was built without plugin support."), FE_MSG_INFO);
#endif
}

void
fe_gtk4_plugingui_load_request (session *sess)
{
#ifdef USE_PLUGIN
	plugingui_load_for_session (sess);
#else
	(void) sess;
	fe_message ((char *) _(PACKAGE_NAME " was built without plugin support."), FE_MSG_INFO);
#endif
}

void
plugingui_open (void)
{
#ifdef USE_PLUGIN
	GtkWidget *load_button;
	GtkWidget *unload_button;
	GtkWidget *reload_button;
	GtkWidget *close_button;
	GtkBuilder *builder;

	if (plugin_window)
	{
		fe_pluginlist_update ();
		gtk_window_present (GTK_WINDOW (plugin_window));
		return;
	}

	builder = fe_gtk4_builder_new_from_resource (PLUGIN_UI_PATH);
	plugin_window = fe_gtk4_builder_get_widget (builder, "plugin_window", GTK_TYPE_WINDOW);
	plugin_listbox = fe_gtk4_builder_get_widget (builder, "plugin_list", GTK_TYPE_LIST_BOX);
	load_button = fe_gtk4_builder_get_widget (builder, "plugin_load_button", GTK_TYPE_BUTTON);
	unload_button = fe_gtk4_builder_get_widget (builder, "plugin_unload_button", GTK_TYPE_BUTTON);
	reload_button = fe_gtk4_builder_get_widget (builder, "plugin_reload_button", GTK_TYPE_BUTTON);
	close_button = fe_gtk4_builder_get_widget (builder, "plugin_close_button", GTK_TYPE_BUTTON);
	g_object_ref_sink (plugin_window);
	g_object_unref (builder);

	gtk_button_set_label (GTK_BUTTON (load_button), _("Load..."));
	gtk_button_set_label (GTK_BUTTON (unload_button), _("Unload"));
	gtk_button_set_label (GTK_BUTTON (reload_button), _("Reload"));
	gtk_button_set_label (GTK_BUTTON (close_button), _("Close"));
	g_signal_connect (load_button, "clicked", G_CALLBACK (plugingui_loadbutton_cb), NULL);
	g_signal_connect (unload_button, "clicked", G_CALLBACK (plugingui_unload), NULL);
	g_signal_connect (reload_button, "clicked", G_CALLBACK (plugingui_reloadbutton_cb), NULL);
	g_signal_connect_swapped (close_button, "clicked",
		G_CALLBACK (gtk_window_close), plugin_window);
	g_signal_connect (plugin_window, "close-request",
		G_CALLBACK (plugingui_close_request_cb), NULL);

	gtk_window_set_title (GTK_WINDOW (plugin_window), _("Plugins and Scripts"));
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (plugin_window), GTK_WINDOW (main_window));

	fe_pluginlist_update ();
	gtk_window_present (GTK_WINDOW (plugin_window));
#else
	fe_message ((char *) _(PACKAGE_NAME " was built without plugin support."), FE_MSG_INFO);
#endif
}

void
fe_pluginlist_update (void)
{
#ifdef USE_PLUGIN
	GSList *list;

	if (!plugin_window || !plugin_listbox)
		return;

	plugingui_clear_rows ();

	for (list = plugin_list; list; list = list->next)
	{
		hexchat_plugin *pl = list->data;

		if (!pl || !pl->version || pl->version[0] == 0)
			continue;

		plugingui_add_row (pl);
	}
#endif
}

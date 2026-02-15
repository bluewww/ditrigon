/* HexChat GTK4 dialog and utility wrappers */
#include "fe-gtk4.h"

GtkBuilder *
fe_gtk4_builder_new_from_resource (const char *resource_path)
{
	GtkBuilder *builder;
	GError *error;

	g_return_val_if_fail (resource_path && resource_path[0], NULL);

	error = NULL;
	builder = gtk_builder_new ();
	if (!gtk_builder_add_from_resource (builder, resource_path, &error))
	{
		g_error ("Failed to load required UI resource %s: %s",
			resource_path, error ? error->message : "unknown error");
		g_object_unref (builder);
		g_clear_error (&error);
		return NULL; /* not reached after g_error */
	}

	return builder;
}

GtkWidget *
fe_gtk4_builder_get_widget (GtkBuilder *builder, const char *id, GType expected_type)
{
	GObject *obj;

	g_return_val_if_fail (builder != NULL, NULL);
	g_return_val_if_fail (id && id[0], NULL);

	obj = gtk_builder_get_object (builder, id);
	if (!obj)
	{
		g_error ("Required UI object '%s' not found", id);
		return NULL; /* not reached */
	}

	if (expected_type != 0 && !g_type_is_a (G_OBJECT_TYPE (obj), expected_type))
	{
		g_error ("UI object '%s' has type '%s', expected '%s'",
			id, G_OBJECT_TYPE_NAME (obj), g_type_name (expected_type));
		return NULL; /* not reached */
	}

	return GTK_WIDGET (obj);
}

typedef struct
{
	void (*callback) (int value, void *userdata);
	void *userdata;
} HcBoolDialog;

typedef struct
{
	void (*callback) (int cancel, int value, void *userdata);
	void *userdata;
	GtkWidget *window;
	GtkWidget *spin;
	gboolean finished;
} HcIntDialog;

typedef struct
{
	void (*callback) (int cancel, char *text, void *userdata);
	void *userdata;
	GtkWidget *window;
	GtkWidget *entry;
	gboolean finished;
} HcStrDialog;

typedef struct
{
	void (*yesproc) (void *);
	void (*noproc) (void *);
	void *userdata;
} HcConfirmDialog;

typedef struct
{
	void (*callback) (void *userdata, char *file);
	void *userdata;
	int flags;
	int mode;
	GtkFileDialog *dialog;
} HcFileDialog;

enum
{
	HC_FILE_MODE_OPEN = 0,
	HC_FILE_MODE_OPEN_MULTIPLE,
	HC_FILE_MODE_SAVE,
	HC_FILE_MODE_SELECT_FOLDER,
	HC_FILE_MODE_SELECT_MULTIPLE_FOLDERS
};

static void
bool_dialog_choose_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	HcBoolDialog *ctx;
	GtkAlertDialog *dialog;
	GError *error;
	int selected;

	ctx = data;
	dialog = GTK_ALERT_DIALOG (source_object);
	error = NULL;
	selected = gtk_alert_dialog_choose_finish (dialog, result, &error);

	if (ctx->callback)
		ctx->callback (error == NULL && selected == 1 ? 1 : 0, ctx->userdata);

	if (error)
		g_error_free (error);
	g_object_unref (dialog);
	g_free (ctx);
}

static void
str_dialog_finish (HcStrDialog *ctx, int cancel)
{
	char *text;

	if (!ctx || ctx->finished)
		return;
	ctx->finished = TRUE;

	text = (char *) gtk_editable_get_text (GTK_EDITABLE (ctx->entry));
	if (ctx->callback)
		ctx->callback (cancel ? 1 : 0, text, ctx->userdata);
	gtk_window_destroy (GTK_WINDOW (ctx->window));
	g_free (ctx);
}

static void
str_dialog_ok_cb (GtkButton *button, gpointer data)
{
	(void) button;
	str_dialog_finish ((HcStrDialog *) data, 0);
}

static void
str_dialog_cancel_cb (GtkButton *button, gpointer data)
{
	(void) button;
	str_dialog_finish ((HcStrDialog *) data, 1);
}

static gboolean
str_dialog_close_request_cb (GtkWindow *window, gpointer data)
{
	(void) window;
	str_dialog_finish ((HcStrDialog *) data, 1);
	return TRUE;
}

static void
str_dialog_activate_cb (GtkEntry *entry, gpointer data)
{
	(void) entry;
	str_dialog_finish ((HcStrDialog *) data, 0);
}

static void
int_dialog_finish (HcIntDialog *ctx, int cancel)
{
	int value;

	if (!ctx || ctx->finished)
		return;
	ctx->finished = TRUE;

	value = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ctx->spin));
	if (ctx->callback)
		ctx->callback (cancel ? 1 : 0, value, ctx->userdata);
	gtk_window_destroy (GTK_WINDOW (ctx->window));
	g_free (ctx);
}

static void
int_dialog_ok_cb (GtkButton *button, gpointer data)
{
	(void) button;
	int_dialog_finish ((HcIntDialog *) data, 0);
}

static void
int_dialog_cancel_cb (GtkButton *button, gpointer data)
{
	(void) button;
	int_dialog_finish ((HcIntDialog *) data, 1);
}

static gboolean
int_dialog_close_request_cb (GtkWindow *window, gpointer data)
{
	(void) window;
	int_dialog_finish ((HcIntDialog *) data, 1);
	return TRUE;
}

static void
confirm_dialog_choose_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	HcConfirmDialog *ctx;
	GtkAlertDialog *dialog;
	GError *error;
	int selected;

	ctx = data;
	dialog = GTK_ALERT_DIALOG (source_object);
	error = NULL;
	selected = gtk_alert_dialog_choose_finish (dialog, result, &error);
	if (!error && selected == 1)
	{
		if (ctx->yesproc)
			ctx->yesproc (ctx->userdata);
	}
	else if (ctx->noproc)
	{
		ctx->noproc (ctx->userdata);
	}
	if (error)
		g_error_free (error);
	g_object_unref (dialog);
	g_free (ctx);
}

static void
file_dialog_emit_file (HcFileDialog *ctx, GFile *file)
{
	char *path;
	char *utf8;

	if (!ctx || !file)
		return;

	path = g_file_get_path (file);
	if (!path)
		return;

	utf8 = g_filename_to_utf8 (path, -1, NULL, NULL, NULL);
	if (ctx->callback)
		ctx->callback (ctx->userdata, utf8 ? utf8 : path);
	g_free (utf8);
	g_free (path);
}

static void
file_dialog_emit_model (HcFileDialog *ctx, GListModel *files)
{
	guint i;
	guint n;
	GFile *file;

	if (!ctx || !files)
		return;

	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	n = g_list_model_get_n_items (files);
	for (i = 0; i < n; i++)
	{
		file = g_list_model_get_item (files, i);
		if (!file)
			continue;
		file_dialog_emit_file (ctx, file);
		g_object_unref (file);
	}
	G_GNUC_END_IGNORE_DEPRECATIONS
}

static void
file_dialog_finish_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	HcFileDialog *ctx;
	GtkFileDialog *dialog;
	GError *error;

	ctx = data;
	dialog = GTK_FILE_DIALOG (source_object);
	error = NULL;

	switch (ctx->mode)
	{
	case HC_FILE_MODE_OPEN:
	case HC_FILE_MODE_SAVE:
	case HC_FILE_MODE_SELECT_FOLDER:
		{
			GFile *file = NULL;

			if (ctx->mode == HC_FILE_MODE_OPEN)
				file = gtk_file_dialog_open_finish (dialog, result, &error);
			else if (ctx->mode == HC_FILE_MODE_SAVE)
				file = gtk_file_dialog_save_finish (dialog, result, &error);
			else
				file = gtk_file_dialog_select_folder_finish (dialog, result, &error);

			if (file)
			{
				file_dialog_emit_file (ctx, file);
				g_object_unref (file);
			}
		}
		break;
	case HC_FILE_MODE_OPEN_MULTIPLE:
	case HC_FILE_MODE_SELECT_MULTIPLE_FOLDERS:
		{
			GListModel *files = NULL;

			if (ctx->mode == HC_FILE_MODE_OPEN_MULTIPLE)
				files = gtk_file_dialog_open_multiple_finish (dialog, result, &error);
			else
				files = gtk_file_dialog_select_multiple_folders_finish (dialog, result, &error);

			if (files)
			{
				file_dialog_emit_model (ctx, files);
				g_object_unref (files);
			}
		}
		break;
	default:
		break;
	}

	if (error && !(error->domain == GTK_DIALOG_ERROR &&
		(error->code == GTK_DIALOG_ERROR_DISMISSED || error->code == GTK_DIALOG_ERROR_CANCELLED)))
	{
		g_warning ("File dialog failed: %s", error->message);
	}
	if (error)
		g_error_free (error);

	if (ctx->callback)
		ctx->callback (ctx->userdata, NULL);

	g_object_unref (ctx->dialog);
	g_free (ctx);
}

void
fe_get_bool (char *title, char *prompt, void *callback, void *userdata)
{
	static const char *buttons[] = { "No", "Yes", NULL };
	GtkAlertDialog *dialog;
	GtkWindow *parent;
	HcBoolDialog *ctx;

	ctx = g_new0 (HcBoolDialog, 1);
	ctx->callback = (void (*) (int, void *)) callback;
	ctx->userdata = userdata;

	parent = main_window ? GTK_WINDOW (main_window) : NULL;
	dialog = gtk_alert_dialog_new ("%s", title ? title : _("Confirm"));
	gtk_alert_dialog_set_detail (dialog, prompt ? prompt : "");
	gtk_alert_dialog_set_buttons (dialog, buttons);
	gtk_alert_dialog_set_cancel_button (dialog, 0);
	gtk_alert_dialog_set_default_button (dialog, 1);
	gtk_alert_dialog_set_modal (dialog, parent != NULL);
	gtk_alert_dialog_choose (dialog, parent, NULL, bool_dialog_choose_cb, ctx);
}

void
fe_get_str (char *prompt, char *def, void *callback, void *ud)
{
	GtkWidget *window;
	GtkWidget *root;
	GtkWidget *label;
	GtkWidget *buttons;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	HcStrDialog *ctx;

	ctx = g_new0 (HcStrDialog, 1);
	ctx->callback = (void (*) (int, char *, void *)) callback;
	ctx->userdata = ud;

	window = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (window), _("Input"));
	gtk_window_set_default_size (GTK_WINDOW (window), 420, 140);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (main_window));
	gtk_window_set_modal (GTK_WINDOW (window), TRUE);

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (root, 12);
	gtk_widget_set_margin_end (root, 12);
	gtk_widget_set_margin_top (root, 12);
	gtk_widget_set_margin_bottom (root, 12);
	gtk_window_set_child (GTK_WINDOW (window), root);

	label = gtk_label_new (prompt ? prompt : _("Enter value:"));
	gtk_label_set_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_box_append (GTK_BOX (root), label);

	ctx->entry = gtk_entry_new ();
	gtk_editable_set_text (GTK_EDITABLE (ctx->entry), def ? def : "");
	gtk_widget_set_hexpand (ctx->entry, TRUE);
	gtk_box_append (GTK_BOX (root), ctx->entry);

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (buttons, GTK_ALIGN_END);
	cancel_button = gtk_button_new_with_label (_("Cancel"));
	ok_button = gtk_button_new_with_label (_("OK"));
	gtk_box_append (GTK_BOX (buttons), cancel_button);
	gtk_box_append (GTK_BOX (buttons), ok_button);
	gtk_box_append (GTK_BOX (root), buttons);

	ctx->window = window;
	ctx->finished = FALSE;

	g_signal_connect (ok_button, "clicked", G_CALLBACK (str_dialog_ok_cb), ctx);
	g_signal_connect (cancel_button, "clicked", G_CALLBACK (str_dialog_cancel_cb), ctx);
	g_signal_connect (ctx->entry, "activate", G_CALLBACK (str_dialog_activate_cb), ctx);
	g_signal_connect (window, "close-request", G_CALLBACK (str_dialog_close_request_cb), ctx);

	gtk_window_set_default_widget (GTK_WINDOW (window), ok_button);
	gtk_window_present (GTK_WINDOW (window));
}

void
fe_get_int (char *prompt, int def, void *callback, void *ud)
{
	GtkWidget *window;
	GtkWidget *root;
	GtkWidget *label;
	GtkWidget *buttons;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	HcIntDialog *ctx;

	ctx = g_new0 (HcIntDialog, 1);
	ctx->callback = (void (*) (int, int, void *)) callback;
	ctx->userdata = ud;

	window = gtk_window_new ();
	gtk_window_set_title (GTK_WINDOW (window), _("Input"));
	gtk_window_set_default_size (GTK_WINDOW (window), 420, 140);
	if (main_window)
		gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (main_window));
	gtk_window_set_modal (GTK_WINDOW (window), TRUE);

	root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start (root, 12);
	gtk_widget_set_margin_end (root, 12);
	gtk_widget_set_margin_top (root, 12);
	gtk_widget_set_margin_bottom (root, 12);
	gtk_window_set_child (GTK_WINDOW (window), root);

	label = gtk_label_new (prompt ? prompt : _("Enter value:"));
	gtk_label_set_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
	gtk_box_append (GTK_BOX (root), label);

	ctx->spin = gtk_spin_button_new_with_range (0, 1024, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (ctx->spin), def);
	gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (ctx->spin), TRUE);
	gtk_spin_button_set_activates_default (GTK_SPIN_BUTTON (ctx->spin), TRUE);
	gtk_box_append (GTK_BOX (root), ctx->spin);

	buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign (buttons, GTK_ALIGN_END);
	cancel_button = gtk_button_new_with_label (_("Cancel"));
	ok_button = gtk_button_new_with_label (_("OK"));
	gtk_box_append (GTK_BOX (buttons), cancel_button);
	gtk_box_append (GTK_BOX (buttons), ok_button);
	gtk_box_append (GTK_BOX (root), buttons);

	ctx->window = window;
	ctx->finished = FALSE;

	g_signal_connect (ok_button, "clicked", G_CALLBACK (int_dialog_ok_cb), ctx);
	g_signal_connect (cancel_button, "clicked", G_CALLBACK (int_dialog_cancel_cb), ctx);
	g_signal_connect (window, "close-request", G_CALLBACK (int_dialog_close_request_cb), ctx);

	gtk_window_set_default_widget (GTK_WINDOW (window), ok_button);
	gtk_window_present (GTK_WINDOW (window));
}

void
fe_confirm (const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud)
{
	static const char *buttons[] = { "No", "Yes", NULL };
	GtkAlertDialog *dialog;
	GtkWindow *parent;
	HcConfirmDialog *ctx;

	ctx = g_new0 (HcConfirmDialog, 1);
	ctx->yesproc = yesproc;
	ctx->noproc = noproc;
	ctx->userdata = ud;

	parent = main_window ? GTK_WINDOW (main_window) : NULL;
	dialog = gtk_alert_dialog_new ("%s", _("Confirm"));
	gtk_alert_dialog_set_detail (dialog, message ? message : "");
	gtk_alert_dialog_set_buttons (dialog, buttons);
	gtk_alert_dialog_set_cancel_button (dialog, 0);
	gtk_alert_dialog_set_default_button (dialog, 1);
	gtk_alert_dialog_set_modal (dialog, parent != NULL);
	gtk_alert_dialog_choose (dialog, parent, NULL, confirm_dialog_choose_cb, ctx);
}

void
fe_get_file (const char *title, char *initial,
				 void (*callback) (void *userdata, char *file), void *userdata,
				 int flags)
{
	GtkWindow *parent;
	GFile *initial_file;
	GFile *initial_folder;
	char *base_name;
	HcFileDialog *ctx;

	parent = main_window ? GTK_WINDOW (main_window) : NULL;

	ctx = g_new0 (HcFileDialog, 1);
	ctx->callback = (void (*) (void *, char *)) callback;
	ctx->userdata = userdata;
	ctx->flags = flags;
	ctx->dialog = gtk_file_dialog_new ();

	if ((flags & FRF_CHOOSEFOLDER) && (flags & FRF_MULTIPLE))
		ctx->mode = HC_FILE_MODE_SELECT_MULTIPLE_FOLDERS;
	else if (flags & FRF_CHOOSEFOLDER)
		ctx->mode = HC_FILE_MODE_SELECT_FOLDER;
	else if (flags & FRF_WRITE)
		ctx->mode = HC_FILE_MODE_SAVE;
	else if (flags & FRF_MULTIPLE)
		ctx->mode = HC_FILE_MODE_OPEN_MULTIPLE;
	else
		ctx->mode = HC_FILE_MODE_OPEN;

	gtk_file_dialog_set_title (ctx->dialog, title ? title : _("Select File"));
	gtk_file_dialog_set_accept_label (ctx->dialog, (flags & FRF_WRITE) ? _("Save") : _("Open"));
	gtk_file_dialog_set_modal (ctx->dialog, (flags & FRF_MODAL) || parent != NULL);

	if (initial && initial[0])
	{
		initial_file = g_file_new_for_path (initial);
		initial_folder = NULL;
		base_name = NULL;

		if ((flags & FRF_FILTERISINITIAL) || (flags & FRF_CHOOSEFOLDER))
		{
			gtk_file_dialog_set_initial_folder (ctx->dialog, initial_file);
		}
		else if (flags & FRF_WRITE)
		{
			initial_folder = g_file_get_parent (initial_file);
			if (initial_folder)
			{
				gtk_file_dialog_set_initial_folder (ctx->dialog, initial_folder);
				g_object_unref (initial_folder);
			}

			base_name = g_file_get_basename (initial_file);
			if (base_name)
			{
				gtk_file_dialog_set_initial_name (ctx->dialog, base_name);
				g_free (base_name);
			}
		}
		else
		{
			gtk_file_dialog_set_initial_file (ctx->dialog, initial_file);
		}

		g_object_unref (initial_file);
	}

	switch (ctx->mode)
	{
	case HC_FILE_MODE_OPEN:
		gtk_file_dialog_open (ctx->dialog, parent, NULL, file_dialog_finish_cb, ctx);
		break;
	case HC_FILE_MODE_OPEN_MULTIPLE:
		gtk_file_dialog_open_multiple (ctx->dialog, parent, NULL, file_dialog_finish_cb, ctx);
		break;
	case HC_FILE_MODE_SAVE:
		gtk_file_dialog_save (ctx->dialog, parent, NULL, file_dialog_finish_cb, ctx);
		break;
	case HC_FILE_MODE_SELECT_FOLDER:
		gtk_file_dialog_select_folder (ctx->dialog, parent, NULL, file_dialog_finish_cb, ctx);
		break;
	case HC_FILE_MODE_SELECT_MULTIPLE_FOLDERS:
		gtk_file_dialog_select_multiple_folders (ctx->dialog, parent, NULL, file_dialog_finish_cb, ctx);
		break;
	default:
		break;
	}
}

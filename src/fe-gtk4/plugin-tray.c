/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 tray integration (SNI/DBusMenu) */

#include "fe-gtk4.h"

#define SNI_ITEM_INTERFACE "org.kde.StatusNotifierItem"
#define SNI_ITEM_PATH "/StatusNotifierItem"
#define SNI_MENU_INTERFACE "org.freedesktop.DBusMenu"
#define SNI_MENU_PATH "/StatusNotifierItem/Menu"
#define SNI_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH "/StatusNotifierWatcher"

#define TRAY_BLINK_DEFAULT_INTERVAL 500

enum
{
	TRAY_MENU_ID_ROOT = 0,
	TRAY_MENU_ID_TOGGLE_WINDOW = 1,
	TRAY_MENU_ID_CLEAR_LOG = 2,
	TRAY_MENU_ID_QUIT = 3
};

static char *tray_tooltip;
static char *tray_icon_name_override;
static feicon tray_icon_state;
static gboolean tray_warned;
static gboolean tray_file_warned;
static GDBusConnection *tray_sni_connection;
static GDBusNodeInfo *tray_sni_node_info;
static guint tray_sni_object_id;
static guint tray_sni_owner_id;
static guint tray_sni_watcher_id;
static char *tray_sni_bus_name;
static gboolean tray_sni_registered;
static GDBusNodeInfo *tray_menu_node_info;
static guint tray_menu_object_id;
static guint tray_menu_revision;
static guint tray_blink_tag;
static gboolean tray_blink_on;
static gboolean tray_blink_custom;
static feicon tray_blink_icon;
static guint tray_blink_interval;
static char *tray_blink_name1;
static char *tray_blink_name2;

static const char tray_sni_introspection_xml[] =
	"<node>"
	" <interface name='org.kde.StatusNotifierItem'>"
	"  <method name='ContextMenu'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/>"
	"  </method>"
	"  <method name='Activate'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/>"
	"  </method>"
	"  <method name='SecondaryActivate'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/>"
	"  </method>"
	"  <method name='Scroll'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='s' direction='in'/>"
	"  </method>"
	"  <signal name='NewTitle'/>"
	"  <signal name='NewIcon'/>"
	"  <signal name='NewAttentionIcon'/>"
	"  <signal name='NewOverlayIcon'/>"
	"  <signal name='NewToolTip'/>"
	"  <signal name='NewStatus'>"
	"   <arg type='s'/>"
	"  </signal>"
	"  <property name='Category' type='s' access='read'/>"
	"  <property name='Id' type='s' access='read'/>"
	"  <property name='Title' type='s' access='read'/>"
	"  <property name='Status' type='s' access='read'/>"
	"  <property name='WindowId' type='u' access='read'/>"
	"  <property name='IconName' type='s' access='read'/>"
	"  <property name='IconPixmap' type='a(iiay)' access='read'/>"
	"  <property name='OverlayIconName' type='s' access='read'/>"
	"  <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
	"  <property name='AttentionIconName' type='s' access='read'/>"
	"  <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
	"  <property name='AttentionMovieName' type='s' access='read'/>"
	"  <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
	"  <property name='ItemIsMenu' type='b' access='read'/>"
	"  <property name='Menu' type='o' access='read'/>"
	" </interface>"
	"</node>";

static const char tray_menu_introspection_xml[] =
	"<node>"
	" <interface name='org.freedesktop.DBusMenu'>"
	"  <method name='GetLayout'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='as' direction='in'/>"
	"   <arg type='u' direction='out'/>"
	"   <arg type='(ia{sv}av)' direction='out'/>"
	"  </method>"
	"  <method name='GetGroupProperties'>"
	"   <arg type='ai' direction='in'/>"
	"   <arg type='as' direction='in'/>"
	"   <arg type='a(ia{sv})' direction='out'/>"
	"  </method>"
	"  <method name='GetProperty'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='s' direction='in'/>"
	"   <arg type='v' direction='out'/>"
	"  </method>"
	"  <method name='Event'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='s' direction='in'/>"
	"   <arg type='v' direction='in'/>"
	"   <arg type='u' direction='in'/>"
	"  </method>"
	"  <method name='EventGroup'>"
	"   <arg type='a(isvu)' direction='in'/>"
	"   <arg type='ai' direction='out'/>"
	"  </method>"
	"  <method name='AboutToShow'>"
	"   <arg type='i' direction='in'/>"
	"   <arg type='b' direction='out'/>"
	"  </method>"
	"  <method name='AboutToShowGroup'>"
	"   <arg type='ai' direction='in'/>"
	"   <arg type='a(ib)' direction='out'/>"
	"   <arg type='ai' direction='out'/>"
	"  </method>"
	"  <signal name='ItemsPropertiesUpdated'>"
	"   <arg type='a(ia{sv})'/>"
	"   <arg type='a(ias)'/>"
	"  </signal>"
	"  <signal name='LayoutUpdated'>"
	"   <arg type='u'/>"
	"   <arg type='i'/>"
	"  </signal>"
	"  <signal name='ItemActivationRequested'>"
	"   <arg type='i'/>"
	"   <arg type='u'/>"
	"  </signal>"
	" </interface>"
	"</node>";

static const char *
tray_icon_name_from_state (feicon icon)
{
	switch (icon)
	{
	case FE_ICON_MESSAGE:
		return "mail-message-new";
	case FE_ICON_HIGHLIGHT:
	case FE_ICON_PRIVMSG:
	case FE_ICON_FILEOFFER:
		return "dialog-warning";
	case FE_ICON_NORMAL:
	default:
		return "io.github.Ditrigon";
	}
}

static const char *
tray_sni_icon_name (void)
{
	if (tray_blink_tag)
	{
		if (tray_blink_custom)
		{
			if (tray_blink_on)
				return (tray_blink_name1 && tray_blink_name1[0]) ? tray_blink_name1 : "dialog-warning";
			if (tray_blink_name2 && tray_blink_name2[0])
				return tray_blink_name2;
			return "io.github.Ditrigon";
		}

		if (tray_blink_on)
			return tray_icon_name_from_state (tray_blink_icon);
		return "io.github.Ditrigon";
	}

	if (tray_icon_name_override && tray_icon_name_override[0])
		return tray_icon_name_override;

	return tray_icon_name_from_state (tray_icon_state);
}

static const char *
tray_sni_status (void)
{
	if (tray_blink_tag)
		return "NeedsAttention";
	if (tray_icon_state != FE_ICON_NORMAL)
		return "NeedsAttention";
	return "Active";
}

static const char *
tray_menu_toggle_label (void)
{
	if (main_window && gtk_widget_get_visible (main_window))
		return _("Hide Window");
	return _("Show Window");
}

static gboolean
tray_window_is_active (void)
{
	return main_window && gtk_widget_get_visible (main_window) &&
		gtk_window_is_active (GTK_WINDOW (main_window));
}

static void
tray_toggle_main_window_visibility (void)
{
	if (!main_window)
		return;

	if (gtk_widget_get_visible (main_window))
		gtk_widget_set_visible (main_window, FALSE);
	else
	{
		gtk_widget_set_visible (main_window, TRUE);
		gtk_window_present (GTK_WINDOW (main_window));
	}
}

static void
tray_apply_icon_to_window (void)
{
	if (main_window)
		gtk_window_set_icon_name (GTK_WINDOW (main_window), tray_sni_icon_name ());
}

static GVariant *
tray_sni_empty_pixmaps (void)
{
	GVariantBuilder builder;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(iiay)"));
	return g_variant_builder_end (&builder);
}

static gboolean
tray_sni_is_ready (void)
{
	return tray_sni_connection != NULL && tray_sni_object_id != 0;
}

static void
tray_sni_emit_signal (const char *name, GVariant *parameters)
{
	if (!tray_sni_is_ready ())
		return;

	g_dbus_connection_emit_signal (tray_sni_connection, NULL,
		SNI_ITEM_PATH, SNI_ITEM_INTERFACE, name, parameters, NULL);
}

static void
tray_sni_emit_icon_changed (void)
{
	tray_sni_emit_signal ("NewIcon", NULL);
	tray_sni_emit_signal ("NewAttentionIcon", NULL);
	tray_sni_emit_signal ("NewStatus", g_variant_new ("(s)", tray_sni_status ()));
}

static void
tray_sni_emit_tooltip_changed (void)
{
	tray_sni_emit_signal ("NewToolTip", NULL);
	tray_sni_emit_signal ("NewTitle", NULL);
}

void
fe_gtk4_tray_menu_emit_layout_updated (gint parent_id)
{
	if (!tray_sni_connection || !tray_menu_object_id)
		return;

	tray_menu_revision++;
	g_dbus_connection_emit_signal (tray_sni_connection, NULL,
		SNI_MENU_PATH, SNI_MENU_INTERFACE, "LayoutUpdated",
		g_variant_new ("(ui)", tray_menu_revision, parent_id), NULL);
}

static gboolean
tray_menu_is_valid_id (gint id)
{
	return id == TRAY_MENU_ID_ROOT ||
		id == TRAY_MENU_ID_TOGGLE_WINDOW ||
		id == TRAY_MENU_ID_CLEAR_LOG ||
		id == TRAY_MENU_ID_QUIT;
}

static GVariant *
tray_menu_property_value (gint id, const char *name)
{
	if (!name || !tray_menu_is_valid_id (id))
		return NULL;

	if (id == TRAY_MENU_ID_ROOT)
	{
		if (g_strcmp0 (name, "children-display") == 0)
			return g_variant_new_string ("submenu");
		if (g_strcmp0 (name, "visible") == 0)
			return g_variant_new_boolean (TRUE);
		return NULL;
	}

	if (g_strcmp0 (name, "type") == 0)
		return g_variant_new_string ("standard");
	if (g_strcmp0 (name, "enabled") == 0)
		return g_variant_new_boolean (TRUE);
	if (g_strcmp0 (name, "visible") == 0)
		return g_variant_new_boolean (TRUE);
	if (g_strcmp0 (name, "label") == 0)
	{
		if (id == TRAY_MENU_ID_TOGGLE_WINDOW)
			return g_variant_new_string (tray_menu_toggle_label ());
		if (id == TRAY_MENU_ID_CLEAR_LOG)
			return g_variant_new_string (_("Clear Log"));
		if (id == TRAY_MENU_ID_QUIT)
			return g_variant_new_string (_("Quit"));
	}

	return NULL;
}

static void
tray_menu_fill_properties (gint id, GVariantBuilder *props)
{
	GVariant *value;

	if (id == TRAY_MENU_ID_ROOT)
	{
		value = tray_menu_property_value (id, "children-display");
		if (value)
			g_variant_builder_add (props, "{sv}", "children-display", value);
		value = tray_menu_property_value (id, "visible");
		if (value)
			g_variant_builder_add (props, "{sv}", "visible", value);
		return;
	}

	value = tray_menu_property_value (id, "type");
	if (value)
		g_variant_builder_add (props, "{sv}", "type", value);
	value = tray_menu_property_value (id, "label");
	if (value)
		g_variant_builder_add (props, "{sv}", "label", value);
	value = tray_menu_property_value (id, "enabled");
	if (value)
		g_variant_builder_add (props, "{sv}", "enabled", value);
	value = tray_menu_property_value (id, "visible");
	if (value)
		g_variant_builder_add (props, "{sv}", "visible", value);
}

static GVariant *
tray_menu_build_layout (gint id, gint recursion_depth)
{
	GVariantBuilder props;
	GVariantBuilder children;
	gint next_depth;

	g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_init (&children, G_VARIANT_TYPE ("av"));

	tray_menu_fill_properties (id, &props);

	if (id == TRAY_MENU_ID_ROOT && recursion_depth != 0)
	{
		next_depth = recursion_depth;
		if (next_depth > 0)
			next_depth--;

		g_variant_builder_add (&children, "v",
			tray_menu_build_layout (TRAY_MENU_ID_TOGGLE_WINDOW, next_depth));
		g_variant_builder_add (&children, "v",
			tray_menu_build_layout (TRAY_MENU_ID_CLEAR_LOG, next_depth));
		g_variant_builder_add (&children, "v",
			tray_menu_build_layout (TRAY_MENU_ID_QUIT, next_depth));
	}

	return g_variant_new ("(ia{sv}av)", id, &props, &children);
}

static void
tray_menu_activate (gint id, guint timestamp)
{
	if (!tray_menu_is_valid_id (id) || id == TRAY_MENU_ID_ROOT)
		return;

	if (tray_sni_connection && tray_menu_object_id)
	{
		g_dbus_connection_emit_signal (tray_sni_connection, NULL,
			SNI_MENU_PATH, SNI_MENU_INTERFACE, "ItemActivationRequested",
			g_variant_new ("(iu)", id, timestamp), NULL);
	}

	if (id == TRAY_MENU_ID_TOGGLE_WINDOW)
	{
		tray_toggle_main_window_visibility ();
		fe_gtk4_tray_menu_emit_layout_updated (TRAY_MENU_ID_ROOT);
		return;
	}

	if (id == TRAY_MENU_ID_CLEAR_LOG)
	{
		fe_text_clear (current_tab, 0);
		return;
	}

	if (id == TRAY_MENU_ID_QUIT)
		hexchat_exit ();
}

static void
tray_menu_method_call_cb (GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *method_name,
	GVariant *parameters,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	(void) connection;
	(void) sender;
	(void) object_path;
	(void) interface_name;
	(void) user_data;

	if (g_strcmp0 (method_name, "GetLayout") == 0)
	{
		gint parent_id;
		gint depth;
		GVariant *property_names;
		GVariant *layout;

		g_variant_get (parameters, "(ii@as)", &parent_id, &depth, &property_names);
		g_variant_unref (property_names);

		if (!tray_menu_is_valid_id (parent_id))
		{
			g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
				G_DBUS_ERROR_INVALID_ARGS, "Unknown menu id: %d", parent_id);
			return;
		}

		layout = tray_menu_build_layout (parent_id, depth);
		g_dbus_method_invocation_return_value (invocation,
			g_variant_new ("(u@(ia{sv}av))", tray_menu_revision, layout));
		return;
	}

	if (g_strcmp0 (method_name, "GetGroupProperties") == 0)
	{
		GVariant *ids;
		GVariant *property_names;
		GVariantIter iter;
		gint32 id;
		GVariantBuilder rows;
		GVariantBuilder props;

		g_variant_get (parameters, "(@ai@as)", &ids, &property_names);
		g_variant_unref (property_names);

		g_variant_builder_init (&rows, G_VARIANT_TYPE ("a(ia{sv})"));
		g_variant_iter_init (&iter, ids);
		while (g_variant_iter_loop (&iter, "i", &id))
		{
			if (!tray_menu_is_valid_id (id))
				continue;

			g_variant_builder_init (&props, G_VARIANT_TYPE ("a{sv}"));
			tray_menu_fill_properties (id, &props);
			g_variant_builder_add (&rows, "(ia{sv})", id, &props);
		}
		g_variant_unref (ids);

		g_dbus_method_invocation_return_value (invocation,
			g_variant_new ("(a(ia{sv}))", &rows));
		return;
	}

	if (g_strcmp0 (method_name, "GetProperty") == 0)
	{
		gint id;
		const char *name;
		GVariant *value;

		g_variant_get (parameters, "(i&s)", &id, &name);
		value = tray_menu_property_value (id, name);
		if (!value)
		{
			g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
				G_DBUS_ERROR_INVALID_ARGS, "Unknown menu property '%s' for id %d", name, id);
			return;
		}

		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(v)", value));
		return;
	}

	if (g_strcmp0 (method_name, "Event") == 0)
	{
		gint id;
		const char *event_id;
		GVariant *data;
		guint timestamp;

		g_variant_get (parameters, "(i&s@vu)", &id, &event_id, &data, &timestamp);
		if (g_strcmp0 (event_id, "clicked") == 0)
			tray_menu_activate (id, timestamp);
		g_variant_unref (data);

		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}

	if (g_strcmp0 (method_name, "EventGroup") == 0)
	{
		GVariant *events;
		GVariantIter iter;
		GVariant *entry;
		GVariantBuilder errors;

		g_variant_get (parameters, "(@a(isvu))", &events);
		g_variant_builder_init (&errors, G_VARIANT_TYPE ("ai"));
		g_variant_iter_init (&iter, events);

		while ((entry = g_variant_iter_next_value (&iter)) != NULL)
		{
			gint id;
			const char *event_id;
			GVariant *data;
			guint timestamp;

			g_variant_get (entry, "(i&s@vu)", &id, &event_id, &data, &timestamp);
			if (g_strcmp0 (event_id, "clicked") == 0)
				tray_menu_activate (id, timestamp);
			g_variant_unref (data);
			g_variant_unref (entry);
		}
		g_variant_unref (events);

		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(ai)", &errors));
		return;
	}

	if (g_strcmp0 (method_name, "AboutToShow") == 0)
	{
		gint id;

		g_variant_get (parameters, "(i)", &id);
		g_dbus_method_invocation_return_value (invocation,
			g_variant_new ("(b)", id == TRAY_MENU_ID_ROOT || id == TRAY_MENU_ID_TOGGLE_WINDOW));
		return;
	}

	if (g_strcmp0 (method_name, "AboutToShowGroup") == 0)
	{
		GVariant *ids;
		GVariantIter iter;
		gint32 id;
		GVariantBuilder updates;
		GVariantBuilder errors;

		g_variant_get (parameters, "(@ai)", &ids);
		g_variant_builder_init (&updates, G_VARIANT_TYPE ("a(ib)"));
		g_variant_builder_init (&errors, G_VARIANT_TYPE ("ai"));

		g_variant_iter_init (&iter, ids);
		while (g_variant_iter_loop (&iter, "i", &id))
		{
			if (!tray_menu_is_valid_id (id))
			{
				g_variant_builder_add (&errors, "i", id);
				continue;
			}
			g_variant_builder_add (&updates, "(ib)", id, id == TRAY_MENU_ID_ROOT || id == TRAY_MENU_ID_TOGGLE_WINDOW);
		}
		g_variant_unref (ids);

		g_dbus_method_invocation_return_value (invocation,
			g_variant_new ("(a(ib)ai)", &updates, &errors));
		return;
	}

	g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
		G_DBUS_ERROR_UNKNOWN_METHOD, "Unsupported method: %s", method_name);
}

static const GDBusInterfaceVTable tray_menu_vtable =
{
	tray_menu_method_call_cb,
	NULL,
	NULL
};

static void
tray_blink_stop (void)
{
	if (tray_blink_tag)
	{
		g_source_remove (tray_blink_tag);
		tray_blink_tag = 0;
	}

	tray_blink_on = FALSE;
	tray_blink_custom = FALSE;
	tray_blink_icon = FE_ICON_NORMAL;
	tray_blink_interval = 0;
	g_clear_pointer (&tray_blink_name1, g_free);
	g_clear_pointer (&tray_blink_name2, g_free);

	tray_apply_icon_to_window ();
	tray_sni_emit_icon_changed ();
}

static gboolean
tray_blink_timeout_cb (gpointer userdata)
{
	(void) userdata;

	tray_blink_on = !tray_blink_on;
	tray_apply_icon_to_window ();
	tray_sni_emit_icon_changed ();
	return G_SOURCE_CONTINUE;
}

static void
tray_blink_start_internal (feicon icon, guint interval)
{
	tray_blink_stop ();

	tray_blink_custom = FALSE;
	tray_blink_on = TRUE;
	tray_blink_icon = icon;
	tray_blink_interval = interval > 0 ? interval : TRAY_BLINK_DEFAULT_INTERVAL;
	tray_blink_tag = g_timeout_add (tray_blink_interval, tray_blink_timeout_cb, NULL);

	tray_apply_icon_to_window ();
	tray_sni_emit_icon_changed ();
}

static void
tray_blink_start_custom (const char *icon1, const char *icon2, guint interval)
{
	tray_blink_stop ();

	tray_blink_custom = TRUE;
	tray_blink_on = TRUE;
	tray_blink_interval = interval > 0 ? interval : TRAY_BLINK_DEFAULT_INTERVAL;
	tray_blink_name1 = g_strdup (icon1 ? icon1 : "dialog-warning");
	tray_blink_name2 = icon2 ? g_strdup (icon2) : NULL;
	tray_blink_tag = g_timeout_add (tray_blink_interval, tray_blink_timeout_cb, NULL);

	tray_apply_icon_to_window ();
	tray_sni_emit_icon_changed ();
}

static gboolean
tray_icon_name_looks_like_path (const char *name)
{
	return name && name[0] && (strchr (name, G_DIR_SEPARATOR) != NULL || strchr (name, '/') != NULL);
}

static guint
tray_custom_blink_interval (int timeout)
{
	if (timeout == -1)
		return TRAY_BLINK_DEFAULT_INTERVAL;
	if (timeout <= 0)
		return TRAY_BLINK_DEFAULT_INTERVAL;
	return (guint) timeout;
}

static void
tray_sni_method_call_cb (GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *method_name,
	GVariant *parameters,
	GDBusMethodInvocation *invocation,
	gpointer user_data)
{
	(void) connection;
	(void) sender;
	(void) object_path;
	(void) interface_name;
	(void) parameters;
	(void) user_data;

	if (g_strcmp0 (method_name, "Activate") == 0)
	{
		tray_toggle_main_window_visibility ();
		fe_gtk4_tray_menu_emit_layout_updated (TRAY_MENU_ID_ROOT);
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}

	if (g_strcmp0 (method_name, "SecondaryActivate") == 0 ||
		 g_strcmp0 (method_name, "ContextMenu") == 0 ||
		 g_strcmp0 (method_name, "Scroll") == 0)
	{
		if (main_window && g_strcmp0 (method_name, "SecondaryActivate") == 0)
		{
			gtk_widget_set_visible (main_window, TRUE);
			gtk_window_present (GTK_WINDOW (main_window));
			fe_gtk4_tray_menu_emit_layout_updated (TRAY_MENU_ID_ROOT);
		}
		if (g_strcmp0 (method_name, "ContextMenu") == 0)
		{
			fe_gtk4_tray_menu_emit_layout_updated (TRAY_MENU_ID_ROOT);
		}

		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}

	g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
		G_DBUS_ERROR_UNKNOWN_METHOD, "Unsupported method: %s", method_name);
}

static GVariant *
tray_sni_get_property_cb (GDBusConnection *connection,
	const gchar *sender,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *property_name,
	GError **error,
	gpointer user_data)
{
	const char *tooltip;
	const char *attention_icon;

	(void) connection;
	(void) sender;
	(void) object_path;
	(void) interface_name;
	(void) error;
	(void) user_data;

	tooltip = (tray_tooltip && tray_tooltip[0]) ? tray_tooltip : PACKAGE_NAME;
	attention_icon = g_strcmp0 (tray_sni_status (), "NeedsAttention") == 0 ? "dialog-warning" : "";

	if (g_strcmp0 (property_name, "Category") == 0)
		return g_variant_new_string ("Communications");
	if (g_strcmp0 (property_name, "Id") == 0)
		return g_variant_new_string ("hexchat");
	if (g_strcmp0 (property_name, "Title") == 0)
		return g_variant_new_string (PACKAGE_NAME);
	if (g_strcmp0 (property_name, "Status") == 0)
		return g_variant_new_string (tray_sni_status ());
	if (g_strcmp0 (property_name, "WindowId") == 0)
		return g_variant_new_uint32 (0);
	if (g_strcmp0 (property_name, "IconName") == 0)
		return g_variant_new_string (tray_sni_icon_name ());
	if (g_strcmp0 (property_name, "IconPixmap") == 0)
		return tray_sni_empty_pixmaps ();
	if (g_strcmp0 (property_name, "OverlayIconName") == 0)
		return g_variant_new_string ("");
	if (g_strcmp0 (property_name, "OverlayIconPixmap") == 0)
		return tray_sni_empty_pixmaps ();
	if (g_strcmp0 (property_name, "AttentionIconName") == 0)
		return g_variant_new_string (attention_icon);
	if (g_strcmp0 (property_name, "AttentionIconPixmap") == 0)
		return tray_sni_empty_pixmaps ();
	if (g_strcmp0 (property_name, "AttentionMovieName") == 0)
		return g_variant_new_string ("");
	if (g_strcmp0 (property_name, "ToolTip") == 0)
		return g_variant_new ("(s@a(iiay)ss)", tray_sni_icon_name (),
			tray_sni_empty_pixmaps (), PACKAGE_NAME, tooltip);
	if (g_strcmp0 (property_name, "ItemIsMenu") == 0)
		return g_variant_new_boolean (FALSE);
	if (g_strcmp0 (property_name, "Menu") == 0)
		return g_variant_new_object_path (tray_menu_object_id ? SNI_MENU_PATH : "/");

	return NULL;
}

static const GDBusInterfaceVTable tray_sni_vtable =
{
	tray_sni_method_call_cb,
	tray_sni_get_property_cb,
	NULL
};

static void
tray_sni_register_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	GError *error;
	GVariant *ret;

	(void) user_data;

	error = NULL;
	ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), result, &error);
	if (ret)
	{
		tray_sni_registered = TRUE;
		g_variant_unref (ret);
	}
	else
	{
		tray_sni_registered = FALSE;
		if (error)
			g_error_free (error);
	}
}

static void
tray_sni_register_with_watcher (void)
{
	if (!tray_sni_connection || !tray_sni_bus_name || !tray_sni_bus_name[0])
		return;

	g_dbus_connection_call (tray_sni_connection,
		SNI_WATCHER_NAME,
		SNI_WATCHER_PATH,
		SNI_WATCHER_INTERFACE,
		"RegisterStatusNotifierItem",
		g_variant_new ("(s)", tray_sni_bus_name),
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		tray_sni_register_cb,
		NULL);
}

static void
tray_sni_watcher_appeared_cb (GDBusConnection *connection,
	const gchar *name,
	const gchar *name_owner,
	gpointer user_data)
{
	(void) connection;
	(void) name;
	(void) name_owner;
	(void) user_data;

	tray_sni_register_with_watcher ();
}

static void
tray_sni_watcher_vanished_cb (GDBusConnection *connection,
	const gchar *name,
	gpointer user_data)
{
	(void) connection;
	(void) name;
	(void) user_data;
	tray_sni_registered = FALSE;
}

static void
tray_sni_bus_acquired_cb (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	GError *error;

	(void) name;
	(void) user_data;

	g_clear_object (&tray_sni_connection);
	tray_sni_connection = g_object_ref (connection);

	if (!tray_sni_node_info)
	{
		error = NULL;
		tray_sni_node_info = g_dbus_node_info_new_for_xml (tray_sni_introspection_xml, &error);
		if (!tray_sni_node_info)
		{
			if (error)
			{
				g_warning ("SNI introspection parse failed: %s", error->message);
				g_error_free (error);
			}
			return;
		}
	}

	if (!tray_sni_object_id)
	{
		error = NULL;
		tray_sni_object_id = g_dbus_connection_register_object (tray_sni_connection,
			SNI_ITEM_PATH,
			tray_sni_node_info->interfaces[0],
			&tray_sni_vtable,
			NULL,
			NULL,
			&error);
		if (!tray_sni_object_id)
		{
			if (error)
			{
				g_warning ("SNI object registration failed: %s", error->message);
				g_error_free (error);
			}
			return;
		}
	}

	if (!tray_menu_node_info)
	{
		error = NULL;
		tray_menu_node_info = g_dbus_node_info_new_for_xml (tray_menu_introspection_xml, &error);
		if (!tray_menu_node_info)
		{
			if (error)
			{
				g_warning ("DBusMenu introspection parse failed: %s", error->message);
				g_error_free (error);
			}
			return;
		}
	}

	if (!tray_menu_object_id)
	{
		error = NULL;
		tray_menu_object_id = g_dbus_connection_register_object (tray_sni_connection,
			SNI_MENU_PATH,
			tray_menu_node_info->interfaces[0],
			&tray_menu_vtable,
			NULL,
			NULL,
			&error);
		if (!tray_menu_object_id)
		{
			if (error)
			{
				g_warning ("DBusMenu object registration failed: %s", error->message);
				g_error_free (error);
			}
			return;
		}
		fe_gtk4_tray_menu_emit_layout_updated (TRAY_MENU_ID_ROOT);
	}

	if (!tray_sni_watcher_id)
	{
		tray_sni_watcher_id = g_bus_watch_name_on_connection (tray_sni_connection,
			SNI_WATCHER_NAME,
			G_BUS_NAME_WATCHER_FLAGS_NONE,
			tray_sni_watcher_appeared_cb,
			tray_sni_watcher_vanished_cb,
			NULL,
			NULL);
	}
}

static void
tray_sni_name_acquired_cb (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	(void) connection;
	(void) name;
	(void) user_data;

	tray_sni_register_with_watcher ();
}

static void
tray_sni_name_lost_cb (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	(void) connection;
	(void) name;
	(void) user_data;

	if (tray_sni_watcher_id)
	{
		g_bus_unwatch_name (tray_sni_watcher_id);
		tray_sni_watcher_id = 0;
	}

	if (tray_sni_connection && tray_sni_object_id)
	{
		g_dbus_connection_unregister_object (tray_sni_connection, tray_sni_object_id);
		tray_sni_object_id = 0;
	}
	if (tray_sni_connection && tray_menu_object_id)
	{
		g_dbus_connection_unregister_object (tray_sni_connection, tray_menu_object_id);
		tray_menu_object_id = 0;
	}

	g_clear_object (&tray_sni_connection);
	tray_sni_registered = FALSE;
}

static void
tray_sni_init (void)
{
	if (tray_sni_owner_id)
		return;

	if (tray_menu_revision == 0)
		tray_menu_revision = 1;

	g_free (tray_sni_bus_name);
	tray_sni_bus_name = g_strdup_printf ("org.hexchat.StatusNotifierItem.instance%u",
		(guint) g_random_int ());

	tray_sni_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
		tray_sni_bus_name,
		G_BUS_NAME_OWNER_FLAGS_NONE,
		tray_sni_bus_acquired_cb,
		tray_sni_name_acquired_cb,
		tray_sni_name_lost_cb,
		NULL,
		NULL);
}

static void
tray_sni_cleanup (void)
{
	if (tray_sni_owner_id)
	{
		g_bus_unown_name (tray_sni_owner_id);
		tray_sni_owner_id = 0;
	}

	if (tray_sni_watcher_id)
	{
		g_bus_unwatch_name (tray_sni_watcher_id);
		tray_sni_watcher_id = 0;
	}

	if (tray_sni_connection && tray_sni_object_id)
	{
		g_dbus_connection_unregister_object (tray_sni_connection, tray_sni_object_id);
		tray_sni_object_id = 0;
	}
	if (tray_sni_connection && tray_menu_object_id)
	{
		g_dbus_connection_unregister_object (tray_sni_connection, tray_menu_object_id);
		tray_menu_object_id = 0;
	}
	g_clear_object (&tray_sni_connection);

	if (tray_sni_node_info)
	{
		g_dbus_node_info_unref (tray_sni_node_info);
		tray_sni_node_info = NULL;
	}
	if (tray_menu_node_info)
	{
		g_dbus_node_info_unref (tray_menu_node_info);
		tray_menu_node_info = NULL;
	}

	tray_blink_stop ();

	g_clear_pointer (&tray_sni_bus_name, g_free);
	tray_sni_registered = FALSE;
}

static void
tray_warn_once (void)
{
	if (tray_sni_is_ready ())
		return;

	if (tray_warned)
		return;
	tray_warned = TRUE;

	if (current_tab)
		fe_print_text (current_tab,
			"* GTK4 frontend: StatusNotifierItem is unavailable, using window fallback behavior.",
			0, FALSE);
}

static void
tray_file_warn_once (void)
{
	if (tray_file_warned)
		return;
	tray_file_warned = TRUE;

	if (current_tab)
		fe_print_text (current_tab,
			"* GTK4 frontend: file-path tray icons are not supported by the current SNI backend.",
			0, FALSE);
}

void
fe_tray_set_flash (const char *filename1, const char *filename2, int timeout)
{
	const char *icon1;
	const char *icon2;

	icon1 = (filename1 && filename1[0]) ? filename1 : NULL;
	icon2 = (filename2 && filename2[0]) ? filename2 : NULL;

	if (!icon1)
	{
		if (tray_blink_tag)
			tray_blink_stop ();
		return;
	}

	if (tray_icon_name_looks_like_path (icon1) || tray_icon_name_looks_like_path (icon2))
		tray_file_warn_once ();

	tray_icon_state = FE_ICON_NORMAL;
	g_clear_pointer (&tray_icon_name_override, g_free);
	tray_blink_start_custom (icon1, icon2, tray_custom_blink_interval (timeout));

	if (!tray_sni_is_ready ())
		tray_warn_once ();
}

void
fe_tray_set_file (const char *filename)
{
	if (tray_blink_tag)
		tray_blink_stop ();
	tray_icon_state = FE_ICON_NORMAL;

	if (!filename || !filename[0])
	{
		g_clear_pointer (&tray_icon_name_override, g_free);
	}
	else
	{
		if (tray_icon_name_looks_like_path (filename))
			tray_file_warn_once ();
		g_free (tray_icon_name_override);
		tray_icon_name_override = g_strdup (filename);
	}

	tray_apply_icon_to_window ();

	if (tray_sni_is_ready ())
		tray_sni_emit_icon_changed ();
	else
		tray_warn_once ();
}

void
fe_tray_set_icon (feicon icon)
{
	gboolean focused;

	focused = tray_window_is_active ();
	tray_icon_state = icon;
	g_clear_pointer (&tray_icon_name_override, g_free);

	if (icon == FE_ICON_NORMAL || focused)
	{
		if (tray_blink_tag)
			tray_blink_stop ();
		if (focused)
			tray_icon_state = FE_ICON_NORMAL;
		tray_apply_icon_to_window ();
		if (tray_sni_is_ready ())
			tray_sni_emit_icon_changed ();
		else
			tray_warn_once ();
		return;
	}

	if (prefs.hex_gui_tray_blink)
	{
		if (!(tray_blink_tag && !tray_blink_custom && tray_blink_icon == icon))
			tray_blink_start_internal (icon, TRAY_BLINK_DEFAULT_INTERVAL);
	}
	else
	{
		if (tray_blink_tag)
			tray_blink_stop ();
		tray_apply_icon_to_window ();
		if (tray_sni_is_ready ())
			tray_sni_emit_icon_changed ();
	}

	if (!tray_sni_is_ready ())
		tray_warn_once ();
}

void
fe_tray_set_tooltip (const char *text)
{
	g_free (tray_tooltip);
	tray_tooltip = g_strdup (text ? text : "");

	if (main_window)
		gtk_widget_set_tooltip_text (main_window, tray_tooltip[0] ? tray_tooltip : NULL);

	if (tray_sni_is_ready ())
		tray_sni_emit_tooltip_changed ();
	else
		tray_warn_once ();
}

void
fe_gtk4_tray_sync_window (void)
{
	tray_apply_icon_to_window ();
	if (main_window)
		gtk_widget_set_tooltip_text (main_window,
			(tray_tooltip && tray_tooltip[0]) ? tray_tooltip : NULL);
}

void
fe_gtk4_tray_init (void)
{
	tray_icon_state = FE_ICON_NORMAL;
	tray_warned = FALSE;
	tray_file_warned = FALSE;
	tray_sni_registered = FALSE;
	tray_sni_init ();
}

void
fe_gtk4_tray_cleanup (void)
{
	g_clear_pointer (&tray_tooltip, g_free);
	g_clear_pointer (&tray_icon_name_override, g_free);
	tray_icon_state = FE_ICON_NORMAL;
	tray_warned = FALSE;
	tray_file_warned = FALSE;
	tray_sni_cleanup ();
}

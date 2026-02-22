/* Ditrigon
 * Copyright (C) 2021 Patrick Griffis.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#include <string.h>
#include <gio/gio.h>

#include "notification-backend.h"

static GDBusProxy *fdo_notifications;
static gboolean strip_markup;
static gboolean supports_actions;
static notification_backend_activate_cb activate_cb;
static void *activate_userdata;
static GHashTable *notification_targets;

typedef struct
{
	char *servname;
	char *channel;
} notification_target;

static void
notification_target_free (notification_target *target)
{
	if (!target)
		return;

	g_free (target->servname);
	g_free (target->channel);
	g_free (target);
}

static notification_target *
notification_target_new (const char *servname, const char *channel)
{
	notification_target *target;

	target = g_new0 (notification_target, 1);
	target->servname = g_strdup (servname);
	target->channel = g_strdup (channel);
	return target;
}

static void
on_notification_signal (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
	notification_target *target;
	guint32 notification_id;
	const char *action;

	(void) proxy;
	(void) sender_name;
	(void) user_data;

	if (!notification_targets)
		return;

	if (g_strcmp0 (signal_name, "ActionInvoked") == 0)
	{
		g_variant_get (parameters, "(u&s)", &notification_id, &action);
		if (g_strcmp0 (action, "default") != 0)
			return;

		target = g_hash_table_lookup (notification_targets, GUINT_TO_POINTER (notification_id));
		if (!target)
			return;

		if (activate_cb)
			activate_cb (target->servname, target->channel, activate_userdata);

		g_hash_table_remove (notification_targets, GUINT_TO_POINTER (notification_id));
		return;
	}

	if (g_strcmp0 (signal_name, "NotificationClosed") == 0)
	{
		g_variant_get (parameters, "(uu)", &notification_id, NULL);
		g_hash_table_remove (notification_targets, GUINT_TO_POINTER (notification_id));
	}
}

static void
on_notify_ready (GDBusProxy *proxy, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	guint32 notification_id;
	GVariant *response;
	notification_target *target;

	target = user_data;
	response = g_dbus_proxy_call_finish (proxy, res, &error);
	if (error)
	{
		g_info ("Failed to send notification: %s", error->message);
		g_error_free (error);
		notification_target_free (target);
		return;
	}

	g_variant_get (response, "(u)", &notification_id);
	g_info ("Notification sent. ID=%u", notification_id);

	if (target && notification_id > 0)
	{
		if (!notification_targets)
			notification_targets = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) notification_target_free);
		g_hash_table_insert (notification_targets, GUINT_TO_POINTER (notification_id), target);
		target = NULL;
	}

	notification_target_free (target);
	g_variant_unref (response);
}

void
notification_backend_show (const char *title, const char *text)
{
	notification_backend_show_for_context (title, text, NULL, NULL);
}

void
notification_backend_show_for_context (const char *title, const char *text, const char *servname, const char *channel)
{
	GVariantBuilder params;
	notification_target *target;
	char *escaped_text;

	g_assert (fdo_notifications);

	escaped_text = strip_markup ? g_markup_escape_text (text, -1) : NULL;

	g_variant_builder_init (&params, G_VARIANT_TYPE ("(susssasa{sv}i)"));
	g_variant_builder_add (&params, "s", "hexchat"); /* App name */
	g_variant_builder_add (&params, "u", 0); /* ID, 0 means don't replace */
	g_variant_builder_add (&params, "s", "io.github.Ditrigon"); /* App icon */
	g_variant_builder_add (&params, "s", title);
	g_variant_builder_add (&params, "s", escaped_text ? escaped_text : text);

	g_variant_builder_open (&params, G_VARIANT_TYPE ("as"));
	if (supports_actions)
	{
		g_variant_builder_add (&params, "s", "default");
		g_variant_builder_add (&params, "s", "Open");
	}
	g_variant_builder_close (&params);

	/* Hints */
	g_variant_builder_open (&params, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_open (&params, G_VARIANT_TYPE ("{sv}"));
	g_variant_builder_add (&params, "s", "desktop-entry");
	g_variant_builder_add (&params, "v", g_variant_new_string ("io.github.Ditrigon"));
	g_variant_builder_close (&params);
	g_variant_builder_close (&params);

	g_variant_builder_add (&params, "i", -1); /* Expiration */

	target = supports_actions ? notification_target_new (servname, channel) : NULL;
	g_dbus_proxy_call (fdo_notifications,
		"Notify",
		g_variant_builder_end (&params),
		G_DBUS_CALL_FLAGS_NONE,
		1000,
		NULL,
		(GAsyncReadyCallback) on_notify_ready,
		target);

	g_free (escaped_text);
}

void
notification_backend_set_activation_callback (notification_backend_activate_cb callback, void *userdata)
{
	activate_cb = callback;
	activate_userdata = userdata;
}

int
notification_backend_init (const char **error)
{
	GError *err = NULL;
	GVariant *response;
	char **capabilities;
	guint i;

	fdo_notifications = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		NULL,
		"org.freedesktop.Notifications",
		"/org/freedesktop/Notifications",
		"org.freedesktop.Notifications",
		NULL,
		&err);

	if (err)
		goto return_error;

	response = g_dbus_proxy_call_sync (fdo_notifications,
		"GetCapabilities",
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		30,
		NULL,
		&err);

	if (err)
	{
		g_clear_object (&fdo_notifications);
		goto return_error;
	}

	g_variant_get (response, "(^a&s)", &capabilities);
	for (i = 0; capabilities[i]; i++)
	{
		if (strcmp (capabilities[i], "actions") == 0)
			supports_actions = TRUE;
		else if (strcmp (capabilities[i], "body-markup") == 0)
			strip_markup = TRUE;
	}

	g_signal_connect (fdo_notifications, "g-signal", G_CALLBACK (on_notification_signal), NULL);

	g_free (capabilities);
	g_variant_unref (response);
	return 1;

return_error:
	*error = g_strdup (err->message);
	g_error_free (err);
	return 0;
}

void
notification_backend_deinit (void)
{
	g_clear_pointer (&notification_targets, g_hash_table_unref);
	activate_cb = NULL;
	activate_userdata = NULL;
	supports_actions = FALSE;
	strip_markup = FALSE;
	g_clear_object (&fdo_notifications);
}

int
notification_backend_supported (void)
{
	return fdo_notifications != NULL;
}

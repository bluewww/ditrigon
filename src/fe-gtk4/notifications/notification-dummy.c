/* Ditrigon
 * Copyright (C) 2015 Patrick Griffis.
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

#include "notification-backend.h"

void
notification_backend_show (const char *title, const char *text)
{
	(void) title;
	(void) text;
}

void
notification_backend_show_for_context (const char *title, const char *text, const char *servname, const char *channel)
{
	(void) title;
	(void) text;
	(void) servname;
	(void) channel;
}

void
notification_backend_set_activation_callback (notification_backend_activate_cb callback, void *userdata)
{
	(void) callback;
	(void) userdata;
}

int
notification_backend_init (const char **error)
{
	(void) error;
	return 0;
}

void
notification_backend_deinit (void)
{
}

int
notification_backend_supported (void)
{
	return 0;
}

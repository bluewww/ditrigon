/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 pixmap/icon loader */

#include "fe-gtk4.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../common/cfgfiles.h"
#include "../common/fe.h"
#include "resources-gtk4.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

GdkPixbuf *pix_ulist_voice;
GdkPixbuf *pix_ulist_halfop;
GdkPixbuf *pix_ulist_op;
GdkPixbuf *pix_ulist_owner;
GdkPixbuf *pix_ulist_founder;
GdkPixbuf *pix_ulist_netop;

GdkPixbuf *pix_tray_normal;
GdkPixbuf *pix_tray_fileoffer;
GdkPixbuf *pix_tray_highlight;
GdkPixbuf *pix_tray_message;

GdkPixbuf *pix_tree_channel;
GdkPixbuf *pix_tree_dialog;
GdkPixbuf *pix_tree_server;
GdkPixbuf *pix_tree_util;

GdkPixbuf *pix_book;
GdkPixbuf *pix_hexchat;

static GdkPixbuf *
load_pixmap (const char *filename)
{
	GdkPixbuf *pixbuf;
	GdkPixbuf *scaledpixbuf;
	const char *scale;
	int iscale;
	gchar *path;

	path = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "icons" G_DIR_SEPARATOR_S "%s.png",
		get_xdir (), filename);
	pixbuf = gdk_pixbuf_new_from_file (path, NULL);
	g_free (path);

	if (!pixbuf)
	{
		path = g_strdup_printf ("/icons/%s.png", filename);
		pixbuf = gdk_pixbuf_new_from_resource (path, NULL);
		g_free (path);
	}

	if (!pixbuf)
		return NULL;

	/* Keep icon sizes readable on HiDPI displays when GDK_SCALE is set. */
	scale = g_getenv ("GDK_SCALE");
	if (scale)
	{
		iscale = atoi (scale);
		if (iscale > 1)
		{
			scaledpixbuf = gdk_pixbuf_scale_simple (pixbuf,
				gdk_pixbuf_get_width (pixbuf) * iscale,
				gdk_pixbuf_get_height (pixbuf) * iscale,
				GDK_INTERP_BILINEAR);
			if (scaledpixbuf)
			{
				g_object_unref (pixbuf);
				pixbuf = scaledpixbuf;
			}
		}
	}

	return pixbuf;
}

GdkPixbuf *
pixmap_load_from_file (char *filename)
{
	GdkPixbuf *pix;
	char buf[256];

	if (!filename || filename[0] == '\0')
		return NULL;

	pix = gdk_pixbuf_new_from_file (filename, NULL);
	if (!pix)
	{
		strcpy (buf, "Cannot open:\n\n");
		strncpy (buf + 14, filename, sizeof (buf) - 14);
		buf[sizeof (buf) - 1] = 0;
		fe_message (buf, FE_MSG_ERROR);
	}

	return pix;
}

void
pixmaps_init (void)
{
	ditrigon_register_resource ();

	/* Make the bundled app icon discoverable by name via the icon theme */
	{
		GdkDisplay *display = gdk_display_get_default ();
		if (display)
		{
			GtkIconTheme *theme = gtk_icon_theme_get_for_display (display);
			if (theme)
				gtk_icon_theme_add_resource_path (theme, "/org/ditrigon/icons");
		}
	}

	pix_ulist_voice = load_pixmap ("ulist_voice");
	pix_ulist_halfop = load_pixmap ("ulist_halfop");
	pix_ulist_op = load_pixmap ("ulist_op");
	pix_ulist_owner = load_pixmap ("ulist_owner");
	pix_ulist_founder = load_pixmap ("ulist_founder");
	pix_ulist_netop = load_pixmap ("ulist_netop");

	pix_tray_normal = load_pixmap ("tray_normal");
	pix_tray_fileoffer = load_pixmap ("tray_fileoffer");
	pix_tray_highlight = load_pixmap ("tray_highlight");
	pix_tray_message = load_pixmap ("tray_message");

	pix_tree_channel = load_pixmap ("tree_channel");
	pix_tree_dialog = load_pixmap ("tree_dialog");
	pix_tree_server = load_pixmap ("tree_server");
	pix_tree_util = load_pixmap ("tree_util");

	pix_book = gdk_pixbuf_new_from_resource ("/icons/book.png", NULL);
	pix_hexchat = load_pixmap ("hexchat");
}

static void
pixmap_clear_ref (GdkPixbuf **pix)
{
	if (!pix || !*pix)
		return;

	g_object_unref (*pix);
	*pix = NULL;
}

void
fe_gtk4_pixmaps_cleanup (void)
{
	pixmap_clear_ref (&pix_ulist_voice);
	pixmap_clear_ref (&pix_ulist_halfop);
	pixmap_clear_ref (&pix_ulist_op);
	pixmap_clear_ref (&pix_ulist_owner);
	pixmap_clear_ref (&pix_ulist_founder);
	pixmap_clear_ref (&pix_ulist_netop);

	pixmap_clear_ref (&pix_tray_normal);
	pixmap_clear_ref (&pix_tray_fileoffer);
	pixmap_clear_ref (&pix_tray_highlight);
	pixmap_clear_ref (&pix_tray_message);

	pixmap_clear_ref (&pix_tree_channel);
	pixmap_clear_ref (&pix_tree_dialog);
	pixmap_clear_ref (&pix_tree_server);
	pixmap_clear_ref (&pix_tree_util);

	pixmap_clear_ref (&pix_book);
	pixmap_clear_ref (&pix_hexchat);
}

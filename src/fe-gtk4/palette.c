/* HexChat GTK4 palette loader/saver */
#include "fe-gtk4.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../common/util.h"
#include "../common/cfgfiles.h"
#include "palette.h"

HcColor16 colors[] =
{
	/* xtext colors */
	{ 0xd3d3, 0xd7d7, 0xcfcf }, /* 0 white */
	{ 0x2e2e, 0x3434, 0x3636 }, /* 1 black */
	{ 0x3434, 0x6565, 0xa4a4 }, /* 2 blue */
	{ 0x4e4e, 0x9a9a, 0x0606 }, /* 3 green */
	{ 0xcccc, 0x0000, 0x0000 }, /* 4 red */
	{ 0x8f8f, 0x3939, 0x0202 }, /* 5 light red */
	{ 0x5c5c, 0x3535, 0x6666 }, /* 6 purple */
	{ 0xcece, 0x5c5c, 0x0000 }, /* 7 orange */
	{ 0xc4c4, 0xa0a0, 0x0000 }, /* 8 yellow */
	{ 0x7373, 0xd2d2, 0x1616 }, /* 9 green */
	{ 0x1111, 0xa8a8, 0x7979 }, /* 10 aqua */
	{ 0x5858, 0xa1a1, 0x9d9d }, /* 11 light aqua */
	{ 0x5757, 0x7979, 0x9e9e }, /* 12 blue */
	{ 0xa0d0, 0x42d4, 0x6562 }, /* 13 light purple */
	{ 0x5555, 0x5757, 0x5353 }, /* 14 grey */
	{ 0x8888, 0x8a8a, 0x8585 }, /* 15 light grey */

	{ 0xd3d3, 0xd7d7, 0xcfcf }, /* 16 white */
	{ 0x2e2e, 0x3434, 0x3636 }, /* 17 black */
	{ 0x3434, 0x6565, 0xa4a4 }, /* 18 blue */
	{ 0x4e4e, 0x9a9a, 0x0606 }, /* 19 green */
	{ 0xcccc, 0x0000, 0x0000 }, /* 20 red */
	{ 0x8f8f, 0x3939, 0x0202 }, /* 21 light red */
	{ 0x5c5c, 0x3535, 0x6666 }, /* 22 purple */
	{ 0xcece, 0x5c5c, 0x0000 }, /* 23 orange */
	{ 0xc4c4, 0xa0a0, 0x0000 }, /* 24 yellow */
	{ 0x7373, 0xd2d2, 0x1616 }, /* 25 green */
	{ 0x1111, 0xa8a8, 0x7979 }, /* 26 aqua */
	{ 0x5858, 0xa1a1, 0x9d9d }, /* 27 light aqua */
	{ 0x5757, 0x7979, 0x9e9e }, /* 28 blue */
	{ 0xa0d0, 0x42d4, 0x6562 }, /* 29 light purple */
	{ 0x5555, 0x5757, 0x5353 }, /* 30 grey */
	{ 0x8888, 0x8a8a, 0x8585 }, /* 31 light grey */

	{ 0xd3d3, 0xd7d7, 0xcfcf }, /* 32 marktext fg */
	{ 0x2020, 0x4a4a, 0x8787 }, /* 33 marktext bg */
	{ 0x2512, 0x29e8, 0x2b85 }, /* 34 foreground */
	{ 0xfae0, 0xfae0, 0xf8c4 }, /* 35 background */
	{ 0x8f8f, 0x3939, 0x0202 }, /* 36 marker line */

	/* GUI colors */
	{ 0x3434, 0x6565, 0xa4a4 }, /* 37 tab new data */
	{ 0x4e4e, 0x9a9a, 0x0606 }, /* 38 tab hilight */
	{ 0xcece, 0x5c5c, 0x0000 }, /* 39 tab new msg */
	{ 0x8888, 0x8a8a, 0x8585 }, /* 40 away */
	{ 0xa4a4, 0x0000, 0x0000 }, /* 41 spell checker */
};

void
palette_alloc (GtkWidget *widget)
{
	(void) widget;
	/* GTK4 uses RGBA and CSS styling; no colormap allocation is required. */
}

void
palette_load (void)
{
	int i;
	int j;
	int fh;
	char prefname[256];
	struct stat st;
	char *cfg;
	guint16 red;
	guint16 green;
	guint16 blue;

	fh = hexchat_open_file ("colors.conf", O_RDONLY, 0, 0);
	if (fh == -1)
		return;

	fstat (fh, &st);
	cfg = g_malloc0 (st.st_size + 1);
	read (fh, cfg, st.st_size);

	/* mIRC colors 0-31 */
	for (i = 0; i < 32; i++)
	{
		g_snprintf (prefname, sizeof prefname, "color_%d", i);
		cfg_get_color (cfg, prefname, &red, &green, &blue);
		colors[i].red = red;
		colors[i].green = green;
		colors[i].blue = blue;
	}

	/* extended colors 256+ -> 32..MAX_COL */
	for (i = 256, j = 32; j < MAX_COL + 1; i++, j++)
	{
		g_snprintf (prefname, sizeof prefname, "color_%d", i);
		cfg_get_color (cfg, prefname, &red, &green, &blue);
		colors[j].red = red;
		colors[j].green = green;
		colors[j].blue = blue;
	}

	g_free (cfg);
	close (fh);
}

void
palette_save (void)
{
	int i;
	int j;
	int fh;
	char prefname[256];

	fh = hexchat_open_file ("colors.conf", O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh == -1)
		return;

	/* mIRC colors 0-31 */
	for (i = 0; i < 32; i++)
	{
		g_snprintf (prefname, sizeof prefname, "color_%d", i);
		cfg_put_color (fh, colors[i].red, colors[i].green, colors[i].blue, prefname);
	}

	/* extended colors 256+ -> 32..MAX_COL */
	for (i = 256, j = 32; j < MAX_COL + 1; i++, j++)
	{
		g_snprintf (prefname, sizeof prefname, "color_%d", i);
		cfg_put_color (fh, colors[j].red, colors[j].green, colors[j].blue, prefname);
	}

	close (fh);
}

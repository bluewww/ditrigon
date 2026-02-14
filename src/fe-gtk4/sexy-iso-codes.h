/* gtkspell - a spell-checking addon for GTK text widgets
 * Copyright (c) 2013 Sandro Mani
 */

#ifndef GTK_SPELL_CODETABLE_H
#define GTK_SPELL_CODETABLE_H

#include <glib.h>

G_BEGIN_DECLS

void codetable_init (void);
void codetable_free (void);
void codetable_lookup (const gchar *language_code,
	const gchar **language_name,
	const gchar **country_name);

G_END_DECLS

#endif

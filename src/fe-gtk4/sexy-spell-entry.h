/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 compatibility shim for libsexy spell entry API */

#ifndef _SEXY_SPELL_ENTRY_H_
#define _SEXY_SPELL_ENTRY_H_

#include <gtk/gtk.h>

typedef GtkEntry SexySpellEntry;
typedef GtkEntryClass SexySpellEntryClass;

#define SEXY_TYPE_SPELL_ENTRY            (GTK_TYPE_ENTRY)
#define SEXY_SPELL_ENTRY(obj)            (GTK_ENTRY (obj))
#define SEXY_SPELL_ENTRY_CLASS(klass)    (GTK_ENTRY_CLASS (klass))
#define SEXY_IS_SPELL_ENTRY(obj)         (GTK_IS_ENTRY (obj))
#define SEXY_IS_SPELL_ENTRY_CLASS(klass) (GTK_IS_ENTRY_CLASS (klass))
#define SEXY_SPELL_ENTRY_GET_CLASS(obj)  (GTK_ENTRY_GET_CLASS (obj))

#define SEXY_SPELL_ERROR                 (sexy_spell_error_quark ())

typedef enum
{
	SEXY_SPELL_ERROR_BACKEND,
} SexySpellError;

G_BEGIN_DECLS

GType sexy_spell_entry_get_type (void);
GtkWidget *sexy_spell_entry_new (void);
GQuark sexy_spell_error_quark (void);

GSList *sexy_spell_entry_get_languages (const SexySpellEntry *entry);
gchar *sexy_spell_entry_get_language_name (const SexySpellEntry *entry, const gchar *lang);
gboolean sexy_spell_entry_language_is_active (const SexySpellEntry *entry, const gchar *lang);
gboolean sexy_spell_entry_activate_language (SexySpellEntry *entry, const gchar *lang, GError **error);
void sexy_spell_entry_deactivate_language (SexySpellEntry *entry, const gchar *lang);
gboolean sexy_spell_entry_set_active_languages (SexySpellEntry *entry, GSList *langs, GError **error);
GSList *sexy_spell_entry_get_active_languages (SexySpellEntry *entry);
gboolean sexy_spell_entry_is_checked (SexySpellEntry *entry);
void sexy_spell_entry_set_checked (SexySpellEntry *entry, gboolean checked);
void sexy_spell_entry_set_parse_attributes (SexySpellEntry *entry, gboolean parse);
void sexy_spell_entry_activate_default_languages (SexySpellEntry *entry);

G_END_DECLS

#endif

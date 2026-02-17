/* SPDX-License_Identifier: GPL-2.0-or-later */
/* GTK4 compatibility shim for libsexy spell entry API */

#include "config.h"

#include "fe-gtk4.h"
#include "sexy-spell-entry.h"
#include "sexy-iso-codes.h"

#define SPELL_DATA_KEY "hexchat-gtk4-spell-data"

typedef struct
{
	gboolean checked;
	gboolean parseattr;
	GSList *active_langs;
} HcSpellState;

static void
spell_state_free (HcSpellState *state)
{
	GSList *iter;

	if (!state)
		return;

	for (iter = state->active_langs; iter; iter = iter->next)
		g_free (iter->data);
	g_slist_free (state->active_langs);
	g_free (state);
}

static HcSpellState *
spell_state_get (SexySpellEntry *entry)
{
	HcSpellState *state;

	if (!entry)
		return NULL;

	state = g_object_get_data (G_OBJECT (entry), SPELL_DATA_KEY);
	if (state)
		return state;

	state = g_new0 (HcSpellState, 1);
	state->checked = TRUE;
	state->parseattr = FALSE;
	g_object_set_data_full (G_OBJECT (entry), SPELL_DATA_KEY, state,
		(GDestroyNotify) spell_state_free);
	return state;
}

static void
spell_state_clear_languages (HcSpellState *state)
{
	GSList *iter;

	if (!state)
		return;

	for (iter = state->active_langs; iter; iter = iter->next)
		g_free (iter->data);
	g_slist_free (state->active_langs);
	state->active_langs = NULL;
}

static gboolean
spell_lang_in_list (GSList *list, const char *lang)
{
	GSList *iter;

	if (!lang || !lang[0])
		return FALSE;

	for (iter = list; iter; iter = iter->next)
	{
		if (g_ascii_strcasecmp ((const char *) iter->data, lang) == 0)
			return TRUE;
	}

	return FALSE;
}

GType
sexy_spell_entry_get_type (void)
{
	return GTK_TYPE_ENTRY;
}

GtkWidget *
sexy_spell_entry_new (void)
{
	GtkWidget *entry = gtk_entry_new ();

	spell_state_get (SEXY_SPELL_ENTRY (entry));
	return entry;
}

GQuark
sexy_spell_error_quark (void)
{
	return g_quark_from_static_string ("sexy-spell-error-quark");
}

GSList *
sexy_spell_entry_get_languages (const SexySpellEntry *entry)
{
	GSList *list = NULL;

	(void) entry;
	list = g_slist_prepend (list, g_strdup ("en"));
	return list;
}

gchar *
sexy_spell_entry_get_language_name (const SexySpellEntry *entry, const gchar *lang)
{
	const char *language_name = "";
	const char *country_name = "";

	(void) entry;
	if (!lang || !lang[0])
		return g_strdup ("");

	codetable_lookup (lang, &language_name, &country_name);
	if (country_name[0])
		return g_strdup_printf ("%s (%s)", language_name, country_name);
	return g_strdup (language_name);
}

gboolean
sexy_spell_entry_language_is_active (const SexySpellEntry *entry, const gchar *lang)
{
	HcSpellState *state;

	state = spell_state_get ((SexySpellEntry *) entry);
	return spell_lang_in_list (state->active_langs, lang);
}

gboolean
sexy_spell_entry_activate_language (SexySpellEntry *entry, const gchar *lang, GError **error)
{
	HcSpellState *state;

	(void) error;
	state = spell_state_get (entry);
	if (!lang || !lang[0])
		return FALSE;
	if (!spell_lang_in_list (state->active_langs, lang))
		state->active_langs = g_slist_append (state->active_langs, g_strdup (lang));
	return TRUE;
}

void
sexy_spell_entry_deactivate_language (SexySpellEntry *entry, const gchar *lang)
{
	HcSpellState *state;
	GSList *iter;

	state = spell_state_get (entry);
	if (!state)
		return;

	if (!lang || !lang[0])
	{
		spell_state_clear_languages (state);
		return;
	}

	for (iter = state->active_langs; iter; iter = iter->next)
	{
		if (g_ascii_strcasecmp ((const char *) iter->data, lang) != 0)
			continue;
		g_free (iter->data);
		state->active_langs = g_slist_delete_link (state->active_langs, iter);
		break;
	}
}

gboolean
sexy_spell_entry_set_active_languages (SexySpellEntry *entry, GSList *langs, GError **error)
{
	HcSpellState *state;
	GSList *iter;

	(void) error;
	state = spell_state_get (entry);
	if (!state)
		return FALSE;

	spell_state_clear_languages (state);
	for (iter = langs; iter; iter = iter->next)
	{
		const char *lang = iter->data;

		if (!lang || !lang[0] || spell_lang_in_list (state->active_langs, lang))
			continue;
		state->active_langs = g_slist_append (state->active_langs, g_strdup (lang));
	}

	return TRUE;
}

GSList *
sexy_spell_entry_get_active_languages (SexySpellEntry *entry)
{
	HcSpellState *state;
	GSList *copy = NULL;
	GSList *iter;

	state = spell_state_get (entry);
	for (iter = state->active_langs; iter; iter = iter->next)
		copy = g_slist_append (copy, g_strdup ((const char *) iter->data));
	return copy;
}

gboolean
sexy_spell_entry_is_checked (SexySpellEntry *entry)
{
	HcSpellState *state = spell_state_get (entry);
	return state ? state->checked : FALSE;
}

void
sexy_spell_entry_set_checked (SexySpellEntry *entry, gboolean checked)
{
	HcSpellState *state = spell_state_get (entry);
	if (state)
		state->checked = checked ? TRUE : FALSE;
}

void
sexy_spell_entry_set_parse_attributes (SexySpellEntry *entry, gboolean parse)
{
	HcSpellState *state = spell_state_get (entry);
	if (state)
		state->parseattr = parse ? TRUE : FALSE;
}

void
sexy_spell_entry_activate_default_languages (SexySpellEntry *entry)
{
	HcSpellState *state;
	gchar **parts;
	int i;

	state = spell_state_get (entry);
	if (!state)
		return;

	spell_state_clear_languages (state);

	parts = g_strsplit (prefs.hex_text_spell_langs, ",", -1);
	for (i = 0; parts && parts[i]; i++)
	{
		char *trimmed = g_strstrip (parts[i]);

		if (!trimmed[0] || spell_lang_in_list (state->active_langs, trimmed))
			continue;
		state->active_langs = g_slist_append (state->active_langs, g_strdup (trimmed));
	}
	g_strfreev (parts);

	if (!state->active_langs)
		state->active_langs = g_slist_append (state->active_langs, g_strdup ("en"));
}

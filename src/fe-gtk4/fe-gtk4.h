#ifndef HEXCHAT_FE_GTK4_H
#define HEXCHAT_FE_GTK4_H

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "../common/hexchat.h"
#include "../common/hexchatc.h"
#include "../common/cfgfiles.h"
#include "../common/outbound.h"
#include "../common/server.h"
#include "../common/servlist.h"
#include "../common/util.h"
#include "../common/fe.h"
#ifdef USE_PLUGIN
#include "../common/plugin.h"
#endif

#include "palette.h"
#include "pixmaps.h"

extern GMainLoop *frontend_loop;

extern GtkWidget *main_window;
extern GtkWidget *main_box;
extern GtkWidget *menu_bar;
extern GtkWidget *content_paned;
extern GtkWidget *session_scroller;
extern GtkWidget *session_list;
extern GtkWidget *log_view;
extern GtkTextBuffer *log_buffer;
extern GtkWidget *command_entry;
extern GSimpleActionGroup *window_actions;

void fe_gtk4_maingui_init (void);
void fe_gtk4_maingui_cleanup (void);
void fe_gtk4_create_main_window (void);
void fe_gtk4_adw_init (void);
GtkApplication *fe_gtk4_adw_application_new (void);
GtkWidget *fe_gtk4_adw_window_new (void);
void fe_gtk4_adw_window_set_content (GtkWidget *window, GtkWidget *content);
void fe_gtk4_adw_attach_menu_bar (GtkWidget *menu_widget);
void fe_gtk4_adw_detach_menu_bar (GtkWidget *menu_widget);
GtkApplication *fe_gtk4_get_application (void);
session *fe_gtk4_window_target_session (void);
void fe_gtk4_append_log_text (const char *text);
void fe_gtk4_session_sidebar_update_label (session *sess);

void fe_gtk4_chanview_init (void);
void fe_gtk4_chanview_cleanup (void);
GtkWidget *fe_gtk4_chanview_create_widget (void);
void fe_gtk4_chanview_add (session *sess);
void fe_gtk4_chanview_remove (session *sess);
void fe_gtk4_chanview_select (session *sess);
void fe_gtk4_chanview_set_layout (int layout);
int fe_gtk4_chanview_get_layout (void);

void fe_gtk4_xtext_init (void);
void fe_gtk4_xtext_cleanup (void);
GtkWidget *fe_gtk4_xtext_create_widget (void);
void fe_gtk4_xtext_append_for_session (session *sess, const char *text);
void fe_gtk4_xtext_show_session (session *sess);
void fe_gtk4_xtext_remove_session (session *sess);
void fe_gtk4_xtext_clear_session (session *sess, int lines);
const char *fe_gtk4_xtext_get_session_text (session *sess);
void fe_gtk4_xtext_apply_prefs (void);
void fe_gtk4_xtext_search_prompt (void);
void fe_gtk4_xtext_search_next (void);
void fe_gtk4_xtext_search_prev (void);

void fe_gtk4_userlist_init (void);
void fe_gtk4_userlist_cleanup (void);
GtkWidget *fe_gtk4_userlist_create_widget (void);
void fe_gtk4_userlist_show (session *sess);
void fe_gtk4_userlist_set_visible (gboolean visible);
gboolean fe_gtk4_userlist_get_visible (void);

void fe_gtk4_menu_init (void);
void fe_gtk4_menu_cleanup (void);
void fe_gtk4_menu_register_actions (void);
void fe_gtk4_rebuild_menu_bar (void);
void fe_gtk4_menu_sync_actions (void);

void fe_gtk4_maingui_set_menubar_visible (gboolean visible);
gboolean fe_gtk4_maingui_get_menubar_visible (void);
void fe_gtk4_topicbar_set_visible (gboolean visible);
gboolean fe_gtk4_topicbar_get_visible (void);
void fe_gtk4_maingui_set_fullscreen (gboolean fullscreen);
gboolean fe_gtk4_maingui_get_fullscreen (void);
void fe_gtk4_maingui_apply_input_font (void);

void setup_open (void);
void fe_gtk4_setup_open (void);
void fe_gtk4_setup_cleanup (void);

void editlist_gui_open (char *title1, char *title2, GSList *list, char *title,
	char *wmclass, char *file, char *help);

void key_init (void);
void key_dialog_show (void);
int key_handle_key_press (GtkWidget *wid, guint keyval, GdkModifierType state, session *sess);
int key_action_insert (GtkWidget *wid, guint keyval, GdkModifierType state,
	char *d1, char *d2, session *sess);

void pevent_dialog_show (void);

void plugingui_open (void);
void plugingui_load (void);
void fe_gtk4_plugingui_load_request (session *sess);

void fe_gtk4_servlistgui_cleanup (void);

void chanlist_opengui (server *serv, int do_refresh);
void fe_gtk4_chanlist_cleanup (void);

void ascii_open (void);
void banlist_opengui (session *sess);
void notify_opengui (void);
void ignore_gui_open (void);
void url_opengui (void);
void open_rawlog (server *serv);
void joind_open (server *serv);
void joind_close (server *serv);

void fe_gtk4_ascii_cleanup (void);
void fe_gtk4_banlist_cleanup (void);
void fe_gtk4_dccgui_cleanup (void);
void fe_gtk4_ignoregui_cleanup (void);
void fe_gtk4_notifygui_cleanup (void);
void fe_gtk4_rawlog_cleanup (void);
void fe_gtk4_urlgrab_cleanup (void);
void fe_gtk4_joind_cleanup (void);

void fe_gtk4_tray_init (void);
void fe_gtk4_tray_cleanup (void);
void fe_gtk4_tray_menu_emit_layout_updated (gint parent_id);
void fe_gtk4_tray_sync_window (void);

void fe_gtk4_pixmaps_cleanup (void);

#endif

/*
 * rb-audioscrobbler-profile-page.c
 *
 * Copyright (C) 2010 Jamie Nicol <jamie@thenicols.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <math.h>

#include <lib/eel-gconf-extensions.h>
#include <lib/gseal-gtk-compat.h>
#include <lib/rb-debug.h>
#include <lib/rb-builder-helpers.h>
#include <lib/rb-file-helpers.h>
#include <lib/rb-preferences.h>
#include <lib/rb-util.h>
#include <sources/rb-display-page-tree.h>
#include <sources/rb-display-page-group.h>

#include "rb-audioscrobbler-profile-page.h"
#include "rb-audioscrobbler.h"
#include "rb-audioscrobbler-account.h"
#include "rb-audioscrobbler-user.h"
#include "rb-audioscrobbler-radio-source.h"
#include "rb-audioscrobbler-radio-track-entry-type.h"

#define CONF_AUDIOSCROBBLER_ENABLE_SCROBBLING CONF_PLUGINS_PREFIX "/audioscrobbler/%s/scrobbling_enabled"
#define AUDIOSCROBBLER_PROFILE_PAGE_POPUP_PATH "/AudioscrobblerProfilePagePopup"

struct _RBAudioscrobblerProfilePagePrivate {
	RBAudioscrobblerService *service;
	RBAudioscrobblerAccount *account;
	RBAudioscrobbler *audioscrobbler;

	/* Used to request the user's profile data */
	RBAudioscrobblerUser *user;
	guint update_timeout_id;

	/* List of radio stations owned by this page */
	GList *radio_sources;

	guint scrobbling_enabled_notification_id;

	GtkWidget *main_vbox;

	/* Login related UI */
	GtkWidget *login_bar;
	GtkWidget *login_status_label;
	GtkWidget *login_response_button;

	/* Profile UI */
	GtkWidget *profile_window;

	GtkWidget *user_info_area;
	GtkWidget *profile_image;
	GtkWidget *username_label;
	GtkWidget *playcount_label;
	GtkWidget *scrobbling_enabled_check;
	GtkWidget *view_profile_link;

	/* Scrobbler statistics */
	GtkWidget *scrobbler_status_msg_label;
	GtkWidget *scrobbler_queue_count_label;
	GtkWidget *scrobbler_submit_count_label;
	GtkWidget *scrobbler_submit_time_label;

	/* Station creation UI */
	GtkWidget *station_creator_type_combo;
	GtkWidget *station_creator_arg_entry;

	/* Profile data lists */
	GtkWidget *recent_tracks_area;
	GtkWidget *recent_tracks_table;
	GtkWidget *top_tracks_area;
	GtkWidget *top_tracks_table;
	GtkWidget *loved_tracks_area;
	GtkWidget *loved_tracks_table;
	GtkWidget *top_artists_area;
	GtkWidget *top_artists_table;
	GtkWidget *recommended_artists_area;
	GtkWidget *recommended_artists_table;

	GHashTable *button_to_popup_menu_map;
	GHashTable *popup_menu_to_data_map;

	guint ui_merge_id;
	GtkActionGroup *profile_action_group;
	GtkActionGroup *service_action_group;
	char *love_action_name;
	char *ban_action_name;
	char *download_action_name;
};


static void rb_audioscrobbler_profile_page_class_init (RBAudioscrobblerProfilePageClass *klass);
static void rb_audioscrobbler_profile_page_init (RBAudioscrobblerProfilePage *page);
static void rb_audioscrobbler_profile_page_constructed (GObject *object);
static void rb_audioscrobbler_profile_page_dispose (GObject* object);
static void rb_audioscrobbler_profile_page_finalize (GObject *object);
static void rb_audioscrobbler_profile_page_get_property (GObject *object,
                                                         guint prop_id,
                                                         GValue *value,
                                                         GParamSpec *pspec);
static void rb_audioscrobbler_profile_page_set_property (GObject *object,
                                                         guint prop_id,
                                                         const GValue *value,
                                                         GParamSpec *pspec);

/* UI initialisation functions */
static void init_login_ui (RBAudioscrobblerProfilePage *page);
static void init_profile_ui (RBAudioscrobblerProfilePage *page);
static void init_actions (RBAudioscrobblerProfilePage *page);

/* login related callbacks */
static void login_bar_response_cb (GtkInfoBar *info_bar,
                                   gint response_id,
                                   RBAudioscrobblerProfilePage *page);
void logout_button_clicked_cb (GtkButton *button, RBAudioscrobblerProfilePage *page);
static void login_status_change_cb (RBAudioscrobblerAccount *account,
                                    RBAudioscrobblerAccountLoginStatus status,
                                    RBAudioscrobblerProfilePage *page);

/* scrobbling enabled preference */
void scrobbling_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                          RBAudioscrobblerProfilePage *page);
static void scrobbling_enabled_changed_cb (GConfClient *client,
                                           guint cnxn_id,
                                           GConfEntry *entry,
                                           RBAudioscrobblerProfilePage *page);

/* callbacks from scrobbler object */
static void scrobbler_authentication_error_cb (RBAudioscrobbler *audioscrobbler,
                                               RBAudioscrobblerProfilePage *page);
static void scrobbler_statistics_changed_cb (RBAudioscrobbler *audioscrobbler,
                                             const char *status_msg,
                                             guint queue_count,
                                             guint submit_count,
                                             const char *submit_time,
                                             RBAudioscrobblerProfilePage *page);

static void playing_song_changed_cb (RBShellPlayer *player,
                                     RhythmDBEntry *entry,
                                     RBAudioscrobblerProfilePage *page);
static void update_service_actions_sensitivity (RBAudioscrobblerProfilePage *page, RhythmDBEntry *entry);

/* GtkAction callbacks */
static void love_track_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page);
static void ban_track_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page);
static void download_track_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page);
static void download_track_batch_complete_cb (RBTrackTransferBatch *batch, RBAudioscrobblerProfilePage *page);
static void refresh_profile_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page);

/* radio station creation/deletion */
void station_creator_button_clicked_cb (GtkButton *button, RBAudioscrobblerProfilePage *page);
static void load_radio_stations (RBAudioscrobblerProfilePage *page);
static void save_radio_stations (RBAudioscrobblerProfilePage *page);
static RBSource *add_radio_station (RBAudioscrobblerProfilePage *page,
                                    const char *url,
                                    const char *name);
static void radio_station_name_changed_cb (RBAudioscrobblerRadioSource *radio,
                                           GParamSpec *spec,
                                           RBAudioscrobblerProfilePage *page);

/* periodically attempts tp update the profile data */
static gboolean update_timeout_cb (RBAudioscrobblerProfilePage *page);

/* callbacks from user profile data requests */
static void user_info_updated_cb (RBAudioscrobblerUser *user,
                                  RBAudioscrobblerUserData *info,
                                  RBAudioscrobblerProfilePage *page);
static void recent_tracks_updated_cb (RBAudioscrobblerUser *user,
                                      GPtrArray *recent_tracks,
                                      RBAudioscrobblerProfilePage *page);
static void top_tracks_updated_cb (RBAudioscrobblerUser *user,
                                   GPtrArray *top_tracks,
                                   RBAudioscrobblerProfilePage *page);
static void loved_tracks_updated_cb (RBAudioscrobblerUser *user,
                                     GPtrArray *loved_tracks,
                                     RBAudioscrobblerProfilePage *page);
static void top_artists_updated_cb (RBAudioscrobblerUser *user,
                                    GPtrArray *top_artists,
                                    RBAudioscrobblerProfilePage *page);
static void recommended_artists_updated_cb (RBAudioscrobblerUser *user,
                                            GPtrArray *recommended_artists,
                                            RBAudioscrobblerProfilePage *page);

/* UI creation for profile data lists, eg top artists, loved tracks */
static void set_user_list (RBAudioscrobblerProfilePage *page,
                           GtkWidget *list_table,
                           GPtrArray *list_data);
static GtkWidget *create_list_button (RBAudioscrobblerProfilePage *page,
                                      RBAudioscrobblerUserData *data,
                                      int max_sibling_image_width);
static GtkWidget *create_popup_menu (RBAudioscrobblerProfilePage *page,
                                     RBAudioscrobblerUserData *data);
static void list_table_pack_start (GtkTable *list_table, GtkWidget *child);
void list_table_realize_cb (GtkWidget *table,
                            gpointer user_data);
void list_table_size_allocate_cb (GtkWidget *layout,
                                  GtkAllocation *allocation,
                                  gpointer user_data);

/* callbacks from data list buttons and related popup menus */
static void list_item_clicked_cb (GtkButton *button, RBAudioscrobblerProfilePage *page);
static void list_item_view_url_activated_cb (GtkMenuItem *menuitem,
                                             RBAudioscrobblerProfilePage *page);
static void list_item_listen_similar_artists_activated_cb (GtkMenuItem *menuitem,
                                                           RBAudioscrobblerProfilePage *page);
static void list_item_listen_top_fans_activated_cb (GtkMenuItem *menuitem,
                                                    RBAudioscrobblerProfilePage *page);

/* RBDisplayPage implementations */
static void impl_selected (RBDisplayPage *page);
static void impl_deselected (RBDisplayPage *page);
static GList *impl_get_ui_actions (RBDisplayPage *page);
static gboolean impl_show_popup (RBDisplayPage *page);
static void impl_delete_thyself (RBDisplayPage *page);

enum {
	PROP_0,
	PROP_SERVICE
};

static GtkActionEntry profile_actions [] =
{
	{ "AudioscrobblerProfileRefresh", NULL, N_("Refresh Profile"), NULL,
	  N_("Refresh your Profile"),
	  G_CALLBACK (refresh_profile_action_cb) }
};


G_DEFINE_TYPE (RBAudioscrobblerProfilePage, rb_audioscrobbler_profile_page, RB_TYPE_DISPLAY_PAGE)

RBDisplayPage *
rb_audioscrobbler_profile_page_new (RBShell *shell, RBPlugin *plugin, RBAudioscrobblerService *service)
{
	RBDisplayPage *page;
	RhythmDB *db;
	char *name;
	gchar *icon_name;
	gchar *icon_path;
	gint icon_size;
	GdkPixbuf *icon_pixbuf;

	g_object_get (shell, "db", &db, NULL);
	g_object_get (service, "name", &name, NULL);

	icon_name = g_strconcat (rb_audioscrobbler_service_get_name (service), "-icon.png", NULL);
	icon_path = rb_plugin_find_file (plugin, icon_name);
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &icon_size, NULL);
	icon_pixbuf = gdk_pixbuf_new_from_file_at_size (icon_path, icon_size, icon_size, NULL);

	page = RB_DISPLAY_PAGE (g_object_new (RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE,
					      "shell", shell,
					      "plugin", plugin,
					      "name", name,
					      "pixbuf", icon_pixbuf,
					      "service", service,
					      NULL));

	g_object_unref (db);
	g_free (name);
	g_free (icon_name);
	g_free (icon_path);
	g_object_unref (icon_pixbuf);

	return page;
}

static void
rb_audioscrobbler_profile_page_class_init (RBAudioscrobblerProfilePageClass *klass)
{
	GObjectClass *object_class;
	RBDisplayPageClass *page_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = rb_audioscrobbler_profile_page_constructed;
	object_class->dispose = rb_audioscrobbler_profile_page_dispose;
	object_class->finalize = rb_audioscrobbler_profile_page_finalize;
	object_class->get_property = rb_audioscrobbler_profile_page_get_property;
	object_class->set_property = rb_audioscrobbler_profile_page_set_property;

	page_class = RB_DISPLAY_PAGE_CLASS (klass);
	page_class->selected = impl_selected;
	page_class->deselected = impl_deselected;
	page_class->get_ui_actions = impl_get_ui_actions;
	page_class->show_popup = impl_show_popup;
	page_class->delete_thyself = impl_delete_thyself;

	g_object_class_install_property (object_class,
	                                 PROP_SERVICE,
	                                 g_param_spec_object ("service",
	                                                      "Service",
	                                                      "Audioscrobbler service for this page",
	                                                      RB_TYPE_AUDIOSCROBBLER_SERVICE,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerProfilePagePrivate));
}

static void
rb_audioscrobbler_profile_page_init (RBAudioscrobblerProfilePage *page)
{
	page->priv = G_TYPE_INSTANCE_GET_PRIVATE (page, RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE, RBAudioscrobblerProfilePagePrivate);

	page->priv->button_to_popup_menu_map = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
	page->priv->popup_menu_to_data_map = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
rb_audioscrobbler_profile_page_constructed (GObject *object)
{
	RBAudioscrobblerProfilePage *page;
	RBShell *shell;
	char *scrobbling_enabled_conf_key;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_profile_page_parent_class, constructed, object);

	page = RB_AUDIOSCROBBLER_PROFILE_PAGE (object);
	g_object_get (page, "shell", &shell, NULL);

	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (page), RB_DISPLAY_PAGE_GROUP_LIBRARY);

	g_signal_connect_object (rb_shell_get_player (shell),
				 "playing-song-changed",
				 G_CALLBACK (playing_song_changed_cb),
				 page, 0);

	/* create the UI */
	page->priv->main_vbox = gtk_vbox_new (FALSE, 4);
	gtk_box_pack_start (GTK_BOX (page), page->priv->main_vbox, TRUE, TRUE, 0);
	gtk_widget_show (page->priv->main_vbox);

	init_login_ui (page);
	init_profile_ui (page);
	init_actions (page);

	/* create the user */
	page->priv->user = rb_audioscrobbler_user_new (page->priv->service);
	g_signal_connect (page->priv->user,
	                  "user-info-updated",
	                  G_CALLBACK (user_info_updated_cb),
	                  page);
	g_signal_connect (page->priv->user,
	                  "recent-tracks-updated",
	                  G_CALLBACK (recent_tracks_updated_cb),
	                  page);
	g_signal_connect (page->priv->user,
	                  "top-tracks-updated",
	                  G_CALLBACK (top_tracks_updated_cb),
	                  page);
	g_signal_connect (page->priv->user,
	                  "loved-tracks-updated",
	                  G_CALLBACK (loved_tracks_updated_cb),
	                  page);
	g_signal_connect (page->priv->user,
	                  "top-artists-updated",
	                  G_CALLBACK (top_artists_updated_cb),
	                  page);
	g_signal_connect (page->priv->user,
	                  "recommended-artists-updated",
	                  G_CALLBACK (recommended_artists_updated_cb),
	                  page);

	/* create the account */
	page->priv->account = rb_audioscrobbler_account_new (page->priv->service);
	g_signal_connect (page->priv->account,
	                  "login-status-changed",
	                  (GCallback)login_status_change_cb,
	                  page);
	login_status_change_cb (page->priv->account,
	                        rb_audioscrobbler_account_get_login_status (page->priv->account),
	                        page);

	/* scrobbling enabled gconf stuff */
	scrobbling_enabled_conf_key = g_strdup_printf (CONF_AUDIOSCROBBLER_ENABLE_SCROBBLING,
	                                               rb_audioscrobbler_service_get_name (page->priv->service));
	page->priv->scrobbling_enabled_notification_id =
		eel_gconf_notification_add (scrobbling_enabled_conf_key,
				            (GConfClientNotifyFunc) scrobbling_enabled_changed_cb,
				            page);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->priv->scrobbling_enabled_check),
	                              eel_gconf_get_boolean (scrobbling_enabled_conf_key));


	g_object_unref (shell);
	g_free (scrobbling_enabled_conf_key);
}

static void
rb_audioscrobbler_profile_page_dispose (GObject* object)
{
	RBAudioscrobblerProfilePage *page;

	page = RB_AUDIOSCROBBLER_PROFILE_PAGE (object);

	if (page->priv->service != NULL) {
		g_object_unref (page->priv->service);
		page->priv->service = NULL;
	}

	if (page->priv->audioscrobbler != NULL) {
		g_object_unref (page->priv->audioscrobbler);
		page->priv->audioscrobbler = NULL;
	}

	if (page->priv->account != NULL) {
		g_object_unref (page->priv->account);
		page->priv->account = NULL;
	}

	if (page->priv->user != NULL) {
		g_object_unref (page->priv->user);
		page->priv->user = NULL;
	}

	if (page->priv->scrobbling_enabled_notification_id != 0) {
		eel_gconf_notification_remove (page->priv->scrobbling_enabled_notification_id);
		page->priv->scrobbling_enabled_notification_id = 0;
	}

	if (page->priv->button_to_popup_menu_map != NULL) {
		g_hash_table_unref (page->priv->button_to_popup_menu_map);
		page->priv->button_to_popup_menu_map = NULL;
	}

	if (page->priv->popup_menu_to_data_map != NULL) {
		g_hash_table_unref (page->priv->popup_menu_to_data_map);
		page->priv->popup_menu_to_data_map = NULL;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_profile_page_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_profile_page_finalize (GObject *object)
{
	RBAudioscrobblerProfilePage *page;
	page = RB_AUDIOSCROBBLER_PROFILE_PAGE (object);

	g_free (page->priv->love_action_name);
	g_free (page->priv->ban_action_name);
	g_free (page->priv->download_action_name);

	G_OBJECT_CLASS (rb_audioscrobbler_profile_page_parent_class)->finalize (object);
}

static void
rb_audioscrobbler_profile_page_get_property (GObject *object,
                                               guint prop_id,
                                               GValue *value,
                                               GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_profile_page_set_property (GObject *object,
                                               guint prop_id,
                                               const GValue *value,
                                               GParamSpec *pspec)
{
	RBAudioscrobblerProfilePage *page = RB_AUDIOSCROBBLER_PROFILE_PAGE (object);
	switch (prop_id) {
	case PROP_SERVICE:
		page->priv->service = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
init_login_ui (RBAudioscrobblerProfilePage *page)
{
	GtkWidget *content_area;

	page->priv->login_bar = gtk_info_bar_new ();
	page->priv->login_status_label = gtk_label_new ("");
	page->priv->login_response_button = gtk_button_new ();
	content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (page->priv->login_bar));
	gtk_container_add (GTK_CONTAINER (content_area), page->priv->login_status_label);
	page->priv->login_response_button =
		gtk_info_bar_add_button (GTK_INFO_BAR (page->priv->login_bar),
		                         "", GTK_RESPONSE_OK);
	g_signal_connect (page->priv->login_bar,
	                  "response",
	                  G_CALLBACK (login_bar_response_cb),
	                  page);
	gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), page->priv->login_bar, FALSE, FALSE, 0);
}

static void
init_profile_ui (RBAudioscrobblerProfilePage *page)
{
	RBPlugin *plugin;
	char *builder_file;
	GtkBuilder *builder;
	GtkWidget *combo_container;
	int i;

	g_object_get (page, "plugin", &plugin, NULL);

	builder_file = rb_plugin_find_file (plugin, "audioscrobbler-profile.ui");
	g_assert (builder_file != NULL);
	builder = rb_builder_load (builder_file, page);

	page->priv->profile_window = GTK_WIDGET (gtk_builder_get_object (builder, "profile_window"));

	page->priv->user_info_area = GTK_WIDGET (gtk_builder_get_object (builder, "user_info_area"));
	page->priv->profile_image = GTK_WIDGET (gtk_builder_get_object (builder, "profile_image"));
	page->priv->username_label = GTK_WIDGET (gtk_builder_get_object (builder, "username_label"));
	page->priv->playcount_label = GTK_WIDGET (gtk_builder_get_object (builder, "playcount_label"));
	page->priv->scrobbling_enabled_check = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbling_enabled_check"));
	page->priv->view_profile_link = GTK_WIDGET (gtk_builder_get_object (builder, "view_profile_link"));

	/* scrobbler statistics */
	page->priv->scrobbler_status_msg_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_status_msg_label"));
	page->priv->scrobbler_queue_count_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_queue_count_label"));
	page->priv->scrobbler_submit_count_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_submit_count_label"));
	page->priv->scrobbler_submit_time_label = GTK_WIDGET (gtk_builder_get_object (builder, "scrobbler_submit_time_label"));

	/* station creator */
	page->priv->station_creator_arg_entry = GTK_WIDGET (gtk_builder_get_object (builder, "station_creator_arg_entry"));
	combo_container = GTK_WIDGET (gtk_builder_get_object (builder, "station_creator_combo_container"));
	page->priv->station_creator_type_combo = gtk_combo_box_text_new ();
	gtk_container_add (GTK_CONTAINER (combo_container), page->priv->station_creator_type_combo);
	for (i = 0; i < RB_AUDIOSCROBBLER_RADIO_TYPE_LAST; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (page->priv->station_creator_type_combo),
						rb_audioscrobbler_radio_type_get_text (i));
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (page->priv->station_creator_type_combo), 0);
	gtk_widget_show (page->priv->station_creator_type_combo);

	/* lists of data */
	page->priv->recent_tracks_area = GTK_WIDGET (gtk_builder_get_object (builder, "recent_tracks_area"));
	page->priv->recent_tracks_table = GTK_WIDGET (gtk_builder_get_object (builder, "recent_tracks_table"));

	page->priv->top_tracks_area = GTK_WIDGET (gtk_builder_get_object (builder, "top_tracks_area"));
	page->priv->top_tracks_table = GTK_WIDGET (gtk_builder_get_object (builder, "top_tracks_table"));

	page->priv->loved_tracks_area = GTK_WIDGET (gtk_builder_get_object (builder, "loved_tracks_area"));
	page->priv->loved_tracks_table = GTK_WIDGET (gtk_builder_get_object (builder, "loved_tracks_table"));

	page->priv->top_artists_area = GTK_WIDGET (gtk_builder_get_object (builder, "top_artists_area"));
	page->priv->top_artists_table = GTK_WIDGET (gtk_builder_get_object (builder, "top_artists_table"));

	page->priv->recommended_artists_area = GTK_WIDGET (gtk_builder_get_object (builder, "recommended_artists_area"));
	page->priv->recommended_artists_table = GTK_WIDGET (gtk_builder_get_object (builder, "recommended_artists_table"));

	/* pack profile into main vbox */
	gtk_box_pack_start (GTK_BOX (page->priv->main_vbox), page->priv->profile_window, TRUE, TRUE, 0);


	g_object_unref (plugin);
	g_free (builder_file);
	g_object_unref (builder);
}

static void
init_actions (RBAudioscrobblerProfilePage *page)
{
	char *ui_file;
	RBShell *shell;
	RBShellPlayer *player;
	RBPlugin *plugin;
	GtkUIManager *ui_manager;
	RhythmDBEntry *entry;
	char *group_name;

	g_object_get (page, "shell", &shell, "plugin", &plugin, "ui-manager", &ui_manager, NULL);
	ui_file = rb_plugin_find_file (plugin, "audioscrobbler-profile-ui.xml");
	page->priv->ui_merge_id = gtk_ui_manager_add_ui_from_file (ui_manager, ui_file, NULL);

	page->priv->profile_action_group = _rb_display_page_register_action_group (RB_DISPLAY_PAGE (page),
										   "AudioscrobblerProfileActions",
										   NULL, 0,
										   page);
	_rb_action_group_add_display_page_actions (page->priv->profile_action_group,
						   G_OBJECT (shell),
						   profile_actions,
						   G_N_ELEMENTS (profile_actions));

	/* Unfortunately we can't use the usual trick of declaring a static array of GtkActionEntry,
	 * and simply using _rb_source_register_action_group with that array.
	 * This is because each instance of this page needs its own love and ban actions
	 * so tracks can be loved/banned differently for different audioscrobbler services.
	 */
	group_name = g_strdup_printf ("%sActions", rb_audioscrobbler_service_get_name (page->priv->service));
	page->priv->love_action_name = g_strdup_printf ("%sLoveTrack", rb_audioscrobbler_service_get_name (page->priv->service));
	page->priv->ban_action_name = g_strdup_printf ("%sBanTrack", rb_audioscrobbler_service_get_name (page->priv->service));
	page->priv->download_action_name = g_strdup_printf ("%sDownloadTrack", rb_audioscrobbler_service_get_name (page->priv->service));

	GtkActionEntry service_actions [] =
	{
		{ page->priv->love_action_name, "emblem-favorite", N_("Love"), NULL,
		  N_("Mark this song as loved"),
		  G_CALLBACK (love_track_action_cb) },
		{ page->priv->ban_action_name, GTK_STOCK_CANCEL, N_("Ban"), NULL,
		  N_("Ban the current track from being played again"),
		  G_CALLBACK (ban_track_action_cb) },
		{ page->priv->download_action_name, GTK_STOCK_SAVE, N_("Download"), NULL,
		  N_("Download the currently playing track"),
		  G_CALLBACK (download_track_action_cb) }
	};

	page->priv->service_action_group = _rb_display_page_register_action_group (RB_DISPLAY_PAGE (page),
										   group_name,
										   service_actions,
										   G_N_ELEMENTS (service_actions),
										   page);
	g_object_get (shell, "shell-player", &player, NULL);
	entry = rb_shell_player_get_playing_entry (player);
	update_service_actions_sensitivity (page, entry);
	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}
	g_object_unref (player);

	g_free (ui_file);
	g_object_unref (shell);
	g_object_unref (plugin);
	g_object_unref (ui_manager);
	g_free (group_name);
}

static void
login_bar_response_cb (GtkInfoBar *info_bar,
                       gint response_id,
                       RBAudioscrobblerProfilePage *page)
{
	switch (rb_audioscrobbler_account_get_login_status (page->priv->account)) {
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT:
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_AUTH_ERROR:
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR:
		rb_audioscrobbler_account_authenticate (page->priv->account);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN:
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN:
		rb_audioscrobbler_account_logout (page->priv->account);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

void
logout_button_clicked_cb (GtkButton *button,
                          RBAudioscrobblerProfilePage *page)
{
	rb_audioscrobbler_account_logout (page->priv->account);
}

static void
login_status_change_cb (RBAudioscrobblerAccount *account,
                        RBAudioscrobblerAccountLoginStatus status,
                        RBAudioscrobblerProfilePage *page)
{
	const char *username;
	const char *session_key;
	char *scrobbling_enabled_conf_key;
	char *label_text = NULL;
	char *button_text = NULL;
	gboolean show_login_bar;
	gboolean show_profile;

	username = rb_audioscrobbler_account_get_username (page->priv->account);
	session_key = rb_audioscrobbler_account_get_session_key (page->priv->account);

	/* delete old scrobbler */
	if (page->priv->audioscrobbler != NULL) {
		g_object_unref (page->priv->audioscrobbler);
		page->priv->audioscrobbler = NULL;
	}

	/* create new scrobbler if new user has logged in and scrobbling is enabled */
	scrobbling_enabled_conf_key = g_strdup_printf (CONF_AUDIOSCROBBLER_ENABLE_SCROBBLING,
	                                               rb_audioscrobbler_service_get_name (page->priv->service));
	if (status == RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN &&
	    eel_gconf_get_boolean (scrobbling_enabled_conf_key)) {
		RBShell *shell;
		g_object_get (page, "shell", &shell, NULL);
		page->priv->audioscrobbler =
			rb_audioscrobbler_new (page->priv->service,
				               RB_SHELL_PLAYER (rb_shell_get_player (shell)),
                                               rb_audioscrobbler_account_get_username (page->priv->account),
			                       rb_audioscrobbler_account_get_session_key (page->priv->account));
		g_signal_connect (page->priv->audioscrobbler,
			          "authentication-error",
			          G_CALLBACK (scrobbler_authentication_error_cb),
			          page);
		g_signal_connect (page->priv->audioscrobbler,
			          "statistics-changed",
			          G_CALLBACK (scrobbler_statistics_changed_cb),
			          page);
		rb_audioscrobbler_statistics_changed (page->priv->audioscrobbler);
		g_object_unref (shell);
	}

	/* set the new user details */
	rb_audioscrobbler_user_set_authentication_details (page->priv->user, username, session_key);
	if (username != NULL) {
		rb_audioscrobbler_user_update (page->priv->user);
	}

	load_radio_stations (page);

	/* update the login ui */
	switch (status) {
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_OUT:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("You are not currently logged in."));
		button_text = g_strdup (_("Log in"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (page->priv->login_bar), GTK_MESSAGE_INFO);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGING_IN:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("Waiting for authentication..."));
		button_text = g_strdup (_("Cancel"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (page->priv->login_bar), GTK_MESSAGE_INFO);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_LOGGED_IN:
		show_login_bar = FALSE;
		show_profile = TRUE;
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_AUTH_ERROR:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("Authentication error. Please try logging in again."));
		button_text = g_strdup (_("Log in"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (page->priv->login_bar), GTK_MESSAGE_WARNING);
		break;
	case RB_AUDIOSCROBBLER_ACCOUNT_LOGIN_STATUS_CONNECTION_ERROR:
		show_login_bar = TRUE;
		show_profile = FALSE;
		label_text = g_strdup (_("Connection error. Please try logging in again."));
		button_text = g_strdup (_("Log in"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (page->priv->login_bar), GTK_MESSAGE_WARNING);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	gtk_label_set_label (GTK_LABEL (page->priv->login_status_label), label_text);
	gtk_button_set_label (GTK_BUTTON (page->priv->login_response_button), button_text);
	if (show_login_bar == TRUE) {
		gtk_widget_show_all (page->priv->login_bar);
	} else {
		gtk_widget_hide (page->priv->login_bar);
	}
	if (show_profile == TRUE) {
		gtk_label_set_label (GTK_LABEL (page->priv->username_label),
			             username);
		gtk_widget_show (page->priv->username_label);
		gtk_widget_show (page->priv->profile_window);
	} else {
		gtk_widget_hide (page->priv->profile_window);
	}

	g_free (scrobbling_enabled_conf_key);
	g_free (label_text);
	g_free (button_text);
}

void
scrobbling_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                     RBAudioscrobblerProfilePage *page)
{
	char *conf_key;

	conf_key = g_strdup_printf (CONF_AUDIOSCROBBLER_ENABLE_SCROBBLING,
	                            rb_audioscrobbler_service_get_name (page->priv->service));
	eel_gconf_set_boolean (conf_key,
			       gtk_toggle_button_get_active (togglebutton));
	g_free (conf_key);
}

static void
scrobbling_enabled_changed_cb (GConfClient *client,
                               guint cnxn_id,
                               GConfEntry *entry,
                               RBAudioscrobblerProfilePage *page)
{
	gboolean enabled = gconf_value_get_bool (entry->value);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->priv->scrobbling_enabled_check),
	                              enabled);

	if (page->priv->audioscrobbler != NULL && enabled == FALSE) {
		g_object_unref (page->priv->audioscrobbler);
		page->priv->audioscrobbler = NULL;
		gtk_label_set_label (GTK_LABEL (page->priv->scrobbler_status_msg_label),
		                     _("Disabled"));
	} else if (page->priv->audioscrobbler == NULL && enabled == TRUE) {
		RBShell *shell;
		g_object_get (page, "shell", &shell, NULL);
		page->priv->audioscrobbler =
			rb_audioscrobbler_new (page->priv->service,
				               RB_SHELL_PLAYER (rb_shell_get_player (shell)),
                                               rb_audioscrobbler_account_get_username (page->priv->account),
			                       rb_audioscrobbler_account_get_session_key (page->priv->account));
		g_signal_connect (page->priv->audioscrobbler,
			          "authentication-error",
			          G_CALLBACK (scrobbler_authentication_error_cb),
			          page);
		g_signal_connect (page->priv->audioscrobbler,
			          "statistics-changed",
			          G_CALLBACK (scrobbler_statistics_changed_cb),
			          page);
		rb_audioscrobbler_statistics_changed (page->priv->audioscrobbler);
		g_object_unref (shell);
	}
}

static void
scrobbler_authentication_error_cb (RBAudioscrobbler *audioscrobbler,
                                   RBAudioscrobblerProfilePage *page)
{
	rb_audioscrobbler_account_notify_of_auth_error (page->priv->account);
}

static void
scrobbler_statistics_changed_cb (RBAudioscrobbler *audioscrobbler,
                                 const char *status_msg,
                                 guint queue_count,
                                 guint submit_count,
                                 const char *submit_time,
                                 RBAudioscrobblerProfilePage *page)
{
	char *queue_count_text;
	char *submit_count_text;

	gtk_label_set_text (GTK_LABEL (page->priv->scrobbler_status_msg_label), status_msg);

	queue_count_text = g_strdup_printf ("%u", queue_count);
	gtk_label_set_text (GTK_LABEL (page->priv->scrobbler_queue_count_label), queue_count_text);

	submit_count_text = g_strdup_printf ("%u", submit_count);
	gtk_label_set_text (GTK_LABEL (page->priv->scrobbler_submit_count_label), submit_count_text);

	gtk_label_set_text (GTK_LABEL (page->priv->scrobbler_submit_time_label), submit_time);

	g_free (queue_count_text);
	g_free (submit_count_text);
}

static void
playing_song_changed_cb (RBShellPlayer *player,
                         RhythmDBEntry *entry,
                         RBAudioscrobblerProfilePage *page)
{
	update_service_actions_sensitivity (page, entry);
}

static void
update_service_actions_sensitivity (RBAudioscrobblerProfilePage *page, RhythmDBEntry *entry)
{
	GtkAction *love;
	GtkAction *ban;
	GtkAction *download;

	/* enable love/ban if an entry is playing */
	love = gtk_action_group_get_action (page->priv->service_action_group, page->priv->love_action_name);
	ban = gtk_action_group_get_action (page->priv->service_action_group, page->priv->ban_action_name);
	if (entry == NULL) {
		gtk_action_set_sensitive (love, FALSE);
		gtk_action_set_sensitive (ban, FALSE);
	} else {
		gtk_action_set_sensitive (love, TRUE);
		gtk_action_set_sensitive (ban, TRUE);
	}

	/* enable download if the playing entry is a radio track from this service which provides a download url */
	download = gtk_action_group_get_action (page->priv->service_action_group, page->priv->download_action_name);
	if (entry != NULL &&
	    rhythmdb_entry_get_entry_type (entry) == RHYTHMDB_ENTRY_TYPE_AUDIOSCROBBLER_RADIO_TRACK) {
		RBAudioscrobblerRadioTrackData *data;
		data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioscrobblerRadioTrackData);

		if (data->service == page->priv->service && data->download_url != NULL) {
			gtk_action_set_sensitive (download, TRUE);
		} else {
			gtk_action_set_sensitive (download, FALSE);
		}
	} else {
		gtk_action_set_sensitive (download, FALSE);
	}
}

static void
love_track_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page)
{
	RBShell *shell;
	RhythmDBEntry *playing;
	GtkAction *ban_action;

	g_object_get (page, "shell", &shell, NULL);
	playing = rb_shell_player_get_playing_entry (RB_SHELL_PLAYER (rb_shell_get_player (shell)));

	if (playing != NULL) {
		rb_audioscrobbler_user_love_track (page->priv->user,
			                           rhythmdb_entry_get_string (playing, RHYTHMDB_PROP_TITLE),
			                           rhythmdb_entry_get_string (playing, RHYTHMDB_PROP_ARTIST));
		rhythmdb_entry_unref (playing);
	}

	/* disable love/ban */
	gtk_action_set_sensitive (action, FALSE);
	ban_action = gtk_action_group_get_action (page->priv->service_action_group, page->priv->ban_action_name);
	gtk_action_set_sensitive (ban_action, FALSE);

	g_object_unref (shell);
}

static void
ban_track_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page)
{
	RBShell *shell;
	RhythmDBEntry *playing;

	g_object_get (page, "shell", &shell, NULL);
	playing = rb_shell_player_get_playing_entry (RB_SHELL_PLAYER (rb_shell_get_player (shell)));

	if (playing != NULL) {
		rb_audioscrobbler_user_ban_track (page->priv->user,
			                          rhythmdb_entry_get_string (playing, RHYTHMDB_PROP_TITLE),
			                          rhythmdb_entry_get_string (playing, RHYTHMDB_PROP_ARTIST));
		rhythmdb_entry_unref (playing);
	}

	/* skip to next track */
	rb_shell_player_do_next (RB_SHELL_PLAYER (rb_shell_get_player (shell)), NULL);

	g_object_unref (shell);
}

static void
download_track_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page)
{
	RBShell *shell;
	RhythmDBEntry *playing;

	/* disable the action */
	gtk_action_set_sensitive (action, FALSE);

	g_object_get (page, "shell", &shell, NULL);
	playing = rb_shell_player_get_playing_entry (RB_SHELL_PLAYER (rb_shell_get_player (shell)));

	if (playing != NULL &&
	    rhythmdb_entry_get_entry_type (playing) == RHYTHMDB_ENTRY_TYPE_AUDIOSCROBBLER_RADIO_TRACK) {
		RBAudioscrobblerRadioTrackData *data;
		data = RHYTHMDB_ENTRY_GET_TYPE_DATA (playing, RBAudioscrobblerRadioTrackData);

		if (data->download_url != NULL) {
			RhythmDB *db;
			RBSource *library;
			RhythmDBEntry *download;
			GValue val = { 0, };
			RBTrackTransferBatch *batch;

			/* we need the library source to paste into */
			g_object_get (shell, "db", &db, "library-source", &library, NULL);

			/* create a new entry to paste */
			download = rhythmdb_entry_new (db,
			                               RHYTHMDB_ENTRY_TYPE_AUDIOSCROBBLER_RADIO_TRACK, /* not really, but it needs a type */
			                               data->download_url);

			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, rhythmdb_entry_get_string (playing, RHYTHMDB_PROP_TITLE));
			rhythmdb_entry_set (db, download, RHYTHMDB_PROP_TITLE, &val);
			g_value_unset (&val);
			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, rhythmdb_entry_get_string (playing, RHYTHMDB_PROP_ARTIST));
			rhythmdb_entry_set (db, download, RHYTHMDB_PROP_ARTIST, &val);
			g_value_unset (&val);
			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, rhythmdb_entry_get_string (playing, RHYTHMDB_PROP_ALBUM));
			rhythmdb_entry_set (db, download, RHYTHMDB_PROP_ALBUM, &val);
			g_value_unset (&val);

			rb_debug ("downloading track from %s", data->download_url);
			batch = rb_source_paste (library, g_list_append (NULL, download));

			if (batch == NULL) {
				/* delete the entry we just created */
				rhythmdb_entry_delete (db, download);
				rhythmdb_entry_unref (download);
			} else {
				/* connect a callback to delete the entry when transfer is done */
				g_signal_connect_object (batch,
				                         "complete",
				                         G_CALLBACK (download_track_batch_complete_cb),
				                         page,
				                         0);
			}

			g_object_unref (db);
			g_object_unref (library);
		} else {
			rb_debug ("cannot download: no download url");
		}
		rhythmdb_entry_unref (playing);
	} else {
		rb_debug ("cannot download: playing entry is not an audioscrobbler radio track");
	}

	g_object_unref (shell);
}

static void
download_track_batch_complete_cb (RBTrackTransferBatch *batch,
                                  RBAudioscrobblerProfilePage *page)
{
	GList *entries;
	RBShell *shell;
	RhythmDB *db;
	GList *i;

	g_object_get (batch, "entry-list", &entries, NULL);
	g_object_get (page, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	/* delete the entries which were transfered.
	 * need to call unref twice as g_object_get will have reffed them
	 */
	for (i = entries; i != NULL; i = i->next) {
		rhythmdb_entry_delete (db, i->data);
		rhythmdb_entry_unref (i->data);
		rhythmdb_entry_unref (i->data);
	}

	g_list_free (entries);
	g_object_unref (shell);
	g_object_unref (db);
}

static void
refresh_profile_action_cb (GtkAction *action, RBAudioscrobblerProfilePage *page)
{
	rb_audioscrobbler_user_force_update (page->priv->user);
}

void
station_creator_button_clicked_cb (GtkButton *button,
                                   RBAudioscrobblerProfilePage *page)
{
	const char *arg;

	arg = gtk_entry_get_text (GTK_ENTRY (page->priv->station_creator_arg_entry));

	if (arg[0] != '\0') {
		RBAudioscrobblerRadioType type;
		char *url;
		char *name;
		RBSource *radio;
		RBShell *shell;
		RBDisplayPageTree *page_tree;

		type = gtk_combo_box_get_active (GTK_COMBO_BOX (page->priv->station_creator_type_combo));

		url = g_strdup_printf (rb_audioscrobbler_radio_type_get_url (type),
		                       arg);
		name = g_strdup_printf (rb_audioscrobbler_radio_type_get_default_name (type),
		                        arg);

		radio = add_radio_station (page, url, name);
		g_object_get (page, "shell", &shell, NULL);
		g_object_get (shell, "display-page-tree", &page_tree, NULL);
		rb_display_page_tree_select (page_tree, RB_DISPLAY_PAGE (radio));

		gtk_entry_set_text (GTK_ENTRY (page->priv->station_creator_arg_entry), "");

		g_free (url);
		g_free (name);
		g_object_unref (shell);
		g_object_unref (page_tree);
	}
}

/* delete old user's radio sources and load ones for new user */
static void
load_radio_stations (RBAudioscrobblerProfilePage *page)
{
	/* destroy existing sources */
	while (page->priv->radio_sources != NULL) {
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (page->priv->radio_sources->data));
		page->priv->radio_sources = g_list_remove (page->priv->radio_sources, page->priv->radio_sources->data);
	}

	/* load the user's saved stations */
	if (rb_audioscrobbler_account_get_username (page->priv->account) != NULL) {
		JsonParser *parser;
		char *filename;

		parser = json_parser_new ();
		filename = g_build_filename (rb_user_data_dir (),
		                             "audioscrobbler",
		                             "stations",
		                             rb_audioscrobbler_service_get_name (page->priv->service),
		                             rb_audioscrobbler_account_get_username (page->priv->account),
		                             NULL);

		if (json_parser_load_from_file (parser, filename, NULL)) {
			JsonArray *stations;
			int i;

			stations = json_node_get_array (json_parser_get_root (parser));

			for (i = 0; i < json_array_get_length (stations); i++) {
				JsonObject *station;
				const char *name;
				const char *url;
				RBSource *radio;

				station = json_array_get_object_element (stations, i);
				name = json_object_get_string_member (station, "name");
				url = json_object_get_string_member (station, "url");

				radio = rb_audioscrobbler_radio_source_new (page,
				                                            page->priv->service,
				                                            rb_audioscrobbler_account_get_username (page->priv->account),
				                                            rb_audioscrobbler_account_get_session_key (page->priv->account),
				                                            name,
				                                            url);
				page->priv->radio_sources = g_list_append (page->priv->radio_sources, radio);
				g_signal_connect (radio, "notify::name",
						  G_CALLBACK (radio_station_name_changed_cb),
						  page);
			}
		}

		/* if the list of stations is still empty then add some defaults */
		if (page->priv->radio_sources == NULL) {
			char *url;
			char *name;

			/* user's library */
			url = g_strdup_printf (rb_audioscrobbler_radio_type_get_url (RB_AUDIOSCROBBLER_RADIO_TYPE_LIBRARY),
			                       rb_audioscrobbler_account_get_username (page->priv->account));
			name = g_strdup (_("My Library"));
			add_radio_station (page, url, name);
			g_free (url);
			g_free (name);

			/* user's recommendations */
			url = g_strdup_printf (rb_audioscrobbler_radio_type_get_url (RB_AUDIOSCROBBLER_RADIO_TYPE_RECOMMENDATION),
			                       rb_audioscrobbler_account_get_username (page->priv->account));
			name = g_strdup (_("My Recommendations"));
			add_radio_station (page, url, name);
			g_free (url);
			g_free (name);

			/* user's neighbourhood */
			url = g_strdup_printf (rb_audioscrobbler_radio_type_get_url (RB_AUDIOSCROBBLER_RADIO_TYPE_NEIGHBOURS),
			                       rb_audioscrobbler_account_get_username (page->priv->account));
			name = g_strdup (_("My Neighbourhood"));
			add_radio_station (page, url, name);
			g_free (url);
			g_free (name);

			/* rhythmbox group */
			url = g_strdup_printf (rb_audioscrobbler_radio_type_get_url (RB_AUDIOSCROBBLER_RADIO_TYPE_GROUP),
			                       "rhythmbox");
			name = g_strdup_printf (rb_audioscrobbler_radio_type_get_default_name (RB_AUDIOSCROBBLER_RADIO_TYPE_GROUP),
			                       "Rhythmbox");
			add_radio_station (page, url, name);
			g_free (url);
			g_free (name);
		}

		g_object_unref (parser);
		g_free (filename);
	}
}

/* save user's radio stations */
static void
save_radio_stations (RBAudioscrobblerProfilePage *page)
{
	JsonNode *root;
	JsonArray *stations;
	GList *i;
	JsonGenerator *generator;
	char *filename;
	char *uri;
	GError *error;

	root = json_node_new (JSON_NODE_ARRAY);
	stations = json_array_new ();

	for (i = page->priv->radio_sources; i != NULL; i = i->next) {
		JsonObject *station;
		char *name;
		char *url;

		g_object_get (i->data, "name", &name, "station-url", &url, NULL);
		station = json_object_new ();
		json_object_set_string_member (station, "name", name);
		json_object_set_string_member (station, "url", url);
		json_array_add_object_element (stations, station);

		g_free (name);
		g_free (url);
	}

	json_node_take_array (root, stations);

	generator = json_generator_new ();
	json_generator_set_root (generator, root);

	filename = g_build_filename (rb_user_data_dir (),
	                             "audioscrobbler",
	                             "stations",
	                             rb_audioscrobbler_service_get_name (page->priv->service),
	                             rb_audioscrobbler_account_get_username (page->priv->account),
	                             NULL);

	uri = g_filename_to_uri (filename, NULL, NULL);
	error = NULL;
	rb_uri_create_parent_dirs (uri, &error);
	json_generator_to_file (generator, filename, NULL);

	json_node_free (root);
	g_object_unref (generator);
	g_free (filename);
	g_free (uri);
}

/* adds a new radio station for the user, if it doesn't already exist */
static RBSource *
add_radio_station (RBAudioscrobblerProfilePage *page,
                   const char *url,
                   const char *name)
{
	GList *i;
	RBSource *radio = NULL;

	/* check for existing station */
	for (i = page->priv->radio_sources; i != NULL; i = i->next) {
		char *existing_url;
		g_object_get (i->data, "station-url", &existing_url, NULL);

		if (strcmp (existing_url, url) == 0) {
			radio = i->data;
		}

		g_free (existing_url);
	}

	if (radio == NULL) {
		const char *username;
		const char *session_key;
		RBShell *shell;

		username = rb_audioscrobbler_account_get_username (page->priv->account);
		session_key = rb_audioscrobbler_account_get_session_key (page->priv->account);
		g_object_get (page, "shell", &shell, NULL);

		radio = rb_audioscrobbler_radio_source_new (page,
		                                            page->priv->service,
		                                            username,
		                                            session_key,
		                                            name,
		                                            url);
		page->priv->radio_sources = g_list_append (page->priv->radio_sources, radio);
		g_signal_connect (radio, "notify::name",
		                  G_CALLBACK (radio_station_name_changed_cb),
		                  page);
		save_radio_stations (page);

		g_object_unref (shell);
	}

	return radio;
}

/* called when a radio station's name changes */
static void
radio_station_name_changed_cb (RBAudioscrobblerRadioSource *radio,
                               GParamSpec *spec,
                               RBAudioscrobblerProfilePage *page)
{
	/* save list of stations with new name */
	save_radio_stations (page);
}

/* removes a station from user's list of radio stations, deletes the source */
void
rb_audioscrobbler_profile_page_remove_radio_station (RBAudioscrobblerProfilePage *page,
						     RBSource *station)
{
	GList *i;

	i = g_list_find (page->priv->radio_sources, station);

	if (i != NULL) {
		rb_display_page_delete_thyself (i->data);
		page->priv->radio_sources = g_list_remove (page->priv->radio_sources, i->data);
		save_radio_stations (page);
	}
}

static gboolean
update_timeout_cb (RBAudioscrobblerProfilePage *page)
{
	rb_audioscrobbler_user_update (page->priv->user);

	return TRUE;
}

static void
user_info_updated_cb (RBAudioscrobblerUser *user,
                      RBAudioscrobblerUserData *data,
                      RBAudioscrobblerProfilePage *page)
{
	if (data != NULL) {
		char *playcount_text;

		gtk_label_set_label (GTK_LABEL (page->priv->username_label),
			             data->user_info.username);
		gtk_widget_show (page->priv->username_label);

		playcount_text = g_strdup_printf (_("%s plays"), data->user_info.playcount);
		gtk_label_set_label (GTK_LABEL (page->priv->playcount_label),
		                     playcount_text);
		g_free (playcount_text);
		gtk_widget_show (page->priv->playcount_label);

		gtk_link_button_set_uri (GTK_LINK_BUTTON (page->priv->view_profile_link),
		                         data->url);
		gtk_widget_show (page->priv->view_profile_link);

		if (data->image != NULL) {
			gtk_image_set_from_pixbuf (GTK_IMAGE (page->priv->profile_image), data->image);
			/* show the parent because the image is packed in a viewport so it has a shadow */
			gtk_widget_show (gtk_widget_get_parent (page->priv->profile_image));
		} else {
			gtk_widget_hide (gtk_widget_get_parent (page->priv->profile_image));
		}
	} else {
		gtk_widget_hide (page->priv->playcount_label);
		gtk_widget_hide (page->priv->view_profile_link);
		gtk_widget_hide (gtk_widget_get_parent (page->priv->profile_image));
	}
}

static void
recent_tracks_updated_cb (RBAudioscrobblerUser *user,
                          GPtrArray *recent_tracks,
                          RBAudioscrobblerProfilePage *page)
{
	set_user_list (page, page->priv->recent_tracks_table, recent_tracks);

	if (recent_tracks != NULL && recent_tracks->len != 0) {
		gtk_widget_show_all (page->priv->recent_tracks_area);
	} else {
		gtk_widget_hide (page->priv->recent_tracks_area);
	}
}

static void
top_tracks_updated_cb (RBAudioscrobblerUser *user,
                       GPtrArray *top_tracks,
                       RBAudioscrobblerProfilePage *page)
{
	set_user_list (page, page->priv->top_tracks_table, top_tracks);

	if (top_tracks != NULL && top_tracks->len != 0) {
		gtk_widget_show_all (page->priv->top_tracks_area);
	} else {
		gtk_widget_hide (page->priv->top_tracks_area);
	}
}

static void
loved_tracks_updated_cb (RBAudioscrobblerUser *user,
                         GPtrArray *loved_tracks,
                         RBAudioscrobblerProfilePage *page)
{
	set_user_list (page, page->priv->loved_tracks_table, loved_tracks);

	if (loved_tracks != NULL && loved_tracks->len != 0) {
		gtk_widget_show_all (page->priv->loved_tracks_area);
	} else {
		gtk_widget_hide (page->priv->loved_tracks_area);
	}
}

static void
top_artists_updated_cb (RBAudioscrobblerUser *user,
                        GPtrArray *top_artists,
                        RBAudioscrobblerProfilePage *page)
{
	set_user_list (page, page->priv->top_artists_table, top_artists);

	if (top_artists != NULL && top_artists->len != 0) {
		gtk_widget_show_all (page->priv->top_artists_area);
	} else {
		gtk_widget_hide (page->priv->top_artists_area);
	}
}

static void
recommended_artists_updated_cb (RBAudioscrobblerUser *user,
                                GPtrArray *recommended_artists,
                                RBAudioscrobblerProfilePage *page)
{
	set_user_list (page, page->priv->recommended_artists_table, recommended_artists);

	if (recommended_artists != NULL && recommended_artists->len != 0) {
		gtk_widget_show_all (page->priv->recommended_artists_area);
	} else {
		gtk_widget_hide (page->priv->recommended_artists_area);
	}
}

/* Creates a list of buttons packed in a table for a list of data
 * eg user's top tracks or recommended artists
 */
static void
set_user_list (RBAudioscrobblerProfilePage *page,
               GtkWidget *list_table,
               GPtrArray *list_data)
{
	GList *button_node;

	/* delete all existing buttons */
	for (button_node = gtk_container_get_children (GTK_CONTAINER (list_table));
	     button_node != NULL;
	     button_node = g_list_next (button_node)) {
		GtkMenu *menu;
		menu = g_hash_table_lookup (page->priv->button_to_popup_menu_map, button_node->data);
		g_hash_table_remove (page->priv->button_to_popup_menu_map, button_node->data);
		g_hash_table_remove (page->priv->popup_menu_to_data_map, menu);
		gtk_widget_destroy (button_node->data);
	}

	if (list_data != NULL) {
		int i;
		int max_image_width;

		if (gtk_widget_get_realized (list_table) == FALSE) {
			rb_debug ("table has not been realized yet. it will need resized later");
		}

		/* get the width of the widest image */
		max_image_width = 0;
		for (i = 0; i < list_data->len; i++) {
			RBAudioscrobblerUserData *data;

			data = g_ptr_array_index (list_data, i);
			if (data->image != NULL) {
				int width = gdk_pixbuf_get_width (data->image);
				max_image_width = MAX (max_image_width, width);
			}

		}

		/* add a new button for each item in the list */
		for (i = 0; i < list_data->len; i++) {
			RBAudioscrobblerUserData *data;
			GtkWidget *button;
			GtkWidget *menu;

			data = g_ptr_array_index (list_data, i);
			button = create_list_button (page, data, max_image_width);
			menu = create_popup_menu (page, data);

			g_hash_table_insert (page->priv->button_to_popup_menu_map, button, g_object_ref_sink (menu));
			g_hash_table_insert (page->priv->popup_menu_to_data_map, menu, data);

			list_table_pack_start (GTK_TABLE (list_table), button);
		}
	}
}

/* creates a button for use in a list */
static GtkWidget *
create_list_button (RBAudioscrobblerProfilePage *page,
                    RBAudioscrobblerUserData *data,
                    int max_sibling_image_width)
{
	GtkWidget *button;
	GtkWidget *button_contents;
	char *button_markup;
	int label_indent;
	GtkWidget *label;
	GtkWidget *label_alignment;

	button = gtk_button_new ();
	gtk_button_set_alignment (GTK_BUTTON (button),
		                  0, 0.5);
	gtk_button_set_focus_on_click (GTK_BUTTON (button),
		                       FALSE);
	gtk_button_set_relief (GTK_BUTTON (button),
		               GTK_RELIEF_NONE);

	button_contents = gtk_hbox_new (FALSE, 4);
	gtk_container_add (GTK_CONTAINER (button), button_contents);

	if (data->image != NULL) {
		GtkWidget *image;
		GtkWidget *viewport;
		GtkWidget *alignment;

		image = gtk_image_new_from_pixbuf (data->image);

		viewport = gtk_viewport_new (NULL, NULL);
		gtk_container_add (GTK_CONTAINER (viewport), image);

		alignment = gtk_alignment_new (0, 0.5, 0, 0);
		gtk_container_add (GTK_CONTAINER (alignment), viewport);

		gtk_box_pack_start (GTK_BOX (button_contents),
		                    alignment,
		                    FALSE, FALSE, 0);

		label_indent = max_sibling_image_width - gdk_pixbuf_get_width (data->image);
	} else {
		label_indent = 4;
	}

	button_markup = NULL;
	if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK) {
		char *escaped_title_text;
		char *escaped_artist_text;

		escaped_title_text = g_markup_escape_text (data->track.title, -1);
		escaped_artist_text = g_markup_escape_text (data->track.artist, -1);
		button_markup = g_strdup_printf ("%s\n<small>%s</small>",
			                         escaped_title_text,
			                         escaped_artist_text);

		g_free (escaped_title_text);
		g_free (escaped_artist_text);

	} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
		button_markup = g_markup_escape_text (data->artist.name, -1);
	}

	label = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (label), button_markup);
	g_free (button_markup);

	label_alignment = gtk_alignment_new (0, 0.5, 0, 0);
	gtk_container_add (GTK_CONTAINER (label_alignment), label);

	gtk_alignment_set_padding (GTK_ALIGNMENT (label_alignment),
	                           0, 0,
	                           label_indent, 0);

	gtk_box_pack_start (GTK_BOX (button_contents),
	                    label_alignment,
	                    FALSE, FALSE, 0);

	g_signal_connect (button,
		          "clicked",
		          G_CALLBACK (list_item_clicked_cb),
		          page);

	return button;
}

/* creates a menu to be popped up when a button is clicked */
static GtkWidget *
create_popup_menu (RBAudioscrobblerProfilePage *page,
                   RBAudioscrobblerUserData *data)
{
	GtkWidget *menu;

	menu = gtk_menu_new ();

	/* Visit on website */
	if (data->url != NULL && data->url[0] != '\0') {
		GtkWidget *view_url_item;
		char *item_text;

		/* Translators: %s is the name of the audioscrobbler service, for example "Last.fm".
		 * This is the label for menu item which when activated will take the user to the
		 * artist/track's page on the service's website. */
		item_text = g_strdup_printf (_("_View on %s"),
		                             rb_audioscrobbler_service_get_name (page->priv->service));
		view_url_item = gtk_menu_item_new_with_mnemonic (item_text);
		g_signal_connect (view_url_item,
				  "activate",
				  G_CALLBACK (list_item_view_url_activated_cb),
				  page);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), view_url_item);
		g_free (item_text);
	}

	/* Similar artists radio */
	if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK ||
	    data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
		GtkWidget *similar_artists_item;

		similar_artists_item = gtk_menu_item_new_with_mnemonic (("Listen to _Similar Artists Radio"));
		g_signal_connect (similar_artists_item,
				  "activate",
				  G_CALLBACK (list_item_listen_similar_artists_activated_cb),
				  page);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), similar_artists_item);
	}

	/* Top fans radio */
	if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK ||
	    data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
		GtkWidget *top_fans_item;

		top_fans_item = gtk_menu_item_new_with_mnemonic (("Listen to _Top Fans Radio"));
		g_signal_connect (top_fans_item,
				  "activate",
				  G_CALLBACK (list_item_listen_top_fans_activated_cb),
				  page);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), top_fans_item);
	}

	gtk_widget_show_all (menu);

	return menu;
}

/* packs a widget into a GtkTable, from left to right then top to bottom */
static void
list_table_pack_start (GtkTable *list_table, GtkWidget *child)
{
	GList *children;
	int num_children;
	int num_columns;

	children = gtk_container_get_children (GTK_CONTAINER (list_table));
	num_children = g_list_length (children);
	g_object_get (list_table, "n-columns", &num_columns, NULL);

	gtk_table_attach (list_table,
	                  child,
	                  num_children % num_columns, num_children % num_columns + 1,
	                  num_children / num_columns, num_children / num_columns + 1,
	                  GTK_FILL | GTK_EXPAND, GTK_FILL,
	                  0, 0);

	g_list_free (children);
}

void
list_table_realize_cb (GtkWidget *table,
                       gpointer user_data)
{
	rb_debug ("table has been realized. queueing resize");
	gtk_widget_queue_resize (table);
}

/* resizes a GtkTable for a particular size allocation */
void
list_table_size_allocate_cb (GtkWidget *table,
                             GtkAllocation *allocation,
                             gpointer user_data)
{
	GList *children;
	int num_children;
	int child_width;
	GList *i;
	int current_num_columns;
	int spacing;
	int new_num_columns;

	children = gtk_container_get_children (GTK_CONTAINER (table));
	num_children = g_list_length (children);
	if (num_children == 0)
		return;

	/* find the desired width of the widest child */
	child_width = 1;
	for (i = children; i != NULL; i = i->next) {
		GtkRequisition child_requisition;

		gtk_widget_size_request (i->data, &child_requisition);
		if (child_requisition.width > child_width) {
			child_width = child_requisition.width;
		}
	}

	g_object_get (table, "n-columns", &current_num_columns, NULL);

	/* calculate the number of columns there should be */
	spacing = gtk_table_get_default_col_spacing (GTK_TABLE (table));
	new_num_columns = allocation->width / (child_width + spacing);
	if (new_num_columns == 0) {
		new_num_columns = 1;
	}

	/* if there's a change in the number of columns we need to move children around */
	if (new_num_columns != current_num_columns) {
		int new_num_rows;

		new_num_rows = (int)ceil ((double)num_children / (double)new_num_columns);

		/* remove each child from the table, reffing it first so that it is not destroyed */
		for (i = children; i != NULL; i = i->next) {
			g_object_ref (i->data);
			gtk_container_remove (GTK_CONTAINER (table), i->data);
		}

		/* resize the table */
		gtk_table_resize (GTK_TABLE (table), new_num_columns, new_num_rows);

		/* Don't know why, but g_table_resize doesn't always update these properties properly.
		 * Looking at gtktable.c this is even stranger, as setting either of these properties
		 * will simply call gtk_table_resize which should then set the values.
		 * Perhaps worthwhile looking into in the future, but this works for now.
		 * Possibly useful to note that AppResizer in libslab stores its own value for the number of columns
		 * instead of using the table's n-columns property, perhaps as a workaround to this.
		 * So does Banshee's TileView, which appears to be a C# port of libslab's code */
		g_object_set (table, "n-columns", new_num_columns, "n-rows", new_num_rows, NULL);

		/* re-attach each child to the table */
		for (i = g_list_last (children); i != NULL; i = i->prev) {

			list_table_pack_start (GTK_TABLE (table), i->data);
			g_object_unref (i->data);
		}
	}

	/* ensure the table will shrink to the correct size */
	gtk_widget_set_size_request (table, 0, -1);

	g_list_free (children);
}

/* popup the appropriate menu */
static void
list_item_clicked_cb (GtkButton *button, RBAudioscrobblerProfilePage *page)
{
	GtkWidget *menu;

	menu = g_hash_table_lookup (page->priv->button_to_popup_menu_map, button);

	/* show menu if it has any items in it */
	if (g_list_length (gtk_container_get_children (GTK_CONTAINER (menu))) != 0) {
		gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time ());
	}
}

static void
list_item_view_url_activated_cb (GtkMenuItem *menuitem,
                                 RBAudioscrobblerProfilePage *page)
{
	GtkWidget *menu;
	RBAudioscrobblerUserData *data;

	menu = gtk_widget_get_parent (GTK_WIDGET (menuitem));
	data = g_hash_table_lookup (page->priv->popup_menu_to_data_map, menu);

	/* some urls are given to us without the http:// prefix */
	if (g_str_has_prefix (data->url, "http://") == TRUE) {
		gtk_show_uri (NULL, data->url, GDK_CURRENT_TIME, NULL);
	} else {
		char *url;
		url = g_strdup_printf ("%s%s", "http://", data->url);
		gtk_show_uri (NULL, url, GDK_CURRENT_TIME, NULL);
		g_free (url);
	}
}

static void
list_item_listen_similar_artists_activated_cb (GtkMenuItem *menuitem,
                                               RBAudioscrobblerProfilePage *page)
{
	GtkWidget *menu;
	RBAudioscrobblerUserData *data;
	const char *artist = NULL;
	char *radio_url;
	char *radio_name;
	RBSource *radio;
	RBShell *shell;
	RBDisplayPageTree *page_tree;

	menu = gtk_widget_get_parent (GTK_WIDGET (menuitem));
	data = g_hash_table_lookup (page->priv->popup_menu_to_data_map, menu);
	if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
		artist = data->artist.name;
	} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK) {
		artist = data->track.artist;
	}

	radio_url = g_strdup_printf (rb_audioscrobbler_radio_type_get_url (RB_AUDIOSCROBBLER_RADIO_TYPE_SIMILAR_ARTISTS),
	                             artist);
	radio_name = g_strdup_printf (rb_audioscrobbler_radio_type_get_default_name (RB_AUDIOSCROBBLER_RADIO_TYPE_SIMILAR_ARTISTS),
	                              artist);

	radio = add_radio_station (page, radio_url, radio_name);
	g_object_get (page, "shell", &shell, NULL);
	g_object_get (shell, "display-page-tree", &page_tree, NULL);
	rb_display_page_tree_select (page_tree, RB_DISPLAY_PAGE (radio));

	g_free (radio_url);
	g_free (radio_name);
	g_object_unref (shell);
	g_object_unref (page_tree);
}

static void
list_item_listen_top_fans_activated_cb (GtkMenuItem *menuitem,
                                        RBAudioscrobblerProfilePage *page)
{
	GtkWidget *menu;
	RBAudioscrobblerUserData *data;
	const char *artist = NULL;
	char *radio_url;
	char *radio_name;
	RBSource *radio;
	RBShell *shell;
	RBDisplayPageTree *page_tree;

	menu = gtk_widget_get_parent (GTK_WIDGET (menuitem));
	data = g_hash_table_lookup (page->priv->popup_menu_to_data_map, menu);
	if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
		artist = data->artist.name;
	} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK) {
		artist = data->track.artist;
	}

	radio_url = g_strdup_printf (rb_audioscrobbler_radio_type_get_url (RB_AUDIOSCROBBLER_RADIO_TYPE_TOP_FANS),
	                             artist);
	radio_name = g_strdup_printf (rb_audioscrobbler_radio_type_get_default_name (RB_AUDIOSCROBBLER_RADIO_TYPE_TOP_FANS),
	                              artist);

	radio = add_radio_station (page, radio_url, radio_name);
	g_object_get (page, "shell", &shell, NULL);
	g_object_get (shell, "display-page-tree", &page_tree, NULL);
	rb_display_page_tree_select (page_tree, RB_DISPLAY_PAGE (radio));

	g_free (radio_url);
	g_free (radio_name);
	g_object_unref (shell);
	g_object_unref (page_tree);
}

static void
impl_selected (RBDisplayPage *bpage)
{
	RBAudioscrobblerProfilePage *page = RB_AUDIOSCROBBLER_PROFILE_PAGE (bpage);

	/* attempt to update now and again every 5 minutes */
	rb_audioscrobbler_user_update (page->priv->user);
	page->priv->update_timeout_id = g_timeout_add_seconds (300,
							       (GSourceFunc) update_timeout_cb,
							       page);
}

static void
impl_deselected (RBDisplayPage *bpage)
{
	RBAudioscrobblerProfilePage *page = RB_AUDIOSCROBBLER_PROFILE_PAGE (bpage);

	g_source_remove (page->priv->update_timeout_id);
	page->priv->update_timeout_id = 0;
}

static GList *
impl_get_ui_actions (RBDisplayPage *bpage)
{
	RBAudioscrobblerProfilePage *page = RB_AUDIOSCROBBLER_PROFILE_PAGE (bpage);
	GList *actions = NULL;

	actions = g_list_append (actions, g_strdup (page->priv->love_action_name));
	actions = g_list_append (actions, g_strdup (page->priv->ban_action_name));
	actions = g_list_append (actions, g_strdup (page->priv->download_action_name));

	return actions;
}

static gboolean
impl_show_popup (RBDisplayPage *page)
{
	_rb_display_page_show_popup (page, AUDIOSCROBBLER_PROFILE_PAGE_POPUP_PATH);
	return TRUE;
}

static void
impl_delete_thyself (RBDisplayPage *bpage)
{
	RBAudioscrobblerProfilePage *page;
	GList *i;
	GtkUIManager *ui_manager;

	rb_debug ("deleting profile page");

	page = RB_AUDIOSCROBBLER_PROFILE_PAGE (bpage);

	for (i = page->priv->radio_sources; i != NULL; i = i->next) {
		rb_display_page_delete_thyself (i->data);
	}

	g_object_get (page, "ui-manager", &ui_manager, NULL);
	gtk_ui_manager_remove_ui (ui_manager, page->priv->ui_merge_id);
	gtk_ui_manager_remove_action_group (ui_manager, page->priv->service_action_group);

	g_object_unref (ui_manager);
}
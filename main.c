/* main.c - This file is part of the xmpp-shell program.
 *
 * Copyright (C) 2009  Lincoln de Sousa <lincoln@minaslivre.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#include <strophe.h>

#define _(x) x

typedef struct _ToolBar {
  GtkWidget *toolbar;
  GtkToolItem *undo;
  GtkToolItem *redo;
  GtkToolItem *indent;
  GtkToolItem *send;
} ToolBar;

typedef struct _UiInfo {
  ToolBar *toolbar;
  GtkWidget *window;
  GtkWidget *send;
  GtkWidget *receive;
  GtkWidget *jid;
  GtkWidget *passwd;
  GtkWidget *connect_btn;
} UiInfo;

typedef struct _XsCtx {
  UiInfo *ui;
  char *jid_str;
  char *passwd_str;
  xmpp_ctx_t *xmpp;
  xmpp_conn_t *conn;
} XsCtx;

static void connect (XsCtx *ctx);
static void reconnect (XsCtx *ctx);
static void send (XsCtx *ctx);
static gboolean enable_widgets (XsCtx *ctx);
static gboolean disable_widgets (XsCtx *ctx);

static int
quit (GtkWidget *widget, GdkEvent *event, XsCtx *ctx) {
  if (ctx->jid_str)
    {
      g_free (ctx->jid_str);
      ctx->jid_str = NULL;
    }

  if (ctx->passwd_str)
    {
      g_free (ctx->passwd_str);
      ctx->passwd_str = NULL;
    }

  gtk_main_quit ();
  xmpp_stop (ctx->xmpp);
  xmpp_shutdown();

  if (ctx->ui)
    g_slice_free (UiInfo, ctx->ui);
  if (ctx)
    g_slice_free (XsCtx, ctx);
  return TRUE;
}

static void
set_connect_btn_sensitivity (XsCtx *ctx)
{
  gboolean sensitive = TRUE;
  char *text;
  char *stripped;

  text = g_strdup (gtk_entry_get_text (GTK_ENTRY (ctx->ui->jid)));
  stripped = g_strstrip (text);
  if (g_strcmp0 (stripped, "") == 0)
    sensitive = FALSE;
  g_free (text);

  text = g_strdup (gtk_entry_get_text (GTK_ENTRY (ctx->ui->passwd)));
  stripped = g_strstrip (text);
  if (g_strcmp0 (stripped, "") == 0)
    sensitive = FALSE;
  g_free (text);

  gtk_widget_set_sensitive (ctx->ui->connect_btn, sensitive);
}

static int
jid_changed (GtkEntry *entry, gpointer userdata)
{
  XsCtx *ctx = (XsCtx *) userdata;
  g_free (ctx->jid_str);
  ctx->jid_str = g_strdup (gtk_entry_get_text (entry));
  set_connect_btn_sensitivity (ctx);
  return TRUE;
}

static int
passwd_changed (GtkEntry *entry, gpointer userdata)
{
  XsCtx *ctx = (XsCtx *) userdata;
  g_free (ctx->passwd_str);
  ctx->passwd_str = g_strdup (gtk_entry_get_text (entry));
  set_connect_btn_sensitivity (ctx);
  return TRUE;
}

static GtkWidget *
login_form (XsCtx *ctx)
{
  GtkWidget *datavbox;
  GtkWidget *label_jid, *label_passwd;
  GtkWidget *hbox_jid, *hbox_passwd;
  GtkWidget *hbbox;

  datavbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

  hbox_jid = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (datavbox), hbox_jid, FALSE, FALSE, 0);

  label_jid = gtk_label_new ("JID:");
  g_object_set (G_OBJECT (label_jid), "xalign", 0, NULL);
  gtk_widget_set_size_request (label_jid, 110, -1);
  gtk_box_pack_start (GTK_BOX (hbox_jid), label_jid, FALSE, FALSE, 0);

  ctx->ui->jid = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox_jid), ctx->ui->jid, TRUE, TRUE, 0);

  hbox_passwd = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (datavbox), hbox_passwd, FALSE, FALSE, 0);

  label_passwd = gtk_label_new ("Password:");
  g_object_set (G_OBJECT (label_passwd), "xalign", 0, NULL);
  gtk_widget_set_size_request (label_passwd, 110, -1);
  gtk_box_pack_start (GTK_BOX (hbox_passwd), label_passwd, FALSE, FALSE, 0);

  ctx->ui->passwd = gtk_entry_new ();
  gtk_entry_set_visibility (GTK_ENTRY (ctx->ui->passwd), FALSE);
  gtk_box_pack_start (GTK_BOX (hbox_passwd), ctx->ui->passwd, TRUE, TRUE, 0);

  hbbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_box_pack_start (GTK_BOX (datavbox), hbbox, FALSE, FALSE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (hbbox), GTK_BUTTONBOX_END);

  ctx->ui->connect_btn = gtk_button_new_with_label (_("Connect"));
  gtk_widget_set_sensitive (ctx->ui->connect_btn, FALSE);
  gtk_box_pack_start (GTK_BOX (hbbox), ctx->ui->connect_btn, FALSE, FALSE, 0);
  g_signal_connect_swapped (ctx->ui->connect_btn, "clicked",
                            G_CALLBACK (reconnect), ctx);

  return datavbox;
}

static GtkToolItem *
toolitem (const gchar *icon_name, const gchar *label)
{
  GtkWidget *icon;
  icon = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
  return gtk_tool_button_new (icon, label);
}

static GtkWidget *
toolbar_widget (XsCtx *ctx)
{
  GtkToolItem *sep;

  /* Tool bar */

  ctx->ui->toolbar->toolbar = gtk_toolbar_new ();

  ctx->ui->toolbar->undo = toolitem ("edit-undo", _("Undo"));
  gtk_toolbar_insert (GTK_TOOLBAR (ctx->ui->toolbar->toolbar),
                      ctx->ui->toolbar->undo, 0);

  ctx->ui->toolbar->redo = toolitem ("edit-redo", _("Redo"));
  gtk_toolbar_insert (GTK_TOOLBAR (ctx->ui->toolbar->toolbar),
                      ctx->ui->toolbar->redo, 1);

  ctx->ui->toolbar->indent = toolitem ("format-indent-more", _("Indent"));
  gtk_toolbar_insert (GTK_TOOLBAR (ctx->ui->toolbar->toolbar),
                      ctx->ui->toolbar->indent, 2);

  sep = gtk_separator_tool_item_new ();
  gtk_toolbar_insert (GTK_TOOLBAR (ctx->ui->toolbar->toolbar), sep, 3);

  ctx->ui->toolbar->send = toolitem ("media-playback-start", _("Run"));
  gtk_toolbar_insert (GTK_TOOLBAR (ctx->ui->toolbar->toolbar),
                      ctx->ui->toolbar->send, 4);
  g_signal_connect_swapped (ctx->ui->toolbar->send, "clicked",
                            G_CALLBACK (send), ctx);

  return ctx->ui->toolbar->toolbar;
}

static void
setup_ui (XsCtx *ctx)
{
  GtkWidget *vbox;
  GtkWidget *sw, *sw2;
  GtkWidget *vpaned;
  GtkSourceBuffer *buff, *buff1;
  GtkSourceLanguageManager *langmanager;
  GtkSourceLanguage *lang;

  /* Main window and vbox */

  ctx->ui->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (ctx->ui->window), 480, 380);
  gtk_container_set_border_width (GTK_CONTAINER (ctx->ui->window), 10);
  g_signal_connect (ctx->ui->window, "delete-event", G_CALLBACK (quit), ctx);
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (ctx->ui->window), vbox);

  /* Some externaly defined widgets */

  gtk_box_pack_start (GTK_BOX (vbox), login_form (ctx), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), toolbar_widget (ctx), FALSE, FALSE, 0);

  /* Send source view */

  langmanager = gtk_source_language_manager_get_default ();
  lang = gtk_source_language_manager_get_language (langmanager, "xml");

  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
                                       GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  buff = gtk_source_buffer_new_with_language (lang);
  ctx->ui->send = gtk_source_view_new_with_buffer (buff);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (ctx->ui->send),
                               GTK_WRAP_WORD);
  gtk_container_add (GTK_CONTAINER (sw), ctx->ui->send);

  /* Receive textview */

  sw2 = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw2),
                                       GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw2),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  buff1 = gtk_source_buffer_new_with_language (lang);
  ctx->ui->receive = gtk_source_view_new_with_buffer (buff1);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (ctx->ui->receive),
                               GTK_WRAP_WORD);
  gtk_text_view_set_editable (GTK_TEXT_VIEW (ctx->ui->receive), FALSE);
  gtk_container_add (GTK_CONTAINER (sw2), ctx->ui->receive);

  vpaned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
  gtk_paned_add1 (GTK_PANED (vpaned), sw);
  gtk_paned_add2 (GTK_PANED (vpaned), sw2);
  gtk_box_pack_start (GTK_BOX (vbox), vpaned, TRUE, TRUE, 0);

  /* Setting entries stuff */

  if (ctx->jid_str != NULL)
    gtk_entry_set_text (GTK_ENTRY (ctx->ui->jid), ctx->jid_str);
  if (ctx->passwd_str != NULL)
    gtk_entry_set_text (GTK_ENTRY (ctx->ui->passwd), ctx->passwd_str);

  disable_widgets (ctx);
  gtk_widget_show_all (ctx->ui->window);
}

static gboolean
enable_widgets (XsCtx *ctx)
{
  GtkToolbar *tb;
  gint len, i;
  gtk_widget_set_sensitive (ctx->ui->send, TRUE);
  gtk_widget_set_sensitive (ctx->ui->receive, TRUE);
  tb = GTK_TOOLBAR (ctx->ui->toolbar->toolbar);
  len = gtk_toolbar_get_n_items (tb);
  for (i = 0; i < len; i++)
    gtk_widget_set_sensitive (GTK_WIDGET (gtk_toolbar_get_nth_item (tb, i)),
                              TRUE);
  return FALSE;
}

static gboolean
disable_widgets (XsCtx *ctx)
{
  GtkToolbar *tb;
  gint len, i;
  gtk_widget_set_sensitive (ctx->ui->send, FALSE);
  gtk_widget_set_sensitive (ctx->ui->receive, FALSE);
  tb = GTK_TOOLBAR (ctx->ui->toolbar->toolbar);
  len = gtk_toolbar_get_n_items (tb);
  for (i = 0; i < len; i++)
    gtk_widget_set_sensitive (GTK_WIDGET (gtk_toolbar_get_nth_item (tb, i)),
                              FALSE);
  return FALSE;
}

int reply_handler(xmpp_conn_t * const conn,
                  xmpp_stanza_t * const stanza,
                  void * const userdata)
{
  char *text;
  size_t len;
  GtkTextBuffer *buffer;
  XsCtx *ctx = (XsCtx *) userdata;
  xmpp_stanza_to_text (stanza, &text, &len);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (ctx->ui->receive));
  gtk_text_buffer_set_text (buffer, text, len);
  g_free (text);
  return 1;
}

static void conn_handler(xmpp_conn_t * const conn,
                         const xmpp_conn_event_t status,
                         const int error,
                         xmpp_stream_error_t * const stream_error,
                         void * const userdata) {
  if (status == XMPP_CONN_CONNECT)
    {
      XsCtx *ctx = (XsCtx *) userdata;
      g_idle_add ((GSourceFunc) enable_widgets, ctx);
      xmpp_handler_add (ctx->conn, reply_handler, NULL, NULL, NULL, ctx);
    }
}

static void
connect (XsCtx *ctx)
{
  if (!ctx->jid_str && !ctx->passwd_str)
    return;
  ctx->conn = xmpp_conn_new (ctx->xmpp);
  xmpp_conn_set_jid (ctx->conn, ctx->jid_str);
  xmpp_conn_set_pass (ctx->conn, ctx->passwd_str);
  xmpp_connect_client (ctx->conn, NULL, 0, conn_handler, ctx);
}

static void
reconnect (XsCtx *ctx)
{
  g_idle_add ((GSourceFunc) disable_widgets, ctx);
  if (ctx->conn)
    {
      xmpp_disconnect (ctx->conn);
      xmpp_conn_release (ctx->conn);
      ctx->conn = NULL;
    }
  connect (ctx);
}

static void
send (XsCtx *ctx)
{
  GtkTextBuffer *buffer;
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (ctx->ui->send));
  if (gtk_text_buffer_get_has_selection (buffer))
    {
      GtkTextIter start, end;
      gchar *text = NULL;
      gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
      text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
      if (text)
        {
          xmpp_send_raw_string (ctx->conn, text);
          g_free (text);
        }
    }
}

static gpointer
run_xmpp_stuff (gpointer userdata)
{
  xmpp_log_t *log;
  XsCtx *ctx;
  ctx = (XsCtx *) userdata;
  log = xmpp_get_default_logger (XMPP_LEVEL_DEBUG);
  ctx->xmpp = xmpp_ctx_new (NULL, log);
  connect (ctx);
  xmpp_run (ctx->xmpp);
  return GINT_TO_POINTER (1);
}

int
main (int argc, char **argv)
{
  XsCtx *ctx;
  gtk_init (&argc, &argv);
  xmpp_initialize ();

  ctx = g_slice_new (XsCtx);
  ctx->ui = g_slice_new (UiInfo);
  ctx->ui->toolbar = g_slice_new (ToolBar);

  ctx->jid_str = NULL;
  if (argc > 1)
    ctx->jid_str = g_strdup (argv[1]);

  ctx->passwd_str = NULL;
  if (argc > 2)
    ctx->passwd_str = g_strdup (argv[2]);

  setup_ui (ctx);

  /* setting up signals */
  g_signal_connect (ctx->ui->jid, "changed", G_CALLBACK (jid_changed), ctx);
  g_signal_connect (ctx->ui->passwd, "changed", G_CALLBACK (passwd_changed), ctx);

  g_thread_new ("xmpp", run_xmpp_stuff, ctx);
  gtk_main ();
  return 0;
}

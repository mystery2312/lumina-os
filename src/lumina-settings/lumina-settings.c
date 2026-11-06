/*
 * lumina-settings : l'appli Reglages de LuminaOS.
 *
 * Meme mise en page que Fichiers (navigation a gauche, contenu a droite,
 * fenetre aux quatre coins arrondis). Chaque section lit l'etat reel du
 * systeme a l'ouverture ; les actions passent par les outils du systeme
 * (iwctl, bluetoothctl, wpctl, brightnessctl) ou par les programmes
 * LuminaOS en administrateur (pkexec lumina-account, lumina-android...).
 *
 * Usage : lumina-settings [section]   (reseau, bluetooth, affichage, ecrans,
 *         son, clavier, souris, compte, linux, android, imprimantes, heure,
 *         energie, accessibilite, raccourcis, maj, stockage, apropos)
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <glib/gstdio.h>
#include <math.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-settings.csd { border-radius: 0 0 16px 16px; background: @surface; }\n"
  "window.lumina-settings.csd decoration { border-radius: 16px;\n"
  "  box-shadow: 0 6px 22px alpha(black, 0.18), 0 0 0 1px alpha(black, 0.06); }\n"
  "window.lumina-settings.maximized, window.lumina-settings.tiled,\n"
  "window.lumina-settings.maximized decoration, window.lumina-settings.tiled decoration { border-radius: 0; }\n"
  ".header { background: @surface; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  "window.maximized .header, window.tiled .header { border-radius: 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 32px; min-height: 32px; color: @on_var; }\n"
  ".header button.titlebutton:hover { background: alpha(@on, 0.08); }\n"
  ".apptitle { font-size: 16px; color: @on; padding-left: 8px; }\n"
  ".nav { background: @surface; padding: 4px 12px 12px 12px; border-radius: 0 0 0 16px; }\n"
  "window.maximized .nav, window.tiled .nav { border-radius: 0; }\n"
  ".nav list, .nav row, .nav scrolledwindow, .nav viewport { background: transparent; }\n"
  ".navsearch { background: @c_high; color: @on; border: none; box-shadow: none; border-radius: 20px; min-height: 38px;\n"
  "             padding: 0 12px; margin: 4px 0 8px 0; caret-color: @pri; }\n"
  ".nav row { border-radius: 24px; padding: 0; margin: 1px 0; }\n"
  ".nav row:hover { background: alpha(@on, 0.06); }\n"
  ".nav row:selected { background: @sec_c; color: @on_sec_c; }\n"
  ".nav .item { padding: 10px 14px; }\n"
  ".nav .item label { font-size: 13.5px; }\n"
  ".content { background: @c_low; border-radius: 20px 0 16px 0; }\n"
  "window.maximized .content, window.tiled .content { border-radius: 20px 0 0 0; }\n"
  ".page { padding: 24px 32px 32px 32px; }\n"
  ".h1 { font-size: 26px; color: @on; }\n"
  ".h2 { font-size: 13px; font-weight: 500; color: @pri; margin-top: 10px; }\n"
  ".group { background: @surface; border-radius: 20px; padding: 4px 0; }\n"
  ".row { padding: 12px 20px; min-height: 28px; }\n"
  ".row .t { font-size: 14px; color: @on; }\n"
  ".row .s { font-size: 12.5px; color: @on_var; }\n"
  ".sep { background: @c_top; min-height: 1px; margin: 0 20px; }\n"
  ".rowbtn { background: transparent; border-radius: 0; padding: 0; }\n"
  ".rowbtn:hover { background: alpha(@on, 0.04); }\n"
  ".tbtn { background: transparent; color: @pri; border-radius: 20px; padding: 8px 16px; font-weight: 500; }\n"
  ".tbtn:hover { background: alpha(@pri, 0.08); }\n"
  ".fbtn { background: @pri; color: @on_pri; border-radius: 20px; padding: 8px 18px; font-weight: 500; }\n"
  ".fbtn:disabled { background: alpha(@on, 0.12); color: alpha(@on, 0.38); }\n"
  ".fbtn.danger { background: #ba1a1a; color: #ffffff; }\n"
  ".tbtn.danger { color: #ba1a1a; }\n"
  ".kbd { background: @c_high; color: @on; border-radius: 8px; padding: 4px 10px; font-size: 12.5px; font-weight: 500; }\n"
  ".swatch { background: transparent; padding: 0; border-radius: 22px; min-width: 44px; min-height: 44px; }\n"
  ".swatch:hover { background: alpha(@on, 0.06); }\n"
  ".swatches { padding: 4px 14px 14px 14px; }\n"
  ".wallstyle { background: transparent; border-radius: 14px; padding: 4px; }\n"
  ".wallstyle:hover { background: alpha(@on, 0.06); }\n"
  ".wallprev { border-radius: 12px; }\n"
  ".field { background: @c_low; color: @on; border: 1px solid @outline_var; border-radius: 8px; padding: 9px 12px;\n"
  "         box-shadow: none; caret-color: @pri; }\n"
  ".field:focus { border: 2px solid @pri; padding: 8px 11px; }\n"
  ".msg { font-size: 12.5px; color: @pri; }\n"
  ".msg.err { color: #ba1a1a; }\n"
  ".opt { background: @c_low; border-radius: 14px; padding: 10px 14px; color: @on; }\n"
  ".opt:hover { background: @c; }\n"
  ".opt:checked { background: @sec_c; color: @on_sec_c; }\n"
  ".opt .s { font-size: 12px; color: @on_var; }\n"
  "switch { background: @c_top; border: 2px solid @outline_var; border-radius: 16px; min-height: 24px; }\n"
  "switch:checked { background: @pri; border-color: @pri; }\n"
  "switch slider { background: @outline_var; border: none; border-radius: 12px; min-width: 20px; min-height: 20px; margin: 2px; box-shadow: none; }\n"
  "switch:checked slider { background: @on_pri; }\n"
  "scale trough { background: @c_top; min-height: 6px; border-radius: 3px; border: none; }\n"
  "scale highlight { background: @pri; border-radius: 3px; border: none; }\n"
  "scale slider { background: @pri; min-width: 18px; min-height: 18px; border-radius: 9px; border: none; box-shadow: none; margin: -6px; }\n"
  "levelbar trough { background: @c_top; border: none; border-radius: 4px; min-height: 8px; padding: 0; }\n"
  "levelbar block.filled { background: @pri; border-radius: 4px; border: none; }\n"
  "levelbar block.empty { background: transparent; border: none; }\n"
  "combobox button { background: @c_low; border: 1px solid @outline_var; border-radius: 8px; color: @on; }\n";

static GtkWidget *win, *nav, *stack;
/* settings-more.c */
static void updates_more(GtkWidget *p);
static void about_more(GtkWidget *p);
static void linux_more(GtkWidget *grp, gboolean installed);
static void fill_screens(void);
static void fill_printers(void);

/* ------------------------------------------------------------------ */
/* Briques d'interface                                                 */
/* ------------------------------------------------------------------ */
static GtkWidget *page_box(const char *id, const char *title) {
  GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  GtkWidget *p = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  add_class(p, "page");
  gtk_widget_set_size_request(p, 520, -1);
  gtk_container_add(GTK_CONTAINER(p), label(title, "h1", 0));
  gtk_container_add(GTK_CONTAINER(sc), p);
  gtk_stack_add_named(GTK_STACK(stack), sc, id);
  return p;
}

static GtkWidget *group(GtkWidget *page, const char *heading) {
  if (heading) gtk_container_add(GTK_CONTAINER(page), label(heading, "h2", 0));
  GtkWidget *g = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(g, "group");
  gtk_container_add(GTK_CONTAINER(page), g);
  return g;
}

/* Ligne : titre (+ sous-titre) a gauche, widget a droite. sub_out : label
   du sous-titre, a mettre a jour. */
static GtkWidget *row(GtkWidget *grp, const char *title, const char *sub, GtkWidget *end, GtkWidget **sub_out) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(grp));
  if (kids) { GtkWidget *s = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); add_class(s, "sep"); gtk_container_add(GTK_CONTAINER(grp), s); }
  g_list_free(kids);
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  add_class(h, "row");
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand(v, TRUE);
  gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
  GtkWidget *t = label(title, "t", 0);
  gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
  gtk_container_add(GTK_CONTAINER(v), t);
  GtkWidget *s = label(sub ? sub : "", "s", 0);
  gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(s), 46);   /* sinon la fenetre s'elargit au texte */
  gtk_widget_set_no_show_all(s, sub == NULL && sub_out == NULL);
  gtk_container_add(GTK_CONTAINER(v), s);
  gtk_container_add(GTK_CONTAINER(h), v);
  if (end) { gtk_widget_set_valign(end, GTK_ALIGN_CENTER); gtk_container_add(GTK_CONTAINER(h), end); }
  gtk_container_add(GTK_CONTAINER(grp), h);
  if (sub_out) *sub_out = s;
  return h;
}

/* Paragraphe explicatif, replie a la largeur de la page */
static GtkWidget *desc(const char *t) {
  GtkWidget *l = label(t, "s", 0);
  gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(l), 60);
  return l;
}

static GtkWidget *button(const char *t, const char *cls, GCallback cb, gpointer d) {
  GtkWidget *b = gtk_button_new_with_label(t);
  add_class(b, cls);
  if (cb) g_signal_connect(b, "clicked", cb, d);
  return b;
}

static GtkWidget *field(const char *ph, gboolean secret) {
  GtkWidget *e = gtk_entry_new();
  add_class(e, "field");
  gtk_entry_set_placeholder_text(GTK_ENTRY(e), ph);
  gtk_entry_set_visibility(GTK_ENTRY(e), !secret);
  return e;
}

static void say(GtkWidget *msg, const char *text, gboolean error) {
  gtk_label_set_text(GTK_LABEL(msg), text);
  set_class(msg, "err", error);
  gtk_widget_show(msg);
}

static void clear_box(GtkWidget *b) {
  GList *k = gtk_container_get_children(GTK_CONTAINER(b));
  for (GList *l = k; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(k);
}

/* Commande en administrateur via pkexec, avec une entree eventuelle.
   Asynchrone : la fenetre reste reactive pendant que pkexec et la
   commande travaillent (plusieurs secondes sur une cle USB lente : figee,
   la fenetre donnait l'impression d'un plantage). done() est appele avec
   le resultat ; le message d'erreur lui appartient le temps de l'appel. */
typedef void (*RootDone)(gboolean ok, const char *err, gpointer d);
typedef struct { RootDone done; gpointer d; } RootCall;
static void root_finished(GObject *o, GAsyncResult *res, gpointer p) {
  RootCall *c = p;
  GSubprocess *sp = G_SUBPROCESS(o);
  char *errs = NULL; GError *err = NULL;
  gboolean ok = g_subprocess_communicate_utf8_finish(sp, res, NULL, &errs, &err) && g_subprocess_get_successful(sp);
  c->done(ok, ok ? NULL : (errs && *g_strstrip(errs) ? errs : "Opération refusée."), c->d);
  g_free(errs); g_clear_error(&err); g_object_unref(sp); g_free(c);
}
static void run_root_async(const char *const *argv, const char *input, RootDone done, gpointer d) {
  GError *err = NULL;
  GSubprocess *sp = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                      G_SUBPROCESS_FLAGS_STDERR_PIPE, &err);
  if (!sp) { done(FALSE, err->message, d); g_clear_error(&err); return; }
  RootCall *c = g_new0(RootCall, 1); c->done = done; c->d = d;
  g_subprocess_communicate_utf8_async(sp, input, NULL, root_finished, c);
}

/* Bouton occupe pendant une operation : grise, texte d'attente. */
static void busy(GtkWidget *b, const char *text) {
  const char *cur = gtk_button_get_label(GTK_BUTTON(b));
  if (text) {
    g_object_set_data_full(G_OBJECT(b), "idle-label", g_strdup(cur), g_free);
    gtk_button_set_label(GTK_BUTTON(b), text);
    gtk_widget_set_sensitive(b, FALSE);
  } else {
    const char *l = g_object_get_data(G_OBJECT(b), "idle-label");
    if (l) gtk_button_set_label(GTK_BUTTON(b), l);
    gtk_widget_set_sensitive(b, TRUE);
  }
}

static char *user_conf_path(void) { return g_build_filename(g_get_user_config_dir(), "lumina", "session.conf", NULL); }

/* Lit / ecrit une cle de ~/.config/lumina/session.conf */
static char *conf_get(const char *key) {
  char *p = user_conf_path(), *c = read_file(p), *r = NULL;
  if (c) {
    char *pat = g_strdup_printf("%s=", key);
    char **l = g_strsplit(c, "\n", -1);
    for (char **i = l; *i && !r; i++)
      if (g_str_has_prefix(*i, pat)) r = g_strdup(*i + strlen(pat));
    g_strfreev(l);
    g_free(pat);
  }
  g_free(p); g_free(c);
  return r;
}

static void conf_set(const char *key, const char *val) {
  char *p = user_conf_path(), *c = read_file(p), *dir = g_path_get_dirname(p);
  GString *out = g_string_new(NULL);
  gboolean done = FALSE;
  char *pat = g_strdup_printf("%s=", key);
  if (c) {
    char **l = g_strsplit(c, "\n", -1);
    for (char **i = l; *i; i++) {
      if (g_str_has_prefix(*i, pat)) { g_string_append_printf(out, "%s%s\n", pat, val); done = TRUE; }
      else if (**i) g_string_append_printf(out, "%s\n", *i);
    }
    g_strfreev(l);
  }
  if (!done) g_string_append_printf(out, "%s%s\n", pat, val);
  g_mkdir_with_parents(dir, 0755);
  g_file_set_contents(p, out->str, -1, NULL);
  g_string_free(out, TRUE); g_free(pat); g_free(p); g_free(c); g_free(dir);
}

/* ------------------------------------------------------------------ */
/* Reseau                                                              */
/* ------------------------------------------------------------------ */
static char *wdev;
static GtkWidget *wifi_sw, *wifi_sub, *nets, *net_msg, *wired_sub;
static gboolean wifi_lock;
static void fill_nets(void);

static void on_join_done(GObject *src, GAsyncResult *res, gpointer d) {
  GSubprocess *sp = G_SUBPROCESS(src);
  g_subprocess_wait_finish(sp, res, NULL);
  gboolean ok = g_subprocess_get_successful(sp);
  g_object_unref(sp);
  GtkWidget *pass = d;
  if (ok) { say(net_msg, "Connecté.", FALSE); fill_nets(); }
  else if (pass && !gtk_widget_get_visible(pass)) { gtk_widget_show(pass); gtk_widget_grab_focus(pass); say(net_msg, "Ce réseau demande un mot de passe.", FALSE); }
  else say(net_msg, "Connexion impossible : vérifiez le mot de passe.", TRUE);
}

static void on_join(GtkWidget *w, gpointer d) {
  GtkWidget *pass = d;
  const char *ssid = g_object_get_data(G_OBJECT(pass), "ssid");
  const char *pw = gtk_entry_get_text(GTK_ENTRY(pass));
  say(net_msg, "Connexion…", FALSE);
  net_wifi_connect(wdev, ssid, gtk_widget_get_visible(pass) && *pw ? pw : NULL, on_join_done, pass);
}

static void fill_nets(void) {
  clear_box(nets);
  char *cur = net_wifi_ssid(wdev);
  gtk_label_set_text(GTK_LABEL(wifi_sub), cur ? cur : net_wifi_powered(wdev) ? "Non connecté" : "Désactivé");
  GPtrArray *list = net_wifi_powered(wdev) ? net_wifi_list(wdev) : g_ptr_array_new();
  for (guint i = 0; i < list->len && i < 10; i++) {
    Net *n = list->pdata[i];
    gboolean open = !g_strcmp0(n->security, "open");
    GtkWidget *pass = field("Mot de passe du réseau", TRUE);
    g_object_set_data_full(G_OBJECT(pass), "ssid", g_strdup(n->ssid), g_free);
    gtk_widget_set_no_show_all(pass, TRUE);
    g_signal_connect(pass, "activate", G_CALLBACK(on_join), pass);
    char *sub = g_strdup_printf("%s · signal %s", open ? "Ouvert" : "Protégé",
                                n->signal >= 4 ? "excellent" : n->signal >= 3 ? "bon" : n->signal >= 2 ? "moyen" : "faible");
    GtkWidget *b = n->current ? label("Connecté", "msg", 1) : button("Se connecter", "tbtn", G_CALLBACK(on_join), pass);
    row(nets, n->ssid, sub, b, NULL);
    gtk_container_add(GTK_CONTAINER(nets), pass);
    gtk_widget_set_margin_start(pass, 20); gtk_widget_set_margin_end(pass, 20); gtk_widget_set_margin_bottom(pass, 8);
    g_free(sub);
  }
  if (!list->len) row(nets, net_wifi_powered(wdev) ? "Recherche des réseaux…" : "Le Wi-Fi est désactivé", NULL, NULL, NULL);
  g_ptr_array_free(list, TRUE);
  g_free(cur);
  gtk_widget_show_all(nets);
  gtk_label_set_text(GTK_LABEL(wired_sub), net_wired_up(wdev) ? "Connecté" : "Débranché");
}

static gboolean rescan_cb(gpointer d) { fill_nets(); return G_SOURCE_REMOVE; }
static void on_rescan(GtkWidget *w, gpointer d) { net_wifi_scan(wdev); g_timeout_add_seconds(4, rescan_cb, NULL); }

static void on_wifi_sw(GObject *o, GParamSpec *ps, gpointer d) {
  if (wifi_lock) return;
  net_wifi_power(wdev, gtk_switch_get_active(GTK_SWITCH(wifi_sw)));
  on_rescan(NULL, NULL);
  fill_nets();
}

static void build_network(void) {
  GtkWidget *p = page_box("reseau", "Réseau");
  wdev = net_wifi_device();
  GtkWidget *g = group(p, NULL);
  row(g, "Réseau filaire", NULL, NULL, &wired_sub);
  if (!wdev) {
    row(group(p, "Wi-Fi"), "Aucune carte Wi-Fi", "Ce PC n'a pas de Wi-Fi reconnu.", NULL, NULL);
    gtk_label_set_text(GTK_LABEL(wired_sub), net_wired_up(NULL) ? "Connecté" : "Débranché");
    return;
  }
  wifi_sw = gtk_switch_new();
  wifi_lock = TRUE; gtk_switch_set_active(GTK_SWITCH(wifi_sw), net_wifi_powered(wdev)); wifi_lock = FALSE;
  g_signal_connect(wifi_sw, "notify::active", G_CALLBACK(on_wifi_sw), NULL);
  row(g, "Wi-Fi", NULL, wifi_sw, &wifi_sub);
  GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(hb), label("Réseaux disponibles", "h2", 0));
  GtkWidget *rb = button("Actualiser", "tbtn", G_CALLBACK(on_rescan), NULL);
  gtk_widget_set_hexpand(rb, TRUE); gtk_widget_set_halign(rb, GTK_ALIGN_END);
  gtk_container_add(GTK_CONTAINER(hb), rb);
  gtk_container_add(GTK_CONTAINER(p), hb);
  nets = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(nets, "group");
  gtk_container_add(GTK_CONTAINER(p), nets);
  net_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(net_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), net_msg);
  net_wifi_scan(wdev);
  fill_nets();
}

/* ------------------------------------------------------------------ */
/* Bluetooth (bluetoothctl ; le demon ne tourne que s'il y a un adaptateur) */
/* ------------------------------------------------------------------ */
static GtkWidget *bt_sw, *bt_list, *bt_msg;
static gboolean bt_lock;

static char *btctl(const char *a, const char *b) {
  const char *argv[] = {"bluetoothctl", a, b, NULL};
  return run_argv(argv, NULL);
}

static gboolean bt_present(void) {
  GDir *d = g_dir_open("/sys/class/bluetooth", 0, NULL);
  gboolean any = d && g_dir_read_name(d);
  if (d) g_dir_close(d);
  return any;
}

static gboolean bt_powered(void) {
  char *o = btctl("show", NULL);
  gboolean on = o && strstr(o, "Powered: yes");
  g_free(o);
  return on;
}

static void fill_bt(void);
static void on_bt_dev(GtkWidget *w, gpointer d) {
  const char *mac = d, *action = g_object_get_data(G_OBJECT(w), "action");
  say(bt_msg, "Connexion…", FALSE);
  while (gtk_events_pending()) gtk_main_iteration();
  gboolean ok = TRUE;
  if (!strcmp(action, "pair")) {
    char *o1 = btctl("pair", mac); g_free(o1);
    char *o2 = btctl("trust", mac); g_free(o2);
  }
  if (!strcmp(action, "disconnect")) { char *o = btctl("disconnect", mac); ok = o && strstr(o, "Successful"); g_free(o); }
  else { char *o = btctl("connect", mac); ok = o && strstr(o, "successful"); g_free(o); }
  say(bt_msg, ok ? "C'est fait." : "L'appareil n'a pas répondu. Vérifiez qu'il est allumé et visible.", !ok);
  fill_bt();
}

/* "Device AA:BB:CC:DD:EE:FF Nom" */
static void bt_rows(const char *list_cmd, const char *filter, gboolean paired) {
  const char *argv[] = {"bluetoothctl", "devices", filter, NULL};
  if (!filter) argv[2] = NULL;
  char *o = run_argv(argv, NULL);
  char **lines = o ? g_strsplit(o, "\n", -1) : NULL;
  for (char **l = lines; l && *l; l++) {
    if (!g_str_has_prefix(*l, "Device ")) continue;
    char **f = g_strsplit(*l + 7, " ", 2);
    if (f[0] && f[1]) {
      char *info = btctl("info", f[0]);
      gboolean connected = info && strstr(info, "Connected: yes");
      gboolean is_paired = info && strstr(info, "Paired: yes");
      g_free(info);
      if (paired == is_paired) {
        GtkWidget *b = button(connected ? "Déconnecter" : paired ? "Connecter" : "Associer", "tbtn",
                              G_CALLBACK(on_bt_dev), g_strdup(f[0]));
        g_object_set_data(G_OBJECT(b), "action", connected ? "disconnect" : paired ? "connect" : "pair");
        row(bt_list, f[1], connected ? "Connecté" : paired ? "Associé" : "Disponible", b, NULL);
      }
    }
    g_strfreev(f);
  }
  g_strfreev(lines); g_free(o);
  (void)list_cmd;
}

static void fill_bt(void) {
  clear_box(bt_list);
  if (bt_powered()) { bt_rows("devices", "Paired", TRUE); bt_rows("devices", NULL, FALSE); }
  GList *k = gtk_container_get_children(GTK_CONTAINER(bt_list));
  if (!k) row(bt_list, bt_powered() ? "Aucun appareil" : "Le Bluetooth est désactivé",
              bt_powered() ? "Lancez une recherche, l'appareil doit être en mode association." : NULL, NULL, NULL);
  g_list_free(k);
  gtk_widget_show_all(bt_list);
}

static void on_bt_sw(GObject *o, GParamSpec *ps, gpointer d) {
  if (bt_lock) return;
  char *r = btctl("power", gtk_switch_get_active(GTK_SWITCH(bt_sw)) ? "on" : "off");
  g_free(r);
  fill_bt();
}

static gboolean bt_scan_done(gpointer d) { say(bt_msg, "", FALSE); fill_bt(); return G_SOURCE_REMOVE; }
static void on_bt_scan(GtkWidget *w, gpointer d) {
  const char *argv[] = {"bluetoothctl", "--timeout", "10", "scan", "on", NULL};
  spawn_async(argv);
  say(bt_msg, "Recherche pendant 10 secondes…", FALSE);
  g_timeout_add_seconds(11, bt_scan_done, NULL);
}

static void build_bluetooth(void) {
  GtkWidget *p = page_box("bluetooth", "Bluetooth");
  if (!bt_present()) { row(group(p, NULL), "Aucun adaptateur Bluetooth", "Ce PC n'a pas de Bluetooth reconnu.", NULL, NULL); return; }
  bt_sw = gtk_switch_new();
  bt_lock = TRUE; gtk_switch_set_active(GTK_SWITCH(bt_sw), bt_powered()); bt_lock = FALSE;
  g_signal_connect(bt_sw, "notify::active", G_CALLBACK(on_bt_sw), NULL);
  row(group(p, NULL), "Bluetooth", NULL, bt_sw, NULL);
  GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(hb), label("Appareils", "h2", 0));
  GtkWidget *sb = button("Rechercher", "tbtn", G_CALLBACK(on_bt_scan), NULL);
  gtk_widget_set_hexpand(sb, TRUE); gtk_widget_set_halign(sb, GTK_ALIGN_END);
  gtk_container_add(GTK_CONTAINER(hb), sb);
  gtk_container_add(GTK_CONTAINER(p), hb);
  bt_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(bt_list, "group");
  gtk_container_add(GTK_CONTAINER(p), bt_list);
  bt_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(bt_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), bt_msg);
  fill_bt();
}

/* ------------------------------------------------------------------ */
/* Affichage                                                           */
/* ------------------------------------------------------------------ */
static GtkWidget *dark_sw;
static guint bright_t;
static gboolean dark_lock;

static void on_dark(GObject *o, GParamSpec *ps, gpointer d) {
  if (!dark_lock) lumina_ui_set_dark(gtk_switch_get_active(GTK_SWITCH(dark_sw)));
}

static gboolean bright_apply(gpointer d) {
  char *v = g_strdup_printf("%d%%", (int)gtk_range_get_value(GTK_RANGE(d)));
  const char *argv[] = {"brightnessctl", "-q", "set", v, NULL};
  spawn_async(argv); g_free(v); bright_t = 0;
  return G_SOURCE_REMOVE;
}
static void on_bright(GtkRange *r, gpointer d) { if (!bright_t) bright_t = g_timeout_add(80, bright_apply, r); }

/* Delais de verrouillage / ecran eteint : choix en minutes, 0 = jamais */
static const int DELAYS[] = {2, 5, 10, 30, 0};
static GtkWidget *delay_combo(const char *key, int def_s) {
  GtkWidget *c = gtk_combo_box_text_new();
  char *cur = conf_get(key);
  int s = cur ? atoi(cur) : def_s, active = 1;
  for (guint i = 0; i < G_N_ELEMENTS(DELAYS); i++) {
    char *t = DELAYS[i] ? g_strdup_printf("%d minutes", DELAYS[i]) : g_strdup("Jamais");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), t);
    g_free(t);
    if (DELAYS[i] * 60 == s || (!DELAYS[i] && s >= 86400)) active = i;
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
  g_object_set_data_full(G_OBJECT(c), "key", g_strdup(key), g_free);
  g_free(cur);
  return c;
}

static void on_delay(GtkComboBox *c, gpointer d) {
  int i = gtk_combo_box_get_active(c);
  if (i < 0) return;
  char *v = g_strdup_printf("%d", DELAYS[i] ? DELAYS[i] * 60 : 86400 * 365);
  conf_set(g_object_get_data(G_OBJECT(c), "key"), v);
  g_free(v);
  const char *argv[] = {"lumina-idle", NULL};   /* redemarre swayidle avec les nouveaux delais */
  spawn_async(argv);
}


/* ------------------------------------------------------------------ */
/* Fond d'ecran et couleur du theme (facon ChromeOS / Chrome)          */
/* ------------------------------------------------------------------ */
static const struct { const char *hex, *name; } ACCENTS[] = {
  {LUMINA_DEFAULT_ACCENT, "Vert (par défaut)"}, {"#1f6fb5", "Bleu"}, {"#4355b9", "Indigo"},
  {"#7b4fa0", "Violet"}, {"#a83f6e", "Rose"}, {"#b3261e", "Rouge"}, {"#b25b14", "Orange"},
  {"#8a6d00", "Ambre"}, {"#5d6b1e", "Olive"}, {"#6b6f72", "Gris"},
};
static GtkWidget *wall_styles;
static GtkWidget *swatch_box, *wall_sub, *wall_reset, *wall_prev;
static void fill_swatches(void);

static gboolean draw_swatch(GtkWidget *w, cairo_t *cr, gpointer d) {
  const char *hex = d;
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), r, g, b;
  double cx = W / 2, cy = H / 2;
  char *cur = lumina_ui_accent();
  gboolean sel = !g_ascii_strcasecmp(cur, hex);
  g_free(cur);
  if (!strcmp(hex, "wallpaper")) {
    /* Pastille "selon la photo" : quatre quarts aux couleurs de la palette */
    const char *q[] = {P->pri, P->ter, P->sec_c, P->pri_c};
    for (int i = 0; i < 4; i++) {
      hex_rgb(q[i], &r, &g, &b);
      cairo_set_source_rgb(cr, r, g, b);
      cairo_move_to(cr, cx, cy);
      cairo_arc(cr, cx, cy, 15, i * M_PI / 2, (i + 1) * M_PI / 2);
      cairo_close_path(cr); cairo_fill(cr);
    }
  } else {
    hex_rgb(hex, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_arc(cr, cx, cy, 15, 0, 2 * M_PI); cairo_fill(cr);
  }
  if (sel) {
    hex_rgb(P->on, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 2.5);
    cairo_arc(cr, cx, cy, 19.5, 0, 2 * M_PI); cairo_stroke(cr);
    /* coche */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, 2.2);
    cairo_move_to(cr, cx - 5.5, cy); cairo_line_to(cr, cx - 1.5, cy + 4); cairo_line_to(cr, cx + 6, cy - 4.5);
    cairo_stroke(cr);
  }
  return FALSE;
}

static void on_swatch(GtkButton *b, gpointer d) {
  lumina_ui_set_accent(d);
  gtk_widget_queue_draw(swatch_box);
}

static GtkWidget *swatch(const char *hex, const char *name) {
  GtkWidget *b = gtk_button_new();
  add_class(b, "swatch");
  gtk_widget_set_tooltip_text(b, name);
  GtkWidget *a = gtk_drawing_area_new();
  gtk_widget_set_size_request(a, 44, 44);
  g_signal_connect(a, "draw", G_CALLBACK(draw_swatch), (gpointer)hex);
  gtk_container_add(GTK_CONTAINER(b), a);
  g_signal_connect(b, "clicked", G_CALLBACK(on_swatch), (gpointer)hex);
  return b;
}

static void fill_swatches(void) {
  clear_box(swatch_box);
  char *wp = lumina_ui_wallpaper_path();
  if (wp) gtk_flow_box_insert(GTK_FLOW_BOX(swatch_box), swatch("wallpaper", "Selon le fond d'écran"), -1);
  g_free(wp);
  for (guint i = 0; i < G_N_ELEMENTS(ACCENTS); i++)
    gtk_flow_box_insert(GTK_FLOW_BOX(swatch_box), swatch(ACCENTS[i].hex, ACCENTS[i].name), -1);
  gtk_widget_show_all(swatch_box);
}

static void update_wall_row(void) {
  char *wp = lumina_ui_wallpaper_path();
  gtk_label_set_text(GTK_LABEL(wall_sub), wp ? "Votre photo" : "Fond LuminaOS, aux couleurs du thème. Déposez une image ici pour l'utiliser.");
  if (wall_styles) gtk_widget_queue_draw(wall_styles);
  gtk_widget_set_visible(wall_reset, wp != NULL);
  GdkPixbuf *pb = wp ? gdk_pixbuf_new_from_file_at_scale(wp, 96, 60, FALSE, NULL) : NULL;
  if (pb) { gtk_image_set_from_pixbuf(GTK_IMAGE(wall_prev), pb); g_object_unref(pb); gtk_widget_show(wall_prev); }
  else gtk_widget_hide(wall_prev);
  g_free(wp);
  fill_swatches();
}

static void on_wall_choose(GtkButton *btn, gpointer d) {
  GtkFileChooserNative *fc = gtk_file_chooser_native_new("Choisir un fond d'écran", GTK_WINDOW(win),
                                                         GTK_FILE_CHOOSER_ACTION_OPEN, "Choisir", "Annuler");
  GtkFileFilter *f = gtk_file_filter_new();
  gtk_file_filter_set_name(f, "Images");
  gtk_file_filter_add_pixbuf_formats(f);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), f);
  const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
  if (pics) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fc), pics);
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
    char *src = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    GdkPixbufFormat *fmt = src ? gdk_pixbuf_get_file_info(src, NULL, NULL) : NULL;
    if (fmt) {
      /* Copie dans ~/.config/lumina : la photo d'origine peut etre
         deplacee ou supprimee sans que le fond d'ecran disparaisse. */
      char *dst = g_build_filename(g_get_user_config_dir(), "lumina", "wallpaper", NULL);
      char *tmp = g_strconcat(dst, ".tmp", NULL);
      GFile *fs = g_file_new_for_path(src), *ft = g_file_new_for_path(tmp);
      if (g_file_copy(fs, ft, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL)) {
        g_rename(tmp, dst);
        /* Comme sur ChromeOS : une nouvelle photo donne ses couleurs au
           theme, sauf si l'utilisateur a choisi une couleur lui-meme. */
        char *acc = lumina_ui_accent();
        if (!g_ascii_strcasecmp(acc, LUMINA_DEFAULT_ACCENT)) lumina_ui_set_accent("wallpaper");
        else lumina_ui_set_accent(acc);
        g_free(acc);
      }
      g_object_unref(fs); g_object_unref(ft); g_free(tmp); g_free(dst);
    }
    g_free(src);
  }
  g_object_unref(fc);
  update_wall_row();
}

static void on_wall_reset(GtkButton *b, gpointer d) {
  char *p = g_build_filename(g_get_user_config_dir(), "lumina", "wallpaper", NULL);
  g_unlink(p);
  g_free(p);
  char *acc = lumina_ui_accent();
  lumina_ui_set_accent(!g_ascii_strcasecmp(acc, "wallpaper") ? NULL : acc);
  g_free(acc);
  update_wall_row();
}

/* Fonds LuminaOS : vignettes dessinees, un clic les applique (et retire
   la photo eventuelle) */
static gboolean draw_wall_thumb(GtkWidget *w, cairo_t *cr, gpointer d) {
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), r = 10;
  cairo_new_sub_path(cr);
  cairo_arc(cr, W - r, r, r, -G_PI / 2, 0); cairo_arc(cr, W - r, H - r, r, 0, G_PI / 2);
  cairo_arc(cr, r, H - r, r, G_PI / 2, G_PI); cairo_arc(cr, r, r, r, G_PI, 3 * G_PI / 2);
  cairo_close_path(cr); cairo_clip(cr);
  lumina_ui_draw_wallpaper(cr, W, H, GPOINTER_TO_INT(d));
  char *wp = lumina_ui_wallpaper_path();
  if (!wp && lumina_ui_wall_style() == GPOINTER_TO_INT(d)) {
    double rr, gg, bb; hex_rgb(P->pri, &rr, &gg, &bb);
    cairo_reset_clip(cr);
    cairo_set_source_rgb(cr, rr, gg, bb); cairo_set_line_width(cr, 6);
    cairo_new_sub_path(cr);
    cairo_arc(cr, W - r, r, r, -G_PI / 2, 0); cairo_arc(cr, W - r, H - r, r, 0, G_PI / 2);
    cairo_arc(cr, r, H - r, r, G_PI / 2, G_PI); cairo_arc(cr, r, r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr); cairo_stroke(cr);
  }
  g_free(wp);
  return FALSE;
}
static void on_wall_style(GtkButton *b, gpointer d) {
  char *p = g_build_filename(g_get_user_config_dir(), "lumina", "wallpaper", NULL);
  g_unlink(p); g_free(p);
  char *acc = lumina_ui_accent();
  if (!g_ascii_strcasecmp(acc, "wallpaper")) lumina_ui_set_accent(NULL);
  g_free(acc);
  lumina_ui_set_wall_style(GPOINTER_TO_INT(d));
  update_wall_row();
  gtk_widget_queue_draw(wall_styles);
}
/* Deposer une image (depuis Fichiers, Chromium...) : nouveau fond d'ecran */
static void on_wall_drop(GtkWidget *w, GdkDragContext *c, gint x, gint y, GtkSelectionData *sd, guint info, guint t, gpointer d) {
  char **uris = gtk_selection_data_get_uris(sd);
  gboolean ok = FALSE;
  if (uris && uris[0]) {
    char *path = g_filename_from_uri(uris[0], NULL, NULL);
    if (path && gdk_pixbuf_get_file_info(path, NULL, NULL)) {
      const char *argv[] = {"lumina-set-wallpaper", path, NULL};
      int st; g_free(run_argv(argv, &st)); ok = st == 0;
    }
    g_free(path);
  }
  g_strfreev(uris);
  gtk_drag_finish(c, ok, FALSE, t);
  if (ok) update_wall_row();
}

static void build_personalize(GtkWidget *p) {
  GtkWidget *g = group(p, "Fond d'écran et couleurs");
  GtkWidget *end = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  wall_prev = gtk_image_new();
  add_class(wall_prev, "wallprev");
  gtk_widget_set_no_show_all(wall_prev, TRUE);
  gtk_container_add(GTK_CONTAINER(end), wall_prev);
  wall_reset = button("Par défaut", "tbtn", G_CALLBACK(on_wall_reset), NULL);
  gtk_widget_set_no_show_all(wall_reset, TRUE);
  gtk_container_add(GTK_CONTAINER(end), wall_reset);
  gtk_container_add(GTK_CONTAINER(end), button("Choisir une photo…", "fbtn", G_CALLBACK(on_wall_choose), NULL));
  GtkWidget *wr = row(g, "Fond d'écran", "", end, &wall_sub);
  /* Glisser une image sur cette ligne la met en fond d'ecran */
  static const GtkTargetEntry URI[] = {{"text/uri-list", 0, 0}};
  gtk_drag_dest_set(wr, GTK_DEST_DEFAULT_ALL, URI, 1, GDK_ACTION_COPY);
  g_signal_connect(wr, "drag-data-received", G_CALLBACK(on_wall_drop), NULL);
  wall_styles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  add_class(wall_styles, "swatches");
  for (int i = 0; i < LUMINA_WALL_COUNT; i++) {
    GtkWidget *b = gtk_button_new();
    add_class(b, "wallstyle");
    gtk_widget_set_tooltip_text(b, LUMINA_WALL_NAMES[i]);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *a = gtk_drawing_area_new();
    gtk_widget_set_size_request(a, 96, 60);
    g_signal_connect(a, "draw", G_CALLBACK(draw_wall_thumb), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(v), a);
    gtk_container_add(GTK_CONTAINER(v), label(LUMINA_WALL_NAMES[i], "s", 0.5));
    gtk_container_add(GTK_CONTAINER(b), v);
    g_signal_connect(b, "clicked", G_CALLBACK(on_wall_style), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(wall_styles), b);
  }
  gtk_container_add(GTK_CONTAINER(g), wall_styles);
  row(g, "Couleur du thème", "Barre, étagère, applis LuminaOS et boutons", NULL, NULL);
  swatch_box = gtk_flow_box_new();
  add_class(swatch_box, "swatches");
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(swatch_box), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(swatch_box), 11);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(swatch_box), 2);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(swatch_box), TRUE);
  gtk_container_add(GTK_CONTAINER(g), swatch_box);
  update_wall_row();
}

static void on_night_mode(GtkComboBox *c, gpointer d) {
  int m = gtk_combo_box_get_active(c);
  char *p = g_build_filename(g_get_user_config_dir(), "lumina", "nightlight", NULL);
  if (m <= 0) g_unlink(p); else g_file_set_contents(p, m == 2 ? "auto\n" : "1\n", -1, NULL);
  g_free(p);
  const char *argv[] = {"lumina-shell", "--action", "night-reload", NULL};
  spawn_async(argv);
}

static void build_display(void) {
  GtkWidget *p = page_box("affichage", "Affichage");
  GtkWidget *g = group(p, NULL);
  dark_sw = gtk_switch_new();
  dark_lock = TRUE; gtk_switch_set_active(GTK_SWITCH(dark_sw), dark); dark_lock = FALSE;
  g_signal_connect(dark_sw, "notify::active", G_CALLBACK(on_dark), NULL);
  row(g, "Thème sombre", "Bureau, applis LuminaOS et pages web compatibles", dark_sw, NULL);
  {
    /* Eclairage nocturne : filtre chaud qui reduit la lumiere bleue */
    char *np = g_build_filename(g_get_user_config_dir(), "lumina", "nightlight", NULL), *nc = read_file(np);
    int m = !nc ? 0 : g_str_has_prefix(nc, "auto") ? 2 : 1;
    g_free(np); g_free(nc);
    GtkWidget *nl = gtk_combo_box_text_new();
    const char *nt[] = {"Désactivé", "Toujours", "Du soir au matin (20 h – 7 h)"};
    for (int i = 0; i < 3; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(nl), nt[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(nl), m);
    g_signal_connect(nl, "changed", G_CALLBACK(on_night_mode), NULL);
    row(g, "Éclairage nocturne", "Teinte plus chaude, moins de lumière bleue le soir", nl, NULL);
  }
  build_personalize(p);

  GDir *d = g_dir_open("/sys/class/backlight", 0, NULL);
  gboolean bl = d && g_dir_read_name(d);
  if (d) g_dir_close(d);
  if (bl) {
    GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
    gtk_widget_set_size_request(s, 220, -1);
    const char *argv[] = {"brightnessctl", "-m", NULL};
    char *o = run_argv(argv, NULL);
    if (o) { char **f = g_strsplit(o, ",", -1); if (g_strv_length(f) >= 4) gtk_range_set_value(GTK_RANGE(s), atoi(f[3])); g_strfreev(f); g_free(o); }
    g_signal_connect(s, "value-changed", G_CALLBACK(on_bright), NULL);
    row(g, "Luminosité", NULL, s, NULL);
  }
  GtkWidget *g2 = group(p, "Mise en veille");
  GtkWidget *c = delay_combo("LUMINA_SCREEN_OFF_DELAY", 600);
  g_signal_connect(c, "changed", G_CALLBACK(on_delay), NULL);
  row(g2, "Éteindre l'écran après", "Sans activité au clavier ou à la souris", c, NULL);
}

/* ------------------------------------------------------------------ */
/* Son                                                                 */
/* ------------------------------------------------------------------ */
static guint vol_t;
static gboolean vol_apply(gpointer d) {
  char *v = g_strdup_printf("%.2f", gtk_range_get_value(GTK_RANGE(d)) / 100.0);
  const char *argv[] = {"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", v, NULL};
  spawn_async(argv); g_free(v); vol_t = 0;
  return G_SOURCE_REMOVE;
}
static void on_vol(GtkRange *r, gpointer d) { if (!vol_t) vol_t = g_timeout_add(80, vol_apply, r); }
static void on_mute(GObject *o, GParamSpec *ps, gpointer d) {
  const char *argv[] = {"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", gtk_switch_get_active(GTK_SWITCH(o)) ? "1" : "0", NULL};
  spawn_async(argv);
}

/* Sorties et entrees audio (wpctl status) : choix de l'appareil par defaut */
typedef struct { int id; char *name; gboolean def; } AudioDev;
static void audio_free(gpointer p) { AudioDev *a = p; g_free(a->name); g_free(a); }
static void audio_list(GPtrArray *sinks, GPtrArray *sources) {
  const char *argv[] = {"wpctl", "status", NULL};
  char *o = run_argv(argv, NULL);
  char **l = o ? g_strsplit(o, "\n", -1) : NULL;
  GPtrArray *cur = NULL;
  GRegex *re = g_regex_new("(\\*)?\\s+(\\d+)\\.\\s+(.+?)\\s+\\[vol", 0, 0, NULL);
  for (char **i = l; i && *i; i++) {
    if (g_str_has_prefix(*i, "Video") || g_str_has_prefix(*i, "Settings")) break;
    if (strstr(*i, "Sinks:")) { cur = sinks; continue; }
    if (strstr(*i, "Sources:")) { cur = sources; continue; }
    if (strstr(*i, "Filters:") || strstr(*i, "Streams:") || strstr(*i, "Devices:")) { cur = NULL; continue; }
    GMatchInfo *m = NULL;
    if (cur && g_regex_match(re, *i, 0, &m)) {
      AudioDev *a = g_new0(AudioDev, 1);
      char *star = g_match_info_fetch(m, 1), *id = g_match_info_fetch(m, 2);
      a->def = star && *star; a->id = atoi(id); a->name = g_match_info_fetch(m, 3);
      g_ptr_array_add(cur, a);
      g_free(star); g_free(id);
    }
    if (m) g_match_info_free(m);
  }
  g_regex_unref(re); g_strfreev(l); g_free(o);
}
static void on_audio_dev(GtkComboBox *c, gpointer d) {
  GPtrArray *a = d;
  int i = gtk_combo_box_get_active(c);
  if (i < 0 || (guint)i >= a->len) return;
  char *id = g_strdup_printf("%d", ((AudioDev *)a->pdata[i])->id);
  const char *argv[] = {"wpctl", "set-default", id, NULL};
  spawn_async(argv);
  g_free(id);
}
static void audio_row(GtkWidget *g, const char *title, const char *sub, GPtrArray *devs) {
  if (devs->len < 1) return;
  GtkWidget *c = gtk_combo_box_text_new();
  int act = 0;
  for (guint i = 0; i < devs->len; i++) {
    AudioDev *a = devs->pdata[i];
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), a->name);
    if (a->def) act = i;
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(c), act);
  g_signal_connect_data(c, "changed", G_CALLBACK(on_audio_dev), devs, (GClosureNotify)(void (*)(void))g_ptr_array_unref, 0);
  gtk_widget_set_size_request(c, 240, -1);
  row(g, title, sub, c, NULL);
}

static void build_sound(void) {
  GtkWidget *p = page_box("son", "Son");
  const char *argv[] = {"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL};
  int st = -1;
  char *o = run_argv(argv, &st);
  if (!o || st || !g_str_has_prefix(o, "Volume:")) {
    row(group(p, NULL), "Aucune sortie audio", "Ce PC n'a pas de carte son reconnue.", NULL, NULL);
    g_free(o); return;
  }
  GtkWidget *g = group(p, NULL);
  GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
  gtk_widget_set_size_request(s, 220, -1);
  gtk_range_set_value(GTK_RANGE(s), g_ascii_strtod(o + 7, NULL) * 100);
  g_signal_connect(s, "value-changed", G_CALLBACK(on_vol), NULL);
  row(g, "Volume", NULL, s, NULL);
  GtkWidget *m = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(m), strstr(o, "MUTED") != NULL);
  g_signal_connect(m, "notify::active", G_CALLBACK(on_mute), NULL);
  row(g, "Couper le son", NULL, m, NULL);
  g_free(o);
  GPtrArray *sinks = g_ptr_array_new_with_free_func(audio_free), *srcs = g_ptr_array_new_with_free_func(audio_free);
  audio_list(sinks, srcs);
  GtkWidget *g2 = group(p, "Appareils");
  audio_row(g2, "Sortie", "Haut-parleurs, casque, écran HDMI…", sinks);
  audio_row(g2, "Entrée", "Micro intégré ou casque-micro", srcs);
  if (!sinks->len && !srcs->len) row(g2, "Aucun appareil détecté", NULL, NULL, NULL);
  if (!sinks->len) g_ptr_array_unref(sinks);
  if (!srcs->len) g_ptr_array_unref(srcs);
}

/* ------------------------------------------------------------------ */
/* Clavier et langue                                                   */
/* ------------------------------------------------------------------ */
static const struct { const char *code, *label; } LANGS[] = {{"fr", "Français"}, {"en-US", "English"}};
static const struct { const char *layout, *variant, *label, *meta; } KEYMAPS[] = {
  {"fr", "", "Français", "AZERTY"}, {"be", "", "Belge", "AZERTY"}, {"ch", "fr", "Suisse romand", "QWERTZ"},
  {"ca", "", "Canadien français", "QWERTY"}, {"us", "", "Anglais (États-Unis)", "QWERTY"},
};
static GtkWidget *kb_msg;

static GtkWidget *opt(GtkWidget *grp_first, const char *t, const char *s) {
  GtkWidget *rb = grp_first ? gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(grp_first)) : gtk_radio_button_new(NULL);
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(rb), FALSE);
  add_class(rb, "opt");
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_container_add(GTK_CONTAINER(v), label(t, NULL, 0));
  if (s) gtk_container_add(GTK_CONTAINER(v), label(s, "s", 0));
  gtk_container_add(GTK_CONTAINER(rb), v);
  return rb;
}

static void on_keymap(GtkToggleButton *b, gpointer d) {
  if (!gtk_toggle_button_get_active(b)) return;
  int i = GPOINTER_TO_INT(d);
  conf_set("LUMINA_KEYMAP", KEYMAPS[i].layout);
  conf_set("LUMINA_KEYMAP_VARIANT", KEYMAPS[i].variant);
  char *dir = g_build_filename(g_get_user_config_dir(), "labwc", NULL), *f = g_build_filename(dir, "environment", NULL);
  char *t = g_strdup_printf("XKB_DEFAULT_LAYOUT=%s\nXKB_DEFAULT_VARIANT=%s\n", KEYMAPS[i].layout, KEYMAPS[i].variant);
  g_mkdir_with_parents(dir, 0755);
  g_file_set_contents(f, t, -1, NULL);
  const char *argv[] = {"labwc", "--reconfigure", NULL};
  spawn_async(argv);
  g_free(dir); g_free(f); g_free(t);
  say(kb_msg, "Clavier appliqué.", FALSE);
}

static void on_lang(GtkToggleButton *b, gpointer d) {
  if (!gtk_toggle_button_get_active(b)) return;
  conf_set("LUMINA_LANG", LANGS[GPOINTER_TO_INT(d)].code);
  say(kb_msg, "La nouvelle langue s'appliquera à la prochaine ouverture de session.", FALSE);
}

static void build_keyboard(void) {
  GtkWidget *p = page_box("clavier", "Clavier et langue");
  char *lang = conf_get("LUMINA_LANG"), *km = conf_get("LUMINA_KEYMAP");
  gtk_container_add(GTK_CONTAINER(p), label("Langue", "h2", 0));
  GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8), *first = NULL;
  gtk_box_set_homogeneous(GTK_BOX(row1), TRUE);
  for (guint i = 0; i < G_N_ELEMENTS(LANGS); i++) {
    GtkWidget *o = opt(first, LANGS[i].label, NULL);
    if (!first) first = o;
    if (!g_strcmp0(lang ? lang : "fr", LANGS[i].code)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(o), TRUE);
    g_signal_connect(o, "toggled", G_CALLBACK(on_lang), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(row1), o);
  }
  gtk_container_add(GTK_CONTAINER(p), row1);
  gtk_container_add(GTK_CONTAINER(p), label("Clavier", "h2", 0));
  GtkWidget *fb = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(fb), GTK_SELECTION_NONE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(fb), 2);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fb), 2);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(fb), TRUE);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(fb), 8); gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(fb), 8);
  first = NULL;
  for (guint i = 0; i < G_N_ELEMENTS(KEYMAPS); i++) {
    GtkWidget *o = opt(first, KEYMAPS[i].label, KEYMAPS[i].meta);
    if (!first) first = o;
    if (!g_strcmp0(km ? km : "fr", KEYMAPS[i].layout)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(o), TRUE);
    g_signal_connect(o, "toggled", G_CALLBACK(on_keymap), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(fb), o);
  }
  gtk_container_add(GTK_CONTAINER(p), fb);
  GtkWidget *test = field("Essayez votre clavier ici", FALSE);
  gtk_container_add(GTK_CONTAINER(p), test);
  kb_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(kb_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), kb_msg);
  g_free(lang); g_free(km);
}

/* ------------------------------------------------------------------ */
/* Compte et verrouillage                                              */
/* ------------------------------------------------------------------ */
static GtkWidget *name_e, *old_e, *new_e, *new2_e, *name_msg, *pw_msg;

static gboolean has_password(void) {
  char *a = read_file("/var/lib/lumina/config/account.conf");
  gboolean r = a && strstr(a, "HAS_PASSWORD=1");
  g_free(a);
  return r;
}

static void name_done(gboolean ok, const char *err, gpointer b) {
  busy(b, NULL);
  say(name_msg, ok ? "Nom enregistré." : err, !ok);
}
static void on_save_name(GtkWidget *w, gpointer d) {
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-account", "set-name", gtk_entry_get_text(GTK_ENTRY(name_e)), NULL};
  busy(w, "Enregistrement…");
  run_root_async(argv, NULL, name_done, w);
}

static void pw_done(gboolean ok, const char *err, gpointer b) {
  busy(b, NULL);
  const char *a = gtk_entry_get_text(GTK_ENTRY(new_e));
  say(pw_msg, ok ? (*a ? "Mot de passe modifié." : "Mot de passe supprimé : l'écran ne se verrouillera plus.") : err, !ok);
  if (ok) { if (old_e) gtk_entry_set_text(GTK_ENTRY(old_e), ""); gtk_entry_set_text(GTK_ENTRY(new_e), ""); gtk_entry_set_text(GTK_ENTRY(new2_e), ""); }
}
static void on_save_pw(GtkWidget *w, gpointer d) {
  const char *a = gtk_entry_get_text(GTK_ENTRY(new_e)), *b = gtk_entry_get_text(GTK_ENTRY(new2_e));
  if (*a && strlen(a) < 4) { say(pw_msg, "Au moins 4 caractères.", TRUE); return; }
  if (strcmp(a, b)) { say(pw_msg, "Les deux mots de passe ne correspondent pas.", TRUE); return; }
  char *in = g_strdup_printf("%s\n%s\n", old_e ? gtk_entry_get_text(GTK_ENTRY(old_e)) : "", a);
  gboolean owner = getuid() == 1000;
  const char *argv[] = {"pkexec", owner ? "/usr/local/sbin/lumina-account" : "/usr/local/sbin/lumina-users", "set-password", NULL};
  busy(w, "Enregistrement…");
  run_root_async(argv, in, pw_done, w);
  g_free(in);
}


/* ------------------------------------------------------------------ */
/* Personnes (comptes) et mode invite : geres par le proprietaire       */
/* ------------------------------------------------------------------ */
static GtkWidget *people_grp, *people_msg, *guest_sw;
static gboolean guest_lock;
static void fill_people(void);

static void people_done(gboolean ok, const char *err, gpointer m) {
  say(people_msg, ok ? m : err, !ok);
  fill_people();
}
static void on_person_remove(GtkWidget *w, gpointer user) {
  GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                                          "Retirer ce compte ?");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "Ses fichiers, réglages et applis seront effacés de ce PC.");
  gtk_dialog_add_buttons(GTK_DIALOG(dlg), "Annuler", GTK_RESPONSE_CANCEL, "Retirer", GTK_RESPONSE_ACCEPT, NULL);
  int r = gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
  if (r != GTK_RESPONSE_ACCEPT) return;
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-users", "remove", user, NULL};
  run_root_async(argv, NULL, people_done, "Compte retiré.");
}
static void on_person_add(GtkWidget *w, gpointer d) {
  GtkWidget *dlg = gtk_dialog_new_with_buttons("Ajouter une personne", GTK_WINDOW(win), GTK_DIALOG_MODAL,
                                               "Annuler", GTK_RESPONSE_CANCEL, "Ajouter", GTK_RESPONSE_ACCEPT, NULL);
  GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  gtk_box_set_spacing(GTK_BOX(box), 10);
  gtk_container_set_border_width(GTK_CONTAINER(box), 18);
  gtk_container_add(GTK_CONTAINER(box), desc("Elle aura son propre bureau, ses fichiers, ses applis et son navigateur, "
                                              "et se connectera depuis l'écran de connexion."));
  GtkWidget *n = field("Prénom", FALSE), *p1 = field("Mot de passe ou code PIN (facultatif)", TRUE);
  gtk_container_add(GTK_CONTAINER(box), n);
  gtk_container_add(GTK_CONTAINER(box), p1);
  gtk_widget_show_all(box);
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT && *gtk_entry_get_text(GTK_ENTRY(n))) {
    const char *pw = gtk_entry_get_text(GTK_ENTRY(p1));
    if (*pw && strlen(pw) < 4) say(people_msg, "Mot de passe : au moins 4 caractères.", TRUE);
    else {
      char *in = g_strdup_printf("%s\n", pw);
      const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-users", "add", "", gtk_entry_get_text(GTK_ENTRY(n)), NULL};
      run_root_async(argv, in, people_done, "Compte ajouté. Il apparaît sur l'écran de connexion.");
      g_free(in);
    }
  }
  gtk_widget_destroy(dlg);
}
static void on_guest(GObject *o, GParamSpec *ps, gpointer d) {
  if (guest_lock) return;
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-users", "guest", gtk_switch_get_active(GTK_SWITCH(o)) ? "on" : "off", NULL};
  run_root_async(argv, NULL, people_done, "Enregistré. Effet à l'écran de connexion.");
}
static void fill_people(void) {
  clear_box(people_grp);
  const char *argv[] = {"/usr/local/sbin/lumina-users", "list", NULL};
  char *out = run_argv(argv, NULL);
  char **l = out ? g_strsplit(out, "\n", -1) : NULL;
  gboolean guest = FALSE;
  for (char **i = l; i && *i; i++) {
    char **f = g_strsplit(*i, "\t", 3);
    if (g_strv_length(f) == 3) {
      if (!strcmp(f[0], "invite")) guest = TRUE;
      else if (!strcmp(f[0], "lumina")) row(people_grp, *f[1] ? f[1] : "Vous", "Propriétaire de ce PC", NULL, NULL);
      else row(people_grp, f[1], !strcmp(f[2], "1") ? "Avec mot de passe" : "Sans mot de passe",
               button("Retirer", "tbtn", G_CALLBACK(on_person_remove), g_strdup(f[0])), NULL);
    }
    g_strfreev(f);
  }
  g_strfreev(l); g_free(out);
  row(people_grp, "Ajouter une personne", "Chacun son bureau, ses fichiers et ses applis",
      button("Ajouter", "fbtn", G_CALLBACK(on_person_add), NULL), NULL);
  guest_sw = gtk_switch_new();
  guest_lock = TRUE; gtk_switch_set_active(GTK_SWITCH(guest_sw), guest); guest_lock = FALSE;
  g_signal_connect(guest_sw, "notify::active", G_CALLBACK(on_guest), NULL);
  row(people_grp, "Mode invité", "Une session sans compte, effacée à la déconnexion", guest_sw, NULL);
  gtk_widget_show_all(people_grp);
}
static void build_people(GtkWidget *p) {
  people_grp = group(p, "Personnes sur ce PC");
  people_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(people_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), people_msg);
  fill_people();
}

static void build_account(void) {
  GtkWidget *p = page_box("compte", "Compte et verrouillage");
  GtkWidget *g = group(p, "Nom");
  name_e = field("Votre prénom", FALSE);
  char *n = read_file("/var/lib/lumina/config/name");
  if (n) gtk_entry_set_text(GTK_ENTRY(name_e), n);
  g_free(n);
  GtkWidget *nb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(name_e, TRUE);
  gtk_container_add(GTK_CONTAINER(nb), name_e);
  gtk_container_add(GTK_CONTAINER(nb), button("Enregistrer", "tbtn", G_CALLBACK(on_save_name), NULL));
  add_class(nb, "row");
  gtk_container_add(GTK_CONTAINER(g), nb);
  /* Messages sous le champ concerne : en bas de page, ils etaient hors de vue. */
  name_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(name_msg, TRUE);
  gtk_widget_set_margin_start(name_msg, 20); gtk_widget_set_margin_bottom(name_msg, 10);
  gtk_container_add(GTK_CONTAINER(g), name_msg);

  GtkWidget *g2 = group(p, "Mot de passe ou code PIN");
  GtkWidget *pb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  add_class(pb, "row");
  old_e = NULL;
  if (has_password()) { old_e = field("Mot de passe ou code PIN actuel", TRUE); gtk_container_add(GTK_CONTAINER(pb), old_e); }
  new_e = field("Nouveau mot de passe ou code PIN, 4 caractères minimum (vide : aucun)", TRUE);
  new2_e = field("Confirmez", TRUE);
  gtk_container_add(GTK_CONTAINER(pb), new_e);
  gtk_container_add(GTK_CONTAINER(pb), new2_e);
  GtkWidget *sb = button("Changer le mot de passe", "fbtn", G_CALLBACK(on_save_pw), NULL);
  gtk_widget_set_halign(sb, GTK_ALIGN_END);
  pw_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(pw_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(pb), pw_msg);
  gtk_container_add(GTK_CONTAINER(pb), sb);
  gtk_container_add(GTK_CONTAINER(g2), pb);

  GtkWidget *g3 = group(p, "Verrouillage");
  GtkWidget *c = delay_combo("LUMINA_LOCK_DELAY", 300);
  g_signal_connect(c, "changed", G_CALLBACK(on_delay), NULL);
  row(g3, "Verrouiller après", "Sans activité. Windows + L verrouille immédiatement.", c, NULL);
  if (getuid() == 1000) build_people(p);
}

/* ------------------------------------------------------------------ */
/* Linux, Android                                                      */
/* ------------------------------------------------------------------ */
/* Comme la page Linux de ChromeOS : etat reel du conteneur, relu a chaque
   affichage de la page (l'installation se fait dans le Terminal, a cote),
   espace occupe, mise a jour, suppression. */
static GtkWidget *linux_grp, *linux_msg;
static void refresh_linux(void);

static void open_terminal(const char *const *cmd) {
  GPtrArray *a = g_ptr_array_new();
  const char *pre[] = {"foot", "-a", "lumina-terminal", "-T", "Terminal"};
  for (guint i = 0; i < G_N_ELEMENTS(pre); i++) g_ptr_array_add(a, (gpointer)pre[i]);
  for (const char *const *c = cmd; *c; c++) g_ptr_array_add(a, (gpointer)*c);
  g_ptr_array_add(a, NULL);
  spawn_async((const char *const *)a->pdata);
  g_ptr_array_free(a, TRUE);
}

static void on_linux_open(GtkWidget *w, gpointer d) {
  const char *c[] = {"lumina-linux", NULL};
  open_terminal(c);
}
static void on_linux_install(GtkWidget *w, gpointer d) {
  const char *c[] = {"env", "LUMINA_LINUX_YES=1", "lumina-linux", NULL};
  open_terminal(c);
  say(linux_msg, "Installation dans la fenêtre Terminal. Cette page se met à jour quand Linux est prêt.", FALSE);
}
static void on_linux_upgrade(GtkWidget *w, gpointer d) {
  const char *c[] = {"lumina-linux", "--upgrade", NULL};
  open_terminal(c);
}

static void linux_removed(GObject *o, GAsyncResult *res, gpointer d) {
  gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(o), res, NULL);
  g_object_unref(o);
  say(linux_msg, ok ? "Linux a été supprimé. Vos fichiers personnels sont intacts." : "La suppression a échoué.", !ok);
  refresh_linux();
}
static void on_linux_remove(GtkWidget *w, gpointer d) {
  GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                          "Supprimer Linux ?");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg),
      "Le conteneur Debian et toutes les applis Linux installées seront effacés. "
      "Vos fichiers personnels (Documents, Téléchargements…) sont conservés.");
  gtk_dialog_add_buttons(GTK_DIALOG(dlg), "Annuler", GTK_RESPONSE_CANCEL, "Supprimer", GTK_RESPONSE_ACCEPT, NULL);
  int r = gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
  if (r != GTK_RESPONSE_ACCEPT) return;
  busy(w, "Suppression…");
  GSubprocess *sp = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                                     "lumina-linux", "--remove", NULL);
  if (sp) g_subprocess_wait_check_async(sp, NULL, linux_removed, NULL);
  else busy(w, NULL);
}

/* Espace occupe par le conteneur : du peut prendre quelques secondes. */
static void linux_size_done(GObject *o, GAsyncResult *res, gpointer lbl) {
  char *out = NULL;
  if (g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), res, &out, NULL, NULL) && out && GTK_IS_LABEL(lbl)) {
    char *sz = g_format_size(g_ascii_strtoull(out, NULL, 10));
    gtk_label_set_text(GTK_LABEL(lbl), sz);
    g_free(sz);
  }
  g_free(out); g_object_unref(o);
  if (lbl) g_object_unref(lbl);
}

static int linux_app_count(void) {
  char *dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
  GDir *d = g_dir_open(dir, 0, NULL);
  int n = 0;
  const char *f;
  while (d && (f = g_dir_read_name(d))) if (g_str_has_prefix(f, "linux-") && g_str_has_suffix(f, ".desktop")) n++;
  if (d) g_dir_close(d);
  g_free(dir);
  return n;
}

static gboolean linux_poll(gpointer d) {
  if (!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(stack)), "linux")) refresh_linux();
  return G_SOURCE_REMOVE;
}

static void linux_state_done(GObject *o, GAsyncResult *res, gpointer d) {
  gboolean installed = g_subprocess_wait_check_finish(G_SUBPROCESS(o), res, NULL);
  g_object_unref(o);
  clear_box(linux_grp);
  char *mark = g_build_filename(g_get_user_runtime_dir(), "lumina-linux-setup", NULL);
  gboolean installing = g_file_test(mark, G_FILE_TEST_EXISTS);
  g_free(mark);
  if (installing) {
    row(linux_grp, "Installation en cours…", "Suivez la progression dans la fenêtre Terminal.", NULL, NULL);
    g_timeout_add_seconds(4, linux_poll, NULL);
  } else if (installed) {
    row(linux_grp, "Linux est installé", "Debian 13 dans un conteneur. Installez des applis avec : sudo apt install <paquet>",
        button("Ouvrir le Terminal", "fbtn", G_CALLBACK(on_linux_open), NULL), NULL);
    int n = linux_app_count();
    char *apps = n ? g_strdup_printf("%d appli%s graphique%s dans le tiroir", n, n > 1 ? "s" : "", n > 1 ? "s" : "")
                   : g_strdup("Aucune appli graphique pour l'instant");
    row(linux_grp, "Applis Linux", apps, NULL, NULL);
    g_free(apps);
    GtkWidget *sz = label("Calcul…", "s", 1);
    row(linux_grp, "Espace utilisé", NULL, sz, NULL);
    char *store = g_build_filename(g_get_user_data_dir(), "containers", NULL);
    char *cmd = g_strdup_printf("du -sb '%s' 2>/dev/null | cut -f1", store);
    GSubprocess *sp = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                                       "sh", "-c", cmd, NULL);
    if (sp) g_subprocess_communicate_utf8_async(sp, NULL, NULL, linux_size_done, g_object_ref(sz));
    g_free(store); g_free(cmd);
    row(linux_grp, "Mettre à jour Linux", "Met à jour les paquets Debian du conteneur (apt full-upgrade).",
        button("Mettre à jour", "tbtn", G_CALLBACK(on_linux_upgrade), NULL), NULL);
    linux_more(linux_grp, TRUE);
    row(linux_grp, "Supprimer Linux", "Efface le conteneur et ses applis. Vos fichiers personnels sont conservés.",
        button("Supprimer", "tbtn", G_CALLBACK(on_linux_remove), NULL), NULL);
  } else {
    row(linux_grp, "Linux n'est pas installé", "Téléchargement d'environ 250 Mo, puis quelques minutes de préparation.",
        button("Installer", "fbtn", G_CALLBACK(on_linux_install), NULL), NULL);
    linux_more(linux_grp, FALSE);
  }
  gtk_widget_show_all(linux_grp);
}

static void refresh_linux(void) {
  GSubprocess *sp = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                                     "podman", "container", "exists", "linux", NULL);
  if (sp) g_subprocess_wait_check_async(sp, NULL, linux_state_done, NULL);
}

static void build_linux(void) {
  GtkWidget *p = page_box("linux", "Linux");
  gtk_container_add(GTK_CONTAINER(p), desc("Un Debian complet dans un conteneur, comme sur ChromeOS : installez des "
                                            "applis avec apt sans toucher au système. Votre dossier personnel est partagé."));
  linux_grp = group(p, NULL);
  row(linux_grp, "Vérification…", NULL, NULL, NULL);
  linux_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(linux_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), linux_msg);
}

static void on_android_install(GtkWidget *w, gpointer d) {
  /* Dans un terminal : l'installation (~1 Go) affiche sa progression */
  const char *argv[] = {"foot", "-T", "Installation d'Android", "sh", "-c",
                        "pkexec /usr/local/bin/lumina-android --install; echo; echo 'Appuyez sur Entrée pour fermer.'; read r", NULL};
  spawn_async(argv);
}

static void build_android(void) {
  GtkWidget *p = page_box("android", "Applis Android");
  GtkWidget *g = group(p, NULL);
  char *mi = read_file("/proc/meminfo");
  long kb = mi ? atol(strstr(mi, "MemTotal:") ? strstr(mi, "MemTotal:") + 9 : "0") : 0;
  g_free(mi);
  char *wd = g_find_program_in_path("waydroid");
  if (wd) row(g, "Android est installé", "Les applis Android apparaissent dans le tiroir. Android démarre à l'ouverture de la première.", NULL, NULL);
  else if (kb < 3500L * 1024)
    row(g, "Indisponible sur ce PC", "Android demande au moins 4 Go de mémoire : il ralentirait tout le système ici.", NULL, NULL);
  else
    row(g, "Installer Android", "Téléchargement d'environ 1 Go. Les applis s'ouvrent ensuite depuis le tiroir.",
        button("Installer", "fbtn", G_CALLBACK(on_android_install), NULL), NULL);
  g_free(wd);
}

/* ------------------------------------------------------------------ */
/* Mises a jour                                                        */
/* ------------------------------------------------------------------ */
static GtkWidget *upd_sub, *upd_btn;
static guint upd_poll;

static gboolean refresh_update(gpointer d) {
  char *st = read_file("/run/lumina-update/status"), *ver = read_file("/etc/lumina/version");
  char *t;
  gboolean busy = st && (g_str_has_prefix(st, "checking") || g_str_has_prefix(st, "downloading"));
  if (!st) t = g_strdup("Pas encore vérifié depuis le démarrage");
  else if (g_str_has_prefix(st, "uptodate")) t = g_strdup("Votre système est à jour");
  else if (g_str_has_prefix(st, "checking")) t = g_strdup("Recherche en cours…");
  else if (g_str_has_prefix(st, "downloading ")) t = g_strdup_printf("Téléchargement de la version %s…", st + 12);
  else if (g_str_has_prefix(st, "ready ")) t = g_strdup_printf("La version %s est prête : redémarrez pour l'utiliser", st + 6);
  else if (g_str_has_prefix(st, "error ")) t = g_strdup_printf("Échec : %s", st + 6);
  else t = g_strdup(st);
  gtk_label_set_text(GTK_LABEL(upd_sub), t);
  gboolean ready = st && g_str_has_prefix(st, "ready ");
  gtk_button_set_label(GTK_BUTTON(upd_btn), ready ? "Redémarrer" : "Rechercher");
  g_object_set_data(G_OBJECT(upd_btn), "ready", GINT_TO_POINTER(ready));
  gtk_widget_set_sensitive(upd_btn, !busy);
  g_free(st); g_free(ver); g_free(t);
  if (!busy) upd_poll = 0;
  return busy ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void on_update(GtkWidget *w, gpointer d) {
  if (g_object_get_data(G_OBJECT(w), "ready")) {
    const char *argv[] = {"systemctl", "reboot", NULL}; spawn_async(argv); return;
  }
  /* Autorise par /etc/polkit-1/rules.d/50-lumina.rules (ce service seulement) */
  const char *argv[] = {"systemctl", "start", "--no-block", "lumina-update.service", NULL};
  spawn_async(argv);
  gtk_label_set_text(GTK_LABEL(upd_sub), "Recherche en cours…");
  gtk_widget_set_sensitive(upd_btn, FALSE);
  if (!upd_poll) upd_poll = g_timeout_add_seconds(2, refresh_update, NULL);
}

static void build_updates(void) {
  GtkWidget *p = page_box("maj", "Mises à jour");
  char *ver = read_file("/etc/lumina/version");
  char *t = g_strdup_printf("LuminaOS %s", ver ? ver : "?");
  upd_btn = button("Rechercher", "fbtn", G_CALLBACK(on_update), NULL);
  row(group(p, NULL), t, "", upd_btn, &upd_sub);
  gtk_container_add(GTK_CONTAINER(p), desc("LuminaOS vérifie seul les mises à jour, les installe en arrière-plan sur "
                                            "une partition de secours, et revient à la version précédente si la "
                                            "nouvelle ne démarre pas."));
  g_free(ver); g_free(t);
  refresh_update(NULL);
  if (gtk_widget_get_sensitive(upd_btn) == FALSE && !upd_poll) upd_poll = g_timeout_add_seconds(2, refresh_update, NULL);
  updates_more(p);
}

/* ------------------------------------------------------------------ */
/* Stockage, a propos                                                  */
/* ------------------------------------------------------------------ */
static void build_storage(void) {
  GtkWidget *p = page_box("stockage", "Stockage");
  GtkWidget *g = group(p, NULL);
  struct statvfs s;
  if (statvfs("/var/lib/lumina", &s) == 0) {
    guint64 tot = (guint64)s.f_blocks * s.f_frsize, fr = (guint64)s.f_bavail * s.f_frsize;
    char *u = g_format_size(tot - fr), *t = g_format_size(tot), *f = g_format_size(fr);
    char *sub = g_strdup_printf("%s utilisés sur %s · %s libres", u, t, f);
    GtkWidget *lb = gtk_level_bar_new_for_interval(0, 1);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(lb), tot ? (double)(tot - fr) / tot : 0);
    gtk_widget_set_size_request(lb, 180, -1);
    row(g, "Vos fichiers et applis", sub, lb, NULL);
    g_free(u); g_free(t); g_free(f); g_free(sub);
  }
  char *mi = read_file("/proc/meminfo");
  if (mi) {
    long tot = atol(strstr(mi, "MemTotal:") + 9), av = atol(strstr(mi, "MemAvailable:") + 13);
    char *u = g_format_size((guint64)(tot - av) * 1024), *t = g_format_size((guint64)tot * 1024);
    char *sub = g_strdup_printf("%s utilisés sur %s", u, t);
    GtkWidget *lb = gtk_level_bar_new_for_interval(0, 1);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(lb), (double)(tot - av) / tot);
    gtk_widget_set_size_request(lb, 180, -1);
    row(g, "Mémoire", sub, lb, NULL);
    g_free(u); g_free(t); g_free(sub); g_free(mi);
  }
}

static void on_github(GtkWidget *w, gpointer d) {
  const char *argv[] = {"lumina-browser", "https://github.com/mystery2312/lumina-os", NULL};
  spawn_async(argv);
}

static void build_about(void) {
  GtkWidget *p = page_box("apropos", "À propos");
  const char *f = dark ? "/usr/share/icons/hicolor/scalable/apps/luminaos-dark.svg" : "/usr/share/icons/hicolor/scalable/apps/luminaos.svg";
  GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(f, 72, 72, NULL);
  if (pb) { GtkWidget *im = gtk_image_new_from_pixbuf(pb); gtk_widget_set_halign(im, GTK_ALIGN_START); gtk_container_add(GTK_CONTAINER(p), im); g_object_unref(pb); }
  GtkWidget *g = group(p, NULL);
  char *ver = read_file("/etc/lumina/version");
  row(g, "Version", ver ? ver : "?", NULL, NULL);
  g_free(ver);
  char *cpu = NULL, *ci = read_file("/proc/cpuinfo");
  if (ci) { char *m = strstr(ci, "model name"); if (m && (m = strchr(m, ':'))) { char *e = strchr(m, '\n'); cpu = g_strstrip(g_strndup(m + 1, e ? (gsize)(e - m - 1) : strlen(m + 1))); } g_free(ci); }
  row(g, "Processeur", cpu ? cpu : "?", NULL, NULL);
  g_free(cpu);
  char *gp = g_build_filename(g_get_user_runtime_dir(), "lumina-gpu", NULL), *gs = read_file(gp);
  row(g, "Affichage", g_strcmp0(gs, "gpu") ? "Rendu logiciel (pas d'accélération 3D)" : "Accélération 3D", NULL, NULL);
  g_free(gp); g_free(gs);
  /* Diagnostic de lenteur : duree du dernier demarrage et support du
     systeme (une cle USB 2.0 est bien plus lente qu'un disque interne). */
  const char *sa[] = {"systemd-analyze", "time", NULL};
  char *st = run_argv(sa, NULL), *tot = st ? strstr(st, "= ") : NULL;
  if (tot) { char *e = strchr(tot, '\n'); if (e) *e = 0; tot += 2; }
  row(g, "Dernier démarrage", tot ? tot : "En cours…", NULL, NULL);
  g_free(st);
  const char *lv[] = {"lumina-is-live", NULL};
  int lst = 1;
  g_free(run_argv(lv, &lst));
  int live = lst == 0;
  row(g, "Système", live ? "Sur la clé USB (mode essai) : plus lent qu'une fois installé" : "Installé sur ce PC", NULL, NULL);
  row(g, "Base", "Debian 13, noyau Linux, Chromium", NULL, NULL);
  GtkWidget *g2 = group(p, NULL);
  row(g2, "Logiciel libre", "LuminaOS est distribué sous licence GPL-3.0.",
      button("Code source", "tbtn", G_CALLBACK(on_github), NULL), NULL);
  about_more(p);
}

#include "settings-more.c"

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
static const struct { const char *id, *glyph, *title; void (*build)(void); } SECTIONS[] = {
  {"reseau", "wifi", "Réseau", build_network},
  {"bluetooth", "bt", "Bluetooth", build_bluetooth},
  {"affichage", "sun", "Affichage", build_display},
  {"ecrans", "monitor", "Écrans", build_screens},
  {"son", "vol", "Son", build_sound},
  {"clavier", "kbd", "Clavier et langue", build_keyboard},
  {"souris", "mouse", "Souris et pavé tactile", build_mouse},
  {"compte", "lock", "Compte et verrouillage", build_account},
  {"linux", "term", "Linux", build_linux},
  {"android", "android", "Applis Android", build_android},
  {"imprimantes", "print", "Imprimantes", build_printers},
  {"heure", "clock", "Date et heure", build_datetime},
  {"energie", "battery", "Énergie", build_power},
  {"accessibilite", "access", "Accessibilité", build_access},
  {"raccourcis", "kbd", "Raccourcis clavier", build_shortcuts},
  {"maj", "reboot", "Mises à jour", build_updates},
  {"stockage", "archive", "Stockage", build_storage},
  {"apropos", "info", "À propos", build_about},
};

/* Pages construites a la premiere visite : l'appli s'ouvre vite, meme sur
   un vieux PC (certaines pages interrogent le systeme : fuseaux horaires,
   imprimantes, ecrans...). */
static void ensure_page(const char *id) {
  if (gtk_stack_get_child_by_name(GTK_STACK(stack), id)) return;
  for (guint i = 0; i < G_N_ELEMENTS(SECTIONS); i++)
    if (!strcmp(SECTIONS[i].id, id)) {
      SECTIONS[i].build();
      gtk_widget_show_all(gtk_stack_get_child_by_name(GTK_STACK(stack), id));
    }
}

static void on_nav(GtkListBox *lb, GtkListBoxRow *r, gpointer d) {
  if (!r) return;
  const char *id = g_object_get_data(G_OBJECT(r), "id");
  gboolean fresh = !gtk_stack_get_child_by_name(GTK_STACK(stack), id);
  ensure_page(id);
  gtk_stack_set_visible_child_name(GTK_STACK(stack), id);
  if (fresh && strcmp(id, "linux")) return;
  if (!strcmp(id, "linux")) refresh_linux();
  else if (!strcmp(id, "ecrans")) fill_screens();
  else if (!strcmp(id, "imprimantes")) fill_printers();
}

/* Retour sur la fenetre (apres le Terminal, par exemple) : la page Linux
   reflete le nouvel etat. */
static gboolean on_focus_in(GtkWidget *w, GdkEvent *e, gpointer d) {
  if (!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(stack)), "linux")) refresh_linux();
  return FALSE;
}

/* Theme, accent ou fond d'ecran changes (ici ou ailleurs) */
static void on_theme_ext(void) { if (wall_sub) update_wall_row(); }


/* Mots-cles de la recherche des Reglages (en plus du titre de la page) */
static const struct { const char *id, *kw; } KEYWORDS[] = {
  {"reseau", "wifi wi-fi internet ethernet cable connexion reseau mot de passe"},
  {"bluetooth", "casque ecouteurs souris clavier appairer enceinte"},
  {"affichage", "theme sombre clair fond d ecran couleur luminosite eclairage nocturne veille photo"},
  {"ecrans", "resolution definition taille zoom echelle rotation second ecran projecteur recopie"},
  {"son", "volume haut-parleur micro casque sortie entree muet"},
  {"clavier", "langue azerty qwerty disposition traduction"},
  {"souris", "pave tactile touchpad vitesse defilement clic gaucher"},
  {"compte", "mot de passe pin code verrouillage nom personnes invite utilisateur"},
  {"linux", "terminal debian apt conteneur sauvegarde"},
  {"android", "applis play waydroid"},
  {"imprimantes", "impression imprimer scanner cups"},
  {"heure", "date fuseau horaire horloge ntp"},
  {"energie", "batterie capot veille alimentation"},
  {"accessibilite", "lecteur d ecran loupe contraste texte grand curseur malvoyant"},
  {"raccourcis", "clavier touches combinaisons"},
  {"maj", "mise a jour version beta canal"},
  {"stockage", "disque espace memoire fichiers"},
  {"apropos", "version processeur reinitialiser powerwash informations"},
};
static GtkWidget *nav_search;
static char *fold(const char *s) {
  char *c = g_utf8_casefold(s, -1), *a = g_str_to_ascii(c, NULL);
  g_free(c);
  return a;
}
static gboolean nav_filter(GtkListBoxRow *r, gpointer d) {
  const char *q = nav_search ? gtk_entry_get_text(GTK_ENTRY(nav_search)) : "";
  if (!*q) return TRUE;
  const char *id = g_object_get_data(G_OBJECT(r), "id");
  const char *title = g_object_get_data(G_OBJECT(r), "title");
  char *hay = g_strdup(title ? title : "");
  for (guint i = 0; i < G_N_ELEMENTS(KEYWORDS); i++)
    if (!g_strcmp0(KEYWORDS[i].id, id)) { char *t = g_strconcat(hay, " ", KEYWORDS[i].kw, NULL); g_free(hay); hay = t; }
  char *fh = fold(hay), *fq = fold(q);
  /* Tous les mots de la recherche doivent apparaitre */
  gboolean ok = TRUE;
  char **words = g_strsplit(fq, " ", -1);
  for (char **w = words; *w && ok; w++) if (**w && !strstr(fh, *w)) ok = FALSE;
  g_strfreev(words); g_free(fh); g_free(fq); g_free(hay);
  return ok;
}
static void on_nav_search(GtkEditable *e, gpointer d) { gtk_list_box_invalidate_filter(GTK_LIST_BOX(nav)); }
static void on_nav_search_go(GtkEntry *e, gpointer d) {
  GList *k = gtk_container_get_children(GTK_CONTAINER(nav));
  for (GList *l = k; l; l = l->next)
    if (gtk_widget_get_child_visible(l->data) && nav_filter(l->data, NULL)) { gtk_list_box_select_row(GTK_LIST_BOX(nav), l->data); break; }
  g_list_free(k);
}

static gboolean on_win_type(GtkWidget *w, GdkEventKey *ev, gpointer d) {
  GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(w));
  if (f && GTK_IS_EDITABLE(f)) return FALSE;
  if (ev->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) return FALSE;
  return gtk_search_entry_handle_event(GTK_SEARCH_ENTRY(nav_search), (GdkEvent *)ev);
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-settings");
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, on_theme_ext);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-settings");
  gtk_window_set_title(GTK_WINDOW(win), "Réglages");
  gtk_window_set_default_size(GTK_WINDOW(win), 900, 620);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,maximize,close");
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), label("Réglages", "apptitle", 0));
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *navsc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(navsc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(navsc, 240, -1);
  /* (classe "nav" portee par la colonne, recherche comprise) */
  nav = gtk_list_box_new();
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(nav), TRUE);
  gtk_list_box_set_filter_func(GTK_LIST_BOX(nav), nav_filter, NULL, NULL);
  g_signal_connect(nav, "row-selected", G_CALLBACK(on_nav), NULL);
  gtk_container_add(GTK_CONTAINER(navsc), nav);
  /* Recherche au-dessus de la liste des pages */
  GtkWidget *navcol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(navcol, "nav");
  nav_search = gtk_search_entry_new();
  add_class(nav_search, "navsearch");
  gtk_entry_set_placeholder_text(GTK_ENTRY(nav_search), "Rechercher un réglage");
  g_signal_connect(nav_search, "changed", G_CALLBACK(on_nav_search), NULL);
  g_signal_connect(nav_search, "activate", G_CALLBACK(on_nav_search_go), NULL);
  gtk_container_add(GTK_CONTAINER(navcol), nav_search);
  gtk_widget_set_vexpand(navsc, TRUE);
  gtk_container_add(GTK_CONTAINER(navcol), navsc);
  gtk_box_pack_start(GTK_BOX(body), navcol, FALSE, FALSE, 0);
  stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  add_class(stack, "content");
  gtk_box_pack_start(GTK_BOX(body), stack, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(win), body);

  const char *want = argc > 1 ? argv[1] : "reseau";
  GtkListBoxRow *sel = NULL;
  for (guint i = 0; i < G_N_ELEMENTS(SECTIONS); i++) {
    GtkWidget *r = gtk_list_box_row_new();
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    add_class(h, "item");
    gtk_container_add(GTK_CONTAINER(h), icon_new(SECTIONS[i].glyph, R_ON_VAR, 20)->img);
    gtk_container_add(GTK_CONTAINER(h), label(SECTIONS[i].title, NULL, 0));
    gtk_container_add(GTK_CONTAINER(r), h);
    g_object_set_data(G_OBJECT(r), "id", (gpointer)SECTIONS[i].id);
    g_object_set_data(G_OBJECT(r), "title", (gpointer)SECTIONS[i].title);
    gtk_container_add(GTK_CONTAINER(nav), r);
    if (!strcmp(want, SECTIONS[i].id)) sel = GTK_LIST_BOX_ROW(r);
  }
  gtk_widget_show_all(win);
  if (sel) gtk_list_box_select_row(GTK_LIST_BOX(nav), sel);
  /* Pas de focus initial dans la recherche (son texte indicatif reste
     visible) ; une frappe hors d'un champ de saisie y part directement. */
  gtk_window_set_focus(GTK_WINDOW(win), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_win_type), NULL);
  gtk_main();
  return 0;
}

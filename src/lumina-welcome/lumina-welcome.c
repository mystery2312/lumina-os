/*
 * lumina-welcome : l'assistant de premier demarrage de LuminaOS.
 *
 * Lance par la session tant que le compte n'est pas configure (apres une
 * installation). Etapes : langue et clavier -> Wi-Fi (si besoin) -> compte
 * (nom, mot de passe) -> termine. Les choix de session vont dans
 * ~/.config/lumina/session.conf ; le compte passe par lumina-account (pkexec).
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <string.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-welcome { background: @surface; }\n"
  ".card { padding: 40px 0; }\n"
  ".title { font-size: 30px; color: @on; }\n"
  ".text { font-size: 14.5px; color: @on_var; }\n"
  ".section { font-size: 12.5px; font-weight: 500; color: @on_var; }\n"
  ".opt { background: @c_low; border-radius: 16px; padding: 12px 16px; color: @on; }\n"
  ".opt:hover { background: @c; }\n"
  ".opt:checked { background: @sec_c; color: @on_sec_c; }\n"
  ".opt .meta { font-size: 12px; color: @on_var; }\n"
  ".field { background: @c_low; color: @on; border: 1px solid @outline_var; border-radius: 8px;\n"
  "         padding: 11px 14px; box-shadow: none; caret-color: @pri; }\n"
  ".field:focus { border: 2px solid @pri; padding: 10px 13px; }\n"
  ".err { color: #ba1a1a; font-size: 13px; }\n"
  ".tbtn { background: transparent; color: @pri; border-radius: 20px; padding: 10px 18px; font-weight: 500; }\n"
  ".tbtn:hover { background: alpha(@pri, 0.08); }\n"
  ".fbtn { background: @pri; color: @on_pri; border-radius: 20px; padding: 10px 24px; font-weight: 500; }\n"
  ".fbtn:disabled { background: alpha(@on, 0.12); color: alpha(@on, 0.38); }\n"
  ".dots { color: @outline_var; font-size: 18px; letter-spacing: 4px; }\n";

static GtkWidget *win, *stack, *kb_test, *wifi_box, *wifi_pass, *wifi_status, *wifi_next;
static GtkWidget *name_e, *pass_e, *pass2_e, *acc_err, *done_title;
static char *wifi_dev, *wifi_ssid_sel;
static const char *lang = "fr", *keymap = "fr", *variant = "";

static const struct { const char *code, *label, *meta; } LANGS[] = {
  {"fr", "Français", "Interface en français"},
  {"en-US", "English", "English interface"},
};
static const struct { const char *layout, *variant, *label, *meta; } KEYMAPS[] = {
  {"fr", "", "Français", "AZERTY"},
  {"be", "", "Belge", "AZERTY"},
  {"ch", "fr", "Suisse romand", "QWERTZ"},
  {"ca", "", "Canadien français", "QWERTY"},
  {"us", "", "Anglais (États-Unis)", "QWERTY"},
};

static void go(const char *n) { gtk_stack_set_visible_child_name(GTK_STACK(stack), n); }

static GtkWidget *wrap(const char *t, const char *cls) {
  GtkWidget *l = label(t, cls, 0);
  gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(l), 52);
  return l;
}

static GtkWidget *btn(const char *t, const char *cls, GCallback cb) {
  GtkWidget *b = gtk_button_new_with_label(t);
  add_class(b, cls);
  g_signal_connect_swapped(b, "clicked", cb, NULL);
  return b;
}

/* Bouton qui mene a une page */
static GtkWidget *btn_to(const char *t, const char *cls, const char *pg) {
  GtkWidget *b = gtk_button_new_with_label(t);
  add_class(b, cls);
  g_signal_connect_swapped(b, "clicked", G_CALLBACK(go), (gpointer)pg);
  return b;
}

static GtkWidget *buttons(void) {
  GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(bb, GTK_ALIGN_END);
  gtk_widget_set_margin_top(bb, 12);
  return bb;
}

/* Colonne centree de 520 px sur fond plein ecran */
static GtkWidget *page(const char *name) {
  GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *p = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  add_class(p, "card");
  gtk_widget_set_size_request(p, 520, -1);
  gtk_widget_set_halign(p, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(p, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(p, TRUE);
  gtk_container_add(GTK_CONTAINER(outer), p);
  gtk_stack_add_named(GTK_STACK(stack), outer, name);
  return p;
}

static GtkWidget *logo(int px) {
  const char *f = dark ? "/usr/share/icons/hicolor/scalable/apps/luminaos-dark.svg"
                       : "/usr/share/icons/hicolor/scalable/apps/luminaos.svg";
  GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(f, px, px, NULL);
  GtkWidget *img = pb ? gtk_image_new_from_pixbuf(pb) : gtk_image_new();
  if (pb) g_object_unref(pb);
  gtk_widget_set_halign(img, GTK_ALIGN_START);
  return img;
}

/* Carte cliquable (bouton radio sans pastille) */
static GtkWidget *option(GtkWidget *group, const char *title, const char *meta) {
  GtkWidget *rb = group ? gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(group)) : gtk_radio_button_new(NULL);
  gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(rb), FALSE);
  add_class(rb, "opt");
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_container_add(GTK_CONTAINER(v), label(title, NULL, 0));
  if (meta) gtk_container_add(GTK_CONTAINER(v), label(meta, "meta", 0));
  gtk_container_add(GTK_CONTAINER(rb), v);
  return rb;
}

/* ------------------------------------------------------------------ */
/* Langue et clavier                                                   */
/* ------------------------------------------------------------------ */
static void on_lang(GtkToggleButton *b, gpointer d) {
  if (gtk_toggle_button_get_active(b)) lang = LANGS[GPOINTER_TO_INT(d)].code;
}

/* Le clavier s'applique tout de suite (labwc relit son environnement) :
   on peut l'essayer dans le champ en dessous. */
static void on_keymap(GtkToggleButton *b, gpointer d) {
  if (!gtk_toggle_button_get_active(b)) return;
  int i = GPOINTER_TO_INT(d);
  keymap = KEYMAPS[i].layout; variant = KEYMAPS[i].variant;
  char *dir = g_build_filename(g_get_user_config_dir(), "labwc", NULL);
  char *f = g_build_filename(dir, "environment", NULL);
  char *t = g_strdup_printf("XKB_DEFAULT_LAYOUT=%s\nXKB_DEFAULT_VARIANT=%s\n", keymap, variant);
  g_mkdir_with_parents(dir, 0755);
  g_file_set_contents(f, t, -1, NULL);
  const char *argv[] = {"labwc", "--reconfigure", NULL};
  spawn_async(argv);
  g_free(dir); g_free(f); g_free(t);
  if (kb_test) gtk_entry_set_text(GTK_ENTRY(kb_test), "");
}

/* ------------------------------------------------------------------ */
/* Wi-Fi                                                               */
/* ------------------------------------------------------------------ */
static void fill_wifi(void);

static void on_net(GtkToggleButton *b, gpointer d) {
  if (!gtk_toggle_button_get_active(b)) return;
  g_free(wifi_ssid_sel);
  wifi_ssid_sel = g_strdup(g_object_get_data(G_OBJECT(b), "ssid"));
  gboolean open = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "open"));
  gtk_widget_set_visible(wifi_pass, !open);
  if (!open) gtk_widget_grab_focus(wifi_pass);
  gtk_button_set_label(GTK_BUTTON(wifi_next), "Se connecter");
}

static gboolean rescan(gpointer d) { fill_wifi(); return G_SOURCE_REMOVE; }

static void fill_wifi(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(wifi_box));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  GPtrArray *nets = net_wifi_list(wifi_dev);
  GtkWidget *group = NULL;
  for (guint i = 0; i < nets->len && i < 6; i++) {
    Net *n = nets->pdata[i];
    gboolean open = !g_strcmp0(n->security, "open");
    GtkWidget *o = option(group, n->ssid, open ? "Ouvert" : "Protégé par un mot de passe");
    if (!group) group = o;
    g_object_set_data_full(G_OBJECT(o), "ssid", g_strdup(n->ssid), g_free);
    g_object_set_data(G_OBJECT(o), "open", GINT_TO_POINTER(open));
    g_signal_connect(o, "toggled", G_CALLBACK(on_net), NULL);
    gtk_container_add(GTK_CONTAINER(wifi_box), o);
  }
  if (group) {   /* rien de preselectionne */
    GtkWidget *none = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(group));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(none), TRUE);
    g_object_ref_sink(none);
  }
  if (!nets->len) gtk_container_add(GTK_CONTAINER(wifi_box), wrap("Recherche des réseaux…", "text"));
  g_ptr_array_free(nets, TRUE);
  gtk_widget_show_all(wifi_box);
}

static void on_connected(GObject *src, GAsyncResult *res, gpointer d) {
  GSubprocess *sp = G_SUBPROCESS(src);
  g_subprocess_wait_finish(sp, res, NULL);
  gboolean ok = g_subprocess_get_successful(sp);
  g_object_unref(sp);
  gtk_widget_set_sensitive(wifi_next, TRUE);
  if (ok) { gtk_label_set_text(GTK_LABEL(wifi_status), ""); go("account"); }
  else {
    gtk_label_set_text(GTK_LABEL(wifi_status), "Connexion impossible : vérifiez le mot de passe.");
    gtk_widget_show(wifi_pass);
  }
}

static void on_wifi_next(void) {
  if (!wifi_ssid_sel) { go("account"); return; }
  const char *pw = gtk_entry_get_text(GTK_ENTRY(wifi_pass));
  gtk_label_set_text(GTK_LABEL(wifi_status), "Connexion…");
  gtk_widget_set_sensitive(wifi_next, FALSE);
  net_wifi_connect(wifi_dev, wifi_ssid_sel, *pw && gtk_widget_get_visible(wifi_pass) ? pw : NULL, on_connected, NULL);
}

static void on_lang_next(void) {
  /* Wi-Fi seulement s'il y a une carte et pas deja de connexion */
  wifi_dev = net_wifi_device();
  if (!wifi_dev || net_wifi_up(wifi_dev) || net_wired_up(wifi_dev)) { go("account"); return; }
  if (!net_wifi_powered(wifi_dev)) net_wifi_power(wifi_dev, TRUE);
  net_wifi_scan(wifi_dev);
  fill_wifi();
  g_timeout_add_seconds(4, rescan, NULL);
  go("wifi");
}

/* ------------------------------------------------------------------ */
/* Compte                                                              */
/* ------------------------------------------------------------------ */
static gboolean run_account(const char *const *argv, const char *input) {
  GError *err = NULL;
  GSubprocess *sp = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                      G_SUBPROCESS_FLAGS_STDERR_SILENCE, &err);
  if (!sp) { g_clear_error(&err); return FALSE; }
  gboolean ok = g_subprocess_communicate_utf8(sp, input, NULL, NULL, NULL, &err) &&
                g_subprocess_get_successful(sp);
  g_clear_error(&err);
  g_object_unref(sp);
  return ok;
}

static void finish_account(const char *password) {
  const char *name = gtk_entry_get_text(GTK_ENTRY(name_e));
  const char *set_name[] = {"pkexec", "/usr/local/sbin/lumina-account", "set-name", name, NULL};
  const char *set_pw[] = {"pkexec", "/usr/local/sbin/lumina-account", "set-password", NULL};
  char *input = g_strdup_printf("\n%s\n", password);
  gboolean ok = (!*name || run_account(set_name, NULL)) && run_account(set_pw, input);
  g_free(input);
  if (!ok) { gtk_label_set_text(GTK_LABEL(acc_err), "Le compte n'a pas pu être enregistré."); return; }
  char *t = *name ? g_strdup_printf("Tout est prêt, %s", name) : g_strdup("Tout est prêt");
  gtk_label_set_text(GTK_LABEL(done_title), t);
  g_free(t);
  go("done");
}

static void on_acc_next(void) {
  const char *a = gtk_entry_get_text(GTK_ENTRY(pass_e)), *b = gtk_entry_get_text(GTK_ENTRY(pass2_e));
  if (strlen(a) < 4) { gtk_label_set_text(GTK_LABEL(acc_err), "Choisissez un mot de passe d'au moins 4 caractères."); return; }
  if (strcmp(a, b)) { gtk_label_set_text(GTK_LABEL(acc_err), "Les deux mots de passe ne correspondent pas."); return; }
  gtk_label_set_text(GTK_LABEL(acc_err), "");
  finish_account(a);
}

static void on_acc_skip(void) { finish_account(""); }

/* ------------------------------------------------------------------ */
/* Fin                                                                 */
/* ------------------------------------------------------------------ */
static void on_done(void) {
  /* Choix de session, conserves dans le dossier personnel */
  char *dir = g_build_filename(g_get_user_config_dir(), "lumina", NULL);
  char *f = g_build_filename(dir, "session.conf", NULL);
  char *t = g_strdup_printf("# Choisi a l'assistant de premier demarrage (modifiable dans Reglages)\n"
                            "LUMINA_LANG=%s\nLUMINA_KEYMAP=%s\nLUMINA_KEYMAP_VARIANT=%s\n", lang, keymap, variant);
  g_mkdir_with_parents(dir, 0755);
  g_file_set_contents(f, t, -1, NULL);
  g_free(dir); g_free(f); g_free(t);
  const char *fin[] = {"pkexec", "/usr/local/sbin/lumina-account", "finish", NULL};
  run_account(fin, NULL);

  if (strcmp(lang, "fr")) {
    /* Autre langue : la session redemarre pour l'appliquer a tout. */
    const char *argv[] = {"labwc", "--exit", NULL};
    spawn_async(argv);
  } else {
    const char *idle[] = {"lumina-idle", NULL}, *web[] = {"lumina-browser", NULL};
    spawn_async(idle);
    spawn_async(web);
  }
  gtk_widget_destroy(win);
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
static GtkWidget *pw_field(const char *ph) {
  GtkWidget *e = gtk_entry_new();
  add_class(e, "field");
  gtk_entry_set_visibility(GTK_ENTRY(e), FALSE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(e), ph);
  return e;
}

static void build(void) {
  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-welcome");
  gtk_window_set_title(GTK_WINDOW(win), "Bienvenue");
  gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
  gtk_window_fullscreen(GTK_WINDOW(win));
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

  /* Accueil */
  GtkWidget *p = page("hello");
  gtk_container_add(GTK_CONTAINER(p), logo(96));
  gtk_container_add(GTK_CONTAINER(p), wrap("Bienvenue dans LuminaOS", "title"));
  gtk_container_add(GTK_CONTAINER(p), wrap("Quelques réglages et votre PC est prêt : langue, clavier, "
                                           "réseau et mot de passe. Comptez une minute.", "text"));
  GtkWidget *bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn_to("Commencer", "fbtn", "lang"));
  gtk_container_add(GTK_CONTAINER(p), bb);

  /* Langue et clavier */
  p = page("lang");
  gtk_container_add(GTK_CONTAINER(p), wrap("Langue et clavier", "title"));
  gtk_container_add(GTK_CONTAINER(p), label("Langue", "section", 0));
  GtkWidget *g = NULL, *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
  for (guint i = 0; i < G_N_ELEMENTS(LANGS); i++) {
    GtkWidget *o = option(g, LANGS[i].label, LANGS[i].meta);
    if (!g) g = o;
    g_signal_connect(o, "toggled", G_CALLBACK(on_lang), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(row), o);
  }
  gtk_container_add(GTK_CONTAINER(p), row);
  gtk_container_add(GTK_CONTAINER(p), label("Clavier", "section", 0));
  GtkWidget *kg = NULL, *grid = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(grid), GTK_SELECTION_NONE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid), 2);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), 2);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid), TRUE);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid), 8);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid), 8);
  for (guint i = 0; i < G_N_ELEMENTS(KEYMAPS); i++) {
    GtkWidget *o = option(kg, KEYMAPS[i].label, KEYMAPS[i].meta);
    if (!kg) kg = o;
    g_signal_connect(o, "toggled", G_CALLBACK(on_keymap), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(grid), o);
  }
  gtk_container_add(GTK_CONTAINER(p), grid);
  kb_test = gtk_entry_new();
  add_class(kb_test, "field");
  gtk_entry_set_placeholder_text(GTK_ENTRY(kb_test), "Essayez votre clavier ici");
  gtk_container_add(GTK_CONTAINER(p), kb_test);
  bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn("Suivant", "fbtn", G_CALLBACK(on_lang_next)));
  gtk_container_add(GTK_CONTAINER(p), bb);

  /* Wi-Fi */
  p = page("wifi");
  gtk_container_add(GTK_CONTAINER(p), wrap("Connexion à Internet", "title"));
  gtk_container_add(GTK_CONTAINER(p), wrap("Choisissez votre réseau Wi-Fi.", "text"));
  wifi_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add(GTK_CONTAINER(p), wifi_box);
  wifi_pass = pw_field("Mot de passe du réseau");
  gtk_widget_set_no_show_all(wifi_pass, TRUE);
  g_signal_connect_swapped(wifi_pass, "activate", G_CALLBACK(on_wifi_next), NULL);
  gtk_container_add(GTK_CONTAINER(p), wifi_pass);
  wifi_status = label("", "err", 0);
  gtk_container_add(GTK_CONTAINER(p), wifi_status);
  bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn_to("Passer", "tbtn", "account"));
  wifi_next = btn("Suivant", "fbtn", G_CALLBACK(on_wifi_next));
  gtk_container_add(GTK_CONTAINER(bb), wifi_next);
  gtk_container_add(GTK_CONTAINER(p), bb);

  /* Compte */
  p = page("account");
  gtk_container_add(GTK_CONTAINER(p), wrap("Votre compte", "title"));
  gtk_container_add(GTK_CONTAINER(p), wrap("Le mot de passe protège votre session : il est demandé à "
                                           "l'ouverture et quand l'écran se verrouille.", "text"));
  name_e = gtk_entry_new();
  add_class(name_e, "field");
  gtk_entry_set_placeholder_text(GTK_ENTRY(name_e), "Votre prénom");
  gtk_container_add(GTK_CONTAINER(p), name_e);
  pass_e = pw_field("Mot de passe");
  pass2_e = pw_field("Confirmez le mot de passe");
  g_signal_connect_swapped(pass2_e, "activate", G_CALLBACK(on_acc_next), NULL);
  gtk_container_add(GTK_CONTAINER(p), pass_e);
  gtk_container_add(GTK_CONTAINER(p), pass2_e);
  acc_err = label("", "err", 0);
  gtk_container_add(GTK_CONTAINER(p), acc_err);
  bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn("Continuer sans mot de passe", "tbtn", G_CALLBACK(on_acc_skip)));
  gtk_container_add(GTK_CONTAINER(bb), btn("Suivant", "fbtn", G_CALLBACK(on_acc_next)));
  gtk_container_add(GTK_CONTAINER(p), bb);

  /* Termine */
  p = page("done");
  gtk_container_add(GTK_CONTAINER(p), logo(96));
  done_title = wrap("Tout est prêt", "title");
  gtk_container_add(GTK_CONTAINER(p), done_title);
  gtk_container_add(GTK_CONTAINER(p), wrap("Le tiroir d'applications est en haut à gauche, les réglages "
                                           "rapides en haut à droite. Windows + L verrouille l'écran.", "text"));
  bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn("Commencer à utiliser LuminaOS", "fbtn", G_CALLBACK(on_done)));
  gtk_container_add(GTK_CONTAINER(p), bb);

  gtk_container_add(GTK_CONTAINER(win), stack);
  gtk_widget_show_all(win);
  go("hello");
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-welcome");
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, NULL);
  build();
  gtk_main();
  return 0;
}

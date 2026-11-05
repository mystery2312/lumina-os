/*
 * lumina-extras.c : fonctions facon ChromeOS du bureau, incluses par
 * lumina-shell.c (elles partagent son etat : fenetres, panneaux, etagere).
 *
 *   - actions a distance (D-Bus org.luminaos.Shell) : raccourcis clavier
 *     de labwc -> "lumina-shell --action overview|clipboard|emoji|..."
 *   - vue d'ensemble : fenetres ouvertes et bureaux virtuels
 *   - historique du presse-papiers (Windows + V) et emojis (Windows + .)
 *   - reglages rapides : Ne pas deranger, Eclairage nocturne, capture et
 *     enregistrement de l'ecran, centre de notifications
 *   - lanceur : calculatrice et recherche de fichiers
 *   - etagere : masquage automatique, position, reorganisation
 */

static char *cfg_path(const char *name) { return g_build_filename(g_get_user_config_dir(), "lumina", name, NULL); }
static gboolean cfg_flag(const char *name) {
  char *p = cfg_path(name); gboolean r = g_file_test(p, G_FILE_TEST_EXISTS); g_free(p); return r;
}
static void cfg_set_flag(const char *name, gboolean on) {
  char *p = cfg_path(name), *dir = g_path_get_dirname(p);
  g_mkdir_with_parents(dir, 0755);
  if (on) g_file_set_contents(p, "1\n", -1, NULL); else g_unlink(p);
  g_free(p); g_free(dir);
}

/* Commande lancee un peu plus tard : le temps que le panneau se ferme et
   que la fenetre precedente retrouve le clavier (frappe d'un emoji,
   collage). */
static gboolean delayed_spawn(gpointer d) {
  char **argv = d;
  spawn_async((const char *const *)argv);
  g_strfreev(argv);
  return G_SOURCE_REMOVE;
}
static void spawn_later(int ms, char **argv) { g_timeout_add(ms, delayed_spawn, argv); }

/* ------------------------------------------------------------------ */
/* Fenetres surgissantes (vue d'ensemble, presse-papiers, emojis)       */
/* ------------------------------------------------------------------ */
static GtkWidget *ov_win, *ov_flow, *ov_empty, *clip_win, *clip_list, *emoji_win, *emoji_flow, *emoji_entry;

static void extras_close(void) {
  if (ov_win) gtk_widget_hide(ov_win);
  if (clip_win) gtk_widget_hide(clip_win);
  if (emoji_win) gtk_widget_hide(emoji_win);
}

static gboolean on_popup_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  if (e->keyval == GDK_KEY_Escape) { close_panels(); return TRUE; }
  return FALSE;
}

static GtkWidget *popup_window(const char *ns, gboolean full) {
  GtkWidget *w = layer_window(GTK_LAYER_SHELL_LAYER_OVERLAY, ns);
  gtk_layer_set_monitor(GTK_WINDOW(w), main_monitor());
  if (full) anchor(w, TRUE, TRUE, TRUE, TRUE);
  else { anchor(w, FALSE, TRUE, FALSE, FALSE); gtk_layer_set_margin(GTK_WINDOW(w), GTK_LAYER_SHELL_EDGE_BOTTOM, 70); }
  gtk_layer_set_keyboard_mode(GTK_WINDOW(w), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
  g_signal_connect(w, "key-press-event", G_CALLBACK(on_popup_key), NULL);
  return w;
}

static void show_popup(GtkWidget *w) {
  close_panels();
  gtk_widget_show(scrim);
  gtk_widget_show(w);
  gtk_window_present(GTK_WINDOW(w));
}

/* ------------------------------------------------------------------ */
/* Vue d'ensemble : fenetres et bureaux                                 */
/* ------------------------------------------------------------------ */
/* labwc n'expose pas ses bureaux aux clients : on passe par ses
   raccourcis (Windows + 1..4), tapes par le clavier virtuel de wtype. */
static void on_desk(GtkButton *b, gpointer d) {
  close_panels();
  char *k = g_strdup_printf("%d", GPOINTER_TO_INT(d));
  char *argv[] = {"wtype", "-M", "logo", k, "-m", "logo", NULL};
  spawn_later(120, g_strdupv(argv));
  g_free(k);
}

static void on_ov_card(GtkButton *b, gpointer d) {
  Toplevel *t = d;
  close_panels();
  if (g_list_find(toplevels, t)) activate(t);
}
static void on_ov_close(GtkButton *b, gpointer d) {
  Toplevel *t = d;
  if (g_list_find(toplevels, t)) zwlr_foreign_toplevel_handle_v1_close(t->h);
  GtkWidget *card = g_object_get_data(G_OBJECT(b), "card");
  if (card) gtk_widget_destroy(gtk_widget_get_parent(card));
}
static gboolean on_ov_bg(GtkWidget *w, GdkEventButton *e, gpointer d) { close_panels(); return TRUE; }

static void fill_overview(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(ov_flow));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  GList *all = g_app_info_get_all();
  int n = 0;
  for (GList *l = toplevels; l; l = l->next) {
    Toplevel *t = l->data;
    GDesktopAppInfo *a = app_for_window(t->app_id, all);
    const char *name = t->title && *t->title ? t->title : a ? g_app_info_get_name(G_APP_INFO(a)) : t->app_id;
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(card, "ovcard");
    if (t->activated) add_class(card, "current");
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    add_class(head, "ovhead");
    GtkWidget *ic = a ? app_icon(a, FALSE) : letter_icon(name);
    gtk_container_add(GTK_CONTAINER(head), ic);
    GtkWidget *tl = label(name ? name : "", "ovtitle", 0);
    gtk_label_set_ellipsize(GTK_LABEL(tl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tl, TRUE);
    gtk_container_add(GTK_CONTAINER(head), tl);
    GtkWidget *x = gtk_button_new();
    add_class(x, "ovclose");
    gtk_widget_set_tooltip_text(x, "Fermer");
    gtk_container_add(GTK_CONTAINER(x), icon_new("close", R_ON_VAR, 16)->img);
    g_object_set_data(G_OBJECT(x), "card", card);
    g_signal_connect(x, "clicked", G_CALLBACK(on_ov_close), t);
    gtk_container_add(GTK_CONTAINER(head), x);
    gtk_container_add(GTK_CONTAINER(card), head);
    /* Pas d'apercu du contenu (labwc ne le fournit pas par fenetre) : la
       grande icone de l'appli, sur fond tonal. */
    GtkWidget *body = gtk_button_new();
    add_class(body, "ovbody");
    GtkWidget *big = a ? app_icon(a, TRUE) : letter_icon(name);
    gtk_container_add(GTK_CONTAINER(body), big);
    if (t->minimized) gtk_widget_set_tooltip_text(body, "Fenêtre réduite");
    g_signal_connect(body, "clicked", G_CALLBACK(on_ov_card), t);
    gtk_container_add(GTK_CONTAINER(card), body);
    gtk_flow_box_insert(GTK_FLOW_BOX(ov_flow), card, -1);
    if (a) g_object_unref(a);
    n++;
  }
  g_list_free_full(all, g_object_unref);
  /* Largeur naturelle = cartes par ligne : centree, meme avec une seule */
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ov_flow), CLAMP(n, 1, 4));
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(ov_flow), CLAMP(n, 1, 4));
  gtk_widget_show_all(ov_flow);
  gtk_widget_set_visible(ov_flow, n > 0);
  gtk_widget_set_visible(ov_empty, n == 0);
}

static void build_overview(void) {
  ov_win = popup_window("lumina-overview", TRUE);
  GtkWidget *eb = gtk_event_box_new();
  g_signal_connect(eb, "button-press-event", G_CALLBACK(on_ov_bg), NULL);
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  add_class(v, "overview");
  /* Bureaux */
  GtkWidget *desks = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(desks, GTK_ALIGN_CENTER);
  for (int i = 1; i <= 4; i++) {
    char *t = g_strdup_printf("Bureau %d", i);
    GtkWidget *b = gtk_button_new_with_label(t);
    add_class(b, "desk");
    char *tip = g_strdup_printf("Windows + %d · Windows + Maj + %d y envoie la fenêtre active", i, i);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", G_CALLBACK(on_desk), GINT_TO_POINTER(i));
    gtk_container_add(GTK_CONTAINER(desks), b);
    g_free(t); g_free(tip);
  }
  gtk_container_add(GTK_CONTAINER(v), desks);
  ov_flow = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(ov_flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(ov_flow), TRUE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(ov_flow), 4);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(ov_flow), 18);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(ov_flow), 18);
  gtk_widget_set_halign(ov_flow, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(ov_flow, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(ov_flow, TRUE);
  gtk_container_add(GTK_CONTAINER(v), ov_flow);
  ov_empty = label("Aucune fenêtre ouverte", "ovempty", 0.5);
  gtk_widget_set_vexpand(ov_empty, TRUE);
  gtk_container_add(GTK_CONTAINER(v), ov_empty);
  gtk_container_add(GTK_CONTAINER(v), label("Windows + Tab : vue d'ensemble · Windows + [ ou ] : bureau précédent ou suivant · "
                                            "Alt + [ ou ] : écran partagé", "ovhint", 0.5));
  gtk_container_add(GTK_CONTAINER(eb), v);
  gtk_container_add(GTK_CONTAINER(ov_win), eb);
  gtk_widget_show_all(eb);
}

static void overview_toggle(void) {
  if (ov_win && gtk_widget_get_visible(ov_win)) { close_panels(); return; }
  fill_overview();
  show_popup(ov_win);
}

/* ------------------------------------------------------------------ */
/* Presse-papiers : historique (cliphist, alimente par wl-paste --watch)*/
/* ------------------------------------------------------------------ */
static void on_clip_item(GtkButton *b, gpointer d) {
  const char *line = d;
  close_panels();
  /* Remet l'element dans le presse-papiers, puis colle (Ctrl + V) dans
     la fenetre qui avait le clavier. */
  char *argv1[] = {"sh", "-c", "printf '%s' \"$1\" | cliphist decode | wl-copy", "sh", (char *)line, NULL};
  spawn_async((const char *const *)argv1);
  char *argv2[] = {"wtype", "-M", "ctrl", "v", "-m", "ctrl", NULL};
  spawn_later(250, g_strdupv(argv2));
}

static void on_clip_clear(GtkButton *b, gpointer d) {
  const char *argv[] = {"cliphist", "wipe", NULL};
  spawn_async(argv);
  close_panels();
}

static void fill_clip(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(clip_list));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  const char *argv[] = {"cliphist", "list", NULL};
  char *out = run_argv(argv, NULL);
  char **lines = out ? g_strsplit(out, "\n", -1) : NULL;
  int n = 0;
  for (char **l = lines; l && *l && n < 25; l++) {
    if (!**l) continue;
    char *tab = strchr(*l, '\t');
    const char *prev = tab ? tab + 1 : *l;
    gboolean img = g_str_has_prefix(prev, "[[ binary data");
    GtkWidget *b = gtk_button_new();
    add_class(b, "clipitem");
    GtkWidget *lab = label(img ? "Image" : prev, img ? "clipimg" : NULL, 0);
    gtk_label_set_ellipsize(GTK_LABEL(lab), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(lab), 44);
    gtk_container_add(GTK_CONTAINER(b), lab);
    g_signal_connect_data(b, "clicked", G_CALLBACK(on_clip_item), g_strdup(*l), (GClosureNotify)(void (*)(void))g_free, 0);
    gtk_container_add(GTK_CONTAINER(clip_list), b);
    n++;
  }
  if (!n) gtk_container_add(GTK_CONTAINER(clip_list), label("Copiez du texte ou une image : ils apparaîtront ici.", "empty", 0.5));
  g_strfreev(lines); g_free(out);
  gtk_widget_show_all(clip_list);
}

static void build_clip(void) {
  clip_win = popup_window("lumina-clipboard", FALSE);
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  add_class(v, "panel");
  gtk_widget_set_size_request(v, 420, -1);
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_container_add(GTK_CONTAINER(h), icon_new("clip", R_ON_VAR, 18)->img);
  GtkWidget *t = label("Presse-papiers", "ptitle", 0);
  gtk_widget_set_hexpand(t, TRUE);
  gtk_container_add(GTK_CONTAINER(h), t);
  GtkWidget *c = gtk_button_new_with_label("Tout effacer");
  add_class(c, "textbtn");
  g_signal_connect(c, "clicked", G_CALLBACK(on_clip_clear), NULL);
  gtk_container_add(GTK_CONTAINER(h), c);
  gtk_container_add(GTK_CONTAINER(v), h);
  GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), 380);
  clip_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_container_add(GTK_CONTAINER(sc), clip_list);
  gtk_container_add(GTK_CONTAINER(v), sc);
  gtk_container_add(GTK_CONTAINER(v), label("Cliquez pour coller. Windows + V ouvre cette liste.", "hint", 0));
  gtk_container_add(GTK_CONTAINER(clip_win), v);
  gtk_widget_show_all(v);
}

static void clip_show(void) {
  if (clip_win && gtk_widget_get_visible(clip_win)) { close_panels(); return; }
  fill_clip();
  show_popup(clip_win);
}

/* ------------------------------------------------------------------ */
/* Emojis                                                               */
/* ------------------------------------------------------------------ */
static const char *EMOJIS[][2] = {
  {"😀", "sourire content joie"}, {"😂", "rire larmes mdr"}, {"🤣", "rire mort de rire"}, {"😊", "sourire timide"},
  {"😍", "amour coeur yeux"}, {"😘", "bisou"}, {"😉", "clin d'oeil"}, {"😎", "lunettes cool"},
  {"🤔", "reflechir hmm"}, {"🙄", "yeux au ciel"}, {"😅", "gene sueur"}, {"😇", "ange innocent"},
  {"🥰", "amour coeurs"}, {"😋", "miam delicieux"}, {"😜", "langue blague"}, {"🤗", "calin"},
  {"🤩", "etoiles wow"}, {"🥳", "fete anniversaire"}, {"😴", "dormir sommeil"}, {"😢", "triste larme"},
  {"😭", "pleurer"}, {"😡", "colere"}, {"😱", "peur cri"}, {"🤯", "choque"},
  {"😬", "grimace"}, {"🤐", "secret"}, {"🤒", "malade"}, {"🤧", "rhume"},
  {"😷", "masque"}, {"🥶", "froid"}, {"🥵", "chaud"}, {"🤡", "clown"},
  {"👍", "pouce oui ok"}, {"👎", "pouce non"}, {"👌", "ok parfait"}, {"✌️", "victoire paix"},
  {"🤞", "croiser doigts chance"}, {"👏", "applaudir bravo"}, {"🙌", "hourra"}, {"🙏", "merci priere svp"},
  {"💪", "muscle force"}, {"👋", "salut coucou"}, {"🤝", "accord poignee"}, {"✍️", "ecrire"},
  {"❤️", "coeur amour rouge"}, {"🧡", "coeur orange"}, {"💛", "coeur jaune"}, {"💚", "coeur vert"},
  {"💙", "coeur bleu"}, {"💜", "coeur violet"}, {"🖤", "coeur noir"}, {"💔", "coeur brise"},
  {"✨", "etincelles magie"}, {"🔥", "feu top"}, {"⭐", "etoile"}, {"🌟", "etoile brillante"},
  {"💯", "cent parfait"}, {"✅", "valide oui fait"}, {"❌", "non croix erreur"}, {"⚠️", "attention"},
  {"❓", "question"}, {"❗", "exclamation"}, {"💡", "idee ampoule"}, {"🎉", "fete bravo"},
  {"🎂", "gateau anniversaire"}, {"🎁", "cadeau"}, {"🎄", "noel sapin"}, {"🎃", "halloween citrouille"},
  {"☀️", "soleil beau"}, {"🌙", "lune nuit"}, {"⛅", "nuage soleil"}, {"🌧️", "pluie"},
  {"❄️", "neige flocon"}, {"⚡", "eclair orage"}, {"🌈", "arc-en-ciel"}, {"🌊", "vague mer"},
  {"🌸", "fleur cerisier"}, {"🌹", "rose fleur"}, {"🌻", "tournesol"}, {"🍀", "trefle chance"},
  {"🐶", "chien"}, {"🐱", "chat"}, {"🐭", "souris"}, {"🐰", "lapin"},
  {"🦊", "renard"}, {"🐻", "ours"}, {"🐼", "panda"}, {"🐸", "grenouille"},
  {"🐔", "poule"}, {"🐧", "pingouin manchot"}, {"🦄", "licorne"}, {"🐝", "abeille"},
  {"🍎", "pomme"}, {"🍌", "banane"}, {"🍓", "fraise"}, {"🍕", "pizza"},
  {"🍔", "burger hamburger"}, {"🍟", "frites"}, {"🥐", "croissant"}, {"🥖", "baguette pain"},
  {"🧀", "fromage"}, {"🍫", "chocolat"}, {"🍰", "gateau"}, {"🍿", "popcorn film"},
  {"☕", "cafe"}, {"🍵", "the"}, {"🍷", "vin"}, {"🍺", "biere"},
  {"⚽", "foot ballon"}, {"🏀", "basket"}, {"🎮", "jeu manette"}, {"🎵", "musique note"},
  {"🎧", "casque musique"}, {"📷", "photo appareil"}, {"🎬", "film cinema"}, {"📚", "livres"},
  {"💻", "ordinateur portable"}, {"📱", "telephone"}, {"⌨️", "clavier"}, {"🖨️", "imprimante"},
  {"📧", "mail courriel"}, {"📅", "calendrier date"}, {"📌", "epingle"}, {"📎", "trombone"},
  {"🔒", "cadenas verrou"}, {"🔑", "cle"}, {"🏠", "maison"}, {"🚗", "voiture"},
  {"🚲", "velo"}, {"✈️", "avion voyage"}, {"🚀", "fusee"}, {"⏰", "reveil heure"},
  {"💰", "argent"}, {"💶", "euro billet"}, {"🛒", "courses panier"}, {"🇫🇷", "france drapeau"},
  {"🇧🇪", "belgique drapeau"}, {"🇨🇭", "suisse drapeau"}, {"🇨🇦", "canada drapeau"}, {"🏳️‍🌈", "drapeau arc-en-ciel"},
  {"👀", "yeux regarder"}, {"🧠", "cerveau"}, {"👶", "bebe"}, {"🧑‍💻", "informaticien"},
  {"🤷", "je sais pas"}, {"🤦", "facepalm"}, {"🙈", "singe pas vu"}, {"💤", "dodo"},
};

static void on_emoji(GtkButton *b, gpointer d) {
  close_panels();
  char *argv[] = {"wtype", (char *)d, NULL};
  spawn_later(150, g_strdupv(argv));
}

static gboolean emoji_filter(GtkFlowBoxChild *c, gpointer d) {
  const char *q = gtk_entry_get_text(GTK_ENTRY(emoji_entry));
  if (!*q) return TRUE;
  const char *kw = g_object_get_data(G_OBJECT(c), "kw");
  char *qf = g_utf8_casefold(q, -1), *qn = g_str_to_ascii(qf, NULL);
  gboolean ok = kw && strstr(kw, qn) != NULL;
  g_free(qf); g_free(qn);
  return ok;
}
static void on_emoji_search(GtkEditable *e, gpointer d) { gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(emoji_flow)); }

static void build_emoji(void) {
  emoji_win = popup_window("lumina-emoji", FALSE);
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  add_class(v, "panel");
  gtk_widget_set_size_request(v, 420, -1);
  GtkWidget *s = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  add_class(s, "search");
  gtk_container_add(GTK_CONTAINER(s), icon_new("search", R_ON_VAR, 18)->img);
  emoji_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(emoji_entry), "Rechercher un emoji (coeur, rire, chat…)");
  gtk_widget_set_hexpand(emoji_entry, TRUE);
  g_signal_connect(emoji_entry, "changed", G_CALLBACK(on_emoji_search), NULL);
  gtk_container_add(GTK_CONTAINER(s), emoji_entry);
  gtk_container_add(GTK_CONTAINER(v), s);
  emoji_flow = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(emoji_flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(emoji_flow), 8);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(emoji_flow), 8);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(emoji_flow), TRUE);
  gtk_flow_box_set_filter_func(GTK_FLOW_BOX(emoji_flow), emoji_filter, NULL, NULL);
  for (guint i = 0; i < G_N_ELEMENTS(EMOJIS); i++) {
    GtkWidget *b = gtk_button_new_with_label(EMOJIS[i][0]);
    add_class(b, "emoji");
    gtk_widget_set_tooltip_text(b, EMOJIS[i][1]);
    g_signal_connect(b, "clicked", G_CALLBACK(on_emoji), (gpointer)EMOJIS[i][0]);
    gtk_flow_box_insert(GTK_FLOW_BOX(emoji_flow), b, -1);
    g_object_set_data(G_OBJECT(gtk_widget_get_parent(b)), "kw", (gpointer)EMOJIS[i][1]);
  }
  GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sc), 300);
  gtk_container_add(GTK_CONTAINER(sc), emoji_flow);
  gtk_container_add(GTK_CONTAINER(v), sc);
  gtk_container_add(GTK_CONTAINER(emoji_win), v);
  gtk_widget_show_all(v);
}

static void emoji_show(void) {
  if (emoji_win && gtk_widget_get_visible(emoji_win)) { close_panels(); return; }
  gtk_entry_set_text(GTK_ENTRY(emoji_entry), "");
  show_popup(emoji_win);
  gtk_widget_grab_focus(emoji_entry);
}

/* ------------------------------------------------------------------ */
/* Reglages rapides : nouvelles tuiles et notifications                 */
/* ------------------------------------------------------------------ */
static GtkWidget *dnd_tile, *dnd_sub, *night_tile, *night_sub, *rec_tile, *rec_sub, *rec_bar_btn;
static GtkWidget *notif_box, *notif_head;

static gboolean recording(void) {
  const char *argv[] = {"pgrep", "-x", "wf-recorder", NULL};
  int st = 1; g_free(run_argv(argv, &st));
  return st == 0;
}
static gboolean night_on(void) { return cfg_flag("nightlight"); }

static void apply_dnd(gboolean on) {
  const char *argv[] = {"makoctl", "mode", on ? "-a" : "-r", "do-not-disturb", NULL};
  spawn_async(argv);
}
static void apply_night(gboolean on) {
  const char *k[] = {"pkill", "-x", "wlsunset", NULL};
  spawn_async(k);
  if (on) {
    /* "Nuit" de 00:00 a 23:59 : filtre chaud tout de suite et en continu */
    char *argv[] = {"wlsunset", "-t", "3800", "-T", "6500", "-S", "23:59", "-s", "00:00", "-d", "30", NULL};
    spawn_later(300, g_strdupv(argv));
  }
}

static void refresh_extras_qs(void) {
  gboolean d = cfg_flag("dnd"), n = night_on(), r = recording();
  set_class(dnd_tile, "on", d);
  gtk_label_set_text(GTK_LABEL(dnd_sub), d ? "Activé" : "Désactivé");
  set_class(night_tile, "on", n);
  gtk_label_set_text(GTK_LABEL(night_sub), n ? "Activé" : "Désactivé");
  set_class(rec_tile, "on", r);
  gtk_label_set_text(GTK_LABEL(rec_sub), r ? "En cours : toucher pour arrêter" : "Ctrl + Impr");
  if (rec_bar_btn) gtk_widget_set_visible(rec_bar_btn, r);
}

static void on_dnd_tile(GtkButton *b, gpointer d) {
  gboolean on = !cfg_flag("dnd");
  cfg_set_flag("dnd", on); apply_dnd(on); refresh_extras_qs();
}
static void on_night_tile(GtkButton *b, gpointer d) {
  gboolean on = !night_on();
  cfg_set_flag("nightlight", on); apply_night(on); refresh_extras_qs();
}
static gboolean rec_refresh_cb(gpointer d) { refresh_extras_qs(); return G_SOURCE_REMOVE; }
static void on_rec_tile(GtkButton *b, gpointer d) {
  close_panels();
  char *argv[] = {"lumina-record", NULL};
  spawn_later(300, g_strdupv(argv));
  g_timeout_add(1500, rec_refresh_cb, NULL);
}
static void on_capture_tile(GtkButton *b, gpointer d) {
  close_panels();
  char *argv[] = {"lumina-screenshot", NULL};
  spawn_later(400, g_strdupv(argv));
}

/* Notifications : historique de mako (D-Bus), les plus recentes d'abord.
   "Tout effacer" memorise le dernier identifiant vu : mako ne sait pas
   vider son historique. */
static guint32 notif_cleared;
static void on_notif_clear(GtkButton *b, gpointer d) {
  notif_cleared = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(b), "max"));
  char *p = g_build_filename(g_get_user_cache_dir(), "lumina-notif-cleared", NULL);
  char *v = g_strdup_printf("%u", notif_cleared);
  g_file_set_contents(p, v, -1, NULL);
  g_free(p); g_free(v);
  refresh_qs();
}

static void fill_notifs(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(notif_box));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
  GVariant *r = c ? g_dbus_connection_call_sync(c, "org.freedesktop.Notifications", "/fr/emersion/Mako",
                                               "fr.emersion.Mako", "ListHistory", NULL, G_VARIANT_TYPE("(aa{sv})"),
                                               G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL) : NULL;
  int n = 0; guint32 maxid = notif_cleared;
  if (r) {
    GVariantIter *it; g_variant_get(r, "(aa{sv})", &it);
    GVariant *dict;
    while (g_variant_iter_loop(it, "@a{sv}", &dict)) {
      guint32 id = 0; const char *sum = "", *body = "", *app = "";
      g_variant_lookup(dict, "id", "u", &id);
      g_variant_lookup(dict, "summary", "&s", &sum);
      g_variant_lookup(dict, "body", "&s", &body);
      g_variant_lookup(dict, "app-name", "&s", &app);
      if (id <= notif_cleared) continue;
      if (id > maxid) maxid = id;
      if (n >= 4) continue;
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
      add_class(row, "notif");
      GtkWidget *a = label(*app ? app : "Notification", "napp", 0);
      gtk_container_add(GTK_CONTAINER(row), a);
      GtkWidget *s = label(sum, "nsum", 0);
      gtk_label_set_ellipsize(GTK_LABEL(s), PANGO_ELLIPSIZE_END);
      gtk_container_add(GTK_CONTAINER(row), s);
      if (*body) {
        GtkWidget *bl = label(body, "nbody", 0);
        gtk_label_set_ellipsize(GTK_LABEL(bl), PANGO_ELLIPSIZE_END);
        gtk_container_add(GTK_CONTAINER(row), bl);
      }
      gtk_container_add(GTK_CONTAINER(notif_box), row);
      n++;
    }
    g_variant_iter_free(it); g_variant_unref(r);
  }
  if (c) g_object_unref(c);
  GtkWidget *clr = g_object_get_data(G_OBJECT(notif_head), "clear");
  g_object_set_data(G_OBJECT(clr), "max", GUINT_TO_POINTER(maxid));
  gtk_widget_set_visible(clr, n > 0);
  if (!n) gtk_container_add(GTK_CONTAINER(notif_box), label("Aucune notification", "hint", 0));
  gtk_widget_show_all(notif_box);
}

static void extras_qs(GtkWidget *panel, GtkWidget *grid) {
  dnd_tile = tile("dnd", "Ne pas déranger", &dnd_sub, NULL);
  g_signal_connect(dnd_tile, "clicked", G_CALLBACK(on_dnd_tile), NULL);
  night_tile = tile("night", "Éclairage nocturne", &night_sub, NULL);
  g_signal_connect(night_tile, "clicked", G_CALLBACK(on_night_tile), NULL);
  GtkWidget *capsub = NULL;
  GtkWidget *cap = tile("capture", "Capture d'écran", &capsub, NULL);
  gtk_label_set_text(GTK_LABEL(capsub), "Impr");
  g_signal_connect(cap, "clicked", G_CALLBACK(on_capture_tile), NULL);
  rec_tile = tile("record", "Enregistrer l'écran", &rec_sub, NULL);
  g_signal_connect(rec_tile, "clicked", G_CALLBACK(on_rec_tile), NULL);
  gtk_grid_attach(GTK_GRID(grid), dnd_tile, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), night_tile, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), cap, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), rec_tile, 1, 2, 1, 1);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);

  /* Notifications, sous les tuiles */
  GtkWidget *nv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  add_class(nv, "notifs");
  notif_head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_container_add(GTK_CONTAINER(notif_head), icon_new("bell", R_ON_VAR, 16)->img);
  GtkWidget *t = label("Notifications", "muted", 0);
  gtk_widget_set_hexpand(t, TRUE);
  gtk_container_add(GTK_CONTAINER(notif_head), t);
  GtkWidget *clr = gtk_button_new_with_label("Tout effacer");
  add_class(clr, "textbtn");
  g_signal_connect(clr, "clicked", G_CALLBACK(on_notif_clear), NULL);
  g_object_set_data(G_OBJECT(notif_head), "clear", clr);
  gtk_container_add(GTK_CONTAINER(notif_head), clr);
  gtk_container_add(GTK_CONTAINER(nv), notif_head);
  notif_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_container_add(GTK_CONTAINER(nv), notif_box);
  gtk_container_add(GTK_CONTAINER(panel), nv);

  char *p = g_build_filename(g_get_user_cache_dir(), "lumina-notif-cleared", NULL), *v = read_file(p);
  if (v) notif_cleared = (guint32)g_ascii_strtoull(v, NULL, 10);
  g_free(p); g_free(v);
}

static void on_rec_stop(GtkButton *b, gpointer d) {
  const char *argv[] = {"lumina-record", NULL};
  spawn_async(argv);
  g_timeout_add(1500, rec_refresh_cb, NULL);
}
static void on_overview_btn(GtkButton *b, gpointer d) { overview_toggle(); }

/* Barre d'etat : bouton de vue d'ensemble, et bouton rouge d'arret pendant
   un enregistrement de l'ecran. */
static void extras_bar(GtkWidget *h) {
  GtkWidget *ob = gtk_button_new();
  add_class(ob, "launch");
  gtk_widget_set_valign(ob, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(ob, "Fenêtres et bureaux (Windows + Tab)");
  gtk_container_add(GTK_CONTAINER(ob), icon_new("overview", R_ON, 15)->img);
  g_signal_connect(ob, "clicked", G_CALLBACK(on_overview_btn), NULL);
  gtk_container_add(GTK_CONTAINER(h), ob);
}
static void extras_bar_end(GtkWidget *h) {
  rec_bar_btn = gtk_button_new_with_label("● Arrêter l'enregistrement");
  add_class(rec_bar_btn, "recbtn");
  gtk_widget_set_valign(rec_bar_btn, GTK_ALIGN_CENTER);
  g_signal_connect(rec_bar_btn, "clicked", G_CALLBACK(on_rec_stop), NULL);
  gtk_widget_set_no_show_all(rec_bar_btn, TRUE);
  gtk_container_add(GTK_CONTAINER(h), rec_bar_btn);
}

/* ------------------------------------------------------------------ */
/* Lanceur : calculatrice et fichiers                                   */
/* ------------------------------------------------------------------ */
static GtkWidget *calc_btn, *calc_l, *files_box;
static GSubprocess *find_sp;
static guint find_timer;

/* Analyseur d'expressions : + - * / % ^, parentheses, virgule decimale. */
typedef struct { const char *p; gboolean err; } Calc;
static double c_expr(Calc *c);
static void c_ws(Calc *c) { while (*c->p == ' ') c->p++; }
static double c_atom(Calc *c) {
  c_ws(c);
  if (*c->p == '(') { c->p++; double v = c_expr(c); c_ws(c); if (*c->p == ')') c->p++; else c->err = TRUE; return v; }
  if (*c->p == '-') { c->p++; return -c_atom(c); }
  if (*c->p == '+') { c->p++; return c_atom(c); }
  char buf[64]; int n = 0;
  while ((g_ascii_isdigit(*c->p) || *c->p == '.' || *c->p == ',') && n < 63) { buf[n++] = *c->p == ',' ? '.' : *c->p; c->p++; }
  buf[n] = 0;
  if (!n) { c->err = TRUE; return 0; }
  return g_ascii_strtod(buf, NULL);
}
static double c_pow(Calc *c) {
  double b = c_atom(c); c_ws(c);
  if (*c->p == '^') { c->p++; return pow(b, c_pow(c)); }
  return b;
}
static double c_term(Calc *c) {
  double v = c_pow(c);
  for (;;) {
    c_ws(c);
    char op = *c->p;
    if (op == '*' || op == 'x' || op == '/' || op == ':' || op == '%') {
      c->p++;
      double r = c_pow(c);
      if (op == '/' || op == ':') { if (r == 0) c->err = TRUE; else v /= r; }
      else if (op == '%') v = fmod(v, r);
      else v *= r;
    } else return v;
  }
}
static double c_expr(Calc *c) {
  double v = c_term(c);
  for (;;) {
    c_ws(c);
    if (*c->p == '+') { c->p++; v += c_term(c); }
    else if (*c->p == '-') { c->p++; v -= c_term(c); }
    else return v;
  }
}
static char *calc_eval(const char *q) {
  /* Au moins un operateur et un chiffre : "2024" seul n'est pas un calcul */
  if (!strpbrk(q, "+-*/x:%^(") || !strpbrk(q, "0123456789")) return NULL;
  Calc c = {q, FALSE};
  double v = c_expr(&c);
  c_ws(&c);
  if (c.err || *c.p || isnan(v) || isinf(v)) return NULL;
  char *s = g_strdup_printf("%.10g", v);
  for (char *p = s; *p; p++) if (*p == '.') *p = ',';
  return s;
}

static void on_calc_copy(GtkButton *b, gpointer d) {
  const char *v = gtk_label_get_text(GTK_LABEL(calc_l));
  if (v[0] == '=' && v[1] == ' ') v += 2;
  char *argv[] = {"wl-copy", (char *)v, NULL};
  spawn_async((const char *const *)argv);
  close_panels();
}

static void on_file_open(GtkButton *b, gpointer d) {
  close_panels();
  char *uri = g_filename_to_uri(d, NULL, NULL);
  if (uri) {
    GFile *f = g_file_new_for_path(d);
    if (g_file_query_file_type(f, 0, NULL) == G_FILE_TYPE_DIRECTORY) {
      const char *argv[] = {"lumina-files", d, NULL};
      spawn_async(argv);
    } else {
      g_app_info_launch_default_for_uri(uri, NULL, NULL);
    }
    g_object_unref(f);
  }
  g_free(uri);
}

static void find_done(GObject *o, GAsyncResult *res, gpointer d) {
  char *out = NULL;
  gboolean ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), res, &out, NULL, NULL);
  if (G_SUBPROCESS(o) != find_sp) { g_free(out); g_object_unref(o); return; }  /* recherche perimee */
  g_clear_object(&find_sp);
  GList *kids = gtk_container_get_children(GTK_CONTAINER(files_box));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  int n = 0;
  if (ok && out) {
    char **lines = g_strsplit(out, "\n", -1);
    const char *home = g_get_home_dir();
    for (char **l = lines; *l && n < 6; l++) {
      if (!**l) continue;
      if (!n) gtk_container_add(GTK_CONTAINER(files_box), label("Fichiers", "section", 0));
      GtkWidget *b = gtk_button_new();
      add_class(b, "fileres");
      GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      gboolean dir = g_file_test(*l, G_FILE_TEST_IS_DIR);
      gtk_container_add(GTK_CONTAINER(h), icon_new(dir ? "folder" : "file", R_PRI, 18)->img);
      char *base = g_path_get_basename(*l), *parent = g_path_get_dirname(*l);
      GtkWidget *nl = label(base, NULL, 0);
      gtk_label_set_ellipsize(GTK_LABEL(nl), PANGO_ELLIPSIZE_MIDDLE);
      gtk_container_add(GTK_CONTAINER(h), nl);
      const char *rel = g_str_has_prefix(parent, home) ? parent + strlen(home) : parent;
      char *where = g_strdup_printf("Mes fichiers%s", rel);
      GtkWidget *wl = label(where, "kind", 1);
      gtk_label_set_ellipsize(GTK_LABEL(wl), PANGO_ELLIPSIZE_START);
      gtk_widget_set_hexpand(wl, TRUE);
      gtk_container_add(GTK_CONTAINER(h), wl);
      gtk_container_add(GTK_CONTAINER(b), h);
      g_signal_connect_data(b, "clicked", G_CALLBACK(on_file_open), g_strdup(*l), (GClosureNotify)(void (*)(void))g_free, 0);
      gtk_container_add(GTK_CONTAINER(files_box), b);
      g_free(base); g_free(parent); g_free(where);
      n++;
    }
    g_strfreev(lines);
  }
  g_free(out); g_object_unref(o);
  gtk_widget_show_all(files_box);
  if (n) gtk_widget_hide(flow_empty);
}

static gboolean find_start(gpointer d) {
  find_timer = 0;
  const char *q = gtk_entry_get_text(GTK_ENTRY(search_entry));
  if (g_utf8_strlen(q, -1) < 2) return G_SOURCE_REMOVE;
  char *pat = g_strdup_printf("*%s*", q);
  if (find_sp) { g_subprocess_force_exit(find_sp); g_clear_object(&find_sp); }
  /* Dossier personnel, sans les dossiers caches ; arret apres 6 resultats */
  find_sp = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                             "sh", "-c", "timeout 3 find \"$HOME\" -mindepth 1 -maxdepth 6 -name '.*' -prune -o -iname \"$1\" -print 2>/dev/null | head -n 6",
                             "sh", pat, NULL);
  if (find_sp) g_subprocess_communicate_utf8_async(find_sp, NULL, NULL, find_done, g_object_ref(find_sp));
  g_free(pat);
  return G_SOURCE_REMOVE;
}

static void extras_search_changed(const char *q) {
  char *r = *q ? calc_eval(q) : NULL;
  if (r) {
    char *t = g_strdup_printf("= %s", r);
    gtk_label_set_text(GTK_LABEL(calc_l), t);
    g_free(t);
    gtk_widget_show(calc_btn);
    gtk_widget_hide(flow_empty);
  } else gtk_widget_hide(calc_btn);
  g_free(r);
  GList *kids = gtk_container_get_children(GTK_CONTAINER(files_box));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  if (find_timer) g_source_remove(find_timer);
  find_timer = *q ? g_timeout_add(300, find_start, NULL) : 0;
}

static void extras_drawer(GtkWidget *v) {
  calc_btn = gtk_button_new();
  add_class(calc_btn, "calc");
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_container_add(GTK_CONTAINER(h), icon_new("calc", R_ON_PRI_C, 20)->img);
  calc_l = label("", "calcres", 0);
  gtk_widget_set_hexpand(calc_l, TRUE);
  gtk_container_add(GTK_CONTAINER(h), calc_l);
  gtk_container_add(GTK_CONTAINER(h), label("Copier", "kind", 1));
  gtk_container_add(GTK_CONTAINER(calc_btn), h);
  g_signal_connect(calc_btn, "clicked", G_CALLBACK(on_calc_copy), NULL);
  gtk_widget_show_all(calc_btn);
  gtk_widget_set_no_show_all(calc_btn, TRUE);
  gtk_widget_hide(calc_btn);
  gtk_container_add(GTK_CONTAINER(v), calc_btn);
  files_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_container_add(GTK_CONTAINER(v), files_box);
}

/* ------------------------------------------------------------------ */
/* Etagere : masquage automatique, position, glisser-deposer            */
/* ------------------------------------------------------------------ */
static gboolean shelf_autohide;
static char shelf_pos[8] = "bottom";
static gboolean shelf_hidden;
static guint shelf_hide_timer;
static GtkWidget *shelf_box;

static void read_shelf_conf(void) {
  char *p = cfg_path("shelf.conf"), *c = read_file(p);
  shelf_autohide = c && strstr(c, "autohide=1");
  g_strlcpy(shelf_pos, c && strstr(c, "position=left") ? "left" : c && strstr(c, "position=right") ? "right" : "bottom",
            sizeof shelf_pos);
  g_free(p); g_free(c);
}
static void write_shelf_conf(void) {
  char *p = cfg_path("shelf.conf"), *dir = g_path_get_dirname(p);
  g_mkdir_with_parents(dir, 0755);
  char *c = g_strdup_printf("autohide=%d\nposition=%s\n", shelf_autohide, shelf_pos);
  g_file_set_contents(p, c, -1, NULL);
  g_free(p); g_free(dir); g_free(c);
}

static GtkLayerShellEdge shelf_edge(void) {
  return !strcmp(shelf_pos, "left") ? GTK_LAYER_SHELL_EDGE_LEFT : !strcmp(shelf_pos, "right") ? GTK_LAYER_SHELL_EDGE_RIGHT
                                                                                              : GTK_LAYER_SHELL_EDGE_BOTTOM;
}

static void shelf_set_hidden(gboolean hide) {
  shelf_hidden = hide;
  gboolean side = strcmp(shelf_pos, "bottom") != 0;
  int thick = side ? gtk_widget_get_allocated_width(shelf_win) : gtk_widget_get_allocated_height(shelf_win);
  if (thick < 10) thick = 56;
  gtk_layer_set_margin(GTK_WINDOW(shelf_win), shelf_edge(), hide ? -(thick - 3) : 0);
}
static gboolean shelf_hide_cb(gpointer d) {
  shelf_hide_timer = 0;
  if (shelf_autohide && !gtk_widget_get_visible(drawer)) shelf_set_hidden(TRUE);
  return G_SOURCE_REMOVE;
}
static gboolean on_shelf_enter(GtkWidget *w, GdkEventCrossing *e, gpointer d) {
  if (shelf_hide_timer) { g_source_remove(shelf_hide_timer); shelf_hide_timer = 0; }
  if (shelf_autohide && shelf_hidden) shelf_set_hidden(FALSE);
  return FALSE;
}
static gboolean on_shelf_leave(GtkWidget *w, GdkEventCrossing *e, gpointer d) {
  if (e->detail == GDK_NOTIFY_INFERIOR) return FALSE;
  if (shelf_autohide && !shelf_hide_timer) shelf_hide_timer = g_timeout_add(700, shelf_hide_cb, NULL);
  return FALSE;
}

static void apply_shelf_layout(void) {
  gboolean side = strcmp(shelf_pos, "bottom") != 0;
  gtk_layer_set_margin(GTK_WINDOW(shelf_win), GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
  gtk_layer_set_margin(GTK_WINDOW(shelf_win), GTK_LAYER_SHELL_EDGE_LEFT, 0);
  gtk_layer_set_margin(GTK_WINDOW(shelf_win), GTK_LAYER_SHELL_EDGE_RIGHT, 0);
  if (!strcmp(shelf_pos, "left")) anchor(shelf_win, TRUE, TRUE, TRUE, FALSE);
  else if (!strcmp(shelf_pos, "right")) anchor(shelf_win, TRUE, TRUE, FALSE, TRUE);
  else anchor(shelf_win, FALSE, TRUE, TRUE, TRUE);
  GtkOrientation o = side ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
  gtk_orientable_set_orientation(GTK_ORIENTABLE(shelf_box), o);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(shelf_apps), o);
  gtk_widget_set_halign(shelf_apps, side ? GTK_ALIGN_FILL : GTK_ALIGN_CENTER);
  gtk_widget_set_valign(shelf_apps, side ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(shelf_apps, !side);
  gtk_widget_set_vexpand(shelf_apps, side);
  set_class(shelf_box, "side", side);
  if (shelf_autohide) {
    gtk_layer_set_exclusive_zone(GTK_WINDOW(shelf_win), 0);
    shelf_set_hidden(TRUE);
  } else {
    gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(shelf_win));
    shelf_hidden = FALSE;
  }
}

static void on_shelf_autohide(GtkCheckMenuItem *m, gpointer d) {
  shelf_autohide = gtk_check_menu_item_get_active(m);
  write_shelf_conf(); apply_shelf_layout();
}
static void on_shelf_pos(GtkCheckMenuItem *m, gpointer d) {
  if (!gtk_check_menu_item_get_active(m)) return;
  g_strlcpy(shelf_pos, d, sizeof shelf_pos);
  write_shelf_conf(); apply_shelf_layout();
}
static void on_shelf_wall(GtkMenuItem *m, gpointer d) {
  const char *argv[] = {"lumina-settings", "affichage", NULL};
  spawn_async(argv);
}

/* Clic droit sur le fond de l'etagere */
static gboolean on_shelf_bg_press(GtkWidget *w, GdkEventButton *e, gpointer d) {
  if (e->type != GDK_BUTTON_PRESS || e->button != GDK_BUTTON_SECONDARY) return FALSE;
  GtkWidget *menu = gtk_menu_new();
  add_class(menu, "appmenu");
  GtkWidget *ah = gtk_check_menu_item_new_with_label("Masquer automatiquement l'étagère");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ah), shelf_autohide);
  g_signal_connect(ah, "toggled", G_CALLBACK(on_shelf_autohide), NULL);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), ah);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
  GSList *grp = NULL;
  const char *pos[][2] = {{"bottom", "Étagère en bas"}, {"left", "Étagère à gauche"}, {"right", "Étagère à droite"}};
  for (int i = 0; i < 3; i++) {
    GtkWidget *mi = gtk_radio_menu_item_new_with_label(grp, pos[i][1]);
    grp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mi));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(mi), !strcmp(shelf_pos, pos[i][0]));
    g_signal_connect(mi, "toggled", G_CALLBACK(on_shelf_pos), (gpointer)pos[i][0]);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
  }
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
  GtkWidget *wp = gtk_menu_item_new_with_label("Fond d'écran et style…");
  g_signal_connect(wp, "activate", G_CALLBACK(on_shelf_wall), NULL);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), wp);
  gtk_widget_show_all(menu);
  g_signal_connect(menu, "deactivate", G_CALLBACK(menu_done), NULL);
  gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)e);
  return TRUE;
}

/* Glisser une appli epinglee sur une autre : elle prend sa place. */
static const GtkTargetEntry SHELF_TARGET[] = {{"application/x-lumina-shelf", GTK_TARGET_SAME_APP, 0}};
static void on_shelf_drag_get(GtkWidget *w, GdkDragContext *c, GtkSelectionData *sd, guint info, guint t, gpointer key) {
  gtk_selection_data_set(sd, gtk_selection_data_get_target(sd), 8, (const guchar *)key, strlen(key));
}
static void on_shelf_drop(GtkWidget *w, GdkDragContext *c, gint x, gint y, GtkSelectionData *sd, guint info, guint t,
                          gpointer target_key) {
  char *src = g_strndup((const char *)gtk_selection_data_get_data(sd), gtk_selection_data_get_length(sd));
  if (g_strcmp0(src, target_key)) {
    GPtrArray *a = read_pins();
    GString *out = g_string_new("# Applis epinglees dans l'etagere, dans l'ordre.\n");
    gboolean placed = FALSE;
    for (guint i = 0; i < a->len; i++) {
      if (!g_strcmp0(a->pdata[i], src)) continue;
      if (!g_strcmp0(a->pdata[i], target_key)) { g_string_append_printf(out, "%s\n", src); placed = TRUE; }
      g_string_append_printf(out, "%s\n", (char *)a->pdata[i]);
    }
    if (!placed) g_string_append_printf(out, "%s\n", src);
    char *p = pins_path(), *dir = g_path_get_dirname(p);
    g_mkdir_with_parents(dir, 0755);
    g_file_set_contents(p, out->str, -1, NULL);
    g_free(p); g_free(dir); g_string_free(out, TRUE); g_ptr_array_free(a, TRUE);
    schedule_shelf();
  }
  g_free(src);
  gtk_drag_finish(c, TRUE, FALSE, t);
}
static void extras_shelf_button(GtkWidget *b, const char *key, gboolean has_app) {
  if (!has_app) return;
  gtk_drag_source_set(b, GDK_BUTTON1_MASK, SHELF_TARGET, 1, GDK_ACTION_MOVE);
  g_signal_connect_data(b, "drag-data-get", G_CALLBACK(on_shelf_drag_get), g_strdup(key), (GClosureNotify)(void (*)(void))g_free, 0);
  gtk_drag_dest_set(b, GTK_DEST_DEFAULT_ALL, SHELF_TARGET, 1, GDK_ACTION_MOVE);
  g_signal_connect_data(b, "drag-data-received", G_CALLBACK(on_shelf_drop), g_strdup(key), (GClosureNotify)(void (*)(void))g_free, 0);
}

static void extras_shelf(GtkWidget *box) {
  shelf_box = box;
  read_shelf_conf();
  gtk_widget_add_events(shelf_win, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK);
  g_signal_connect(shelf_win, "enter-notify-event", G_CALLBACK(on_shelf_enter), NULL);
  g_signal_connect(shelf_win, "leave-notify-event", G_CALLBACK(on_shelf_leave), NULL);
  g_signal_connect(shelf_win, "button-press-event", G_CALLBACK(on_shelf_bg_press), NULL);
  apply_shelf_layout();
}

/* ------------------------------------------------------------------ */
/* Actions a distance                                                   */
/* ------------------------------------------------------------------ */
static const char *SHELL_XML =
  "<node><interface name='org.luminaos.Shell'>"
  "<method name='Action'><arg type='s' name='what' direction='in'/></method>"
  "</interface></node>";

static void shell_call(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                       const char *method, GVariant *params, GDBusMethodInvocation *inv, gpointer d) {
  const char *what = NULL;
  g_variant_get(params, "(&s)", &what);
  if (!strcmp(what, "overview")) overview_toggle();
  else if (!strcmp(what, "clipboard")) clip_show();
  else if (!strcmp(what, "emoji")) emoji_show();
  else if (!strcmp(what, "launcher")) toggle_panel(drawer);
  else if (!strcmp(what, "quick-settings")) toggle_panel(qs);
  else if (!strcmp(what, "refresh")) { refresh_extras_qs(); schedule_shelf(); }
  g_dbus_method_invocation_return_value(inv, NULL);
}
static const GDBusInterfaceVTable shell_vtable = { shell_call, NULL, NULL };

static void on_shell_bus(GDBusConnection *c, const char *name, gpointer d) {
  GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(SHELL_XML, NULL);
  g_dbus_connection_register_object(c, "/org/luminaos/Shell", info->interfaces[0], &shell_vtable, NULL, NULL, NULL);
  g_dbus_node_info_unref(info);
}

/* "lumina-shell --action X" : transmis au shell deja lance */
static int action_client(const char *what) {
  GError *err = NULL;
  GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
  if (!c) { g_printerr("%s\n", err->message); return 1; }
  GVariant *r = g_dbus_connection_call_sync(c, "org.luminaos.Shell", "/org/luminaos/Shell", "org.luminaos.Shell",
                                            "Action", g_variant_new("(s)", what), NULL, G_DBUS_CALL_FLAGS_NONE,
                                            3000, NULL, &err);
  if (!r) { g_printerr("%s\n", err->message); return 1; }
  g_variant_unref(r);
  return 0;
}

static void extras_init(void) {
  build_overview();
  build_clip();
  build_emoji();
  g_bus_own_name(G_BUS_TYPE_SESSION, "org.luminaos.Shell", G_BUS_NAME_OWNER_FLAGS_NONE,
                 on_shell_bus, NULL, NULL, NULL, NULL);
  /* Etats retrouves a l'ouverture de session */
  if (cfg_flag("dnd")) apply_dnd(TRUE);
  if (night_on()) apply_night(TRUE);
  refresh_extras_qs();
}

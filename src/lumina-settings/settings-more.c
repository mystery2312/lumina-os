/*
 * settings-more.c : pages de Reglages facon ChromeOS, incluses par
 * lumina-settings.c : Ecrans, Souris et pave tactile, Date et heure,
 * Imprimantes, Energie, Accessibilite, Raccourcis clavier ; et ajouts aux
 * pages Mises a jour (canal), A propos (reinitialisation) et Linux
 * (sauvegarde).
 */

static char *ucfg(const char *name) { return g_build_filename(g_get_user_config_dir(), "lumina", name, NULL); }
static gboolean uflag(const char *name) { char *p = ucfg(name); gboolean r = g_file_test(p, G_FILE_TEST_EXISTS); g_free(p); return r; }
static void uflag_set(const char *name, gboolean on) {
  char *p = ucfg(name), *dir = g_path_get_dirname(p);
  g_mkdir_with_parents(dir, 0755);
  if (on) g_file_set_contents(p, "1\n", -1, NULL); else g_unlink(p);
  g_free(p); g_free(dir);
}

static GtkWidget *combo(const char *const *items, int active) {
  GtkWidget *c = gtk_combo_box_text_new();
  for (int i = 0; items[i]; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), items[i]);
  gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
  return c;
}

static GtkWidget *switch_new(gboolean on, GCallback cb, gpointer d) {
  GtkWidget *s = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(s), on);
  g_signal_connect(s, "notify::active", cb, d);
  return s;
}

/* Commande d'administration (pkexec), dont seul le resultat compte */
static void root_done_msg(gboolean ok, const char *err, gpointer msg) {
  if (msg) say(msg, ok ? "Enregistré." : err, !ok);
}

/* ------------------------------------------------------------------ */
/* Ecrans                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
  char *name, *desc, *cur_mode, *transform;
  GPtrArray *modes;          /* "1920x1080@60.000000Hz" */
  gboolean enabled;
  int x, y, w, h;
  double scale;
  GtkWidget *mode_c, *scale_c, *rot_c, *pos_c;
} Out;

static GPtrArray *outs;
static GtkWidget *scr_box, *scr_msg;

static void out_free(gpointer p) {
  Out *o = p;
  g_free(o->name); g_free(o->desc); g_free(o->cur_mode); g_free(o->transform);
  if (o->modes) g_ptr_array_free(o->modes, TRUE);
  g_free(o);
}

static GPtrArray *read_outputs(void) {
  GPtrArray *a = g_ptr_array_new_with_free_func(out_free);
  const char *argv[] = {"wlr-randr", NULL};
  char *out = run_argv(argv, NULL);
  char **lines = out ? g_strsplit(out, "\n", -1) : NULL;
  Out *o = NULL;
  gboolean in_modes = FALSE;
  for (char **l = lines; l && *l; l++) {
    char *s = *l;
    if (*s && *s != ' ') {
      o = g_new0(Out, 1);
      o->modes = g_ptr_array_new_with_free_func(g_free);
      char *q = strchr(s, ' ');
      o->name = q ? g_strndup(s, q - s) : g_strdup(s);
      char *d1 = strchr(s, '"'), *d2 = d1 ? strrchr(s, '"') : NULL;
      o->desc = (d1 && d2 > d1) ? g_strndup(d1 + 1, d2 - d1 - 1) : g_strdup(o->name);
      o->scale = 1; o->transform = g_strdup("normal");
      g_ptr_array_add(a, o);
      in_modes = FALSE;
      continue;
    }
    if (!o) continue;
    char *t = g_strstrip(g_strdup(s));
    if (g_str_has_prefix(t, "Modes:")) in_modes = TRUE;
    else if (in_modes && strstr(t, " px, ")) {
      int w, h;
      char *comma = strstr(t, " px, ");
      if (sscanf(t, "%dx%d", &w, &h) == 2 && comma) {
        /* Nombres a point decimal, quelle que soit la langue (wlr-randr) */
        double hz = g_ascii_strtod(comma + 5, NULL);
        char hzs[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(hzs, sizeof hzs, "%.6f", hz);
        char *m = g_strdup_printf("%dx%d@%sHz", w, h, hzs);
        if (strstr(t, "current")) { g_free(o->cur_mode); o->cur_mode = g_strdup(m); o->w = w; o->h = h; }
        g_ptr_array_add(o->modes, m);
      }
    } else {
      in_modes = FALSE;
      if (g_str_has_prefix(t, "Enabled:")) o->enabled = strstr(t, "yes") != NULL;
      else if (g_str_has_prefix(t, "Position:")) sscanf(t, "Position: %d,%d", &o->x, &o->y);
      else if (g_str_has_prefix(t, "Transform:")) { g_free(o->transform); o->transform = g_strdup(g_strstrip(t + 10)); }
      else if (g_str_has_prefix(t, "Scale:")) o->scale = g_ascii_strtod(t + 6, NULL);
    }
    g_free(t);
  }
  g_strfreev(lines); g_free(out);
  return a;
}

static const char *SCALES_T[] = {"100 %", "125 %", "150 %", "175 %", "200 %", NULL};
static const double SCALES[] = {1, 1.25, 1.5, 1.75, 2};
static const char *ROT_T[] = {"Normale", "90°", "180°", "270°", NULL};
static const char *ROT_V[] = {"normal", "90", "180", "270"};
static const char *POS_T[] = {"À droite de l'écran principal", "À gauche", "Au-dessus", "En dessous",
                              "Recopie de l'écran principal", NULL};

static char *mode_label(const char *m) {
  int w, h;
  const char *at = strchr(m, '@');
  if (sscanf(m, "%dx%d", &w, &h) == 2 && at)
    return g_strdup_printf("%d × %d (%.0f Hz)", w, h, g_ascii_strtod(at + 1, NULL));
  return g_strdup(m);
}

/* Arguments wlr-randr de chaque ecran, d'apres les choix de la page ;
   enregistres pour etre rejoues a l'ouverture de session (lumina-displays). */
static char *displays_plan(GString *conf) {
  GString *cmd = g_string_new("");
  Out *p0 = outs->len ? outs->pdata[0] : NULL;
  int pw = 0, ph = 0;
  for (guint i = 0; i < outs->len; i++) {
    Out *o = outs->pdata[i];
    int mi = gtk_combo_box_get_active(GTK_COMBO_BOX(o->mode_c));
    const char *mode = mi >= 0 && (guint)mi < o->modes->len ? o->modes->pdata[mi] : o->cur_mode;
    int si = gtk_combo_box_get_active(GTK_COMBO_BOX(o->scale_c)), ri = gtk_combo_box_get_active(GTK_COMBO_BOX(o->rot_c));
    double sc = SCALES[si < 0 ? 0 : si];
    int w = 0, h = 0;
    if (mode) sscanf(mode, "%dx%d", &w, &h);
    if (ri == 1 || ri == 3) { int t = w; w = h; h = t; }
    int lw = (int)(w / sc), lh = (int)(h / sc);
    int x = 0, y = 0;
    gboolean mirror = FALSE;
    if (o == p0) { pw = lw; ph = lh; }
    else if (o->pos_c) {
      switch (gtk_combo_box_get_active(GTK_COMBO_BOX(o->pos_c))) {
        case 1: x = -lw; break;
        case 2: y = -lh; break;
        case 3: y = ph; break;
        case 4: mirror = TRUE; break;
        default: x = pw; break;
      }
    }
    char scs[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(scs, sizeof scs, "%.2f", sc);
    char *args = g_strdup_printf("%s%s --scale %s --transform %s --pos %d,%d", mode ? "--mode " : "", mode ? mode : "",
                                 scs, ROT_V[ri < 0 ? 0 : ri], x, y);
    g_string_append_printf(cmd, "wlr-randr --output '%s' %s; ", o->name, args);
    g_string_append_printf(conf, "%s\t%s\n", o->name, args);
    if (mirror && p0) g_string_append_printf(conf, "mirror\t%s\t%s\n", p0->name, o->name);
    g_free(args);
  }
  return g_string_free(cmd, FALSE);
}

static char *displays_backup;   /* plan precedent, pour annuler */
static int keep_left;
static guint keep_timer;
static GtkWidget *keep_dlg;

static void displays_run(const char *conf_text) {
  char *p = ucfg("displays.conf");
  if (conf_text) g_file_set_contents(p, conf_text, -1, NULL); else g_unlink(p);
  g_free(p);
  const char *argv[] = {"lumina-displays", "apply", NULL};
  int st; g_free(run_argv(argv, &st));
}

static void keep_finish(gboolean keep) {
  if (keep_timer) { g_source_remove(keep_timer); keep_timer = 0; }
  if (keep_dlg) { gtk_widget_destroy(keep_dlg); keep_dlg = NULL; }
  if (!keep) { displays_run(displays_backup); say(scr_msg, "Anciens paramètres rétablis.", FALSE); }
  else say(scr_msg, "Paramètres d'affichage enregistrés.", FALSE);
}
static gboolean keep_tick(gpointer d) {
  if (--keep_left <= 0) { keep_timer = 0; keep_finish(FALSE); return G_SOURCE_REMOVE; }
  char *t = g_strdup_printf("Retour aux anciens paramètres dans %d s.", keep_left);
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(keep_dlg), "%s", t);
  g_free(t);
  return G_SOURCE_CONTINUE;
}
static void on_keep_resp(GtkDialog *d, int r, gpointer u) { keep_finish(r == GTK_RESPONSE_ACCEPT); }

static void on_displays_apply(GtkButton *b, gpointer d) {
  char *p = ucfg("displays.conf");
  g_free(displays_backup); displays_backup = read_file(p);
  g_free(p);
  GString *conf = g_string_new("# Ecrans (Reglages > Ecrans) : sortie <TAB> arguments de wlr-randr\n");
  g_free(displays_plan(conf));
  displays_run(conf->str);
  g_string_free(conf, TRUE);
  /* Comme ChromeOS : confirmer, sinon retour en arriere tout seul (un
     ecran noir ne doit pas bloquer l'utilisateur). */
  keep_left = 12;
  keep_dlg = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                    "Conserver ces paramètres d'affichage ?");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(keep_dlg), "Retour aux anciens paramètres dans 12 s.");
  gtk_dialog_add_buttons(GTK_DIALOG(keep_dlg), "Rétablir", GTK_RESPONSE_REJECT, "Conserver", GTK_RESPONSE_ACCEPT, NULL);
  g_signal_connect(keep_dlg, "response", G_CALLBACK(on_keep_resp), NULL);
  gtk_widget_show_all(keep_dlg);
  keep_timer = g_timeout_add_seconds(1, keep_tick, NULL);
}

static void on_displays_reset(GtkButton *b, gpointer d) {
  displays_run(NULL);
  const char *argv[] = {"lumina-display-follow", "--once", NULL};
  int st; g_free(run_argv(argv, &st));
  say(scr_msg, "Réglages automatiques rétablis.", FALSE);
}

static void fill_screens(void) {
  clear_box(scr_box);
  if (outs) g_ptr_array_free(outs, TRUE);
  outs = read_outputs();
  for (guint i = 0; i < outs->len; i++) {
    Out *o = outs->pdata[i];
    char *head = i == 0 ? g_strdup_printf("%s (principal)", o->desc) : g_strdup(o->desc);
    GtkWidget *g = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(scr_box), label(head, "h2", 0));
    add_class(g, "group");
    gtk_container_add(GTK_CONTAINER(scr_box), g);
    g_free(head);
    o->mode_c = gtk_combo_box_text_new();
    int act = 0;
    for (guint m = 0; m < o->modes->len; m++) {
      char *l = mode_label(o->modes->pdata[m]);
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(o->mode_c), l);
      g_free(l);
      if (!g_strcmp0(o->modes->pdata[m], o->cur_mode)) act = m;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(o->mode_c), act);
    row(g, "Définition", NULL, o->mode_c, NULL);
    int sa = 0;
    for (int k = 0; k < 5; k++) if (fabs(SCALES[k] - o->scale) < 0.06) sa = k;
    o->scale_c = combo(SCALES_T, sa);
    row(g, "Taille de l'affichage", "Agrandit textes et fenêtres", o->scale_c, NULL);
    int ra = 0;
    for (int k = 0; k < 4; k++) if (!g_strcmp0(o->transform, ROT_V[k])) ra = k;
    o->rot_c = combo(ROT_T, ra);
    row(g, "Orientation", NULL, o->rot_c, NULL);
    o->pos_c = NULL;
    if (i > 0) {
      Out *p0 = outs->pdata[0];
      int pa = o->x < p0->x ? 1 : o->y < p0->y ? 2 : o->y > p0->y ? 3 : 0;
      o->pos_c = combo(POS_T, pa);
      row(g, "Disposition", "Bureau étendu ou recopie", o->pos_c, NULL);
    }
  }
  GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(bb, GTK_ALIGN_END);
  gtk_container_add(GTK_CONTAINER(bb), button("Réglages automatiques", "tbtn", G_CALLBACK(on_displays_reset), NULL));
  gtk_container_add(GTK_CONTAINER(bb), button("Appliquer", "fbtn", G_CALLBACK(on_displays_apply), NULL));
  gtk_container_add(GTK_CONTAINER(scr_box), bb);
  gtk_widget_show_all(scr_box);
}

static void build_screens(void) {
  GtkWidget *p = page_box("ecrans", "Écrans");
  scr_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_add(GTK_CONTAINER(p), scr_box);
  scr_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(scr_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), scr_msg);
  fill_screens();
}

/* ------------------------------------------------------------------ */
/* Souris et pave tactile (libinput, via la configuration de labwc)     */
/* ------------------------------------------------------------------ */
static GtkWidget *tp_speed, *tp_tap, *tp_nat, *ms_speed, *ms_nat, *ms_left;
static guint input_timer;

static gboolean input_write(gpointer d) {
  input_timer = 0;
  char *dir = g_build_filename(g_get_user_config_dir(), "labwc", NULL);
  g_mkdir_with_parents(dir, 0755);
  char *p = g_build_filename(dir, "rc.xml", NULL);
  char ts[16], ms[16];
  g_ascii_formatd(ts, sizeof ts, "%.2f", gtk_range_get_value(GTK_RANGE(tp_speed)));
  g_ascii_formatd(ms, sizeof ms, "%.2f", gtk_range_get_value(GTK_RANGE(ms_speed)));
#define YN(w) (gtk_switch_get_active(GTK_SWITCH(w)) ? "yes" : "no")
  char *x = g_strdup_printf(
    "<?xml version=\"1.0\"?>\n<!-- Ecrit par Reglages > Souris et pave tactile -->\n<labwc_config>\n  <libinput>\n"
    "    <device category=\"touchpad\">\n      <pointerSpeed>%s</pointerSpeed>\n      <tap>%s</tap>\n"
    "      <naturalScroll>%s</naturalScroll>\n      <disableWhileTyping>yes</disableWhileTyping>\n    </device>\n"
    "    <device category=\"non-touch\">\n      <pointerSpeed>%s</pointerSpeed>\n      <naturalScroll>%s</naturalScroll>\n"
    "      <leftHanded>%s</leftHanded>\n    </device>\n  </libinput>\n</labwc_config>\n",
    ts, YN(tp_tap), YN(tp_nat), ms, YN(ms_nat), YN(ms_left));
#undef YN
  g_file_set_contents(p, x, -1, NULL);
  g_free(x); g_free(p); g_free(dir);
  const char *argv[] = {"labwc", "--reconfigure", NULL};
  spawn_async(argv);
  return G_SOURCE_REMOVE;
}
static void on_input_changed(void) { if (input_timer) g_source_remove(input_timer); input_timer = g_timeout_add(400, input_write, NULL); }
static void on_input_sw(GObject *o, GParamSpec *ps, gpointer d) { on_input_changed(); }
static void on_input_range(GtkRange *r, gpointer d) { on_input_changed(); }

static double xml_num(const char *x, const char *cat, const char *tag, double def) {
  if (!x) return def;
  char *c = strstr(x, cat); if (!c) return def;
  char *open = g_strdup_printf("<%s>", tag), *t = strstr(c, open);
  double v = t ? g_ascii_strtod(t + strlen(open), NULL) : def;
  g_free(open);
  return v;
}
static gboolean xml_yes(const char *x, const char *cat, const char *tag, gboolean def) {
  if (!x) return def;
  char *c = strstr(x, cat); if (!c) return def;
  char *open = g_strdup_printf("<%s>", tag), *t = strstr(c, open);
  gboolean v = t ? g_str_has_prefix(t + strlen(open), "yes") : def;
  g_free(open);
  return v;
}

static GtkWidget *speed_scale(double v) {
  GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -1, 1, 0.1);
  gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
  gtk_widget_set_size_request(s, 220, -1);
  gtk_range_set_value(GTK_RANGE(s), v);
  g_signal_connect(s, "value-changed", G_CALLBACK(on_input_range), NULL);
  return s;
}

static void build_mouse(void) {
  GtkWidget *p = page_box("souris", "Souris et pavé tactile");
  char *rp = g_build_filename(g_get_user_config_dir(), "labwc", "rc.xml", NULL), *x = read_file(rp);
  g_free(rp);
  const char *tc = "\"touchpad\"", *mc = "\"non-touch\"";
  GtkWidget *g = group(p, "Pavé tactile");
  tp_speed = speed_scale(xml_num(x, tc, "pointerSpeed", 0));
  row(g, "Vitesse du curseur", NULL, tp_speed, NULL);
  tp_tap = switch_new(xml_yes(x, tc, "tap", TRUE), G_CALLBACK(on_input_sw), NULL);
  row(g, "Toucher pour cliquer", "Un appui léger vaut un clic", tp_tap, NULL);
  tp_nat = switch_new(xml_yes(x, tc, "naturalScroll", TRUE), G_CALLBACK(on_input_sw), NULL);
  row(g, "Défilement naturel", "Le contenu suit les doigts, comme sur un téléphone", tp_nat, NULL);
  GtkWidget *g2 = group(p, "Souris");
  ms_speed = speed_scale(xml_num(x, mc, "pointerSpeed", 0));
  row(g2, "Vitesse du curseur", NULL, ms_speed, NULL);
  ms_nat = switch_new(xml_yes(x, mc, "naturalScroll", FALSE), G_CALLBACK(on_input_sw), NULL);
  row(g2, "Inverser le défilement", NULL, ms_nat, NULL);
  ms_left = switch_new(xml_yes(x, mc, "leftHanded", FALSE), G_CALLBACK(on_input_sw), NULL);
  row(g2, "Inverser les boutons", "Pour les gauchers : clic principal à droite", ms_left, NULL);
  g_free(x);
}

/* ------------------------------------------------------------------ */
/* Date et heure                                                        */
/* ------------------------------------------------------------------ */
static GtkWidget *tz_combo, *tz_msg, *ntp_sw;
static GPtrArray *tz_list;
static gboolean tz_lock;

static void on_tz(GtkComboBox *c, gpointer d) {
  if (tz_lock) return;
  int i = gtk_combo_box_get_active(c);
  if (i < 0) return;
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-system", "set-timezone", tz_list->pdata[i], NULL};
  run_root_async(argv, NULL, root_done_msg, tz_msg);
}
static void on_ntp(GObject *o, GParamSpec *ps, gpointer d) {
  const char *argv[] = {"timedatectl", "set-ntp", gtk_switch_get_active(GTK_SWITCH(o)) ? "true" : "false", NULL};
  run_root_async(argv, NULL, root_done_msg, tz_msg);
}

/* "Europe/Paris" -> "Europe · Paris" */
static char *tz_label(const char *z) {
  char *s = g_strdup(z);
  g_strdelimit(s, "_", ' ');
  char *slash = strchr(s, '/');
  if (slash) { *slash = 0; char *r = g_strdup_printf("%s · %s", s, slash + 1); g_free(s); return r; }
  return s;
}

static void build_datetime(void) {
  GtkWidget *p = page_box("heure", "Date et heure");
  GtkWidget *g = group(p, NULL);
  const char *ta[] = {"timedatectl", "show", "-p", "Timezone", "--value", NULL};
  char *cur = run_argv(ta, NULL);
  if (cur) g_strstrip(cur);
  const char *la[] = {"timedatectl", "list-timezones", NULL};
  char *all = run_argv(la, NULL);
  tz_list = g_ptr_array_new_with_free_func(g_free);
  tz_combo = gtk_combo_box_text_new();
  int act = -1;
  char **z = all ? g_strsplit(all, "\n", -1) : NULL;
  for (char **i = z; i && *i; i++) {
    if (!**i) continue;
    char *l = tz_label(*i);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tz_combo), l);
    g_free(l);
    if (cur && !strcmp(cur, *i)) act = tz_list->len;
    g_ptr_array_add(tz_list, g_strdup(*i));
  }
  tz_lock = TRUE; gtk_combo_box_set_active(GTK_COMBO_BOX(tz_combo), act); tz_lock = FALSE;
  g_signal_connect(tz_combo, "changed", G_CALLBACK(on_tz), NULL);
  row(g, "Fuseau horaire", NULL, tz_combo, NULL);
  const char *na[] = {"timedatectl", "show", "-p", "NTP", "--value", NULL};
  char *ntp = run_argv(na, NULL);
  ntp_sw = switch_new(!ntp || g_str_has_prefix(ntp, "yes"), G_CALLBACK(on_ntp), NULL);
  row(g, "Heure automatique", "Mise à l'heure par Internet", ntp_sw, NULL);
  row(g, "Format", "24 heures (jj/mm/aaaa)", NULL, NULL);
  tz_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(tz_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), tz_msg);
  g_strfreev(z); g_free(all); g_free(cur); g_free(ntp);
}

/* ------------------------------------------------------------------ */
/* Imprimantes (CUPS, imprimantes sans pilote : IPP Everywhere)         */
/* ------------------------------------------------------------------ */
static GtkWidget *prn_grp, *prn_found, *prn_msg;

static void fill_printers(void);
static void on_prn_test(GtkButton *b, gpointer d) {
  const char *argv[] = {"lp", "-d", d, "/usr/share/cups/data/testprint", NULL};
  int st; g_free(run_argv(argv, &st));
  say(prn_msg, st == 0 ? "Page de test envoyée." : "Impression impossible.", st != 0);
}
static void on_prn_default(GtkButton *b, gpointer d) {
  const char *argv[] = {"lpoptions", "-d", d, NULL};
  int st; g_free(run_argv(argv, &st));
  fill_printers();
}
static void on_prn_remove(GtkButton *b, gpointer d) {
  const char *argv[] = {"lpadmin", "-x", d, NULL};
  int st; g_free(run_argv(argv, &st));
  say(prn_msg, st == 0 ? "Imprimante supprimée." : "Suppression impossible.", st != 0);
  fill_printers();
}

static void fill_printers(void) {
  clear_box(prn_grp);
  const char *da[] = {"lpstat", "-d", NULL};
  char *def = run_argv(da, NULL), *dn = def ? strstr(def, ": ") : NULL;
  if (dn) { dn += 2; g_strstrip(dn); }
  const char *ea[] = {"lpstat", "-e", NULL};
  char *list = run_argv(ea, NULL);
  char **l = list ? g_strsplit(list, "\n", -1) : NULL;
  int n = 0;
  for (char **i = l; i && *i; i++) {
    if (!**i) continue;
    gboolean isdef = dn && !strcmp(dn, *i);
    GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_add(GTK_CONTAINER(bb), button("Page de test", "tbtn", G_CALLBACK(on_prn_test), g_strdup(*i)));
    if (!isdef) gtk_container_add(GTK_CONTAINER(bb), button("Par défaut", "tbtn", G_CALLBACK(on_prn_default), g_strdup(*i)));
    gtk_container_add(GTK_CONTAINER(bb), button("Supprimer", "tbtn", G_CALLBACK(on_prn_remove), g_strdup(*i)));
    char *name = g_strdup(*i);
    g_strdelimit(name, "_", ' ');
    row(prn_grp, name, isdef ? "Imprimante par défaut" : NULL, bb, NULL);
    g_free(name);
    n++;
  }
  if (!n) row(prn_grp, "Aucune imprimante", "Les imprimantes Wi-Fi et USB récentes s'ajoutent toutes seules une fois allumées.", NULL, NULL);
  gtk_widget_show_all(prn_grp);
  g_strfreev(l); g_free(list); g_free(def);
}

static void on_prn_add(GtkButton *b, gpointer d) {
  const char *uri = d;
  /* Nom lisible tire de l'adresse, sans caracteres refuses par CUPS */
  char *base = g_path_get_basename(uri), *dec = g_uri_unescape_string(base, NULL);
  GString *nm = g_string_new(NULL);
  for (const char *p = dec ? dec : base; *p && nm->len < 40; p++) g_string_append_c(nm, g_ascii_isalnum(*p) ? *p : '_');
  if (!nm->len) g_string_append(nm, "Imprimante");
  const char *argv[] = {"lpadmin", "-p", nm->str, "-E", "-v", uri, "-m", "everywhere", NULL};
  int st; char *o = run_argv(argv, &st);
  say(prn_msg, st == 0 ? "Imprimante ajoutée." : "Cette imprimante n'est pas compatible sans pilote.", st != 0);
  g_free(o); g_free(base); g_free(dec); g_string_free(nm, TRUE);
  fill_printers();
}

static void on_prn_search(GtkButton *b, gpointer d) {
  clear_box(prn_found);
  const char *argv[] = {"lpinfo", "--include-schemes", "dnssd,usb,ipp,ipps", "-v", NULL};
  char *out = run_argv(argv, NULL);
  char **l = out ? g_strsplit(out, "\n", -1) : NULL;
  int n = 0;
  GtkWidget *g = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(g, "group");
  for (char **i = l; i && *i; i++) {
    char *sp = strchr(*i, ' ');
    if (!sp || !strstr(sp, "://")) continue;
    const char *uri = sp + 1;
    char *dec = g_uri_unescape_string(uri, NULL);
    row(g, dec ? dec : uri, NULL, button("Ajouter", "fbtn", G_CALLBACK(on_prn_add), g_strdup(uri)), NULL);
    g_free(dec);
    n++;
  }
  if (!n) row(g, "Aucune imprimante trouvée", "Vérifiez qu'elle est allumée et sur le même Wi-Fi, ou branchée en USB.", NULL, NULL);
  gtk_container_add(GTK_CONTAINER(prn_found), g);
  gtk_widget_show_all(prn_found);
  g_strfreev(l); g_free(out);
}

static void build_printers(void) {
  GtkWidget *p = page_box("imprimantes", "Imprimantes");
  prn_grp = group(p, NULL);
  GtkWidget *b = button("Rechercher des imprimantes", "fbtn", G_CALLBACK(on_prn_search), NULL);
  gtk_widget_set_halign(b, GTK_ALIGN_START);
  gtk_container_add(GTK_CONTAINER(p), b);
  prn_found = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(p), prn_found);
  prn_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(prn_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), prn_msg);
  fill_printers();
}

/* ------------------------------------------------------------------ */
/* Energie                                                              */
/* ------------------------------------------------------------------ */
static GtkWidget *pw_msg2;
static const char *LID_T[] = {"Mettre en veille", "Ne rien faire", "Éteindre", NULL};
static const char *LID_V[] = {"suspend", "ignore", "poweroff"};
static void on_lid(GtkComboBox *c, gpointer d) {
  int i = gtk_combo_box_get_active(c);
  if (i < 0) return;
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-system", "set-lid", LID_V[i], NULL};
  run_root_async(argv, NULL, root_done_msg, pw_msg2);
}

static void build_power(void) {
  GtkWidget *p = page_box("energie", "Énergie");
  /* Batterie : niveau, etat et temps restant */
  GDir *bd = g_dir_open("/sys/class/power_supply", 0, NULL);
  const char *bn;
  while (bd && (bn = g_dir_read_name(bd))) {
    char *tp = g_strdup_printf("/sys/class/power_supply/%s/type", bn), *t = read_file(tp);
    if (!g_strcmp0(t, "Battery")) {
      char *cp = g_strdup_printf("/sys/class/power_supply/%s/capacity", bn), *c = read_file(cp);
      char *sp = g_strdup_printf("/sys/class/power_supply/%s/status", bn), *st = read_file(sp);
      char *eta = battery_eta();
      const char *state = !g_strcmp0(st, "Charging") ? "En charge" : !g_strcmp0(st, "Full") ? "Chargée" : "Sur batterie";
      char *title = g_strdup_printf("Batterie : %s %%", c ? c : "?"), *sub = g_strdup_printf("%s%s%s", state, eta ? " · " : "", eta ? eta : "");
      row(group(p, NULL), title, sub, NULL, NULL);
      g_free(cp); g_free(c); g_free(sp); g_free(st); g_free(eta); g_free(title); g_free(sub);
      g_free(tp); g_free(t);
      break;
    }
    g_free(tp); g_free(t);
  }
  if (bd) g_dir_close(bd);
  GtkWidget *g = group(p, NULL);
  char *lid = read_file("/var/lib/lumina/config/lid");
  int li = 0;
  for (int i = 0; i < 3; i++) if (!g_strcmp0(lid, LID_V[i])) li = i;
  g_free(lid);
  GtkWidget *lc = combo(LID_T, li);
  g_signal_connect(lc, "changed", G_CALLBACK(on_lid), NULL);
  row(g, "Quand le capot est fermé", NULL, lc, NULL);
  GtkWidget *sc = delay_combo("LUMINA_SUSPEND_DELAY", g_file_test("/sys/class/power_supply/BAT0", G_FILE_TEST_EXISTS) ? 1800 : 86400 * 365);
  g_signal_connect(sc, "changed", G_CALLBACK(on_delay), NULL);
  row(g, "Mise en veille après", "Sans activité au clavier ou à la souris", sc, NULL);
  pw_msg2 = label("", "msg", 0);
  gtk_widget_set_no_show_all(pw_msg2, TRUE);
  gtk_container_add(GTK_CONTAINER(p), pw_msg2);
}

/* ------------------------------------------------------------------ */
/* Accessibilite                                                        */
/* ------------------------------------------------------------------ */
static void on_reader(GObject *o, GParamSpec *ps, gpointer d) {
  gboolean on = gtk_switch_get_active(GTK_SWITCH(o));
  uflag_set("screenreader", on);
  if (on) { const char *a[] = {"orca", NULL}; spawn_async(a); }
  else { const char *a[] = {"pkill", "-x", "orca", NULL}; spawn_async(a); }
}
static void on_magnify(GtkButton *b, gpointer d) {
  const char *a[] = {"wtype", "-M", "logo", "-M", "ctrl", "m", "-m", "ctrl", "-m", "logo", NULL};
  spawn_async(a);
}
/* Curseur et texte : lus par labwc et les applis a l'ouverture de session */
static void write_access_env(void) {
  char *dir = g_build_filename(g_get_user_config_dir(), "labwc", NULL);
  g_mkdir_with_parents(dir, 0755);
  char *p = g_build_filename(dir, "environment", NULL);
  GString *s = g_string_new("# Ecrit par Reglages > Accessibilite\n");
  if (uflag("bigcursor")) g_string_append(s, "XCURSOR_SIZE=48\n");
  g_file_set_contents(p, s->str, -1, NULL);
  g_string_free(s, TRUE); g_free(p); g_free(dir);
}
static void on_access_flag(GObject *o, GParamSpec *ps, gpointer name) {
  uflag_set(name, gtk_switch_get_active(GTK_SWITCH(o)));
  write_access_env();
  /* Le shell relit ces drapeaux (portail : taille du texte, curseur) */
  const char *a[] = {"lumina-shell", "--action", "refresh", NULL};
  spawn_async(a);
  /* Contraste eleve : les applis LuminaOS surveillent ce fichier et
     suivent d'elles-memes (lumina-ui). */
}

static void build_access(void) {
  GtkWidget *p = page_box("accessibilite", "Accessibilité");
  GtkWidget *g = group(p, "Vision");
  row(g, "Lecteur d'écran", "Lit à voix haute ce qui est à l'écran (Orca)",
      switch_new(uflag("screenreader"), G_CALLBACK(on_reader), NULL), NULL);
  row(g, "Loupe", "Windows + Ctrl + M, puis Windows + Ctrl + + ou − pour zoomer",
      button("Activer / désactiver", "tbtn", G_CALLBACK(on_magnify), NULL), NULL);
  row(g, "Contraste élevé", "Textes et contours plus marqués dans LuminaOS",
      switch_new(uflag("contrast"), G_CALLBACK(on_access_flag), "contrast"), NULL);
  row(g, "Texte plus grand", "Applis LuminaOS et Linux (à la prochaine ouverture)",
      switch_new(uflag("bigtext"), G_CALLBACK(on_access_flag), "bigtext"), NULL);
  row(g, "Grand curseur", "Prend effet à la prochaine ouverture de session",
      switch_new(uflag("bigcursor"), G_CALLBACK(on_access_flag), "bigcursor"), NULL);
  gtk_container_add(GTK_CONTAINER(p), desc("Pour agrandir tout l'écran, choisissez une taille d'affichage plus grande "
                                            "dans Écrans. Dans Chromium, Ctrl + + agrandit la page."));
}

/* ------------------------------------------------------------------ */
/* Raccourcis clavier                                                   */
/* ------------------------------------------------------------------ */
static void build_shortcuts(void) {
  GtkWidget *p = page_box("raccourcis", "Raccourcis clavier");
  const char *S[][3] = {
    {"Général", NULL, NULL},
    {"Windows", "Ouvrir le lanceur d'applications", NULL}, {"Windows + A", "Réglages rapides", NULL},
    {"Windows + L", "Verrouiller l'écran", NULL}, {"Windows + V", "Historique du presse-papiers", NULL},
    {"Windows + .", "Emojis", NULL}, {"Ctrl + Alt + /", "Cette liste de raccourcis", NULL},
    {"Windows + Échap", "Gestionnaire de tâches", NULL},
    {"Fenêtres et bureaux", NULL, NULL},
    {"Windows + Tab ou F5", "Vue d'ensemble des fenêtres", NULL}, {"Alt + Tab", "Passer d'une fenêtre à l'autre", NULL},
    {"Alt + [  /  Alt + ]", "Écran partagé : fenêtre à gauche / à droite", NULL},
    {"Alt + =", "Agrandir la fenêtre", NULL}, {"Alt + −", "Réduire la fenêtre", NULL},
    {"Windows + [  /  ]", "Bureau précédent / suivant", NULL}, {"Windows + 1 … 4", "Aller au bureau 1 à 4", NULL},
    {"Windows + Maj + 1 … 4", "Envoyer la fenêtre sur un bureau", NULL},
    {"Captures", NULL, NULL},
    {"Impr", "Capture de l'écran", NULL}, {"Maj + Impr", "Capture d'une zone", NULL},
    {"Windows + Maj + S", "Barre de capture (photo ou vidéo, écran ou zone)", NULL},
    {"Ctrl + Impr", "Enregistrer l'écran (démarrer / arrêter)", NULL},
    {"Accessibilité", NULL, NULL},
    {"Windows + Ctrl + M", "Loupe", NULL}, {"Windows + Ctrl + + / −", "Zoom de la loupe", NULL},
    {"Pavé tactile", NULL, NULL},
    {"3 doigts vers le haut / le bas", "Ouvrir / fermer la vue d'ensemble", NULL},
    {"4 doigts à gauche / à droite", "Bureau précédent / suivant", NULL},
  };
  GtkWidget *g = NULL;
  for (guint i = 0; i < G_N_ELEMENTS(S); i++) {
    if (!S[i][1]) { g = group(p, S[i][0]); continue; }
    GtkWidget *k = label(S[i][0], "kbd", 1);
    row(g, S[i][1], NULL, k, NULL);
  }
}

/* ------------------------------------------------------------------ */
/* Ajouts : Mises a jour (canal), A propos (reinitialisation), Linux    */
/* ------------------------------------------------------------------ */
static GtkWidget *chan_msg;
static void on_channel(GtkComboBox *c, gpointer d) {
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-system", "set-channel",
                        gtk_combo_box_get_active(c) == 1 ? "beta" : "stable", NULL};
  run_root_async(argv, NULL, root_done_msg, chan_msg);
}
static void updates_more(GtkWidget *p) {
  GtkWidget *g = group(p, NULL);
  char *u = read_file("/var/lib/lumina/config/update.conf");
  const char *ct[] = {"Stable", "Bêta (nouveautés en avance)", NULL};
  GtkWidget *c = combo(ct, u && strstr(u, "CHANNEL=beta") ? 1 : 0);
  g_free(u);
  g_signal_connect(c, "changed", G_CALLBACK(on_channel), NULL);
  row(g, "Canal", "La bêta reçoit les nouveautés plus tôt, avec plus de risques de bugs", c, NULL);
  chan_msg = label("", "msg", 0);
  gtk_widget_set_no_show_all(chan_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), chan_msg);
}

static void on_powerwash(GtkButton *b, gpointer d) {
  GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                                          "Réinitialiser LuminaOS ?");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg),
      "Le PC redémarre et TOUT est effacé : compte, fichiers personnels, réglages, applis Linux et Android. "
      "LuminaOS repart comme neuf. Sauvegardez d'abord vos fichiers.");
  gtk_dialog_add_buttons(GTK_DIALOG(dlg), "Annuler", GTK_RESPONSE_CANCEL, "Réinitialiser et redémarrer", GTK_RESPONSE_ACCEPT, NULL);
  int r = gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
  if (r != GTK_RESPONSE_ACCEPT) return;
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-system", "powerwash", NULL};
  run_root_async(argv, NULL, root_done_msg, NULL);
}
static void about_more(GtkWidget *p) {
  GtkWidget *g = group(p, NULL);
  GtkWidget *b = button("Réinitialiser", "tbtn", G_CALLBACK(on_powerwash), NULL);
  add_class(b, "danger");
  row(g, "Réinitialiser (Powerwash)", "Efface toutes les données et remet LuminaOS à neuf", b, NULL);
}

static void on_linux_backup(GtkWidget *w, gpointer d) {
  const char *c[] = {"lumina-linux", "--backup", NULL};
  open_terminal(c);
}
static void on_linux_restore(GtkWidget *w, gpointer d) {
  GtkFileChooserNative *fc = gtk_file_chooser_native_new("Restaurer Linux", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN,
                                                         "Restaurer", "Annuler");
  GtkFileFilter *f = gtk_file_filter_new();
  gtk_file_filter_set_name(f, "Sauvegardes Linux (.tar)");
  gtk_file_filter_add_pattern(f, "*.tar");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), f);
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
    char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    const char *c[] = {"lumina-linux", "--restore", fn, NULL};
    open_terminal(c);
    g_free(fn);
  }
  g_object_unref(fc);
}
static void linux_more(GtkWidget *grp, gboolean installed) {
  if (installed)
    row(grp, "Sauvegarder", "Copie Linux et ses applis dans Téléchargements (fichier .tar)",
        button("Sauvegarder", "tbtn", G_CALLBACK(on_linux_backup), NULL), NULL);
  row(grp, "Restaurer une sauvegarde", installed ? "Remplace le Linux actuel" : "Depuis un fichier .tar",
      button("Restaurer…", "tbtn", G_CALLBACK(on_linux_restore), NULL), NULL);
  if (installed)
    row(grp, "Ports", "Un serveur lancé dans Linux (ex. port 8080) s'ouvre dans Chromium à l'adresse localhost:8080", NULL, NULL);
}

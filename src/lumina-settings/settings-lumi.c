/* Reglages > Lumi : l'assistant IA de LuminaOS (inclus par lumina-settings.c).
 *
 *   - Desactive / Sur ce PC (prive) / Service en ligne ;
 *   - modeles locaux du catalogue : telechargement (pkexec
 *     lumina-ai-model, verifie par empreinte), choix, suppression ;
 *   - service en ligne compatible OpenAI : adresse, modele, cle. */
#include "lumina-ai.h"

#define AI_MODEL_TOOL "/usr/local/sbin/lumina-ai-model"
#define AI_CATALOG "/usr/share/lumina/ai-models.conf"

typedef struct { char *id, *name, *desc; guint64 size; GtkWidget *sub, *act, *del; } AiModel;
static GPtrArray *ai_models;
static GtkWidget *ai_mode, *ai_local_box, *ai_remote_box, *ai_url, *ai_rmodel, *ai_key, *ai_msg;
static guint ai_poll;
static gboolean ai_installing;

static void ai_model_free(gpointer p) { AiModel *m = p; g_free(m->id); g_free(m->name); g_free(m->desc); g_free(m); }

static long ai_mem_mb(void) {
  char *mi = read_file("/proc/meminfo");
  long kb = mi && strstr(mi, "MemTotal:") ? atol(strstr(mi, "MemTotal:") + 9) : 0;
  g_free(mi);
  return kb / 1024;
}

/* Etat des modeles : id -> "installe" | "absent" | "telechargement|octets" */
static GHashTable *ai_states(void) {
  GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  const char *argv[] = {AI_MODEL_TOOL, "list", NULL};
  char *o = run_argv(argv, NULL);
  char **l = o ? g_strsplit(o, "\n", -1) : NULL;
  for (char **i = l; i && *i; i++) {
    char *bar = strchr(*i, '|');
    if (bar) { *bar = 0; g_hash_table_insert(h, g_strdup(*i), g_strdup(bar + 1)); }
  }
  g_strfreev(l); g_free(o);
  return h;
}

static gboolean ai_refresh(gpointer d) {
  LuminaAiConfig *c = lumina_ai_config_load();
  GHashTable *st = ai_states();
  gboolean downloading = FALSE;
  for (guint i = 0; i < ai_models->len; i++) {
    AiModel *m = ai_models->pdata[i];
    const char *s = g_hash_table_lookup(st, m->id);
    char *size = g_format_size(m->size), *t;
    gboolean inst = s && !strcmp(s, "installe");
    gboolean active = inst && c->mode == LUMINA_AI_LOCAL && !g_strcmp0(c->model, m->id);
    if (s && g_str_has_prefix(s, "telechargement")) {
      guint64 got = g_ascii_strtoull(strchr(s, '|') ? strchr(s, '|') + 1 : "0", NULL, 10);
      t = ai_installing ? g_strdup_printf("Téléchargement… %d %%", (int)(got * 100 / MAX(m->size, 1)))
                        : g_strdup_printf("Téléchargement interrompu (%d %%) : reprenez-le", (int)(got * 100 / MAX(m->size, 1)));
      downloading |= ai_installing;
    } else if (active) t = g_strdup_printf("Utilisé par Lumi · %s", m->desc);
    else if (inst) t = g_strdup_printf("Installé · %s", m->desc);
    else t = g_strdup_printf("%s · %s à télécharger", m->desc, size);
    gtk_label_set_text(GTK_LABEL(m->sub), t);
    gtk_button_set_label(GTK_BUTTON(m->act), active ? "Utilisé" : inst ? "Utiliser" : "Télécharger");
    gtk_widget_set_sensitive(m->act, !active && !ai_installing);
    gtk_widget_set_visible(m->del, (inst || (s && g_str_has_prefix(s, "telechargement"))) && !ai_installing);
    g_free(size); g_free(t);
  }
  g_hash_table_unref(st);
  lumina_ai_config_free(c);
  return downloading;
}
/* Minuterie : tant que l'installation tourne (meme avant que le fichier
   partiel n'apparaisse : pkexec met un instant a demarrer). */
static gboolean ai_tick(gpointer d) {
  ai_refresh(NULL);
  if (ai_installing) return G_SOURCE_CONTINUE;
  ai_poll = 0;
  return G_SOURCE_REMOVE;
}

static void ai_show_mode(void) {
  int m = gtk_combo_box_get_active(GTK_COMBO_BOX(ai_mode));
  gtk_widget_set_visible(ai_local_box, m == 1);
  gtk_widget_set_visible(ai_remote_box, m == 2);
}

static void on_ai_mode(GtkComboBox *cb, gpointer d) {
  LuminaAiConfig *c = lumina_ai_config_load();
  int m = gtk_combo_box_get_active(cb);
  c->mode = m == 1 ? LUMINA_AI_LOCAL : m == 2 ? LUMINA_AI_REMOTE : LUMINA_AI_OFF;
  lumina_ai_config_save(c);
  lumina_ai_config_free(c);
  ai_show_mode();
  ai_refresh(NULL);
}

static void ai_use(const char *id) {
  LuminaAiConfig *c = lumina_ai_config_load();
  g_free(c->model); c->model = g_strdup(id);
  c->mode = LUMINA_AI_LOCAL;
  lumina_ai_config_save(c);
  lumina_ai_config_free(c);
  /* Le serveur deja lance garde l'ancien modele : on l'arrete */
  const char *argv[] = {"pkill", "-u", g_get_user_name(), "-f", "^/usr/lib/lumina-ai/llama-server", NULL};
  spawn_async(argv);
  ai_refresh(NULL);
}

static void on_ai_installed(GObject *o, GAsyncResult *res, gpointer d) {
  AiModel *m = d;
  char *err = NULL;
  g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), res, NULL, &err, NULL);
  gboolean ok = g_subprocess_get_if_exited(G_SUBPROCESS(o)) && g_subprocess_get_exit_status(G_SUBPROCESS(o)) == 0;
  int code = g_subprocess_get_if_exited(G_SUBPROCESS(o)) ? g_subprocess_get_exit_status(G_SUBPROCESS(o)) : -1;
  g_object_unref(o);
  ai_installing = FALSE;
  if (ok) { ai_use(m->id); say(ai_msg, "Lumi est prêt. Ouvrez-le avec Windows + Espace.", FALSE); }
  else if (code == 126 || code == 127) gtk_widget_hide(ai_msg);   /* autorisation refusee */
  else say(ai_msg, err && *g_strstrip(err) ? err : "Le téléchargement a échoué.", TRUE);
  g_free(err);
  ai_refresh(NULL);
}

static void on_ai_act(GtkButton *b, gpointer d) {
  AiModel *m = d;
  char *path = lumina_ai_model_path(m->id);
  if (path) { g_free(path); ai_use(m->id); return; }
  GError *e = NULL;
  GSubprocess *s = g_subprocess_new(G_SUBPROCESS_FLAGS_STDERR_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &e,
                                    "pkexec", AI_MODEL_TOOL, "install", m->id, NULL);
  if (!s) { say(ai_msg, "Impossible de lancer le téléchargement.", TRUE); g_clear_error(&e); return; }
  ai_installing = TRUE;
  gtk_widget_hide(ai_msg);
  g_subprocess_communicate_utf8_async(s, NULL, NULL, on_ai_installed, m);
  if (!ai_poll) ai_poll = g_timeout_add_seconds(1, ai_tick, NULL);
  ai_refresh(NULL);
}

static void on_ai_del_done(GObject *o, GAsyncResult *res, gpointer d) {
  g_subprocess_wait_finish(G_SUBPROCESS(o), res, NULL);
  g_object_unref(o);
  ai_refresh(NULL);
}

static void on_ai_del(GtkButton *b, gpointer d) {
  AiModel *m = d;
  GSubprocess *s = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "pkexec", AI_MODEL_TOOL, "remove", m->id, NULL);
  if (s) g_subprocess_wait_async(s, NULL, on_ai_del_done, NULL);
}

static void on_ai_remote_save(GtkButton *b, gpointer d) {
  const char *url = gtk_entry_get_text(GTK_ENTRY(ai_url));
  if (!g_str_has_prefix(url, "https://") && !g_str_has_prefix(url, "http://localhost") && !g_str_has_prefix(url, "http://127.0.0.1")) {
    say(ai_msg, "L'adresse doit commencer par https:// (connexion chiffrée).", TRUE);
    return;
  }
  LuminaAiConfig *c = lumina_ai_config_load();
  g_free(c->remote_url); c->remote_url = g_strdup(url);
  g_free(c->remote_model); c->remote_model = g_strdup(gtk_entry_get_text(GTK_ENTRY(ai_rmodel)));
  c->mode = LUMINA_AI_REMOTE;
  lumina_ai_config_save(c);
  lumina_ai_config_free(c);
  const char *k = gtk_entry_get_text(GTK_ENTRY(ai_key));
  if (*k) lumina_ai_key_save(k);
  gtk_entry_set_text(GTK_ENTRY(ai_key), "");
  char *saved = lumina_ai_key_load();
  gtk_entry_set_placeholder_text(GTK_ENTRY(ai_key), saved && *saved ? "Clé enregistrée (laisser vide pour la garder)" : "Clé d'accès");
  g_free(saved);
  say(ai_msg, "Service en ligne enregistré.", FALSE);
}

static void build_lumi(void) {
  GtkWidget *p = page_box("lumi", "Lumi");
  gtk_container_add(GTK_CONTAINER(p), desc("Lumi répond à vos questions, résume et reformule vos textes (Texte, recherche du lanceur, Windows + Espace). "
                                           "Il peut se tromper : vérifiez les informations importantes."));
  GtkWidget *g = group(p, NULL);
  ai_mode = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ai_mode), "Désactivé");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ai_mode), "Sur ce PC (privé)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ai_mode), "Service en ligne");
  LuminaAiConfig *c = lumina_ai_config_load();
  gtk_combo_box_set_active(GTK_COMBO_BOX(ai_mode), c->mode == LUMINA_AI_LOCAL ? 1 : c->mode == LUMINA_AI_REMOTE ? 2 : 0);
  g_signal_connect(ai_mode, "changed", G_CALLBACK(on_ai_mode), NULL);
  row(g, "Fonctionnement", NULL, ai_mode, NULL);

  ai_msg = label("", "msg", 0);
  gtk_label_set_line_wrap(GTK_LABEL(ai_msg), TRUE);
  gtk_widget_set_no_show_all(ai_msg, TRUE);
  gtk_container_add(GTK_CONTAINER(p), ai_msg);

  /* Sur ce PC */
  ai_local_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_add(GTK_CONTAINER(p), ai_local_box);
  gtk_container_add(GTK_CONTAINER(ai_local_box), label("Modèle sur ce PC", "h2", 0));
  long mem = ai_mem_mb();
  char *intro = g_strdup_printf("Le modèle tourne sur ce PC : vos textes ne quittent jamais l'appareil, même hors connexion. "
                                "Il occupe la mémoire seulement quand vous utilisez Lumi. Ce PC a %ld Go de mémoire : %s",
                                (mem + 512) / 1024, mem < 7000 ? "le modèle « Léger » est conseillé." : "les deux modèles conviennent.");
  gtk_container_add(GTK_CONTAINER(ai_local_box), desc(intro));
  g_free(intro);
  GtkWidget *mg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(mg, "group");
  gtk_container_add(GTK_CONTAINER(ai_local_box), mg);
  ai_models = g_ptr_array_new_with_free_func(ai_model_free);
  char *cat = read_file(AI_CATALOG);
  char **lines = cat ? g_strsplit(cat, "\n", -1) : NULL;
  for (char **l = lines; l && *l; l++) {
    if (!**l || **l == '#') continue;
    char **f = g_strsplit(*l, "|", 6);
    if (g_strv_length(f) == 6) {
      AiModel *m = g_new0(AiModel, 1);
      m->id = g_strdup(f[0]); m->name = g_strdup(f[1]); m->desc = g_strdup(f[2]);
      m->size = g_ascii_strtoull(f[3], NULL, 10);
      GtkWidget *end = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      m->del = button("Supprimer", "tbtn", G_CALLBACK(on_ai_del), m);
      gtk_widget_set_no_show_all(m->del, TRUE);
      m->act = button("Télécharger", "fbtn", G_CALLBACK(on_ai_act), m);
      gtk_container_add(GTK_CONTAINER(end), m->del);
      gtk_container_add(GTK_CONTAINER(end), m->act);
      row(mg, m->name, "", end, &m->sub);
      g_ptr_array_add(ai_models, m);
    }
    g_strfreev(f);
  }
  g_strfreev(lines); g_free(cat);

  /* Service en ligne */
  ai_remote_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_add(GTK_CONTAINER(p), ai_remote_box);
  gtk_container_add(GTK_CONTAINER(ai_remote_box), label("Service en ligne", "h2", 0));
  gtk_container_add(GTK_CONTAINER(ai_remote_box),
                    desc("Pour un PC trop juste, ou des réponses plus poussées : tout service compatible avec l'API OpenAI. "
                         "Vos messages lui sont alors envoyés : choisissez un service de confiance. La clé reste sur ce PC, "
                         "lisible par votre compte seulement."));
  GtkWidget *rg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  ai_url = field("Adresse, ex. https://api.exemple.com/v1", FALSE);
  ai_rmodel = field("Nom du modèle", FALSE);
  ai_key = field("Clé d'accès", TRUE);
  if (c->remote_url) gtk_entry_set_text(GTK_ENTRY(ai_url), c->remote_url);
  if (c->remote_model) gtk_entry_set_text(GTK_ENTRY(ai_rmodel), c->remote_model);
  char *saved = lumina_ai_key_load();
  if (saved && *saved) gtk_entry_set_placeholder_text(GTK_ENTRY(ai_key), "Clé enregistrée (laisser vide pour la garder)");
  g_free(saved);
  gtk_container_add(GTK_CONTAINER(rg), ai_url);
  gtk_container_add(GTK_CONTAINER(rg), ai_rmodel);
  gtk_container_add(GTK_CONTAINER(rg), ai_key);
  GtkWidget *sv = button("Enregistrer", "fbtn", G_CALLBACK(on_ai_remote_save), NULL);
  gtk_widget_set_halign(sv, GTK_ALIGN_END);
  gtk_container_add(GTK_CONTAINER(rg), sv);
  gtk_container_add(GTK_CONTAINER(ai_remote_box), rg);
  lumina_ai_config_free(c);

  /* Contenu affiche d'abord ; ensuite seul le mode choisi rend visible
     son bloc (show_all de la page ne doit pas tout montrer). */
  gtk_widget_show_all(ai_local_box);
  gtk_widget_show_all(ai_remote_box);
  for (guint i = 0; i < ai_models->len; i++) gtk_widget_hide(((AiModel *)ai_models->pdata[i])->del);
  gtk_widget_set_no_show_all(ai_local_box, TRUE);
  gtk_widget_set_no_show_all(ai_remote_box, TRUE);
  ai_show_mode();
  ai_refresh(NULL);
}

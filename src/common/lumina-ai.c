/* Lumi : acces commun a l'IA (voir lumina-ai.h).
 *
 * Le dialogue HTTP passe par curl (deja dans le systeme) : en local sur le
 * socket prive $XDG_RUNTIME_DIR/lumina-ai.sock, demarre au besoin par
 * lumina-ai-serve ; en ligne vers l'adresse choisie, la cle etant lue par
 * curl dans un fichier prive (jamais sur la ligne de commande, visible de
 * tous dans ps). Les reponses sont lues en flux (Server-Sent Events). */
#define _GNU_SOURCE   /* strcasestr */
#include "lumina-ai.h"
#include <string.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */
static char *conf_path(const char *name) {
  return g_build_filename(g_get_user_config_dir(), "lumina", name, NULL);
}

LuminaAiConfig *lumina_ai_config_load(void) {
  LuminaAiConfig *c = g_new0(LuminaAiConfig, 1);
  char *p = conf_path("ai.conf"), *t = NULL;
  if (g_file_get_contents(p, &t, NULL, NULL)) {
    char **l = g_strsplit(t, "\n", -1);
    for (char **i = l; *i; i++) {
      char *eq = strchr(*i, '=');
      if (!eq) continue;
      *eq = 0;
      const char *k = g_strstrip(*i), *v = g_strstrip(eq + 1);
      if (!strcmp(k, "MODE")) c->mode = !strcmp(v, "local") ? LUMINA_AI_LOCAL : !strcmp(v, "remote") ? LUMINA_AI_REMOTE : LUMINA_AI_OFF;
      else if (!strcmp(k, "MODEL")) { g_free(c->model); c->model = g_strdup(v); }
      else if (!strcmp(k, "REMOTE_URL")) { g_free(c->remote_url); c->remote_url = g_strdup(v); }
      else if (!strcmp(k, "REMOTE_MODEL")) { g_free(c->remote_model); c->remote_model = g_strdup(v); }
    }
    g_strfreev(l);
  }
  g_free(t); g_free(p);
  if (!c->model || !*c->model) {
    /* Par defaut : « Equilibre » des 4 Go de memoire (bien plus fiable) */
    char *mi = NULL;
    long kb = 0;
    if (g_file_get_contents("/proc/meminfo", &mi, NULL, NULL) && strstr(mi, "MemTotal:")) kb = atol(strstr(mi, "MemTotal:") + 9);
    g_free(mi);
    g_free(c->model);
    c->model = g_strdup(kb >= 3500L * 1024 ? "equilibre" : "leger");
  }
  return c;
}

gboolean lumina_ai_config_save(const LuminaAiConfig *c) {
  char *d = g_build_filename(g_get_user_config_dir(), "lumina", NULL);
  g_mkdir_with_parents(d, 0700);
  char *p = conf_path("ai.conf");
  char *t = g_strdup_printf("MODE=%s\nMODEL=%s\nREMOTE_URL=%s\nREMOTE_MODEL=%s\n",
                            c->mode == LUMINA_AI_LOCAL ? "local" : c->mode == LUMINA_AI_REMOTE ? "remote" : "off",
                            c->model ? c->model : "leger", c->remote_url ? c->remote_url : "",
                            c->remote_model ? c->remote_model : "");
  gboolean ok = g_file_set_contents(p, t, -1, NULL);
  g_free(t); g_free(p); g_free(d);
  return ok;
}

void lumina_ai_config_free(LuminaAiConfig *c) {
  if (!c) return;
  g_free(c->model); g_free(c->remote_url); g_free(c->remote_model); g_free(c);
}

char *lumina_ai_key_load(void) {
  char *p = conf_path("ai.key"), *t = NULL;
  g_file_get_contents(p, &t, NULL, NULL);
  g_free(p);
  if (t) g_strstrip(t);
  return t;
}

gboolean lumina_ai_key_save(const char *key) {
  char *d = g_build_filename(g_get_user_config_dir(), "lumina", NULL);
  g_mkdir_with_parents(d, 0700);
  char *p = conf_path("ai.key");
  gboolean ok = TRUE;
  if (!key || !*key) g_unlink(p);
  /* Ecriture atomique : disque plein = echec signale, ancienne cle intacte */
  else ok = g_file_set_contents_full(p, key, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
  g_free(p); g_free(d);
  return ok;
}

/* Les services donnent souvent l'adresse complete : on garde la base. */
char *lumina_ai_url_clean(const char *url) {
  char *u = g_strstrip(g_strdup(url ? url : ""));
  for (;;) {
    gsize n = strlen(u);
    if (n && u[n - 1] == '/') u[n - 1] = 0;
    else if (g_str_has_suffix(u, "/chat/completions")) u[n - strlen("/chat/completions")] = 0;
    else return u;
  }
}

char *lumina_ai_model_path(const char *id) {
  if (!id || strchr(id, '/') || strchr(id, '.')) return NULL;
  char *f = g_strdup_printf("/var/lib/lumina/ai/%s.gguf", id);
  if (g_file_test(f, G_FILE_TEST_IS_REGULAR)) return f;
  g_free(f);
  return NULL;
}

gboolean lumina_ai_ready(char **why) {
  LuminaAiConfig *c = lumina_ai_config_load();
  gboolean ok = FALSE;
  const char *w = NULL;
  if (c->mode == LUMINA_AI_OFF) w = "Lumi est désactivé. Activez-le dans Réglages › Lumi.";
  else if (c->mode == LUMINA_AI_LOCAL) {
    char *m = lumina_ai_model_path(c->model);
    if (m) ok = TRUE; else w = "Le modèle de Lumi n'est pas encore téléchargé (Réglages › Lumi).";
    g_free(m);
  } else {
    char *k = lumina_ai_key_load();
    if (c->remote_url && *c->remote_url && k && *k) ok = TRUE;
    else w = "Le service en ligne n'est pas configuré (Réglages › Lumi).";
    g_free(k);
  }
  if (why) *why = w ? g_strdup(w) : NULL;
  lumina_ai_config_free(c);
  return ok;
}

/* ------------------------------------------------------------------ */
/* JSON minimal                                                        */
/* ------------------------------------------------------------------ */
char *lumina_ai_json_escape(const char *s) {
  GString *o = g_string_new("\"");
  for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
    switch (*p) {
      case '"': g_string_append(o, "\\\""); break;
      case '\\': g_string_append(o, "\\\\"); break;
      case '\n': g_string_append(o, "\\n"); break;
      case '\r': g_string_append(o, "\\r"); break;
      case '\t': g_string_append(o, "\\t"); break;
      default:
        if (*p < 0x20) g_string_append_printf(o, "\\u%04x", *p);
        else g_string_append_c(o, *p);
    }
  }
  g_string_append_c(o, '"');
  return g_string_free(o, FALSE);
}

/* Premiere valeur chaine de "key" dans json (NULL si absente ou null). */
char *lumina_ai_json_string_at(const char *json, const char *key) {
  char *pat = g_strdup_printf("\"%s\"", key);
  const char *p = json;
  char *res = NULL;
  while ((p = strstr(p, pat))) {
    const char *q = p + strlen(pat);
    while (*q == ' ') q++;
    if (*q != ':') { p += strlen(pat); continue; }
    q++;
    while (*q == ' ') q++;
    if (*q != '"') break;   /* null, nombre... */
    q++;
    GString *o = g_string_new(NULL);
    while (*q && *q != '"') {
      if (*q != '\\') { g_string_append_c(o, *q++); continue; }
      q++;
      switch (*q) {
        case 'n': g_string_append_c(o, '\n'); q++; break;
        case 't': g_string_append_c(o, '\t'); q++; break;
        case 'r': q++; break;
        case 'b': case 'f': q++; break;
        case 'u': {
          char h[5] = {0};
          strncpy(h, q + 1, 4);
          gunichar u = (gunichar)strtoul(h, NULL, 16);
          q += strlen(h) + 1;
          if (u >= 0xD800 && u <= 0xDBFF && q[0] == '\\' && q[1] == 'u') {
            char l[5] = {0};
            strncpy(l, q + 2, 4);
            gunichar lo = (gunichar)strtoul(l, NULL, 16);
            if (lo >= 0xDC00 && lo <= 0xDFFF) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); q += 6; }
          }
          if (g_unichar_validate(u) && u) g_string_append_unichar(o, u);
          break;
        }
        case 0: break;
        default: g_string_append_c(o, *q++);
      }
    }
    res = g_string_free(o, FALSE);
    break;
  }
  g_free(pat);
  return res;
}

/* ------------------------------------------------------------------ */
/* Requete                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
  LuminaAiProgress on_progress;
  LuminaAiPiece on_piece;
  LuminaAiDone on_done;
  gpointer d;
  GCancellable *cancel;
  LuminaAiConfig *conf;
  char *body;
  GSubprocess *proc;
  GDataInputStream *in;
  GString *raw;          /* lignes hors flux (message d'erreur eventuel) */
  gboolean got, finished;
  gulong cancel_id;
} Req;

static void req_finish(Req *r, const char *err) {
  if (r->finished) return;
  r->finished = TRUE;
  if (r->cancel_id) g_cancellable_disconnect(r->cancel, r->cancel_id);
  if (!g_cancellable_is_cancelled(r->cancel)) r->on_done(err, r->d);
  if (r->proc) g_object_unref(r->proc);
  if (r->in) g_object_unref(r->in);
  g_string_free(r->raw, TRUE);
  g_free(r->body);
  lumina_ai_config_free(r->conf);
  g_object_unref(r->cancel);
  g_free(r);
}

static void on_cancel(GCancellable *c, gpointer d) {
  Req *r = d;
  if (r->proc) g_subprocess_force_exit(r->proc);
}

/* Premier nombre de "key" dans json (-1 si absent). */
static double json_number_at(const char *json, const char *key) {
  char *pat = g_strdup_printf("\"%s\"", key);
  const char *p = strstr(json, pat);
  double v = -1;
  if (p) {
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (g_ascii_isdigit(*p)) v = g_ascii_strtod(p, NULL);
  }
  g_free(pat);
  return v;
}

static void on_curl_exit(GObject *o, GAsyncResult *res, gpointer d) {
  Req *r = d;
  g_subprocess_wait_finish(G_SUBPROCESS(o), res, NULL);
  /* curl : 28 = plus aucun signe de vie du moteur (ou duree maximale) */
  gboolean timeout = g_subprocess_get_if_exited(G_SUBPROCESS(o)) && g_subprocess_get_exit_status(G_SUBPROCESS(o)) == 28;
  if (r->conf->mode == LUMINA_AI_REMOTE) {
    /* La cle ne reste pas sur le disque au-dela de la requete */
    char *h = g_build_filename(g_get_user_runtime_dir(), "lumina-ai-auth", NULL);
    g_unlink(h);
    g_free(h);
  }
  if (r->got) { req_finish(r, NULL); return; }
  char *msg = lumina_ai_json_string_at(r->raw->str, "message");
  char *err;
  if (msg && strstr(msg, "context size"))
    err = g_strdup("Ce texte est trop long pour Lumi. Sélectionnez un passage plus court, ou commencez une nouvelle conversation.");
  else if (msg && r->conf->mode == LUMINA_AI_REMOTE && (strcasestr(msg, "api key") || strcasestr(msg, "unauthorized")))
    err = g_strdup("Le service refuse la clé d'accès. Vérifiez-la (Réglages › Lumi).");
  else if (msg && r->conf->mode == LUMINA_AI_REMOTE && strcasestr(msg, "model") &&
           (strcasestr(msg, "does not exist") || strcasestr(msg, "not found")))
    err = g_strdup_printf("Le service ne connaît pas le modèle « %s ». Vérifiez son nom (Réglages › Lumi).", r->conf->remote_model);
  else if (msg) err = g_strdup_printf("Lumi n'a pas pu répondre : %s", msg);
  else if (timeout && r->conf->mode == LUMINA_AI_LOCAL)
    err = g_strdup_printf("Lumi a mis trop de temps à répondre : ce PC est peut-être trop lent pour %s. %s",
                          g_strcmp0(r->conf->model, "leger") ? "ce modèle ou pour un texte aussi long" : "un texte aussi long",
                          g_strcmp0(r->conf->model, "leger") ? "Essayez avec un passage plus court, ou le modèle « Léger » (Réglages › Lumi)."
                                                             : "Essayez avec un passage plus court.");
  else if (r->conf->mode == LUMINA_AI_REMOTE)
    err = g_strdup("Le service en ligne ne répond pas. Vérifiez la connexion, l'adresse et la clé (Réglages › Lumi).");
  else err = g_strdup("Lumi n'a pas pu répondre. Réessayez dans un instant.");
  req_finish(r, err);
  g_free(msg); g_free(err);
}

static void on_line(GObject *o, GAsyncResult *res, gpointer d) {
  Req *r = d;
  gsize n = 0;
  char *line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(o), res, &n, NULL);
  if (!line) { g_subprocess_wait_async(r->proc, NULL, on_curl_exit, r); return; }
  if (g_str_has_prefix(line, "data:")) {
    const char *j = line + 5;
    while (*j == ' ') j++;
    if (strcmp(j, "[DONE]") && strstr(j, "\"prompt_progress\"")) {
      /* Lecture de la demande en cours (llama.cpp, return_progress) */
      double total = json_number_at(j, "total"), done = json_number_at(j, "processed");
      if (r->on_progress && total > 0 && done >= 0 && !g_cancellable_is_cancelled(r->cancel))
        r->on_progress(CLAMP(done / total, 0.0, 1.0), r->d);
    } else if (strcmp(j, "[DONE]")) {
      char *piece = lumina_ai_json_string_at(j, "content");
      if (piece && *piece && !g_cancellable_is_cancelled(r->cancel)) { r->got = TRUE; r->on_piece(piece, r->d); }
      g_free(piece);
      if (!r->got && strstr(j, "\"error\"")) g_string_append(r->raw, j);
    }
  } else if (r->raw->len < 4096) g_string_append(r->raw, line);
  g_free(line);
  g_data_input_stream_read_line_async(r->in, G_PRIORITY_DEFAULT, NULL, on_line, r);
}

static void start_curl(Req *r) {
  GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
  g_ptr_array_add(a, g_strdup("curl"));
  g_ptr_array_add(a, g_strdup("-sS"));
  g_ptr_array_add(a, g_strdup("-N"));
  /* Pas de duree fixe (un vieux PC peut lire longtemps un long texte) :
     abandon seulement apres 5 min sans aucune donnee, et au plus 30 min. */
  g_ptr_array_add(a, g_strdup("--speed-limit"));
  g_ptr_array_add(a, g_strdup("1"));
  g_ptr_array_add(a, g_strdup("--speed-time"));
  g_ptr_array_add(a, g_strdup("300"));
  g_ptr_array_add(a, g_strdup("--max-time"));
  g_ptr_array_add(a, g_strdup("1800"));
  g_ptr_array_add(a, g_strdup("-H"));
  g_ptr_array_add(a, g_strdup("Content-Type: application/json"));
  g_ptr_array_add(a, g_strdup("--data-binary"));
  g_ptr_array_add(a, g_strdup("@-"));
  if (r->conf->mode == LUMINA_AI_LOCAL) {
    g_ptr_array_add(a, g_strdup("--unix-socket"));
    g_ptr_array_add(a, g_build_filename(g_get_user_runtime_dir(), "lumina-ai.sock", NULL));
    g_ptr_array_add(a, g_strdup("http://localhost/v1/chat/completions"));
  } else {
    /* En-tete d'autorisation dans un fichier prive, lu par curl */
    char *h = g_build_filename(g_get_user_runtime_dir(), "lumina-ai-auth", NULL);
    char *k = lumina_ai_key_load();
    char *line = g_strdup_printf("Authorization: Bearer %s\n", k ? k : "");
    int fd = g_open(h, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) { fchmod(fd, 0600); if (write(fd, line, strlen(line)) < 0) {} close(fd); }
    g_ptr_array_add(a, g_strdup("-H"));
    g_ptr_array_add(a, g_strdup_printf("@%s", h));
    char *base = lumina_ai_url_clean(r->conf->remote_url);
    g_ptr_array_add(a, g_strdup("--proto"));
    g_ptr_array_add(a, g_strdup("=https,http"));
    g_ptr_array_add(a, g_strdup_printf("%s/chat/completions", base));
    g_free(base); g_free(line); g_free(k); g_free(h);
  }
  g_ptr_array_add(a, NULL);
  GError *e = NULL;
  r->proc = g_subprocess_newv((const char *const *)a->pdata,
                              G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE, &e);
  g_ptr_array_free(a, TRUE);
  if (!r->proc) { req_finish(r, "Lumi n'a pas pu démarrer (curl absent)."); g_clear_error(&e); return; }
  GOutputStream *out = g_subprocess_get_stdin_pipe(r->proc);
  g_output_stream_write_all(out, r->body, strlen(r->body), NULL, NULL, NULL);
  g_output_stream_close(out, NULL, NULL);
  r->in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(r->proc));
  g_data_input_stream_set_newline_type(r->in, G_DATA_STREAM_NEWLINE_TYPE_ANY);
  g_data_input_stream_read_line_async(r->in, G_PRIORITY_DEFAULT, NULL, on_line, r);
}

static void on_serve_exit(GObject *o, GAsyncResult *res, gpointer d) {
  Req *r = d;
  char *err = NULL;
  g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), res, NULL, &err, NULL);
  gboolean ok = g_subprocess_get_if_exited(G_SUBPROCESS(o)) && g_subprocess_get_exit_status(G_SUBPROCESS(o)) == 0;
  g_object_unref(o);
  if (g_cancellable_is_cancelled(r->cancel)) { req_finish(r, NULL); g_free(err); return; }
  if (!ok) {
    char *m = err && *g_strstrip(err) ? g_strdup(err) : g_strdup("Lumi n'a pas pu démarrer.");
    req_finish(r, m);
    g_free(m); g_free(err);
    return;
  }
  g_free(err);
  start_curl(r);
}

void lumina_ai_chat(const char *const *messages, LuminaAiPiece on_piece, LuminaAiDone on_done,
                    gpointer d, GCancellable *cancel) {
  lumina_ai_chat_progress(messages, NULL, on_piece, on_done, d, cancel);
}

void lumina_ai_chat_progress(const char *const *messages, LuminaAiProgress on_progress, LuminaAiPiece on_piece,
                             LuminaAiDone on_done, gpointer d, GCancellable *cancel) {
  Req *r = g_new0(Req, 1);
  r->on_progress = on_progress;
  r->on_piece = on_piece; r->on_done = on_done; r->d = d;
  r->cancel = cancel ? g_object_ref(cancel) : g_cancellable_new();
  r->conf = lumina_ai_config_load();
  r->raw = g_string_new(NULL);
  char *why = NULL;
  if (!lumina_ai_ready(&why)) { req_finish(r, why); g_free(why); return; }

  GString *b = g_string_new("{");
  char *model = lumina_ai_json_escape(r->conf->mode == LUMINA_AI_REMOTE && r->conf->remote_model && *r->conf->remote_model
                                      ? r->conf->remote_model : "lumi");
  g_string_append_printf(b, "\"model\":%s,\"stream\":true,\"temperature\":0.5,%s\"messages\":[", model,
                         /* progression de la lecture : moteur local seulement */
                         r->conf->mode == LUMINA_AI_LOCAL ? "\"return_progress\":true," : "");
  g_free(model);
  for (int i = 0; messages[i] && messages[i + 1]; i += 2) {
    char *role = lumina_ai_json_escape(messages[i]), *content = lumina_ai_json_escape(messages[i + 1]);
    g_string_append_printf(b, "%s{\"role\":%s,\"content\":%s}", i ? "," : "", role, content);
    g_free(role); g_free(content);
  }
  g_string_append(b, "]}");
  r->body = g_string_free(b, FALSE);
  r->cancel_id = g_cancellable_connect(r->cancel, G_CALLBACK(on_cancel), r, NULL);

  if (r->conf->mode == LUMINA_AI_LOCAL) {
    /* Demarre le serveur local au besoin (chargement du modele : quelques
       secondes), puis envoie la requete. */
    GError *e = NULL;
    GSubprocess *s = g_subprocess_new(G_SUBPROCESS_FLAGS_STDERR_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &e,
                                      "lumina-ai-serve", NULL);
    if (!s) { req_finish(r, "Lumi n'a pas pu démarrer."); g_clear_error(&e); return; }
    g_subprocess_communicate_utf8_async(s, NULL, NULL, on_serve_exit, r);
  } else start_curl(r);
}

void lumina_ai_ask(const char *system, const char *user, LuminaAiPiece on_piece, LuminaAiDone on_done,
                   gpointer d, GCancellable *cancel) {
  const char *m[] = {"system", system, "user", user, NULL, NULL};
  lumina_ai_chat(m, on_piece, on_done, d, cancel);
}

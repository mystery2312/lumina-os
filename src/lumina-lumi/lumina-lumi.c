/*
 * lumina-lumi : Lumi, l'assistant de LuminaOS (Windows + Espace).
 *
 * Une conversation simple, en francais. Par defaut le modele tourne sur ce
 * PC (rien ne part sur Internet) ; un service en ligne peut etre choisi
 * dans Reglages > Lumi. Les reponses s'affichent au fil de l'eau.
 *
 *   lumina-lumi ["question"]   ouvre Lumi, et pose la question s'il y en a une
 *   lumina-lumi --context F    Lumi sur une page web ou un passage (fichier
 *                              JSON prive envoye par le module du navigateur,
 *                              efface des sa lecture)
 *   lumina-lumi --write        aide a l'ecriture dans n'importe quelle appli
 *   lumina-lumi --file F       Lumi sur un fichier (texte, PDF, bureautique)
 *   lumina-lumi --screen       Lumi sur une zone de l'ecran (lecture par OCR)
 *   lumina-lumi chrome-extension://...   hote de messagerie native (Chromium)
 */
#include "lumina-ui.h"
#include "lumina-ai.h"
#include "lumi.h"
#include <string.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-lumi.csd { border-radius: 0 0 16px 16px; background: @surface; }\n"
  "window.lumina-lumi.csd decoration { border-radius: 16px; box-shadow: 0 6px 22px alpha(black, 0.18), 0 0 0 1px alpha(black, 0.06); }\n"
  ".header { background: @surface; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 32px; min-height: 32px; color: @on_var; }\n"
  ".header button.titlebutton:hover, .tbtn:hover { background: alpha(@on, 0.08); }\n"
  ".apptitle { font-size: 15px; color: @on; padding-left: 4px; }\n"
  ".tbtn { background: transparent; border-radius: 16px; padding: 6px; }\n"
  ".chat { padding: 8px 18px 16px 18px; }\n"
  ".me { background: @pri_c; color: @on_pri_c; border-radius: 20px 20px 6px 20px; padding: 10px 14px; margin-left: 60px; }\n"
  ".lumi { background: @c_low; color: @on; border-radius: 20px 20px 20px 6px; padding: 10px 14px; margin-right: 40px; }\n"
  ".lumi.err { background: @ter_c; color: @on_ter_c; }\n"
  ".msg { font-size: 14px; }\n"
  ".hello { font-size: 24px; color: @on; }\n"
  ".sub { font-size: 13.5px; color: @on_var; }\n"
  ".chip { background: @c_low; color: @on; border-radius: 18px; padding: 8px 14px; font-size: 13px; }\n"
  ".chip:hover { background: @c_high; }\n"
  ".fbtn { background: @pri; color: @on_pri; border-radius: 20px; padding: 8px 20px; font-weight: 500; }\n"
  ".composer { background: @c_high; border-radius: 26px; padding: 4px 6px 4px 18px; margin: 4px 14px 6px 14px; }\n"
  ".composer entry { background: transparent; border: none; box-shadow: none; color: @on; font-size: 14px; min-height: 40px; caret-color: @pri; }\n"
  ".send { background: @pri; color: @on_pri; border-radius: 20px; min-width: 40px; min-height: 40px; padding: 0; }\n"
  ".send:disabled { background: alpha(@on, 0.12); }\n"
  ".foot { color: @on_var; font-size: 11.5px; margin: 0 0 10px 0; }\n"
  ".ctx { background: @c_low; border-radius: 16px; padding: 10px 14px; margin: 4px 14px 0 14px; }\n"
  ".ctxtitle { font-size: 13.5px; color: @on; font-weight: 500; }\n"
  ".ctxsub { font-size: 12px; color: @on_var; }\n"
  ".cbtn { background: transparent; border-radius: 18px; min-width: 36px; min-height: 36px; padding: 0; }\n"
  ".cbtn:hover { background: alpha(@on, 0.08); }\n"
  ".cbtn:checked { background: @pri_c; }\n"
  ".cbtn.rec { background: @ter_c; }\n"
  ".src { background: @c_low; color: @on; border-radius: 14px; padding: 6px 12px; font-size: 12.5px; }\n"
  ".src:hover { background: @c_high; }\n";

static const char *SYSTEM_PROMPT =
  "Tu es Lumi, l'assistant intégré à LuminaOS, un système d'exploitation léger. "
  "Réponds en français, de façon claire, utile et concise, sauf si on te demande une autre langue. "
  "Si tu ne sais pas, dis-le simplement plutôt que d'inventer.";

/* Contexte : page web ou passage envoye par le navigateur */
static char *ctx_prompt, *ctx_title, *ctx_host, *ctx_action;
static gboolean ctx_selection, ctx_cut;
/* Sujet : cette page (defaut), ce fichier, cette zone de l'ecran */
static const char *ctx_noun = "cette page", *ctx_read = "la page", *ctx_fail = "Cette page n'a pas pu être lue (page protégée ou vide).";
static GtkWidget *ctx_sub_l;
static char *screen_png;            /* capture en attente de lecture */
/* Question sur les fichiers de l'utilisateur */
static GtkWidget *files_b;
static GPtrArray *sources;          /* fichiers cites par la reponse en cours */

static GtkWidget *win, *chat, *scroll, *entry, *send_b, *empty_box, *foot_l;
static Icon *send_ic;
static GPtrArray *history;          /* role, contenu, role, contenu... */
static GCancellable *busy;          /* non NULL : reponse en cours */
static GString *answer;
static GtkWidget *answer_l;

/* ------------------------------------------------------------------ */
/* Mise en forme legere (gras, code, titres, listes) en balisage Pango  */
/* ------------------------------------------------------------------ */
static char *md_to_markup(const char *md) {
  char *esc = g_markup_escape_text(md, -1);
  GString *o = g_string_new(NULL);
  char **lines = g_strsplit(esc, "\n", -1);
  for (char **l = lines; *l; l++) {
    const char *t = *l;
    gboolean heading = FALSE;
    if (t[0] == '#') { while (*t == '#') t++; while (*t == ' ') t++; heading = TRUE; }
    if ((t[0] == '-' || t[0] == '*') && t[1] == ' ') { g_string_append(o, "  • "); t += 2; }
    if (heading) g_string_append(o, "<b>");
    gboolean bold = FALSE, code = FALSE;
    for (const char *p = t; *p; p++) {
      if (p[0] == '*' && p[1] == '*') { g_string_append(o, bold ? "</b>" : "<b>"); bold = !bold; p++; }
      else if (p[0] == '`') { g_string_append(o, code ? "</tt>" : "<tt>"); code = !code; }
      else g_string_append_c(o, *p);
    }
    if (code) g_string_append(o, "</tt>");
    if (bold) g_string_append(o, "</b>");
    if (heading) g_string_append(o, "</b>");
    if (l[1]) g_string_append_c(o, '\n');
  }
  g_strfreev(lines); g_free(esc);
  return g_string_free(o, FALSE);
}

static void set_msg(GtkWidget *l, const char *text) {
  char *m = md_to_markup(text);
  /* Balisage invalide (texte en cours d'arrivee) : texte brut */
  if (pango_parse_markup(m, -1, 0, NULL, NULL, NULL, NULL)) gtk_label_set_markup(GTK_LABEL(l), m);
  else gtk_label_set_text(GTK_LABEL(l), text);
  g_free(m);
}

static gboolean scroll_end(gpointer d) {
  GtkAdjustment *a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
  gtk_adjustment_set_value(a, gtk_adjustment_get_upper(a));
  return G_SOURCE_REMOVE;
}

static GtkWidget *bubble(const char *text, gboolean mine) {
  GtkWidget *l = gtk_label_new(NULL);
  add_class(l, "msg");
  add_class(l, mine ? "me" : "lumi");
  gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
  gtk_label_set_line_wrap_mode(GTK_LABEL(l), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_selectable(GTK_LABEL(l), TRUE);
  gtk_widget_set_can_focus(l, FALSE);
  gtk_label_set_xalign(GTK_LABEL(l), 0);
  gtk_widget_set_halign(l, mine ? GTK_ALIGN_END : GTK_ALIGN_START);
  if (mine) gtk_label_set_text(GTK_LABEL(l), text); else set_msg(l, text);
  gtk_container_add(GTK_CONTAINER(chat), l);
  gtk_widget_show(l);
  g_idle_add(scroll_end, NULL);
  return l;
}

/* ------------------------------------------------------------------ */
/* Envoi et reception                                                   */
/* ------------------------------------------------------------------ */
static void update_send(void) {
  icon_set(send_ic, busy ? "stop" : "send", R_ON_PRI);
  gtk_widget_set_tooltip_text(send_b, busy ? "Arrêter" : "Envoyer (Entrée)");
  gtk_widget_set_sensitive(send_b, busy || *gtk_entry_get_text(GTK_ENTRY(entry)));
}

static void ask(const char *q);

static void on_piece(const char *piece, gpointer d) {
  if (!answer_l) return;
  g_string_append(answer, piece);
  set_msg(answer_l, answer->str);
  g_idle_add(scroll_end, NULL);
}

static void on_progress(double f, gpointer d) {
  if (!answer_l || answer->len) return;
  char *t = g_strdup_printf("Lumi lit %s… %d %%", ctx_prompt ? ctx_read : sources && sources->len ? "vos fichiers" : "votre demande", (int)(f * 100));
  gtk_label_set_text(GTK_LABEL(answer_l), t);
  g_free(t);
}

static void on_source(GtkButton *b, gpointer path) {
  char *uri = g_filename_to_uri(path, NULL, NULL);
  if (uri) g_app_info_launch_default_for_uri(uri, NULL, NULL);
  g_free(uri);
}

/* Sous la reponse : les fichiers d'ou elle vient, a ouvrir d'un clic. */
static void show_sources(void) {
  if (!sources || !sources->len) return;
  GtkWidget *fb = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(fb), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fb), 2);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(fb), 6);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(fb), 6);
  gtk_widget_set_halign(fb, GTK_ALIGN_START);
  for (guint i = 0; i < sources->len; i++) {
    char *base = g_path_get_basename(sources->pdata[i]);
    GtkWidget *b = gtk_button_new(), *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add_class(b, "src");
    gtk_container_add(GTK_CONTAINER(h), icon_new("docs", R_PRI, 16)->img);
    GtkWidget *l = gtk_label_new(base);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 26);
    gtk_container_add(GTK_CONTAINER(h), l);
    gtk_container_add(GTK_CONTAINER(b), h);
    gtk_widget_set_tooltip_text(b, sources->pdata[i]);
    g_signal_connect_data(b, "clicked", G_CALLBACK(on_source), g_strdup(sources->pdata[i]), (GClosureNotify)(void (*)(void))g_free, 0);
    gtk_container_add(GTK_CONTAINER(fb), b);
    g_free(base);
  }
  gtk_container_add(GTK_CONTAINER(chat), fb);
  gtk_widget_show_all(fb);
  g_idle_add(scroll_end, NULL);
}

/* ------------------------------------------------------------------ */
/* Voix : micro -> whisper.cpp -> question ; reponse -> Piper -> son     */
/* Tout tourne sur ce PC. Les modeles viennent de Reglages > Lumi.       */
/* ------------------------------------------------------------------ */
#define VOICE_DIR  "/var/lib/lumina/ai/voice"
#define ENGINE_DIR "/usr/lib/lumina-ai"
static GtkWidget *mic_b;
static GSubprocess *rec_sp, *say_sp;
static char *rec_wav;
static guint rec_limit;
static gboolean spoken, transcribing;

static gboolean voice_ready(void) {
  return g_file_test(VOICE_DIR "/.ready", G_FILE_TEST_EXISTS) && g_file_test(ENGINE_DIR "/whisper/whisper-cli", G_FILE_TEST_IS_EXECUTABLE);
}

static void say_stop(void) {
  if (!say_sp) return;
  const char *pid = g_subprocess_get_identifier(say_sp);
  if (pid) kill(-(pid_t)atoi(pid), SIGTERM);       /* piper et le lecteur */
  g_clear_object(&say_sp);
}

static void say_done(GObject *o, GAsyncResult *res, gpointer d) {
  g_subprocess_communicate_finish(G_SUBPROCESS(o), res, NULL, NULL, NULL);
  if (say_sp == G_SUBPROCESS(o)) g_clear_object(&say_sp);
  g_object_unref(o);
}

static void new_group(gpointer d) { setsid(); }

/* Lit la reponse a voix haute (sans mise en forme, 1500 caracteres au plus). */
static void say_start(const char *text) {
  say_stop();
  GString *t = g_string_new(NULL);
  for (const char *p = text; *p; p++) if (!strchr("*#`_", *p)) g_string_append_c(t, *p);
  if (g_utf8_strlen(t->str, -1) > 1500) g_string_truncate(t, g_utf8_offset_to_pointer(t->str, 1500) - t->str);
  GSubprocessLauncher *l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  g_subprocess_launcher_set_child_setup(l, new_group, NULL, NULL);
  g_subprocess_launcher_setenv(l, "LD_LIBRARY_PATH", ENGINE_DIR "/piper", TRUE);
  GSubprocess *p = g_subprocess_launcher_spawn(l, NULL, "sh", "-c",
    ENGINE_DIR "/piper/piper --model " VOICE_DIR "/fr.onnx --espeak_data " ENGINE_DIR "/piper/espeak-ng-data --output-raw "
    "| env -u LD_LIBRARY_PATH pw-play --raw --rate 22050 --channels 1 --format s16 -", NULL);
  g_object_unref(l);
  if (p) {
    say_sp = g_object_ref(p);
    GBytes *b = g_bytes_new(t->str, t->len);
    g_subprocess_communicate_async(p, b, NULL, say_done, NULL);
    g_bytes_unref(b);
  }
  g_string_free(t, TRUE);
}

static void mic_state(const char *placeholder, gboolean recording) {
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
  GtkStyleContext *c = gtk_widget_get_style_context(mic_b);
  if (recording) gtk_style_context_add_class(c, "rec"); else gtk_style_context_remove_class(c, "rec");
  gtk_widget_set_tooltip_text(mic_b, recording ? "Terminer" : "Parler à Lumi");
}

static const char *idle_placeholder(void) {
  return ctx_title ? "Poser une question" : "Demander à Lumi";
}

static void trans_done(GObject *o, GAsyncResult *res, gpointer d) {
  char *out = NULL;
  g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), res, &out, NULL, NULL);
  g_object_unref(o);
  transcribing = FALSE;
  g_unlink(rec_wav);
  g_clear_pointer(&rec_wav, g_free);
  /* Retire les annotations du modele : [BLANK_AUDIO], (musique)... */
  GString *t = g_string_new(NULL);
  int depth = 0;
  for (const char *p = out ? out : ""; *p; p++) {
    if (*p == '[' || *p == '(') depth++;
    else if ((*p == ']' || *p == ')') && depth) depth--;
    else if (!depth) g_string_append_c(t, *p == '\n' ? ' ' : *p);
  }
  g_free(out);
  g_strstrip(t->str);
  gtk_widget_set_sensitive(mic_b, TRUE);
  if (*t->str) {
    mic_state(idle_placeholder(), FALSE);
    spoken = TRUE;
    ask(t->str);
  } else mic_state("Je n'ai rien entendu. Réessayez.", FALSE);
  g_string_free(t, TRUE);
}

static void rec_done(GObject *o, GAsyncResult *res, gpointer d) {
  g_subprocess_wait_finish(G_SUBPROCESS(o), res, NULL);
  g_object_unref(o);
  g_clear_object(&rec_sp);
  if (rec_limit) { g_source_remove(rec_limit); rec_limit = 0; }
  GStatBuf st;
  /* Moins d'un quart de seconde a 32 ko/s : rien d'enregistre */
  if (g_stat(rec_wav, &st) != 0 || st.st_size < 8000) {
    g_unlink(rec_wav);
    g_clear_pointer(&rec_wav, g_free);
    mic_state("Micro indisponible, ou rien d'enregistré.", FALSE);
    return;
  }
  transcribing = TRUE;
  gtk_widget_set_sensitive(mic_b, FALSE);
  mic_state("Lumi écoute ce que vous avez dit…", FALSE);
  char *th = g_strdup_printf("%d", CLAMP(g_get_num_processors(), 1, 4));
  GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                                    ENGINE_DIR "/whisper/whisper-cli", "-m", VOICE_DIR "/whisper-fr.bin", "-l", "fr",
                                    "-nt", "-np", "-t", th, "-f", rec_wav, NULL);
  g_free(th);
  if (p) g_subprocess_communicate_utf8_async(p, NULL, NULL, trans_done, NULL);
  else {
    transcribing = FALSE;
    g_unlink(rec_wav);
    g_clear_pointer(&rec_wav, g_free);
    gtk_widget_set_sensitive(mic_b, TRUE);
    mic_state("La reconnaissance vocale n'a pas pu démarrer.", FALSE);
  }
}

static void rec_stop(void) { if (rec_sp) g_subprocess_send_signal(rec_sp, SIGINT); }
static gboolean rec_timeout(gpointer d) { rec_limit = 0; rec_stop(); return G_SOURCE_REMOVE; }

static void on_mic(GtkButton *b, gpointer d) {
  say_stop();
  if (transcribing) return;
  if (rec_sp) { rec_stop(); return; }
  char *wav = g_build_filename(g_get_user_runtime_dir(), "lumi-voice-XXXXXX.wav", NULL);
  int fd = g_mkstemp_full(wav, O_WRONLY, 0600);
  if (fd < 0) { g_free(wav); return; }
  close(fd);
  rec_wav = wav;
  rec_sp = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                            "pw-record", "--rate", "16000", "--channels", "1", "--format", "s16", rec_wav, NULL);
  if (!rec_sp) {
    g_unlink(rec_wav); g_clear_pointer(&rec_wav, g_free);
    mic_state("Micro indisponible.", FALSE);
    return;
  }
  g_subprocess_wait_async(rec_sp, NULL, rec_done, g_object_ref(rec_sp));
  rec_limit = g_timeout_add_seconds(60, rec_timeout, NULL);
  mic_state("Je vous écoute… Cliquez sur le micro quand vous avez fini.", TRUE);
}

static void on_done(const char *err, gpointer d) {
  if (err) {
    if (!answer->len) { set_msg(answer_l, err); add_class(answer_l, "err"); }
    /* question sans reponse : retiree de l'historique */
    if (history->len >= 2) g_ptr_array_remove_range(history, history->len - 2, 2);
  } else {
    g_ptr_array_add(history, g_strdup("assistant"));
    g_ptr_array_add(history, g_strdup(answer->str));
    show_sources();
    if (spoken && answer->len) say_start(answer->str);
  }
  spoken = FALSE;
  g_ptr_array_set_size(sources, 0);
  g_clear_object(&busy);
  answer_l = NULL;
  update_send();
  gtk_widget_grab_focus(entry);
}

/* Caracteres de contexte qu'accepte le modele en service. */
static glong ctx_budget(glong local, glong remote) {
  LuminaAiConfig *c = lumina_ai_config_load();
  glong n = c->mode == LUMINA_AI_REMOTE ? remote : local;
  lumina_ai_config_free(c);
  return n;
}

/* La question porte-t-elle sur les fichiers ? Bouton « Vos fichiers » ou
   mention explicite (« dans mes documents »...). */
static gboolean about_files(const char *q) {
  if (ctx_prompt) return FALSE;
  if (files_b && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(files_b))) return TRUE;
  char *low = g_utf8_strdown(q, -1);
  gboolean r = strstr(low, "mes fichiers") || strstr(low, "mes documents") || strstr(low, "mes notes") || strstr(low, "mes pdf");
  g_free(low);
  return r;
}

/* Consigne garnie des passages trouves ; remplit `sources`. NULL : rien. */
static char *files_prompt(const char *q) {
  char *budget = g_strdup_printf("%ld", ctx_budget(3000, 16000)), *out = NULL;
  const char *argv[] = {"lumina-index", "context", q, budget, NULL};
  g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, NULL, NULL);
  g_free(budget);
  if (!out || !g_str_has_prefix(out, "@@ ") || !g_utf8_validate(out, -1, NULL)) { g_free(out); return NULL; }
  GString *p = g_string_new(SYSTEM_PROMPT);
  g_string_append(p, "\n\nVoici des extraits des fichiers personnels de l'utilisateur, trouvés d'après sa question :\n");
  char **lines = g_strsplit(out, "\n", -1);
  for (char **l = lines; *l; l++) {
    if (g_str_has_prefix(*l, "@@ ")) {
      g_ptr_array_add(sources, g_strdup(*l + 3));
      char *base = g_path_get_basename(*l + 3);
      g_string_append_printf(p, "\n<<< Fichier « %s » :\n", base);
      g_free(base);
    } else { g_string_append(p, *l); g_string_append_c(p, '\n'); }
  }
  g_strfreev(lines); g_free(out);
  g_string_append(p, "\nRéponds en t'appuyant seulement sur ces extraits et cite le nom du fichier utilisé. "
                     "S'ils ne contiennent pas la réponse, dis-le clairement, sans rien inventer.");
  return g_string_free(p, FALSE);
}

static void ask(const char *q) {
  if (busy || !q || !*g_strstrip((char *)q)) return;
  gtk_widget_hide(empty_box);
  bubble(q, TRUE);
  char *fp = NULL;
  if (about_files(q)) {
    fp = files_prompt(q);
    if (!fp) {
      bubble("Je n'ai rien trouvé à ce sujet dans vos fichiers. Les fichiers récents sont lus en tâche de fond : ils peuvent mettre quelques minutes à arriver.", FALSE);
      return;
    }
  }
  g_ptr_array_add(history, g_strdup("user"));
  g_ptr_array_add(history, g_strdup(q));
  /* Petit modele, petit contexte : consigne + 10 derniers messages */
  GPtrArray *m = g_ptr_array_new();
  g_ptr_array_add(m, "system");
  g_ptr_array_add(m, fp ? fp : ctx_prompt ? ctx_prompt : (gpointer)SYSTEM_PROMPT);
  guint from = history->len > 20 ? history->len - 20 : 0;
  for (guint i = from; i < history->len; i++) g_ptr_array_add(m, history->pdata[i]);
  g_ptr_array_add(m, NULL);
  g_ptr_array_add(m, NULL);
  g_string_truncate(answer, 0);
  answer_l = bubble("…", FALSE);
  busy = g_cancellable_new();
  update_send();
  lumina_ai_chat_progress((const char *const *)m->pdata, on_progress, on_piece, on_done, NULL, busy);
  g_ptr_array_free(m, TRUE);
  g_free(fp);
}

static void on_send(GtkWidget *w, gpointer d) {
  if (busy) { g_cancellable_cancel(busy); on_done(NULL, NULL); return; }
  char *q = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
  gtk_entry_set_text(GTK_ENTRY(entry), "");
  ask(q);
  g_free(q);
}

static void on_changed(GtkEditable *e, gpointer d) { update_send(); }

static void on_chip(GtkButton *b, gpointer d) {
  gtk_entry_set_text(GTK_ENTRY(entry), d);
  gtk_widget_grab_focus(entry);
  gtk_editable_set_position(GTK_EDITABLE(entry), -1);
}

static void on_new(GtkButton *b, gpointer d) {
  say_stop();
  if (busy) { g_cancellable_cancel(busy); on_done(NULL, NULL); }
  GList *k = gtk_container_get_children(GTK_CONTAINER(chat));
  for (GList *l = k; l; l = l->next) if (l->data != empty_box) gtk_widget_destroy(l->data);
  g_list_free(k);
  g_ptr_array_set_size(history, 0);
  gtk_widget_show(empty_box);
  gtk_widget_grab_focus(entry);
}

static void on_settings(GtkButton *b, gpointer d) {
  const char *argv[] = {"lumina-settings", "lumi", NULL};
  g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

/* Etat : pret, ou message + bouton vers les reglages */
static GtkWidget *setup_card, *chips;
static void refresh_state(void) {
  char *why = NULL;
  gboolean ok = lumina_ai_ready(&why);
  gtk_widget_set_visible(setup_card, !ok);
  gtk_widget_set_visible(chips, ok && (!ctx_title || ctx_prompt));
  if (!ok) {
    GtkWidget *l = g_object_get_data(G_OBJECT(setup_card), "why");
    gtk_label_set_text(GTK_LABEL(l), why);
  }
  LuminaAiConfig *c = lumina_ai_config_load();
  if (c->mode == LUMINA_AI_REMOTE && c->remote_url && *c->remote_url) {
    GUri *u = g_uri_parse(c->remote_url, G_URI_FLAGS_NONE, NULL);
    char *t = g_strdup_printf("Service en ligne : %s. Vos messages lui sont envoyés.", u ? g_uri_get_host(u) : c->remote_url);
    gtk_label_set_text(GTK_LABEL(foot_l), t);
    g_free(t);
    if (u) g_uri_unref(u);
  } else if (c->mode == LUMINA_AI_LOCAL)
    gtk_label_set_text(GTK_LABEL(foot_l), "Lumi fonctionne sur ce PC : vos messages ne quittent pas l'appareil. Il peut se tromper.");
  else gtk_label_set_text(GTK_LABEL(foot_l), "");
  gtk_widget_set_visible(foot_l, c->mode != LUMINA_AI_OFF);
  gtk_widget_set_visible(mic_b, ok && voice_ready());
  lumina_ai_config_free(c);
  g_free(why);
}

/* Charge le contexte envoye par le navigateur (et efface le fichier).
   Un petit modele local a une memoire de travail limitee (4096 jetons) :
   on garde le debut de la page, environ 4000 caracteres (lecture en 30 s
   a 1 min sur un vieux PC). */
static void load_context(const char *path) {
  char *json = NULL;
  /* Seulement nos fichiers prives de $XDG_RUNTIME_DIR */
  char *dir = g_path_get_dirname(path), *base = g_path_get_basename(path);
  gboolean ours = !g_strcmp0(dir, g_get_user_runtime_dir()) && g_str_has_prefix(base, "lumi-context-");
  g_free(dir); g_free(base);
  if (!ours || !g_file_get_contents(path, &json, NULL, NULL)) { g_free(json); return; }
  g_unlink(path);
  char *text = lumina_ai_json_string_at(json, "text");
  ctx_title = lumina_ai_json_string_at(json, "title");
  ctx_action = lumina_ai_json_string_at(json, "action");
  char *url = lumina_ai_json_string_at(json, "url");
  ctx_selection = !g_strcmp0(ctx_action, "expliquer");
  if (url) {
    GUri *u = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
    if (u && g_uri_get_host(u)) ctx_host = g_strdup(g_uri_get_host(u));
    if (u) g_uri_unref(u);
  }
  if (!ctx_title || !*ctx_title) { g_free(ctx_title); ctx_title = g_strdup(ctx_host ? ctx_host : "Page web"); }
  if (text && *g_strstrip(text)) {
    LuminaAiConfig *c = lumina_ai_config_load();
    glong max = c->mode == LUMINA_AI_REMOTE ? 40000 : 4000;
    lumina_ai_config_free(c);
    if (g_utf8_strlen(text, -1) > max) { *g_utf8_offset_to_pointer(text, max) = 0; ctx_cut = TRUE; }
    ctx_prompt = g_strdup_printf(
      "%s\n\n%s « %s »%s%s :\n<<<\n%s\n>>>\n"
      "Réponds aux questions en t'appuyant d'abord sur ce contenu. S'il ne contient pas la réponse, dis-le.",
      SYSTEM_PROMPT, ctx_selection ? "Voici un passage de la page web" : "Voici le contenu de la page web",
      ctx_title, ctx_host ? " (" : "", ctx_host ? ctx_host : "", text);
  }
  g_free(text); g_free(url); g_free(json);
}

/* --file : texte du fichier, lu comme le fait l'index. */
static void load_file(const char *path) {
  ctx_title = g_path_get_basename(path);
  ctx_noun = "ce fichier"; ctx_read = "le fichier";
  ctx_fail = "Ce fichier n'a pas pu être lu (format non pris en charge, ou sans texte).";
  ctx_action = g_strdup("fichier");
  glong max = ctx_budget(4000, 40000);
  char *n = g_strdup_printf("%ld", max + 1), *out = NULL;
  const char *argv[] = {"lumina-index", "extract", path, n, NULL};
  gint st = 1;
  g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &st, NULL);
  g_free(n);
  if (st == 0 && out && g_utf8_validate(out, -1, NULL) && *g_strstrip(out)) {
    if (g_utf8_strlen(out, -1) > max) { *g_utf8_offset_to_pointer(out, max) = 0; ctx_cut = TRUE; }
    ctx_prompt = g_strdup_printf("%s\n\nVoici le contenu du fichier « %s » :\n<<<\n%s\n>>>\n"
                                 "Réponds aux questions en t'appuyant d'abord sur ce contenu. S'il ne contient pas la réponse, dis-le.",
                                 SYSTEM_PROMPT, ctx_title, out);
  }
  g_free(out);
}

/* --screen : l'utilisateur trace une zone (ou clique sur un ecran pour le
   prendre en entier), capturee AVANT que Lumi n'apparaisse. */
static gboolean capture_screen(void) {
  char *zone = NULL;
  gint st = 1;
  const char *argv[] = {"slurp", "-o", "-b", "00000066", "-c", "85d6bcff", "-w", "2", NULL};
  if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &zone, NULL, &st, NULL)
      || st != 0 || !zone || !*g_strstrip(zone)) { g_free(zone); return FALSE; }
  char *png = g_build_filename(g_get_user_runtime_dir(), "lumi-screen-XXXXXX", NULL);
  int fd = g_mkstemp_full(png, O_WRONLY, 0600);
  if (fd < 0) { g_free(zone); g_free(png); return FALSE; }
  close(fd);
  const char *g[] = {"grim", "-g", zone, png, NULL};
  gboolean ok = g_spawn_sync(NULL, (char **)g, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                             NULL, NULL, NULL, NULL, &st, NULL) && st == 0;
  g_free(zone);
  if (!ok) { g_unlink(png); g_free(png); return FALSE; }
  screen_png = png;
  return TRUE;
}

static void screen_read_done(GObject *o, GAsyncResult *res, gpointer d) {
  char *out = NULL;
  g_subprocess_communicate_utf8_finish(G_SUBPROCESS(o), res, &out, NULL, NULL);
  g_object_unref(o);
  g_unlink(screen_png);
  /* Lignes vides en moins : c'est autant de place pour le modele */
  GString *t = g_string_new(NULL);
  char **lines = out ? g_strsplit(out, "\n", -1) : NULL;
  for (char **l = lines; l && *l; l++) if (*g_strstrip(*l)) { g_string_append(t, *l); g_string_append_c(t, '\n'); }
  g_strfreev(lines); g_free(out);
  glong max = ctx_budget(4000, 40000);
  if (t->len && g_utf8_strlen(t->str, -1) > max) {
    g_string_truncate(t, g_utf8_offset_to_pointer(t->str, max) - t->str);
    ctx_cut = TRUE;
  }
  if (t->len) {
    ctx_prompt = g_strdup_printf("%s\n\nVoici le texte visible sur une zone de l'écran de l'utilisateur. Il a été lu "
                                 "automatiquement (OCR) : des erreurs ou un désordre sont possibles.\n<<<\n%s\n>>>\n"
                                 "Réponds aux questions en t'appuyant d'abord sur ce texte. S'il ne contient pas la réponse, dis-le.",
                                 SYSTEM_PROMPT, t->str);
    gtk_label_set_text(GTK_LABEL(ctx_sub_l), ctx_cut ? "Texte lu à l'écran · début seulement (zone très chargée)" : "Lumi a lu le texte de cette zone");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Poser une question sur cette zone");
    gtk_widget_set_visible(chips, lumina_ai_ready(NULL));
  } else {
    gtk_label_set_text(GTK_LABEL(ctx_sub_l), "Aucun texte reconnu dans cette zone.");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Demander à Lumi");
  }
  g_string_free(t, TRUE);
  gtk_widget_set_sensitive(entry, TRUE);
  update_send();
  gtk_widget_grab_focus(entry);
}

/* Agrandie deux fois, l'image d'un texte d'ecran (petits caracteres, 96 dpi)
   est bien mieux reconnue. Lecture limitee a 2 fils : le PC reste utilisable. */
static void read_screen(void) {
  GdkPixbuf *px = gdk_pixbuf_new_from_file(screen_png, NULL);
  if (px) {
    int w = gdk_pixbuf_get_width(px), h = gdk_pixbuf_get_height(px);
    if (MAX(w, h) <= 2100) {
      GdkPixbuf *big = gdk_pixbuf_scale_simple(px, w * 2, h * 2, GDK_INTERP_BILINEAR);
      if (big) { gdk_pixbuf_save(big, screen_png, "png", NULL, "compression", "1", NULL); g_object_unref(big); }
    }
    g_object_unref(px);
  }
  GSubprocessLauncher *l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
  g_subprocess_launcher_setenv(l, "OMP_THREAD_LIMIT", "2", TRUE);
  GSubprocess *p = g_subprocess_launcher_spawn(l, NULL, "tesseract", screen_png, "stdout", "-l", "fra+eng", "--dpi", "192", NULL);
  g_object_unref(l);
  if (p) g_subprocess_communicate_utf8_async(p, NULL, NULL, screen_read_done, NULL);
  else {
    g_unlink(screen_png);
    gtk_label_set_text(GTK_LABEL(ctx_sub_l), "La lecture de l'écran n'est pas disponible.");
    gtk_widget_set_sensitive(entry, TRUE);
  }
}

static void on_screen(GtkButton *b, gpointer d) {
  const char *argv[] = {"lumina-lumi", "--screen", NULL};
  g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
  if (!history->len) gtk_widget_destroy(win);   /* rien en cours : on laisse la place */
}

static void on_files_toggle(GtkToggleButton *b, gpointer d) {
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), gtk_toggle_button_get_active(b) ? "Demander à Lumi sur vos fichiers" : "Demander à Lumi");
  gtk_widget_grab_focus(entry);
}

static GtkWidget *context_card(void) {
  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  add_class(b, "ctx");
  GtkWidget *ic = icon_new(screen_png ? "scanscreen" : ctx_selection || !g_strcmp0(ctx_action, "fichier") ? "docs" : "web", R_PRI, 20)->img;
  gtk_widget_set_valign(ic, GTK_ALIGN_CENTER);
  gtk_container_add(GTK_CONTAINER(b), ic);
  GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *l = label(ctx_title, "ctxtitle", 0);
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
  gtk_container_add(GTK_CONTAINER(t), l);
  char *s = screen_png ? g_strdup("Lumi lit le texte à l'écran…")
          : !ctx_prompt ? g_strdup(ctx_fail)
          : g_strdup_printf("%s%s%s", ctx_selection ? "Passage sélectionné" : !g_strcmp0(ctx_action, "fichier") ? "Lumi a lu ce fichier" : "Lumi a lu cette page",
                            ctx_host ? " · " : "", ctx_host ? ctx_host : "");
  if (ctx_cut) { char *x = g_strconcat(s, " · début seulement (long texte)", NULL); g_free(s); s = x; }
  GtkWidget *sl = ctx_sub_l = label(s, "ctxsub", 0);
  gtk_label_set_ellipsize(GTK_LABEL(sl), PANGO_ELLIPSIZE_END);
  gtk_container_add(GTK_CONTAINER(t), sl);
  g_free(s);
  gtk_container_add(GTK_CONTAINER(b), t);
  return b;
}

static gboolean on_focus_in(GtkWidget *w, GdkEvent *e, gpointer d) { refresh_state(); return FALSE; }

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer d) {
  if (ev->keyval == GDK_KEY_Escape && busy) { on_send(NULL, NULL); return TRUE; }
  if (ev->keyval == GDK_KEY_Escape && say_sp) { say_stop(); return TRUE; }
  return FALSE;
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-lumi");
  /* Lance par Chromium (messagerie native) : pas d'interface ici */
  if (argc > 1 && g_str_has_prefix(argv[1], "chrome-extension://")) return lumi_host_main(argv[0]);
  gtk_init(&argc, &argv);
  if (argc > 1 && !strcmp(argv[1], "--write")) return lumi_write_main();
  if (argc > 2 && !strcmp(argv[1], "--context")) load_context(argv[2]);
  if (argc > 2 && !strcmp(argv[1], "--file")) load_file(argv[2]);
  if (argc > 1 && !strcmp(argv[1], "--screen")) {
    if (!capture_screen()) return 0;       /* zone abandonnee (Echap) */
    ctx_title = g_strdup("Zone de l'écran");
    ctx_noun = "cette zone"; ctx_read = "le texte de l'écran";
  }
  lumina_ui_init(CSS, NULL);
  history = g_ptr_array_new_with_free_func(g_free);
  answer = g_string_new(NULL);
  sources = g_ptr_array_new_with_free_func(g_free);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-lumi");
  gtk_window_set_title(GTK_WINDOW(win), "Lumi");
  gtk_window_set_default_size(GTK_WINDOW(win), 560, 680);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "focus-in-event", G_CALLBACK(on_focus_in), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,maximize,close");
  GtkWidget *tt = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_container_add(GTK_CONTAINER(tt), icon_new("sparkle", R_PRI, 20)->img);
  gtk_container_add(GTK_CONTAINER(tt), label("Lumi", "apptitle", 0));
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), tt);
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  GtkWidget *nb = gtk_button_new();
  add_class(nb, "tbtn");
  gtk_container_add(GTK_CONTAINER(nb), icon_new("reset", R_ON_VAR, 20)->img);
  gtk_widget_set_tooltip_text(nb, "Nouvelle conversation");
  g_signal_connect(nb, "clicked", G_CALLBACK(on_new), NULL);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), nb);
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  if (ctx_title) gtk_container_add(GTK_CONTAINER(v), context_card());
  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  chat = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  add_class(chat, "chat");
  gtk_container_add(GTK_CONTAINER(scroll), chat);
  gtk_container_add(GTK_CONTAINER(v), scroll);

  /* Accueil */
  empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_vexpand(empty_box, TRUE);
  gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
  GtkWidget *ic = icon_new("sparkle", R_PRI, 48)->img;
  gtk_container_add(GTK_CONTAINER(empty_box), ic);
  gtk_container_add(GTK_CONTAINER(empty_box), label("Bonjour, je suis Lumi", "hello", 0.5));
  GtkWidget *sub = label("Posez une question, faites résumer ou reformuler un texte, demandez une idée.", "sub", 0.5);
  gtk_label_set_line_wrap(GTK_LABEL(sub), TRUE);
  gtk_label_set_justify(GTK_LABEL(sub), GTK_JUSTIFY_CENTER);
  gtk_container_add(GTK_CONTAINER(empty_box), sub);
  chips = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(chips), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(chips), 2);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(chips), 8);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(chips), 8);
  gtk_widget_set_halign(chips, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(chips, 12);
  char *ctx_ideas[][2] = {
    {g_strdup("Résumer"), g_strdup_printf("Résume %s en quelques points.", ctx_noun)},
    {g_strdup("Points clés"), g_strdup("Quels sont les points clés à retenir ?")},
    {g_strdup("Expliquer simplement"), g_strdup_printf("Explique simplement de quoi parle %s.", ctx_noun)},
    {g_strdup("Traduire en français"), g_strdup_printf("Traduis en français l'essentiel de %s.", ctx_noun)},
  };
  static const char *IDEAS[][2] = {
    {"Rédiger un e-mail", "Rédige un e-mail poli pour "},
    {"Expliquer simplement", "Explique-moi simplement "},
    {"Trouver des idées", "Donne-moi des idées pour "},
    {"Résumer un texte", "Résume ce texte en quelques points :\n"},
  };
  for (guint i = 0; i < G_N_ELEMENTS(IDEAS); i++) {
    const char *const *idea = ctx_title ? (const char *const *)ctx_ideas[i] : IDEAS[i];
    GtkWidget *b = gtk_button_new_with_label(idea[0]);
    add_class(b, "chip");
    g_signal_connect(b, "clicked", G_CALLBACK(on_chip), (gpointer)idea[1]);
    gtk_container_add(GTK_CONTAINER(chips), b);
  }
  gtk_container_add(GTK_CONTAINER(empty_box), chips);
  setup_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(setup_card, 12);
  GtkWidget *why = label("", "sub", 0.5);
  gtk_label_set_line_wrap(GTK_LABEL(why), TRUE);
  g_object_set_data(G_OBJECT(setup_card), "why", why);
  gtk_container_add(GTK_CONTAINER(setup_card), why);
  GtkWidget *sb = gtk_button_new_with_label("Ouvrir les réglages de Lumi");
  add_class(sb, "fbtn");
  gtk_widget_set_halign(sb, GTK_ALIGN_CENTER);
  g_signal_connect(sb, "clicked", G_CALLBACK(on_settings), NULL);
  gtk_container_add(GTK_CONTAINER(setup_card), sb);
  gtk_container_add(GTK_CONTAINER(empty_box), setup_card);
  gtk_container_add(GTK_CONTAINER(chat), empty_box);

  /* Saisie */
  GtkWidget *comp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  add_class(comp, "composer");
  if (!ctx_title) {
    files_b = gtk_toggle_button_new();
    add_class(files_b, "cbtn");
    gtk_container_add(GTK_CONTAINER(files_b), icon_new("folder", R_ON_VAR, 18)->img);
    gtk_widget_set_tooltip_text(files_b, "Chercher la réponse dans vos fichiers");
    gtk_widget_set_valign(files_b, GTK_ALIGN_CENTER);
    g_signal_connect(files_b, "toggled", G_CALLBACK(on_files_toggle), NULL);
    gtk_container_add(GTK_CONTAINER(comp), files_b);
    GtkWidget *sc_b = gtk_button_new();
    add_class(sc_b, "cbtn");
    gtk_container_add(GTK_CONTAINER(sc_b), icon_new("scanscreen", R_ON_VAR, 18)->img);
    gtk_widget_set_tooltip_text(sc_b, "Poser une question sur l'écran (Windows + S)");
    gtk_widget_set_valign(sc_b, GTK_ALIGN_CENTER);
    g_signal_connect(sc_b, "clicked", G_CALLBACK(on_screen), NULL);
    gtk_container_add(GTK_CONTAINER(comp), sc_b);
  }
  entry = gtk_entry_new();
  gtk_widget_set_hexpand(entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Demander à Lumi");
  g_signal_connect(entry, "activate", G_CALLBACK(on_send), NULL);
  g_signal_connect(entry, "changed", G_CALLBACK(on_changed), NULL);
  gtk_container_add(GTK_CONTAINER(comp), entry);
  mic_b = gtk_button_new();
  add_class(mic_b, "cbtn");
  gtk_container_add(GTK_CONTAINER(mic_b), icon_new("mic", R_ON_VAR, 20)->img);
  gtk_widget_set_tooltip_text(mic_b, "Parler à Lumi");
  gtk_widget_set_valign(mic_b, GTK_ALIGN_CENTER);
  g_signal_connect(mic_b, "clicked", G_CALLBACK(on_mic), NULL);
  gtk_container_add(GTK_CONTAINER(comp), mic_b);
  send_b = gtk_button_new();
  add_class(send_b, "send");
  send_ic = icon_new("send", R_ON_PRI, 20);
  gtk_container_add(GTK_CONTAINER(send_b), send_ic->img);
  gtk_widget_set_valign(send_b, GTK_ALIGN_CENTER);
  g_signal_connect(send_b, "clicked", G_CALLBACK(on_send), NULL);
  gtk_container_add(GTK_CONTAINER(comp), send_b);
  gtk_container_add(GTK_CONTAINER(v), comp);
  foot_l = label("", "foot", 0.5);
  gtk_label_set_line_wrap(GTK_LABEL(foot_l), TRUE);
  gtk_container_add(GTK_CONTAINER(v), foot_l);

  gtk_container_add(GTK_CONTAINER(win), v);
  gtk_widget_show_all(win);
  refresh_state();
  update_send();
  gtk_widget_grab_focus(entry);
  if (screen_png) {
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Lecture de l'écran…");
    gtk_widget_set_sensitive(entry, FALSE);
    gtk_widget_hide(chips);
    while (gtk_events_pending()) gtk_main_iteration();
    read_screen();
  } else if (ctx_title) {
    char *ph = g_strdup_printf("Poser une question sur %s", ctx_noun);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), ph);
    g_free(ph);
    char *q = NULL;
    if (ctx_prompt && !g_strcmp0(ctx_action, "resumer")) q = g_strdup("Résume cette page en quelques points.");
    else if (ctx_prompt && ctx_selection) q = g_strdup("Explique simplement ce passage.");
    if (q && lumina_ai_ready(NULL)) ask(q);
    g_free(q);
  } else if (argc > 1 && *argv[1] && argv[1][0] != '-') ask(argv[1]);
  gtk_main();
  say_stop();
  if (rec_sp) g_subprocess_force_exit(rec_sp);
  if (rec_wav) g_unlink(rec_wav);
  return 0;
}

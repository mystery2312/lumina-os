/*
 * Lumi, au-dela de la conversation :
 *
 *  1. Hote de messagerie native de Chromium (module « Lumi » du navigateur).
 *     Chromium lance « lumina-lumi chrome-extension://<id>/ » et lui envoie
 *     UN message JSON (page ou selection) : on le range dans un fichier prive
 *     de $XDG_RUNTIME_DIR, on ouvre Lumi dessus (--context) et on repond.
 *
 *  2. Aide a l'ecriture partout (Windows + E) : le texte selectionne dans
 *     n'importe quelle appli (selection primaire Wayland) est ameliore,
 *     corrige, raccourci, traduit... puis insere a sa place (tape par un
 *     clavier virtuel, et copie dans le presse-papiers) ou copie. Sans selection : Lumi redige a la demande.
 */
#include "lumina-ui.h"
#include "lumina-ai.h"
#include "lumi.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <glib/gstdio.h>

/* ------------------------------------------------------------------ */
/* 1. Hote de messagerie native                                         */
/* ------------------------------------------------------------------ */
static void detach(gpointer d) { setsid(); }

static void host_reply(const char *json) {
  uint32_t n = (uint32_t)strlen(json);
  fwrite(&n, 4, 1, stdout);
  fwrite(json, 1, n, stdout);
  fflush(stdout);
}

int lumi_host_main(const char *self) {
  uint32_t n = 0;
  if (fread(&n, 4, 1, stdin) != 1 || n == 0 || n > 4 * 1024 * 1024) return 1;
  char *msg = g_malloc(n + 1);
  if (fread(msg, 1, n, stdin) != n) { g_free(msg); return 1; }
  msg[n] = 0;
  char *path = g_build_filename(g_get_user_runtime_dir(), "lumi-context-XXXXXX", NULL);
  int fd = g_mkstemp_full(path, O_WRONLY, 0600);
  gboolean ok = fd >= 0 && write(fd, msg, n) == (ssize_t)n;
  if (fd >= 0) close(fd);
  if (ok) {
    const char *argv[] = {self, "--context", path, NULL};
    ok = g_spawn_async(NULL, (char **)argv, NULL,
                       G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                       detach, NULL, NULL, NULL);
    if (!ok) g_unlink(path);
  }
  host_reply(ok ? "{\"ok\":true}" : "{\"ok\":false}");
  g_free(path); g_free(msg);
  return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* 2. Aide a l'ecriture                                                 */
/* ------------------------------------------------------------------ */
static const char *W_CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumi-write.csd { border-radius: 0 0 16px 16px; background: @surface; }\n"
  "window.lumi-write.csd decoration { border-radius: 16px; box-shadow: 0 6px 22px alpha(black, 0.18), 0 0 0 1px alpha(black, 0.06); }\n"
  ".header { background: @surface; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 32px; min-height: 32px; color: @on_var; }\n"
  ".header button.titlebutton:hover { background: alpha(@on, 0.08); }\n"
  ".apptitle { font-size: 15px; color: @on; padding-left: 4px; }\n"
  ".body { padding: 4px 18px 16px 18px; }\n"
  ".sub { font-size: 13px; color: @on_var; }\n"
  ".src { background: @c_low; color: @on_var; border-radius: 14px; padding: 10px 14px; font-size: 13px; }\n"
  ".chip { background: @c_low; color: @on; border-radius: 18px; padding: 8px 14px; font-size: 13px; }\n"
  ".chip:hover { background: @c_high; }\n"
  ".res { background: @c_high; color: @on; border-radius: 14px; padding: 12px 14px; font-size: 14px; }\n"
  ".res.err { background: @ter_c; color: @on_ter_c; }\n"
  ".composer { background: @c_high; border-radius: 24px; padding: 2px 6px 2px 16px; }\n"
  ".composer entry { background: transparent; border: none; box-shadow: none; color: @on; font-size: 14px; min-height: 40px; caret-color: @pri; }\n"
  ".fbtn { background: @pri; color: @on_pri; border-radius: 20px; padding: 8px 20px; font-weight: 500; }\n"
  ".fbtn:disabled { background: alpha(@on, 0.12); color: alpha(@on, 0.38); }\n"
  ".tbtn { background: transparent; color: @pri; border-radius: 20px; padding: 8px 16px; font-weight: 500; }\n"
  ".tbtn:hover { background: alpha(@pri, 0.08); }\n"
  ".foot { color: @on_var; font-size: 11.5px; }\n";

static const struct { const char *label, *prompt; } W_ACTIONS[] = {
  {"Améliorer", "Réécris le texte suivant pour le rendre plus clair et plus fluide, sans changer son sens ni sa langue. Réponds seulement par le texte réécrit."},
  {"Corriger les fautes", "Corrige l'orthographe, la grammaire et la ponctuation du texte suivant, sans rien changer d'autre. Réponds seulement par le texte corrigé."},
  {"Raccourcir", "Raccourcis le texte suivant de moitié environ en gardant l'essentiel, dans sa langue. Réponds seulement par le texte raccourci."},
  {"Plus formel", "Réécris le texte suivant sur un ton plus formel et professionnel, dans sa langue. Réponds seulement par le texte réécrit."},
  {"Plus amical", "Réécris le texte suivant sur un ton plus chaleureux et détendu, dans sa langue. Réponds seulement par le texte réécrit."},
  {"Traduire en anglais", "Traduis le texte suivant en anglais. Réponds seulement par la traduction."},
};
static const char *W_FREE =
  "Tu es Lumi, l'assistant d'écriture de LuminaOS. Rédige le texte demandé, en français sauf si on te demande "
  "une autre langue. Réponds seulement par le texte à insérer, sans commentaire ni introduction.";

static GtkWidget *w_win, *w_sc, *w_res, *w_insert, *w_copy, *w_again, *w_choices, *w_entry, *w_state;
static char *w_src;                 /* texte selectionne (NULL : redaction libre) */
static GString *w_out;
static GCancellable *w_busy;

/* La fenetre reprend la hauteur de son contenu (pas de vide en bas) */
static void w_fit(void) { gtk_window_resize(GTK_WINDOW(w_win), 520, 1); }

static void w_buttons(gboolean done) {
  gtk_widget_set_sensitive(w_insert, done && w_out->len);
  gtk_widget_set_sensitive(w_copy, done && w_out->len);
  gtk_widget_set_visible(w_again, done);
}

static void w_piece(const char *p, gpointer d) {
  g_string_append(w_out, p);
  char *t = g_strstrip(g_strdup(w_out->str));
  gtk_label_set_text(GTK_LABEL(w_res), t);
  g_free(t);
}

static void w_done(const char *err, gpointer d) {
  if (err && !w_out->len) {
    gtk_label_set_text(GTK_LABEL(w_res), err);
    gtk_style_context_add_class(gtk_widget_get_style_context(w_res), "err");
  }
  g_clear_object(&w_busy);
  gtk_label_set_text(GTK_LABEL(w_state), "Lumi peut se tromper : relisez avant d'insérer.");
  w_buttons(TRUE);
}

static void w_run(const char *system, const char *text) {
  char *why = NULL;
  gtk_widget_hide(w_choices);
  gtk_widget_show(w_sc);
  w_fit();
  gtk_style_context_remove_class(gtk_widget_get_style_context(w_res), "err");
  g_string_truncate(w_out, 0);
  if (!lumina_ai_ready(&why)) { w_done(why, NULL); g_free(why); return; }
  gtk_label_set_text(GTK_LABEL(w_res), "…");
  gtk_label_set_text(GTK_LABEL(w_state), "Lumi rédige…");
  w_buttons(FALSE);
  gtk_widget_hide(w_again);
  w_busy = g_cancellable_new();
  lumina_ai_ask(system, text, w_piece, w_done, NULL, w_busy);
}

static void w_on_action(GtkButton *b, gpointer d) { w_run(W_ACTIONS[GPOINTER_TO_INT(d)].prompt, w_src); }

static void w_on_free(GtkWidget *w, gpointer d) {
  const char *t = gtk_entry_get_text(GTK_ENTRY(w_entry));
  if (!*t || w_busy) return;
  if (w_src) {
    /* Consigne libre sur la selection */
    char *sys = g_strdup_printf("%s Consigne de l'utilisateur pour le texte suivant : %s", W_FREE, t);
    w_run(sys, w_src);
    g_free(sys);
  } else w_run(W_FREE, t);
}

static void w_on_again(GtkButton *b, gpointer d) {
  if (w_busy) { g_cancellable_cancel(w_busy); g_clear_object(&w_busy); }
  gtk_widget_hide(w_sc);
  gtk_widget_show(w_choices);
  gtk_label_set_text(GTK_LABEL(w_state), "");
  w_buttons(FALSE);
  gtk_widget_hide(w_again);
  w_fit();
  gtk_widget_grab_focus(w_entry);
}

/* Copie dans le presse-papiers (wl-copy reste en fond pour le servir). */
static gboolean w_clip(const char *t) {
  GError *e = NULL;
  GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                    G_SUBPROCESS_FLAGS_STDERR_SILENCE, &e, "wl-copy", NULL);
  if (!p) { g_clear_error(&e); return FALSE; }
  GBytes *b = g_bytes_new(t, strlen(t));
  gboolean ok = g_subprocess_communicate(p, b, NULL, NULL, NULL, NULL);
  g_bytes_unref(b);
  g_object_unref(p);
  return ok;
}

static char *w_result(void) { return g_strstrip(g_strdup(w_out->str)); }

static void w_on_copy(GtkButton *b, gpointer d) {
  char *t = w_result();
  if (w_clip(t)) gtk_label_set_text(GTK_LABEL(w_state), "Copié. Collez-le avec Ctrl+V.");
  g_free(t);
}

/* Inserer : le texte est TAPE dans l'appli d'origine (clavier virtuel), ce
   qui remplace la selection, et reste dans le presse-papiers. Un Ctrl+V
   simule serait plus direct, mais labwc ignore les touches de modification
   d'un clavier virtuel (constate en essai). La fenetre se ferme d'abord,
   pour que l'appli d'origine reprenne la main. */
static void w_on_insert(GtkButton *b, gpointer d) {
  char *t = w_result();
  w_clip(t);
  GError *e = NULL;
  GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                    G_SUBPROCESS_FLAGS_STDERR_SILENCE, &e,
                                    "sh", "-c", "sleep 0.6; exec wtype -", NULL);
  if (p) {
    GOutputStream *in = g_subprocess_get_stdin_pipe(p);
    g_output_stream_write_all(in, t, strlen(t), NULL, NULL, NULL);
    g_output_stream_close(in, NULL, NULL);
    g_object_unref(p);
    gtk_widget_destroy(w_win);
  } else {
    g_clear_error(&e);
    gtk_label_set_text(GTK_LABEL(w_state), "Copié. Collez-le avec Ctrl+V.");
  }
  g_free(t);
}

/* Texte selectionne dans l'appli au premier plan (selection primaire). */
static char *w_selection(void) {
  char *out = NULL;
  const char *argv[] = {"timeout", "2", "wl-paste", "--primary", "--no-newline", "--type", "text", NULL};
  if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                    NULL, NULL, &out, NULL, NULL, NULL)) return NULL;
  if (!out || !g_utf8_validate(out, -1, NULL) || !*g_strstrip(out)) { g_free(out); return NULL; }
  if (strlen(out) > 12000) { g_free(out); return NULL; }   /* pas une vraie selection de texte */
  return out;
}

static gboolean w_key(GtkWidget *w, GdkEventKey *ev, gpointer d) {
  if (ev->keyval == GDK_KEY_Escape) { gtk_widget_destroy(w_win); return TRUE; }
  return FALSE;
}

int lumi_write_main(void) {
  w_src = w_selection();       /* avant d'ouvrir la fenetre : la selection peut changer */
  w_out = g_string_new(NULL);
  lumina_ui_init(W_CSS, NULL);

  w_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(w_win, "lumi-write");
  gtk_window_set_title(GTK_WINDOW(w_win), "Lumi : aide à l'écriture");
  gtk_window_set_default_size(GTK_WINDOW(w_win), 520, -1);
  gtk_window_set_position(GTK_WINDOW(w_win), GTK_WIN_POS_CENTER);
  g_signal_connect(w_win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(w_win, "key-press-event", G_CALLBACK(w_key), NULL);

  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":close");
  GtkWidget *tt = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_container_add(GTK_CONTAINER(tt), icon_new("sparkle", R_PRI, 20)->img);
  gtk_container_add(GTK_CONTAINER(tt), label("Aide à l'écriture", "apptitle", 0));
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), tt);
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_window_set_titlebar(GTK_WINDOW(w_win), hb);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  add_class(v, "body");

  if (w_src) {
    char *ex = g_utf8_strlen(w_src, -1) > 220 ? g_strdup_printf("%.*s…", (int)(g_utf8_offset_to_pointer(w_src, 220) - w_src), w_src) : g_strdup(w_src);
    GtkWidget *s = label(ex, "src", 0);
    gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(s), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(s), 60);
    gtk_container_add(GTK_CONTAINER(v), s);
    g_free(ex);
  }

  w_choices = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  if (w_src) {
    GtkWidget *fb = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(fb), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(fb), 3);   /* sinon la fenetre se cale sur 6 lignes */
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fb), 3);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(fb), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(fb), 8);
    for (guint i = 0; i < G_N_ELEMENTS(W_ACTIONS); i++) {
      GtkWidget *b = gtk_button_new_with_label(W_ACTIONS[i].label);
      add_class(b, "chip");
      g_signal_connect(b, "clicked", G_CALLBACK(w_on_action), GINT_TO_POINTER(i));
      gtk_container_add(GTK_CONTAINER(fb), b);
    }
    gtk_container_add(GTK_CONTAINER(w_choices), fb);
  } else {
    GtkWidget *s = label("Lumi rédige pour vous, puis insère le texte là où se trouve le curseur. "
                         "Astuce : sélectionnez d'abord un texte pour le faire améliorer, corriger ou traduire.", "sub", 0);
    gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(s), 60);
    gtk_container_add(GTK_CONTAINER(w_choices), s);
  }
  GtkWidget *comp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  add_class(comp, "composer");
  w_entry = gtk_entry_new();
  gtk_widget_set_hexpand(w_entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(w_entry), w_src ? "Ou dites à Lumi quoi changer" : "Que voulez-vous écrire ? (ex. : un mot d'excuse pour…)");
  g_signal_connect(w_entry, "activate", G_CALLBACK(w_on_free), NULL);
  gtk_container_add(GTK_CONTAINER(comp), w_entry);
  gtk_container_add(GTK_CONTAINER(w_choices), comp);
  gtk_container_add(GTK_CONTAINER(v), w_choices);

  w_res = label("", "res", 0);
  gtk_label_set_line_wrap(GTK_LABEL(w_res), TRUE);
  gtk_label_set_line_wrap_mode(GTK_LABEL(w_res), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_selectable(GTK_LABEL(w_res), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(w_res), 60);
  gtk_widget_set_can_focus(w_res, FALSE);
  GtkWidget *sc = w_sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), 340);
  gtk_container_add(GTK_CONTAINER(sc), w_res);
  gtk_container_add(GTK_CONTAINER(v), sc);

  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  w_state = label("", "foot", 0);
  gtk_widget_set_hexpand(w_state, TRUE);
  gtk_label_set_line_wrap(GTK_LABEL(w_state), TRUE);
  gtk_container_add(GTK_CONTAINER(bar), w_state);
  w_again = gtk_button_new_with_label("Autre choix");
  add_class(w_again, "tbtn");
  g_signal_connect(w_again, "clicked", G_CALLBACK(w_on_again), NULL);
  gtk_container_add(GTK_CONTAINER(bar), w_again);
  w_copy = gtk_button_new_with_label("Copier");
  add_class(w_copy, "tbtn");
  g_signal_connect(w_copy, "clicked", G_CALLBACK(w_on_copy), NULL);
  gtk_container_add(GTK_CONTAINER(bar), w_copy);
  w_insert = gtk_button_new_with_label(w_src ? "Remplacer" : "Insérer");
  add_class(w_insert, "fbtn");
  g_signal_connect(w_insert, "clicked", G_CALLBACK(w_on_insert), NULL);
  gtk_container_add(GTK_CONTAINER(bar), w_insert);
  gtk_container_add(GTK_CONTAINER(v), bar);

  gtk_container_add(GTK_CONTAINER(w_win), v);
  gtk_widget_show_all(w_win);
  gtk_widget_hide(w_sc);
  w_buttons(FALSE);
  gtk_widget_hide(w_again);
  w_fit();
  gtk_widget_grab_focus(w_entry);
  gtk_main();
  if (w_busy) g_cancellable_cancel(w_busy);
  return 0;
}

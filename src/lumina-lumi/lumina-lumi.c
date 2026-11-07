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
 *   lumina-lumi chrome-extension://...   hote de messagerie native (Chromium)
 */
#include "lumina-ui.h"
#include "lumina-ai.h"
#include "lumi.h"
#include <string.h>
#include <glib/gstdio.h>

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
  ".ctxsub { font-size: 12px; color: @on_var; }\n";

static const char *SYSTEM_PROMPT =
  "Tu es Lumi, l'assistant intégré à LuminaOS, un système d'exploitation léger. "
  "Réponds en français, de façon claire, utile et concise, sauf si on te demande une autre langue. "
  "Si tu ne sais pas, dis-le simplement plutôt que d'inventer.";

/* Contexte : page web ou passage envoye par le navigateur */
static char *ctx_prompt, *ctx_title, *ctx_host, *ctx_action;
static gboolean ctx_selection, ctx_cut;

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

static void on_piece(const char *piece, gpointer d) {
  if (!answer_l) return;
  g_string_append(answer, piece);
  set_msg(answer_l, answer->str);
  g_idle_add(scroll_end, NULL);
}

static void on_progress(double f, gpointer d) {
  if (!answer_l || answer->len) return;
  char *t = g_strdup_printf("%s… %d %%", ctx_prompt ? "Lumi lit la page" : "Lumi lit votre demande", (int)(f * 100));
  gtk_label_set_text(GTK_LABEL(answer_l), t);
  g_free(t);
}

static void on_done(const char *err, gpointer d) {
  if (err) {
    if (!answer->len) { set_msg(answer_l, err); add_class(answer_l, "err"); }
    /* question sans reponse : retiree de l'historique */
    if (history->len >= 2) g_ptr_array_remove_range(history, history->len - 2, 2);
  } else {
    g_ptr_array_add(history, g_strdup("assistant"));
    g_ptr_array_add(history, g_strdup(answer->str));
  }
  g_clear_object(&busy);
  answer_l = NULL;
  update_send();
  gtk_widget_grab_focus(entry);
}

static void ask(const char *q) {
  if (busy || !q || !*g_strstrip((char *)q)) return;
  gtk_widget_hide(empty_box);
  bubble(q, TRUE);
  g_ptr_array_add(history, g_strdup("user"));
  g_ptr_array_add(history, g_strdup(q));
  /* Petit modele, petit contexte : consigne + 10 derniers messages */
  GPtrArray *m = g_ptr_array_new();
  g_ptr_array_add(m, "system");
  g_ptr_array_add(m, ctx_prompt ? ctx_prompt : (gpointer)SYSTEM_PROMPT);
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
  gtk_widget_set_visible(chips, ok);
  if (!ok) {
    GtkWidget *l = g_object_get_data(G_OBJECT(setup_card), "why");
    gtk_label_set_text(GTK_LABEL(l), why);
  }
  LuminaAiConfig *c = lumina_ai_config_load();
  if (c->mode == LUMINA_AI_REMOTE && c->remote_url) {
    GUri *u = g_uri_parse(c->remote_url, G_URI_FLAGS_NONE, NULL);
    char *t = g_strdup_printf("Service en ligne : %s. Vos messages lui sont envoyés.", u ? g_uri_get_host(u) : c->remote_url);
    gtk_label_set_text(GTK_LABEL(foot_l), t);
    g_free(t);
    if (u) g_uri_unref(u);
  } else if (c->mode == LUMINA_AI_LOCAL)
    gtk_label_set_text(GTK_LABEL(foot_l), "Lumi fonctionne sur ce PC : vos messages ne quittent pas l'appareil. Il peut se tromper.");
  else gtk_label_set_text(GTK_LABEL(foot_l), "");
  gtk_widget_set_visible(foot_l, c->mode != LUMINA_AI_OFF);
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

static GtkWidget *context_card(void) {
  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  add_class(b, "ctx");
  GtkWidget *ic = icon_new(ctx_selection ? "docs" : "web", R_PRI, 20)->img;
  gtk_widget_set_valign(ic, GTK_ALIGN_CENTER);
  gtk_container_add(GTK_CONTAINER(b), ic);
  GtkWidget *t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  GtkWidget *l = label(ctx_title, "ctxtitle", 0);
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
  gtk_container_add(GTK_CONTAINER(t), l);
  char *s = !ctx_prompt ? g_strdup("Cette page n'a pas pu être lue (page protégée ou vide).")
          : g_strdup_printf("%s%s%s", ctx_selection ? "Passage sélectionné" : "Lumi a lu cette page",
                            ctx_host ? " · " : "", ctx_host ? ctx_host : "");
  if (ctx_cut) { char *x = g_strconcat(s, " · début seulement (page longue)", NULL); g_free(s); s = x; }
  GtkWidget *sl = label(s, "ctxsub", 0);
  gtk_label_set_ellipsize(GTK_LABEL(sl), PANGO_ELLIPSIZE_END);
  gtk_container_add(GTK_CONTAINER(t), sl);
  g_free(s);
  gtk_container_add(GTK_CONTAINER(b), t);
  return b;
}

static gboolean on_focus_in(GtkWidget *w, GdkEvent *e, gpointer d) { refresh_state(); return FALSE; }

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer d) {
  if (ev->keyval == GDK_KEY_Escape && busy) { on_send(NULL, NULL); return TRUE; }
  return FALSE;
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-lumi");
  /* Lance par Chromium (messagerie native) : pas d'interface ici */
  if (argc > 1 && g_str_has_prefix(argv[1], "chrome-extension://")) return lumi_host_main(argv[0]);
  gtk_init(&argc, &argv);
  if (argc > 1 && !strcmp(argv[1], "--write")) return lumi_write_main();
  if (argc > 2 && !strcmp(argv[1], "--context")) load_context(argv[2]);
  lumina_ui_init(CSS, NULL);
  history = g_ptr_array_new_with_free_func(g_free);
  answer = g_string_new(NULL);

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
  static const char *PAGE_IDEAS[][2] = {
    {"Résumer la page", "Résume cette page en quelques points."},
    {"Points clés", "Quels sont les points clés à retenir ?"},
    {"Expliquer simplement", "Explique simplement de quoi parle cette page."},
    {"Traduire en français", "Traduis en français l'essentiel de cette page."},
  };
  static const char *IDEAS[][2] = {
    {"Rédiger un e-mail", "Rédige un e-mail poli pour "},
    {"Expliquer simplement", "Explique-moi simplement "},
    {"Trouver des idées", "Donne-moi des idées pour "},
    {"Résumer un texte", "Résume ce texte en quelques points :\n"},
  };
  for (guint i = 0; i < G_N_ELEMENTS(IDEAS); i++) {
    const char *const *idea = ctx_prompt ? PAGE_IDEAS[i] : IDEAS[i];
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
  entry = gtk_entry_new();
  gtk_widget_set_hexpand(entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Demander à Lumi");
  g_signal_connect(entry, "activate", G_CALLBACK(on_send), NULL);
  g_signal_connect(entry, "changed", G_CALLBACK(on_changed), NULL);
  gtk_container_add(GTK_CONTAINER(comp), entry);
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
  if (ctx_title) {
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Poser une question sur cette page");
    char *q = NULL;
    if (ctx_prompt && !g_strcmp0(ctx_action, "resumer")) q = g_strdup("Résume cette page en quelques points.");
    else if (ctx_prompt && ctx_selection) q = g_strdup("Explique simplement ce passage.");
    if (q && lumina_ai_ready(NULL)) ask(q);
    g_free(q);
  } else if (argc > 1 && *argv[1]) ask(argv[1]);
  gtk_main();
  return 0;
}

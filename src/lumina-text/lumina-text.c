/*
 * lumina-text : l'editeur de texte de LuminaOS (comme l'appli Texte de
 * ChromeOS). Fichiers ouverts dans une liste a gauche, numeros de ligne,
 * coloration du code, recherche et remplacement, annuler / retablir,
 * retour a la ligne, taille du texte. Enregistrement automatique propose a
 * la fermeture.
 *
 * Usage : lumina-text [fichier...]
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <gtksourceview/gtksource.h>
#include <string.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-text.csd { border-radius: 0 0 16px 16px; background: @surface; }\n"
  "window.lumina-text.csd decoration { border-radius: 16px; box-shadow: 0 6px 22px alpha(black, 0.18), 0 0 0 1px alpha(black, 0.06); }\n"
  "window.lumina-text.maximized, window.lumina-text.maximized decoration, window.lumina-text.tiled decoration { border-radius: 0; }\n"
  ".header { background: @surface; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  "window.maximized .header { border-radius: 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 32px; min-height: 32px; color: @on_var; }\n"
  ".header button.titlebutton:hover { background: alpha(@on, 0.08); }\n"
  ".apptitle { font-size: 16px; color: @on; padding-left: 8px; }\n"
  ".round { background: transparent; color: @on; border-radius: 18px; min-width: 36px; min-height: 36px; padding: 0; }\n"
  ".round:hover { background: alpha(@on, 0.08); }\n"
  ".round:disabled { opacity: 0.4; }\n"
  ".side { background: @surface; padding: 4px 10px 12px 10px; }\n"
  ".side list, .side row, .side scrolledwindow, .side viewport { background: transparent; }\n"
  ".side row { border-radius: 20px; margin: 1px 0; }\n"
  ".side row:hover { background: alpha(@on, 0.06); }\n"
  ".side row:selected { background: @sec_c; color: @on_sec_c; }\n"
  ".doc { padding: 9px 12px; }\n"
  ".doc label { font-size: 13.5px; }\n"
  ".doc .dirty { color: @pri; font-weight: 700; }\n"
  ".editor { background: @c_low; border-radius: 20px 0 16px 0; }\n"
  "textview, textview text { background: @c_low; color: @on; }\n"
  "textview border, textview border.left { background: @c_low; color: @on_var; }\n"
  ".fbar { background: @c_high; border-radius: 20px; padding: 4px 6px 4px 14px; margin: 8px 14px; }\n"
  ".fbar entry { background: transparent; border: none; box-shadow: none; color: @on; min-height: 32px; }\n"
  ".tbtn { background: transparent; color: @pri; border-radius: 16px; padding: 6px 12px; font-weight: 500; }\n"
  ".tbtn:hover { background: alpha(@pri, 0.08); }\n"
  ".status { color: @on_var; font-size: 12px; padding: 4px 18px 8px 18px; }\n"
  ".newbtn { background: @pri_c; color: @on_pri_c; border-radius: 16px; padding: 8px 14px; font-weight: 500; margin: 4px 0 8px 0; }\n";

typedef struct {
  GtkSourceBuffer *buf;
  GtkWidget *view, *scroll, *row, *name_l, *dot;
  char *path;              /* NULL : document non enregistre */
} Doc;

static GtkWidget *win, *list, *stack, *fbar_rev, *find_e, *repl_e, *status_l, *undo_b, *redo_b;
static GPtrArray *docs;
static Doc *cur;
static int font_pt = 11;
static gboolean wrap = TRUE;
static GtkCssProvider *font_css;
static int untitled_n;

static void update_title(void);

static char *doc_name(Doc *d) {
  if (d->path) return g_path_get_basename(d->path);
  return g_strdup_printf("Sans titre %d", GPOINTER_TO_INT(g_object_get_data(G_OBJECT(d->buf), "n")));
}

static gboolean doc_dirty(Doc *d) { return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(d->buf)); }

static void apply_font(void) {
  char *c = g_strdup_printf("textview { font-family: 'DejaVu Sans Mono', monospace; font-size: %dpt; }", font_pt);
  gtk_css_provider_load_from_data(font_css, c, -1, NULL);
  g_free(c);
}

/* Coloration : d'apres le nom du fichier ; style clair ou sombre */
static void apply_lang(Doc *d) {
  GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
  GtkSourceLanguage *l = d->path ? gtk_source_language_manager_guess_language(lm, d->path, NULL) : NULL;
  gtk_source_buffer_set_language(d->buf, l);
  GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
  GtkSourceStyleScheme *sc = gtk_source_style_scheme_manager_get_scheme(sm, dark ? "oblivion" : "tango");
  if (sc) gtk_source_buffer_set_style_scheme(d->buf, sc);
}

static void on_theme(void) {
  if (docs) for (guint i = 0; i < docs->len; i++) apply_lang(docs->pdata[i]);
}

static void refresh_row(Doc *d) {
  char *n = doc_name(d);
  gtk_label_set_text(GTK_LABEL(d->name_l), n);
  gtk_widget_set_visible(d->dot, doc_dirty(d));
  g_free(n);
}

static void update_status(void) {
  if (!cur) { gtk_label_set_text(GTK_LABEL(status_l), ""); return; }
  GtkTextIter it;
  GtkTextBuffer *b = GTK_TEXT_BUFFER(cur->buf);
  gtk_text_buffer_get_iter_at_mark(b, &it, gtk_text_buffer_get_insert(b));
  GtkSourceLanguage *l = gtk_source_buffer_get_language(cur->buf);
  char *s = g_strdup_printf("Ligne %d, colonne %d · %s · %d caractères", gtk_text_iter_get_line(&it) + 1,
                            gtk_text_iter_get_line_offset(&it) + 1, l ? gtk_source_language_get_name(l) : "Texte brut",
                            gtk_text_buffer_get_char_count(b));
  gtk_label_set_text(GTK_LABEL(status_l), s);
  g_free(s);
  gtk_widget_set_sensitive(undo_b, gtk_source_buffer_can_undo(cur->buf));
  gtk_widget_set_sensitive(redo_b, gtk_source_buffer_can_redo(cur->buf));
}

static void update_title(void) {
  char *n = cur ? doc_name(cur) : g_strdup("Texte");
  char *t = g_strdup_printf("%s%s — Texte", cur && doc_dirty(cur) ? "• " : "", n);
  gtk_window_set_title(GTK_WINDOW(win), t);
  g_free(n); g_free(t);
  update_status();
}

static void on_modified(GtkTextBuffer *b, gpointer d) { refresh_row(d); update_title(); }
static void on_cursor(GObject *o, GParamSpec *ps, gpointer d) { update_status(); }

static void select_doc(Doc *d) {
  cur = d;
  gtk_stack_set_visible_child(GTK_STACK(stack), d->scroll);
  gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(d->row));
  gtk_widget_grab_focus(d->view);
  update_title();
}

static void on_row(GtkListBox *lb, GtkListBoxRow *r, gpointer x) {
  if (!r) return;
  Doc *d = g_object_get_data(G_OBJECT(r), "doc");
  if (d && d != cur) select_doc(d);
}

static Doc *new_doc(const char *path) {
  Doc *d = g_new0(Doc, 1);
  d->buf = gtk_source_buffer_new(NULL);
  g_object_set_data(G_OBJECT(d->buf), "n", GINT_TO_POINTER(++untitled_n));
  d->path = path ? g_strdup(path) : NULL;
  if (path) {
    char *c = NULL; gsize n = 0;
    if (g_file_get_contents(path, &c, &n, NULL)) {
      if (!g_utf8_validate(c, n, NULL)) { char *u = g_convert(c, n, "UTF-8", "ISO-8859-15", NULL, NULL, NULL); if (u) { g_free(c); c = u; } }
      gtk_source_buffer_begin_not_undoable_action(d->buf);
      gtk_text_buffer_set_text(GTK_TEXT_BUFFER(d->buf), c, -1);
      gtk_source_buffer_end_not_undoable_action(d->buf);
      GtkTextIter s; gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(d->buf), &s);
      gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(d->buf), &s);
    }
    g_free(c);
  }
  gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(d->buf), FALSE);
  d->view = gtk_source_view_new_with_buffer(d->buf);
  gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(d->view), TRUE);
  gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(d->view), TRUE);
  gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(d->view), TRUE);
  gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(d->view), 4);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(d->view), wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(d->view), 8);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(d->view), 10);
  gtk_style_context_add_provider(gtk_widget_get_style_context(d->view), GTK_STYLE_PROVIDER(font_css),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
  d->scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(d->scroll), d->view);
  gtk_widget_show_all(d->scroll);
  gtk_stack_add_named(GTK_STACK(stack), d->scroll, NULL);
  apply_lang(d);

  d->row = gtk_list_box_row_new();
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  add_class(h, "doc");
  d->name_l = label("", NULL, 0);
  gtk_label_set_ellipsize(GTK_LABEL(d->name_l), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_set_hexpand(d->name_l, TRUE);
  gtk_container_add(GTK_CONTAINER(h), d->name_l);
  d->dot = label("●", "dirty", 1);
  gtk_widget_set_no_show_all(d->dot, TRUE);
  gtk_container_add(GTK_CONTAINER(h), d->dot);
  gtk_container_add(GTK_CONTAINER(d->row), h);
  g_object_set_data(G_OBJECT(d->row), "doc", d);
  gtk_widget_show_all(d->row);
  gtk_container_add(GTK_CONTAINER(list), d->row);
  g_signal_connect(d->buf, "modified-changed", G_CALLBACK(on_modified), d);
  g_signal_connect(d->buf, "notify::cursor-position", G_CALLBACK(on_cursor), d);
  g_signal_connect(d->buf, "notify::can-undo", G_CALLBACK(on_cursor), d);
  g_ptr_array_add(docs, d);
  refresh_row(d);
  return d;
}

static Doc *find_open(const char *path) {
  for (guint i = 0; i < docs->len; i++) { Doc *d = docs->pdata[i]; if (d->path && !strcmp(d->path, path)) return d; }
  return NULL;
}

static void open_path(const char *path) {
  Doc *d = find_open(path);
  if (!d) {
    /* Remplace le document vide et intact du demarrage */
    if (docs->len == 1 && !((Doc *)docs->pdata[0])->path && !doc_dirty(docs->pdata[0]) &&
        gtk_text_buffer_get_char_count(GTK_TEXT_BUFFER(((Doc *)docs->pdata[0])->buf)) == 0) {
      Doc *e = docs->pdata[0];
      g_ptr_array_remove_index(docs, 0);
      gtk_widget_destroy(e->row); gtk_widget_destroy(e->scroll); g_free(e);
    }
    d = new_doc(path);
  }
  select_doc(d);
}

static gboolean save_as(Doc *d);
static gboolean save_doc(Doc *d) {
  if (!d->path) return save_as(d);
  GtkTextIter a, b;
  gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(d->buf), &a, &b);
  char *t = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(d->buf), &a, &b, FALSE);
  GError *err = NULL;
  gboolean ok = g_file_set_contents(d->path, t, -1, &err);
  g_free(t);
  if (!ok) {
    GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                          "Enregistrement impossible");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(m), "%s", err->message);
    gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
    g_error_free(err);
    return FALSE;
  }
  gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(d->buf), FALSE);
  return TRUE;
}
static gboolean save_as(Doc *d) {
  GtkFileChooserNative *fc = gtk_file_chooser_native_new("Enregistrer sous", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_SAVE,
                                                         "Enregistrer", "Annuler");
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
  if (d->path) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fc), d->path);
  else {
    const char *docsdir = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
    if (docsdir) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fc), docsdir);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), "Sans titre.txt");
  }
  gboolean ok = FALSE;
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
    g_free(d->path);
    d->path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    ok = save_doc(d);
    apply_lang(d); refresh_row(d); update_title();
  }
  g_object_unref(fc);
  return ok;
}

/* Fermer un document : proposer d'enregistrer s'il a change */
static gboolean confirm_close(Doc *d) {
  if (!doc_dirty(d)) return TRUE;
  char *n = doc_name(d);
  GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                        "Enregistrer les modifications de « %s » ?", n);
  g_free(n);
  gtk_dialog_add_buttons(GTK_DIALOG(m), "Ne pas enregistrer", 1, "Annuler", GTK_RESPONSE_CANCEL, "Enregistrer", 2, NULL);
  int r = gtk_dialog_run(GTK_DIALOG(m));
  gtk_widget_destroy(m);
  if (r == 2) return save_doc(d);
  return r == 1;
}

static void close_doc(Doc *d) {
  if (!confirm_close(d)) return;
  guint i; g_ptr_array_find(docs, d, &i);
  g_ptr_array_remove_index(docs, i);
  gtk_widget_destroy(d->row); gtk_widget_destroy(d->scroll);
  g_free(d->path); g_free(d);
  if (!docs->len) new_doc(NULL);
  select_doc(docs->pdata[MIN(i, docs->len - 1)]);
}

static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer x) {
  for (guint i = 0; i < docs->len; i++) {
    Doc *d = docs->pdata[i];
    if (doc_dirty(d)) { select_doc(d); if (!confirm_close(d)) return TRUE; }
  }
  return FALSE;
}

static void on_new(GtkWidget *w, gpointer x) { select_doc(new_doc(NULL)); }
static void on_open(GtkWidget *w, gpointer x) {
  GtkFileChooserNative *fc = gtk_file_chooser_native_new("Ouvrir", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN, "Ouvrir", "Annuler");
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(fc), TRUE);
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
    GSList *fs = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(fc));
    for (GSList *l = fs; l; l = l->next) open_path(l->data);
    g_slist_free_full(fs, g_free);
  }
  g_object_unref(fc);
}
static void on_save(GtkWidget *w, gpointer x) { if (cur) save_doc(cur); }
static void on_save_as(GtkWidget *w, gpointer x) { if (cur) save_as(cur); }
static void on_close(GtkWidget *w, gpointer x) { if (cur) close_doc(cur); }
static void on_undo(GtkWidget *w, gpointer x) { if (cur && gtk_source_buffer_can_undo(cur->buf)) gtk_source_buffer_undo(cur->buf); }
static void on_redo(GtkWidget *w, gpointer x) { if (cur && gtk_source_buffer_can_redo(cur->buf)) gtk_source_buffer_redo(cur->buf); }
static void on_wrap(GtkCheckMenuItem *m, gpointer x) {
  wrap = gtk_check_menu_item_get_active(m);
  for (guint i = 0; i < docs->len; i++)
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(((Doc *)docs->pdata[i])->view), wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
}
static void on_zoom(GtkWidget *w, gpointer d) { font_pt = CLAMP(font_pt + GPOINTER_TO_INT(d), 7, 32); apply_font(); }

/* --- Recherche et remplacement ------------------------------------- */
static GtkSourceSearchContext *search_ctx(void) {
  if (!cur) return NULL;
  GtkSourceSearchContext *c = g_object_get_data(G_OBJECT(cur->buf), "search");
  if (!c) {
    c = gtk_source_search_context_new(cur->buf, NULL);
    g_object_set_data_full(G_OBJECT(cur->buf), "search", c, g_object_unref);
  }
  GtkSourceSearchSettings *s = gtk_source_search_context_get_settings(c);
  gtk_source_search_settings_set_search_text(s, *gtk_entry_get_text(GTK_ENTRY(find_e)) ? gtk_entry_get_text(GTK_ENTRY(find_e)) : NULL);
  gtk_source_search_settings_set_wrap_around(s, TRUE);
  return c;
}
static void find_step(gboolean fwd) {
  GtkSourceSearchContext *c = search_ctx();
  if (!c) return;
  GtkTextBuffer *b = GTK_TEXT_BUFFER(cur->buf);
  GtkTextIter is, ie, ms, me;
  gtk_text_buffer_get_selection_bounds(b, &is, &ie);
  gboolean found = fwd ? gtk_source_search_context_forward(c, &ie, &ms, &me, NULL)
                       : gtk_source_search_context_backward(c, &is, &ms, &me, NULL);
  if (found) {
    gtk_text_buffer_select_range(b, &ms, &me);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(cur->view), &ms, 0.2, FALSE, 0, 0);
  }
}
static void on_find_next(GtkWidget *w, gpointer x) { find_step(TRUE); }
static void on_find_prev(GtkWidget *w, gpointer x) { find_step(FALSE); }
static void on_find_changed(GtkEditable *e, gpointer x) { search_ctx(); }
static void on_replace(GtkWidget *w, gpointer x) {
  GtkSourceSearchContext *c = search_ctx();
  if (!c) return;
  GtkTextIter s, e;
  if (gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(cur->buf), &s, &e))
    gtk_source_search_context_replace(c, &s, &e, gtk_entry_get_text(GTK_ENTRY(repl_e)), -1, NULL);
  find_step(TRUE);
}
static void on_replace_all(GtkWidget *w, gpointer x) {
  GtkSourceSearchContext *c = search_ctx();
  if (c) gtk_source_search_context_replace_all(c, gtk_entry_get_text(GTK_ENTRY(repl_e)), -1, NULL);
}
static void toggle_find(gboolean show) {
  gtk_revealer_set_reveal_child(GTK_REVEALER(fbar_rev), show);
  if (show) {
    GtkTextIter s, e;
    if (cur && gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(cur->buf), &s, &e)) {
      char *t = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(cur->buf), &s, &e, FALSE);
      if (!strchr(t, '\n')) gtk_entry_set_text(GTK_ENTRY(find_e), t);
      g_free(t);
    }
    gtk_widget_grab_focus(find_e);
  } else if (cur) gtk_widget_grab_focus(cur->view);
}
static void on_find_btn(GtkWidget *w, gpointer x) { toggle_find(!gtk_revealer_get_reveal_child(GTK_REVEALER(fbar_rev))); }

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer x) {
  gboolean ctrl = e->state & GDK_CONTROL_MASK, shift = e->state & GDK_SHIFT_MASK;
  guint k = gdk_keyval_to_lower(e->keyval);
  if (ctrl && k == GDK_KEY_s) { shift ? on_save_as(NULL, NULL) : on_save(NULL, NULL); return TRUE; }
  if (ctrl && k == GDK_KEY_o) { on_open(NULL, NULL); return TRUE; }
  if (ctrl && k == GDK_KEY_n) { on_new(NULL, NULL); return TRUE; }
  if (ctrl && k == GDK_KEY_w) { on_close(NULL, NULL); return TRUE; }
  if (ctrl && (k == GDK_KEY_f || k == GDK_KEY_h)) { toggle_find(TRUE); return TRUE; }
  if (ctrl && (k == GDK_KEY_plus || k == GDK_KEY_equal || k == GDK_KEY_KP_Add)) { on_zoom(NULL, GINT_TO_POINTER(1)); return TRUE; }
  if (ctrl && (k == GDK_KEY_minus || k == GDK_KEY_KP_Subtract)) { on_zoom(NULL, GINT_TO_POINTER(-1)); return TRUE; }
  if (k == GDK_KEY_Escape && gtk_revealer_get_reveal_child(GTK_REVEALER(fbar_rev))) { toggle_find(FALSE); return TRUE; }
  if (k == GDK_KEY_F3) { find_step(!shift); return TRUE; }
  return FALSE;
}
static void on_find_activate(GtkEntry *e, gpointer x) { find_step(TRUE); }

static GtkWidget *round_btn(const char *glyph, const char *tip, GCallback cb, gpointer d) {
  GtkWidget *b = gtk_button_new();
  add_class(b, "round");
  gtk_widget_set_tooltip_text(b, tip);
  gtk_container_add(GTK_CONTAINER(b), icon_new(glyph, R_ON_VAR, 20)->img);
  g_signal_connect(b, "clicked", cb, d);
  return b;
}

static void on_menu(GtkWidget *b, gpointer x) {
  GtkWidget *m = gtk_menu_new();
  const struct { const char *t; GCallback cb; gpointer d; } items[] = {
    {"Nouveau (Ctrl+N)", G_CALLBACK(on_new), NULL}, {"Ouvrir… (Ctrl+O)", G_CALLBACK(on_open), NULL},
    {"Enregistrer sous… (Ctrl+Maj+S)", G_CALLBACK(on_save_as), NULL}, {"Fermer le fichier (Ctrl+W)", G_CALLBACK(on_close), NULL},
    {"Agrandir le texte (Ctrl++)", G_CALLBACK(on_zoom), GINT_TO_POINTER(1)}, {"Réduire le texte (Ctrl+−)", G_CALLBACK(on_zoom), GINT_TO_POINTER(-1)},
  };
  for (guint i = 0; i < G_N_ELEMENTS(items); i++) {
    GtkWidget *mi = gtk_menu_item_new_with_label(items[i].t);
    g_signal_connect(mi, "activate", items[i].cb, items[i].d);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), mi);
  }
  GtkWidget *w = gtk_check_menu_item_new_with_label("Retour automatique à la ligne");
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), wrap);
  g_signal_connect(w, "toggled", G_CALLBACK(on_wrap), NULL);
  gtk_menu_shell_append(GTK_MENU_SHELL(m), w);
  gtk_widget_show_all(m);
  gtk_menu_popup_at_widget(GTK_MENU(m), b, GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, NULL);
}

/* Deposer des fichiers sur la fenetre : ils s'ouvrent */
static void on_drop(GtkWidget *w, GdkDragContext *c, gint x, gint y, GtkSelectionData *sd, guint info, guint t, gpointer d) {
  char **uris = gtk_selection_data_get_uris(sd);
  for (char **u = uris; u && *u; u++) { char *p = g_filename_from_uri(*u, NULL, NULL); if (p) open_path(p); g_free(p); }
  g_strfreev(uris);
  gtk_drag_finish(c, TRUE, FALSE, t);
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-text");
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, on_theme);
  docs = g_ptr_array_new();
  font_css = gtk_css_provider_new();
  apply_font();

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-text");
  gtk_window_set_default_size(GTK_WINDOW(win), 960, 640);
  g_signal_connect(win, "delete-event", G_CALLBACK(on_delete), NULL);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,maximize,close");
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), label("Texte", "apptitle", 0));
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  GtkWidget *more = gtk_button_new();
  add_class(more, "round");
  gtk_container_add(GTK_CONTAINER(more), icon_new("more", R_ON_VAR, 20)->img);
  g_signal_connect(more, "clicked", G_CALLBACK(on_menu), NULL);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), more);
  GtkWidget *sv = gtk_button_new_with_label("Enregistrer");
  add_class(sv, "tbtn");
  g_signal_connect(sv, "clicked", G_CALLBACK(on_save), NULL);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), sv);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), round_btn("search", "Rechercher (Ctrl+F)", G_CALLBACK(on_find_btn), NULL));
  redo_b = round_btn("fwd", "Rétablir (Ctrl+Maj+Z)", G_CALLBACK(on_redo), NULL);
  undo_b = round_btn("back", "Annuler (Ctrl+Z)", G_CALLBACK(on_undo), NULL);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), redo_b);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), undo_b);
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  add_class(side, "side");
  gtk_widget_set_size_request(side, 220, -1);
  GtkWidget *nb = gtk_button_new_with_label("+  Nouveau");
  add_class(nb, "newbtn");
  gtk_widget_set_halign(nb, GTK_ALIGN_START);
  g_signal_connect(nb, "clicked", G_CALLBACK(on_new), NULL);
  gtk_container_add(GTK_CONTAINER(side), nb);
  GtkWidget *ob = gtk_button_new_with_label("Ouvrir…");
  add_class(ob, "tbtn");
  gtk_widget_set_halign(ob, GTK_ALIGN_START);
  g_signal_connect(ob, "clicked", G_CALLBACK(on_open), NULL);
  gtk_container_add(GTK_CONTAINER(side), ob);
  list = gtk_list_box_new();
  g_signal_connect(list, "row-selected", G_CALLBACK(on_row), NULL);
  GtkWidget *lsc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(lsc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(lsc, TRUE);
  gtk_container_add(GTK_CONTAINER(lsc), list);
  gtk_container_add(GTK_CONTAINER(side), lsc);
  gtk_box_pack_start(GTK_BOX(body), side, FALSE, FALSE, 0);

  GtkWidget *ed = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(ed, "editor");
  fbar_rev = gtk_revealer_new();
  GtkWidget *fb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  add_class(fb, "fbar");
  find_e = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(find_e), "Rechercher");
  gtk_widget_set_hexpand(find_e, TRUE);
  g_signal_connect(find_e, "changed", G_CALLBACK(on_find_changed), NULL);
  g_signal_connect(find_e, "activate", G_CALLBACK(on_find_activate), NULL);
  gtk_container_add(GTK_CONTAINER(fb), find_e);
  gtk_container_add(GTK_CONTAINER(fb), round_btn("up", "Précédent (Maj+F3)", G_CALLBACK(on_find_prev), NULL));
  GtkWidget *dn = round_btn("down", "Suivant (F3)", G_CALLBACK(on_find_next), NULL);
  gtk_container_add(GTK_CONTAINER(fb), dn);
  repl_e = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(repl_e), "Remplacer par");
  gtk_widget_set_hexpand(repl_e, TRUE);
  gtk_container_add(GTK_CONTAINER(fb), repl_e);
  GtkWidget *rb = gtk_button_new_with_label("Remplacer");
  add_class(rb, "tbtn");
  g_signal_connect(rb, "clicked", G_CALLBACK(on_replace), NULL);
  gtk_container_add(GTK_CONTAINER(fb), rb);
  GtkWidget *rab = gtk_button_new_with_label("Tout");
  add_class(rab, "tbtn");
  g_signal_connect(rab, "clicked", G_CALLBACK(on_replace_all), NULL);
  gtk_container_add(GTK_CONTAINER(fb), rab);
  gtk_container_add(GTK_CONTAINER(fb), round_btn("close", "Fermer (Échap)", G_CALLBACK(on_find_btn), NULL));
  gtk_container_add(GTK_CONTAINER(fbar_rev), fb);
  gtk_container_add(GTK_CONTAINER(ed), fbar_rev);
  stack = gtk_stack_new();
  gtk_widget_set_vexpand(stack, TRUE);
  gtk_container_add(GTK_CONTAINER(ed), stack);
  status_l = label("", "status", 0);
  gtk_container_add(GTK_CONTAINER(ed), status_l);
  gtk_box_pack_start(GTK_BOX(body), ed, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(win), body);

  static const GtkTargetEntry URI_T[] = {{"text/uri-list", 0, 0}};
  gtk_drag_dest_set(list, GTK_DEST_DEFAULT_ALL, URI_T, 1, GDK_ACTION_COPY);
  g_signal_connect(list, "drag-data-received", G_CALLBACK(on_drop), NULL);
  gtk_widget_show_all(win);
  new_doc(NULL);
  for (int i = 1; i < argc; i++) {
    GFile *f = g_file_new_for_commandline_arg(argv[i]);
    char *p = g_file_get_path(f);
    if (p) open_path(p);
    g_free(p); g_object_unref(f);
  }
  select_doc(cur ? cur : docs->pdata[docs->len - 1]);
  gtk_main();
  return 0;
}

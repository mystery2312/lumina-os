/*
 * lumina-files : le gestionnaire de fichiers de LuminaOS.
 *
 * Mise en page de l'appli Fichiers d'Android / Aluminium, en Material 3 :
 * navigation a gauche (dossiers usuels, cles USB, corbeille), chemin en
 * pastilles et recherche en haut, grille ou liste de fichiers. GTK3 + GIO
 * uniquement ; meme palette, memes glyphes et meme theme que le bureau
 * (src/common/lumina-ui).
 *
 * La fenetre dessine elle-meme son cadre (CSD) : ses quatre coins sont
 * arrondis, ce que le compositeur ne fait pas pour les autres fenetres.
 *
 * Usage : lumina-files [dossier ou URI]
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <gio/gio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Style                                                               */
/* ------------------------------------------------------------------ */
static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  /* Fenetre : quatre coins arrondis, ombre douce ; droits si agrandie */
  "window.lumina-files.csd { border-radius: 0 0 16px 16px; background: @surface; }\n"
  "window.lumina-files.csd decoration { border-radius: 16px;\n"
  "  box-shadow: 0 6px 22px alpha(black, 0.18), 0 0 0 1px alpha(black, 0.06); }\n"
  "window.lumina-files.maximized, window.lumina-files.tiled,\n"
  "window.lumina-files.maximized decoration, window.lumina-files.tiled decoration { border-radius: 0; }\n"
  ".header { background: @surface; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px;\n"
  "          border-radius: 16px 16px 0 0; }\n"
  "window.maximized .header, window.tiled .header { border-radius: 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 32px; min-height: 32px;\n"
  "          color: @on_var; -gtk-icon-shadow: none; }\n"
  ".header button.titlebutton:hover { background: alpha(@on, 0.08); }\n"
  ".round { background: transparent; color: @on; border-radius: 20px; min-width: 36px; min-height: 36px; padding: 0; }\n"
  ".round:hover { background: alpha(@on, 0.08); }\n"
  ".round:disabled { opacity: 0.35; }\n"
  ".crumb { background: transparent; color: @on_var; border-radius: 16px; padding: 4px 10px; font-size: 15px; }\n"
  ".crumb:hover { background: alpha(@on, 0.08); }\n"
  ".crumb.current { color: @on; font-weight: 500; }\n"
  ".search { background: @c_high; border-radius: 20px; padding: 0 6px 0 12px; min-height: 38px; }\n"
  ".search entry { background: transparent; border: none; box-shadow: none; color: @on; caret-color: @pri; min-width: 200px; }\n"
  /* Navigation */
  ".nav { background: @surface; padding: 4px 12px 12px 12px; border-radius: 0 0 0 16px; }\n"
  "window.maximized .nav, window.tiled .nav { border-radius: 0; }\n"
  ".nav list, .nav row { background: transparent; }\n"
  ".nav row { border-radius: 24px; padding: 0; margin: 1px 0; }\n"
  ".nav row:hover { background: alpha(@on, 0.06); }\n"
  ".nav row:selected { background: @sec_c; color: @on_sec_c; }\n"
  ".nav .item { padding: 10px 14px; }\n"
  ".nav .item label { font-size: 13.5px; }\n"
  ".nav row:selected .item label { font-weight: 500; }\n"
  ".nav .section { color: @on_var; font-size: 12px; font-weight: 500; padding: 14px 16px 6px 16px; }\n"
  ".eject { background: transparent; border-radius: 16px; min-width: 28px; min-height: 28px; padding: 0; }\n"
  ".eject:hover { background: alpha(@on, 0.10); }\n"
  /* Contenu */
  ".content { background: @c_low; border-radius: 20px 0 16px 0; }\n"
  "window.maximized .content, window.tiled .content { border-radius: 20px 0 0 0; }\n"
  ".content flowbox { padding: 12px; }\n"
  ".content flowboxchild { background: transparent; border-radius: 16px; padding: 0; outline: none; }\n"
  ".content flowboxchild:hover { background: alpha(@on, 0.05); }\n"
  ".content flowboxchild:selected { background: @pri_c; color: @on_pri_c; }\n"
  ".tile { padding: 10px 6px 8px 6px; }\n"
  ".tile .name { font-size: 12.5px; }\n"
  ".row { padding: 6px 12px; }\n"
  ".row .name { font-size: 13.5px; }\n"
  ".row .meta { color: @on_var; font-size: 12px; }\n"
  "flowboxchild:selected .meta { color: @on_pri_c; }\n"
  ".fico { border-radius: 14px; }\n"
  ".fico.pri { background: @pri_c; color: @on_pri_c; }\n"
  ".fico.ter { background: @ter_c; color: @on_ter_c; }\n"
  ".fico.sec { background: @sec_c; color: @on_sec_c; }\n"
  ".empty { color: @on_var; font-size: 14px; }\n"
  /* Menu contextuel, dialogues, notification */
  "menu, .menu { background: @c_high; border: none; border-radius: 12px; padding: 6px 0; min-width: 210px; }\n"
  "window.popup decoration, .csd.popup decoration { border-radius: 12px; box-shadow: 0 3px 12px alpha(black, 0.20); }\n"
  "menuitem { color: @on; padding: 9px 20px; min-height: 0; }\n"
  "menuitem:hover { background: alpha(@on, 0.08); color: @on; }\n"
  "menuitem label { font-size: 13.5px; }\n"
  "menuitem.danger label { color: #ba1a1a; }\n"
  "window.dlg.csd, window.dlg.csd decoration { border-radius: 28px; }\n"
  "window.dlg { background: @c_high; }\n"
  /* Barre de titre vide (sert seulement a obtenir un cadre arrondi) : invisible */
  "window.dlg .titlebar { background: transparent; min-height: 0; padding: 0; margin: 0;\n"
  "                       border: none; box-shadow: none; }\n"
  ".dlg-body { padding: 24px; }\n"
  ".dlg-title { font-size: 22px; color: @on; }\n"
  ".dlg-text { color: @on_var; font-size: 13.5px; }\n"
  ".dlg entry { background: transparent; color: @on; border: 1px solid @outline_var; border-radius: 6px;\n"
  "             padding: 10px 12px; box-shadow: none; caret-color: @pri; }\n"
  ".dlg entry:focus { border: 2px solid @pri; padding: 9px 11px; }\n"
  ".tbtn { background: transparent; color: @pri; border-radius: 20px; padding: 8px 16px; font-weight: 500; }\n"
  ".tbtn:hover { background: alpha(@pri, 0.08); }\n"
  ".fbtn { background: @pri; color: @on_pri; border-radius: 20px; padding: 8px 20px; font-weight: 500; }\n"
  ".pickbar { background: @c_low; padding: 10px 16px 14px 20px; border-radius: 0 0 16px 0; }\n"
  ".pickname { background: @surface; color: @on; border: 1px solid @outline_var; border-radius: 8px; padding: 8px 12px; }\n"
  ".snack { background: @on; color: @surface; border-radius: 8px; padding: 6px 8px 6px 16px; margin: 16px;\n"
  "         box-shadow: 0 3px 10px alpha(black, 0.25); }\n"
  "window.qlwin.csd, window.qlwin.csd decoration { border-radius: 24px; }\n"
  "window.qlwin { background: @surface; }\n"
  "window.qlwin .titlebar { background: transparent; min-height: 0; padding: 0; margin: 0; border: none; box-shadow: none; }\n"
  ".ql { background: transparent; padding: 18px 20px; }\n"
  ".ql textview, .ql scrolledwindow { border-radius: 12px; }\n"
  ".ql .qltitle { font-size: 16px; color: @on; }\n"
  ".ql textview, .ql textview text { background: @c_low; color: @on; }\n"
  ".snack button { background: transparent; color: @pri_c; font-weight: 500; border-radius: 16px; padding: 6px 12px; }\n";

/* ------------------------------------------------------------------ */
/* Modele                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
  GFile *file;
  char *name;          /* nom affiche */
  GFileType type;
  char *ctype;         /* type MIME */
  goffset size;
  gint64 mtime;
  gboolean hidden;
  char *orig_path;     /* corbeille : emplacement d'origine */
} Entry;

static void entry_free(gpointer p) {
  Entry *e = p;
  g_object_unref(e->file);
  g_free(e->name); g_free(e->ctype); g_free(e->orig_path); g_free(e);
}

#define ATTRS "standard::name,standard::display-name,standard::type,standard::content-type," \
              "standard::size,standard::is-hidden,standard::is-backup,time::modified,trash::orig-path"

static GtkWidget *win, *crumbs, *back_btn, *fwd_btn, *search_entry, *view_btn, *nav, *flow, *scroll;
static GtkWidget *empty_box, *empty_l, *snack_rev, *snack_l, *snack_btn;
static Icon *view_ic;
static GFile *cur, *home, *trash;
static char *select_path;   /* --select : element a selectionner a l'ouverture */
/* Mode « choisir » (selecteur de fichiers) */
static gboolean pick_mode, pick_multi, pick_dir;
static char *pick_save, *pick_title, *pick_filter;
static GtkWidget *pick_btn, *pick_name;
static void pick_finish(void);
static void setup_drag(GtkWidget *child);
static void on_nav_drop(GtkWidget *w, GdkDragContext *ctx, gint x, gint y, GtkSelectionData *sd, guint info, guint t, gpointer d);
static void setup_drop(GtkWidget *w);
static GList *hist_back, *hist_fwd;
static GPtrArray *entries;
static gboolean show_hidden, list_mode;
static GCancellable *load_cancel;
static GFileMonitor *dir_mon;
static guint reload_timer, snack_timer;
static GList *clip;              /* GFile* copies ou coupees */
static gboolean clip_cut;
static GVolumeMonitor *volumes;

static void navigate(GFile *f, gboolean push);
static void load_dir(void);
static void snack(const char *text, const char *action, GCallback cb, gpointer d, GDestroyNotify free_d);

static gboolean in_trash(void) { return cur && g_file_has_uri_scheme(cur, "trash"); }

/* ------------------------------------------------------------------ */
/* Icones des fichiers                                                 */
/* ------------------------------------------------------------------ */
static void type_glyph(const char *ct, const char **glyph, const char **tone) {
  *glyph = "file"; *tone = "sec";
  if (!ct) return;
  if (g_str_has_prefix(ct, "image/"))       { *glyph = "photo"; *tone = "ter"; }
  else if (g_str_has_prefix(ct, "video/"))  { *glyph = "vid";   *tone = "ter"; }
  else if (g_str_has_prefix(ct, "audio/"))  { *glyph = "music"; *tone = "sec"; }
  else if (!strcmp(ct, "application/pdf"))  { *glyph = "pdf";   *tone = "pri"; }
  else if (g_str_has_prefix(ct, "text/") || strstr(ct, "document") || strstr(ct, "opendocument") ||
           strstr(ct, "msword") || strstr(ct, "spreadsheet") || strstr(ct, "presentation"))
                                            { *glyph = "docs";  *tone = "pri"; }
  else if (strstr(ct, "zip") || strstr(ct, "tar") || strstr(ct, "compressed") || strstr(ct, "7z") ||
           strstr(ct, "rar") || strstr(ct, "xz") || strstr(ct, "zstd") || strstr(ct, "gzip"))
                                            { *glyph = "archive"; *tone = "sec"; }
}

static int tone_role(const char *t) {
  return !strcmp(t, "ter") ? R_ON_TER_C : !strcmp(t, "sec") ? R_ON_SEC_C : R_ON_PRI_C;
}

/* Miniatures d'images : chargees dans un fil, recadrees en carre, dessinees
   avec des coins arrondis. Abandonnees si on change de dossier. */
typedef struct { char *path; int px; GtkWidget *box; GCancellable *cancel; } Thumb;

static void thumb_free(gpointer p) {
  Thumb *t = p;
  g_free(t->path); g_object_unref(t->box); g_object_unref(t->cancel); g_free(t);
}

static void thumb_thread(GTask *task, gpointer src, gpointer d, GCancellable *c) {
  Thumb *t = d;
  if (g_cancellable_is_cancelled(c)) { g_task_return_pointer(task, NULL, NULL); return; }
  int w = 0, h = 0;
  if (!gdk_pixbuf_get_file_info(t->path, &w, &h) || w <= 0 || h <= 0) { g_task_return_pointer(task, NULL, NULL); return; }
  double sc = (double)t->px / MIN(w, h);
  GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(t->path, MAX(1, (int)(w * sc)), MAX(1, (int)(h * sc)), FALSE, NULL);
  if (!pb) { g_task_return_pointer(task, NULL, NULL); return; }
  int pw = gdk_pixbuf_get_width(pb), ph = gdk_pixbuf_get_height(pb), s = MIN(pw, ph);
  GdkPixbuf *sq = gdk_pixbuf_new_subpixbuf(pb, (pw - s) / 2, (ph - s) / 2, s, s);
  GdkPixbuf *out = gdk_pixbuf_copy(sq);
  g_object_unref(sq); g_object_unref(pb);
  g_task_return_pointer(task, out, g_object_unref);
}

static gboolean draw_thumb(GtkWidget *w, cairo_t *cr, gpointer d) {
  GdkPixbuf *pb = d;
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), r = 14;
  cairo_new_sub_path(cr);
  cairo_arc(cr, W - r, r, r, -G_PI / 2, 0); cairo_arc(cr, W - r, H - r, r, 0, G_PI / 2);
  cairo_arc(cr, r, H - r, r, G_PI / 2, G_PI); cairo_arc(cr, r, r, r, G_PI, 3 * G_PI / 2);
  cairo_close_path(cr); cairo_clip(cr);
  double s = W / gdk_pixbuf_get_width(pb);
  cairo_scale(cr, s, s);
  gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
  cairo_paint(cr);
  return FALSE;
}

static void thumb_done(GObject *src, GAsyncResult *res, gpointer d) {
  Thumb *t = g_task_get_task_data(G_TASK(res));
  GdkPixbuf *pb = g_task_propagate_pointer(G_TASK(res), NULL);
  if (!pb) return;
  if (g_cancellable_is_cancelled(t->cancel) || g_object_get_data(G_OBJECT(t->box), "gone")) { g_object_unref(pb); return; }
  GList *kids = gtk_container_get_children(GTK_CONTAINER(t->box));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  GtkWidget *da = gtk_drawing_area_new();
  gtk_widget_set_size_request(da, t->px, t->px);
  g_signal_connect_data(da, "draw", G_CALLBACK(draw_thumb), pb, (GClosureNotify)(void (*)(void))g_object_unref, 0);
  gtk_container_add(GTK_CONTAINER(t->box), da);
  gtk_widget_show(da);
  set_class(t->box, "pri", FALSE); set_class(t->box, "ter", FALSE);
}

static void mark_gone(GtkWidget *w, gpointer d) { g_object_set_data(G_OBJECT(w), "gone", GINT_TO_POINTER(1)); }

static GtkWidget *entry_icon(Entry *e, int px) {
  if (e->type == G_FILE_TYPE_DIRECTORY) {
    GtkWidget *img = icon_new("folder", R_PRI, px)->img;
    gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
    return img;
  }
  const char *glyph, *tone;
  type_glyph(e->ctype, &glyph, &tone);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(box, "fico"); add_class(box, tone);
  gtk_widget_set_size_request(box, px, px);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  GtkWidget *g = icon_new(glyph, tone_role(tone), px / 2)->img;
  gtk_widget_set_vexpand(g, TRUE);
  gtk_container_add(GTK_CONTAINER(box), g);

  char *path = g_file_get_path(e->file);
  if (path && e->ctype && g_str_has_prefix(e->ctype, "image/") && e->size < 40 * 1024 * 1024) {
    Thumb *t = g_new0(Thumb, 1);
    t->path = path; t->px = px;
    t->box = g_object_ref(box); t->cancel = g_object_ref(load_cancel);
    g_signal_connect(box, "destroy", G_CALLBACK(mark_gone), NULL);
    GTask *task = g_task_new(NULL, t->cancel, thumb_done, NULL);
    g_task_set_task_data(task, t, thumb_free);
    g_task_run_in_thread(task, thumb_thread);
    g_object_unref(task);
  } else g_free(path);
  return box;
}

/* ------------------------------------------------------------------ */
/* Affichage du dossier                                                */
/* ------------------------------------------------------------------ */
static char *fmt_date(gint64 t) {
  if (!t) return g_strdup("");
  GDateTime *d = g_date_time_new_from_unix_local(t), *now = g_date_time_new_now_local();
  char *s = g_date_time_get_year(d) == g_date_time_get_year(now)
          ? g_date_time_format(d, "%e %b, %H:%M") : g_date_time_format(d, "%e %b %Y");
  g_date_time_unref(d); g_date_time_unref(now);
  return g_strstrip(s);
}

static GtkWidget *make_child(Entry *e) {
  GtkWidget *w;
  if (!list_mode) {
    w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    add_class(w, "tile");
    gtk_container_add(GTK_CONTAINER(w), entry_icon(e, 56));
    GtkWidget *n = label(e->name, "name", 0.5);
    gtk_label_set_justify(GTK_LABEL(n), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(n), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(n), 2);
    gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(n), 14);
    gtk_label_set_width_chars(GTK_LABEL(n), 14);
    gtk_container_add(GTK_CONTAINER(w), n);
  } else {
    w = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    add_class(w, "row");
    gtk_container_add(GTK_CONTAINER(w), entry_icon(e, 36));
    GtkWidget *n = label(e->name, "name", 0);
    gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(n, TRUE);
    gtk_container_add(GTK_CONTAINER(w), n);
    char *date = fmt_date(e->mtime);
    GtkWidget *dl = label(date, "meta", 1);
    gtk_label_set_width_chars(GTK_LABEL(dl), 16);
    gtk_container_add(GTK_CONTAINER(w), dl);
    char *size = e->type == G_FILE_TYPE_DIRECTORY ? g_strdup("—") : g_format_size(e->size);
    GtkWidget *sl = label(size, "meta", 1);
    gtk_label_set_width_chars(GTK_LABEL(sl), 9);
    gtk_container_add(GTK_CONTAINER(w), sl);
    g_free(date); g_free(size);
  }
  gtk_widget_set_tooltip_text(w, e->orig_path ? e->orig_path : e->name);
  return w;
}

static gboolean pick_match(Entry *e);
static gboolean flow_filter(GtkFlowBoxChild *c, gpointer d) {
  Entry *e = g_object_get_data(G_OBJECT(c), "entry");
  if (!e) return TRUE;
  if (!pick_match(e)) return FALSE;
  if (e->hidden && !show_hidden) return FALSE;
  const char *q = gtk_entry_get_text(GTK_ENTRY(search_entry));
  if (!*q) return TRUE;
  char *n = g_utf8_casefold(e->name, -1), *k = g_utf8_casefold(q, -1);
  gboolean ok = strstr(n, k) != NULL;
  g_free(n); g_free(k);
  return ok;
}

static void update_empty(void) {
  guint visible = 0;
  for (guint i = 0; entries && i < entries->len; i++) {
    Entry *e = entries->pdata[i];
    if (!e->hidden || show_hidden) visible++;
  }
  gboolean searching = *gtk_entry_get_text(GTK_ENTRY(search_entry));
  gtk_label_set_text(GTK_LABEL(empty_l), in_trash() ? "La corbeille est vide" : "Ce dossier est vide");
  gtk_widget_set_visible(empty_box, visible == 0 && !searching);
  gtk_widget_set_visible(gtk_widget_get_parent(empty_box), visible == 0 && !searching);
  gtk_widget_set_visible(scroll, visible > 0 || searching);
}

static void rebuild_view(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(flow));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);

  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), !list_mode);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), list_mode ? 1 : 20);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
  for (guint i = 0; i < entries->len; i++) {
    Entry *e = entries->pdata[i];
    /* GtkFlowBoxChild n'a pas de fenetre GDK : une source de glisser doit
       en avoir une, d'ou la boite d'evenements (invisible) autour. */
    GtkWidget *c = make_child(e), *eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb), FALSE);
    gtk_container_add(GTK_CONTAINER(eb), c);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), eb, -1);
    GtkWidget *child = gtk_widget_get_parent(eb);
    g_object_set_data(G_OBJECT(child), "entry", e);
    setup_drag(eb);
  }
  gtk_widget_show_all(flow);
  gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(flow));
  update_empty();
  /* « Afficher dans le dossier » : l'element demande est selectionne */
  if (select_path) {
    GList *kids = gtk_container_get_children(GTK_CONTAINER(flow));
    for (GList *l = kids; l; l = l->next) {
      Entry *e = g_object_get_data(G_OBJECT(l->data), "entry");
      char *p = e ? g_file_get_path(e->file) : NULL;
      if (p && !strcmp(p, select_path)) {
        gtk_flow_box_select_child(GTK_FLOW_BOX(flow), l->data);
        gtk_widget_grab_focus(l->data);
      }
      g_free(p);
    }
    g_list_free(kids);
    g_clear_pointer(&select_path, g_free);
  }
}

static gint entry_cmp(gconstpointer a, gconstpointer b) {
  const Entry *x = *(Entry **)a, *y = *(Entry **)b;
  gboolean dx = x->type == G_FILE_TYPE_DIRECTORY, dy = y->type == G_FILE_TYPE_DIRECTORY;
  if (dx != dy) return dx ? -1 : 1;
  char *kx = g_utf8_collate_key_for_filename(x->name, -1), *ky = g_utf8_collate_key_for_filename(y->name, -1);
  int r = strcmp(kx, ky);
  g_free(kx); g_free(ky);
  return r;
}

typedef struct { GPtrArray *acc; GCancellable *cancel; } Load;

static void on_next_files(GObject *src, GAsyncResult *res, gpointer d) {
  GFileEnumerator *en = G_FILE_ENUMERATOR(src);
  Load *ld = d;
  GError *err = NULL;
  GList *infos = g_file_enumerator_next_files_finish(en, res, &err);
  if (err || g_cancellable_is_cancelled(ld->cancel)) {
    g_clear_error(&err);
    g_list_free_full(infos, g_object_unref);
    g_ptr_array_free(ld->acc, TRUE); g_object_unref(ld->cancel); g_free(ld);
    g_object_unref(en);
    return;
  }
  for (GList *l = infos; l; l = l->next) {
    GFileInfo *fi = l->data;
    Entry *e = g_new0(Entry, 1);
    e->file = g_file_enumerator_get_child(en, fi);
    e->name = g_strdup(g_file_info_get_display_name(fi));
    e->type = g_file_info_get_file_type(fi);
    e->ctype = g_strdup(g_file_info_get_content_type(fi));
    e->size = g_file_info_get_size(fi);
    GDateTime *mt = g_file_info_get_modification_date_time(fi);
    if (mt) { e->mtime = g_date_time_to_unix(mt); g_date_time_unref(mt); }
    e->hidden = g_file_info_get_is_hidden(fi) || g_file_info_get_is_backup(fi);
    if (g_file_info_has_attribute(fi, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH))
      e->orig_path = g_strdup(g_file_info_get_attribute_byte_string(fi, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH));
    g_ptr_array_add(ld->acc, e);
  }
  if (infos) {
    g_list_free_full(infos, g_object_unref);
    g_file_enumerator_next_files_async(en, 200, G_PRIORITY_DEFAULT, ld->cancel, on_next_files, ld);
    return;
  }
  g_object_unref(en);
  g_ptr_array_sort(ld->acc, entry_cmp);
  if (entries) g_ptr_array_free(entries, TRUE);
  entries = ld->acc;
  g_object_unref(ld->cancel); g_free(ld);
  rebuild_view();
}

static void on_enum_ready(GObject *src, GAsyncResult *res, gpointer d) {
  Load *ld = d;
  GError *err = NULL;
  GFileEnumerator *en = g_file_enumerate_children_finish(G_FILE(src), res, &err);
  if (!en) {
    if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      if (entries) g_ptr_array_set_size(entries, 0);
      rebuild_view();
      char *m = g_strdup_printf("Impossible d'ouvrir ce dossier : %s", err->message);
      gtk_label_set_text(GTK_LABEL(empty_l), m);
      gtk_widget_show(empty_box); gtk_widget_hide(scroll);
      g_free(m);
    }
    g_clear_error(&err);
    g_ptr_array_free(ld->acc, TRUE); g_object_unref(ld->cancel); g_free(ld);
    return;
  }
  g_file_enumerator_next_files_async(en, 200, G_PRIORITY_DEFAULT, ld->cancel, on_next_files, ld);
}

static gboolean reload_cb(gpointer d) { reload_timer = 0; load_dir(); return G_SOURCE_REMOVE; }

/* Le dossier change sur le disque (copie, telechargement...) : on recharge,
   regroupe sur 300 ms pour ne pas tout refaire a chaque fichier. */
static void on_dir_changed(GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent ev, gpointer d) {
  if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED) return;
  if (!reload_timer) reload_timer = g_timeout_add(300, reload_cb, NULL);
}

static void load_dir(void) {
  if (load_cancel) { g_cancellable_cancel(load_cancel); g_object_unref(load_cancel); }
  load_cancel = g_cancellable_new();
  if (dir_mon) { g_file_monitor_cancel(dir_mon); g_object_unref(dir_mon); }
  dir_mon = g_file_monitor_directory(cur, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
  if (dir_mon) g_signal_connect(dir_mon, "changed", G_CALLBACK(on_dir_changed), NULL);
  Load *ld = g_new0(Load, 1);
  ld->acc = g_ptr_array_new_with_free_func(entry_free);
  ld->cancel = g_object_ref(load_cancel);
  g_file_enumerate_children_async(cur, ATTRS, G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
                                  load_cancel, on_enum_ready, ld);
}

/* ------------------------------------------------------------------ */
/* Chemin en pastilles et navigation                                   */
/* ------------------------------------------------------------------ */
static void on_crumb(GtkButton *b, gpointer d) { navigate(G_FILE(d), TRUE); }

static const char *special_name(GFile *f) {
  if (g_file_equal(f, home)) return "Mes fichiers";
  if (g_file_equal(f, trash)) return "Corbeille";
  return NULL;
}

static void update_crumbs(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(crumbs));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);

  /* Remonte jusqu'au dossier personnel, a la racine d'une cle USB, ou a / */
  GList *chain = NULL;
  GFile *f = g_object_ref(cur);
  while (f) {
    chain = g_list_prepend(chain, f);
    if (special_name(f)) break;
    GMount *m = g_file_find_enclosing_mount(f, NULL, NULL);
    if (m) {
      GFile *root = g_mount_get_root(m);
      gboolean at_root = g_file_equal(root, f);
      g_object_unref(root); g_object_unref(m);
      if (at_root) break;
    }
    f = g_file_get_parent(f);
  }
  for (GList *l = chain; l; l = l->next) {
    GFile *c = l->data;
    char *name = NULL;
    const char *sp = special_name(c);
    if (sp) name = g_strdup(sp);
    else {
      GMount *m = g_file_find_enclosing_mount(c, NULL, NULL);
      GFile *root = m ? g_mount_get_root(m) : NULL;
      if (root && g_file_equal(root, c)) name = g_mount_get_name(m);
      if (root) g_object_unref(root);
      if (m) g_object_unref(m);
      if (!name) {
        GFileInfo *fi = g_file_query_info(c, "standard::display-name", 0, NULL, NULL);
        name = fi ? g_strdup(g_file_info_get_display_name(fi)) : g_file_get_basename(c);
        if (fi) g_object_unref(fi);
      }
    }
    if (l != chain) {
      GtkWidget *sep = icon_new("chev", R_ON_VAR, 16)->img;
      gtk_container_add(GTK_CONTAINER(crumbs), sep);
    }
    GtkWidget *b = gtk_button_new_with_label(name);
    add_class(b, "crumb");
    if (!l->next) add_class(b, "current");
    g_signal_connect_data(b, "clicked", G_CALLBACK(on_crumb), g_object_ref(c),
                          (GClosureNotify)(void (*)(void))g_object_unref, 0);
    gtk_container_add(GTK_CONTAINER(crumbs), b);
    g_free(name);
  }
  g_list_free_full(chain, g_object_unref);
  gtk_widget_show_all(crumbs);

  char *title = g_strdup_printf("%s — Fichiers", special_name(cur) ? special_name(cur) : g_file_peek_path(cur) ? g_file_peek_path(cur) : "");
  gtk_window_set_title(GTK_WINDOW(win), title);
  g_free(title);
}

static void update_nav_selection(void) {
  GList *rows = gtk_container_get_children(GTK_CONTAINER(nav));
  GtkListBoxRow *sel = NULL;
  for (GList *l = rows; l; l = l->next) {
    GFile *f = g_object_get_data(G_OBJECT(l->data), "file");
    if (f && g_file_equal(f, cur)) sel = l->data;
  }
  g_list_free(rows);
  if (sel) gtk_list_box_select_row(GTK_LIST_BOX(nav), sel);
  else gtk_list_box_unselect_all(GTK_LIST_BOX(nav));
}

static void navigate(GFile *f, gboolean push) {
  if (cur && g_file_equal(cur, f)) return;
  if (cur && push) {
    hist_back = g_list_prepend(hist_back, g_object_ref(cur));
    g_list_free_full(hist_fwd, g_object_unref); hist_fwd = NULL;
  }
  g_set_object(&cur, f);
  gtk_entry_set_text(GTK_ENTRY(search_entry), "");
  load_dir();
  update_crumbs();
  update_nav_selection();
  gtk_widget_set_sensitive(back_btn, hist_back != NULL);
  gtk_widget_set_sensitive(fwd_btn, hist_fwd != NULL);
}

static void go_back(void) {
  if (!hist_back) return;
  GFile *f = hist_back->data;
  hist_back = g_list_delete_link(hist_back, hist_back);
  hist_fwd = g_list_prepend(hist_fwd, g_object_ref(cur));
  navigate(f, FALSE);
  g_object_unref(f);
  gtk_widget_set_sensitive(back_btn, hist_back != NULL);
  gtk_widget_set_sensitive(fwd_btn, TRUE);
}

static void go_fwd(void) {
  if (!hist_fwd) return;
  GFile *f = hist_fwd->data;
  hist_fwd = g_list_delete_link(hist_fwd, hist_fwd);
  hist_back = g_list_prepend(hist_back, g_object_ref(cur));
  navigate(f, FALSE);
  g_object_unref(f);
  gtk_widget_set_sensitive(back_btn, TRUE);
  gtk_widget_set_sensitive(fwd_btn, hist_fwd != NULL);
}

static void go_up(void) {
  if (special_name(cur)) return;
  GFile *p = g_file_get_parent(cur);
  if (p) { navigate(p, TRUE); g_object_unref(p); }
}

/* ------------------------------------------------------------------ */
/* Panneau de navigation                                               */
/* ------------------------------------------------------------------ */
static void rebuild_nav(void);

static void on_mounted(GObject *src, GAsyncResult *res, gpointer d) {
  GError *err = NULL;
  if (!g_volume_mount_finish(G_VOLUME(src), res, &err)) {
    snack(err->message, NULL, NULL, NULL, NULL);
    g_clear_error(&err);
    return;
  }
  GMount *m = g_volume_get_mount(G_VOLUME(src));
  if (m) { GFile *root = g_mount_get_root(m); navigate(root, TRUE); g_object_unref(root); g_object_unref(m); }
}

static void on_ejected(GObject *src, GAsyncResult *res, gpointer d) {
  GError *err = NULL;
  gboolean ok = g_mount_can_eject(G_MOUNT(src)) ? g_mount_eject_with_operation_finish(G_MOUNT(src), res, &err)
                                                : g_mount_unmount_with_operation_finish(G_MOUNT(src), res, &err);
  if (!ok) { snack(err->message, NULL, NULL, NULL, NULL); g_clear_error(&err); }
  else snack("Vous pouvez retirer l'appareil en toute sécurité.", NULL, NULL, NULL, NULL);
}

static void on_eject_clicked(GtkButton *b, gpointer d) {
  GMount *m = d;
  GFile *root = g_mount_get_root(m);
  if (g_file_has_prefix(cur, root) || g_file_equal(cur, root)) navigate(home, TRUE);
  g_object_unref(root);
  GMountOperation *op = gtk_mount_operation_new(GTK_WINDOW(win));
  if (g_mount_can_eject(m)) g_mount_eject_with_operation(m, G_MOUNT_UNMOUNT_NONE, op, NULL, on_ejected, NULL);
  else g_mount_unmount_with_operation(m, G_MOUNT_UNMOUNT_NONE, op, NULL, on_ejected, NULL);
  g_object_unref(op);
}

static void on_nav_row(GtkListBox *lb, GtkListBoxRow *row, gpointer d) {
  GFile *f = g_object_get_data(G_OBJECT(row), "file");
  GVolume *v = g_object_get_data(G_OBJECT(row), "volume");
  if (f) navigate(f, TRUE);
  else if (v) {
    GMountOperation *op = gtk_mount_operation_new(GTK_WINDOW(win));
    g_volume_mount(v, G_MOUNT_MOUNT_NONE, op, NULL, on_mounted, NULL);
    g_object_unref(op);
  }
}

static GtkWidget *nav_row(const char *glyph, const char *text, GFile *f, GVolume *v, GMount *ejectable) {
  GtkWidget *row = gtk_list_box_row_new();
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  add_class(h, "item");
  gtk_container_add(GTK_CONTAINER(h), icon_new(glyph, R_ON_VAR, 20)->img);
  GtkWidget *l = label(text, NULL, 0);
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(l, TRUE);
  gtk_container_add(GTK_CONTAINER(h), l);
  if (ejectable) {
    GtkWidget *e = gtk_button_new();
    add_class(e, "eject");
    gtk_widget_set_tooltip_text(e, "Éjecter");
    gtk_container_add(GTK_CONTAINER(e), icon_new("eject", R_ON_VAR, 16)->img);
    g_signal_connect_data(e, "clicked", G_CALLBACK(on_eject_clicked), g_object_ref(ejectable),
                          (GClosureNotify)(void (*)(void))g_object_unref, 0);
    gtk_container_add(GTK_CONTAINER(h), e);
  }
  gtk_container_add(GTK_CONTAINER(row), h);
  if (f) {
    g_object_set_data_full(G_OBJECT(row), "file", g_object_ref(f), g_object_unref);
    /* Deposer des fichiers sur un dossier du panneau : ils y vont */
    static const GtkTargetEntry URI_T[] = {{"text/uri-list", 0, 0}};
    gtk_drag_dest_set(row, GTK_DEST_DEFAULT_ALL, URI_T, 1, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(row, "drag-data-received", G_CALLBACK(on_nav_drop), NULL);
  }
  if (v) g_object_set_data_full(G_OBJECT(row), "volume", g_object_ref(v), g_object_unref);
  return row;
}

static GtkWidget *nav_section(const char *text) {
  GtkWidget *row = gtk_list_box_row_new();
  gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
  gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
  gtk_container_add(GTK_CONTAINER(row), label(text, "section", 0));
  return row;
}

static void rebuild_nav(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(nav));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);

  gtk_container_add(GTK_CONTAINER(nav), nav_row("home", "Mes fichiers", home, NULL, NULL));
  const struct { GUserDirectory dir; const char *glyph, *fallback; } places[] = {
    {G_USER_DIRECTORY_DOWNLOAD,  "download", "Téléchargements"},
    {G_USER_DIRECTORY_DOCUMENTS, "docs",     "Documents"},
    {G_USER_DIRECTORY_PICTURES,  "photo",    "Images"},
    {G_USER_DIRECTORY_VIDEOS,    "vid",      "Vidéos"},
    {G_USER_DIRECTORY_MUSIC,     "music",    "Musique"},
  };
  for (guint i = 0; i < G_N_ELEMENTS(places); i++) {
    const char *p = g_get_user_special_dir(places[i].dir);
    if (!p || !g_strcmp0(p, g_get_home_dir()) || !g_file_test(p, G_FILE_TEST_IS_DIR)) continue;
    GFile *f = g_file_new_for_path(p);
    char *name = g_path_get_basename(p);
    gtk_container_add(GTK_CONTAINER(nav), nav_row(places[i].glyph, name, f, NULL, NULL));
    g_free(name); g_object_unref(f);
  }

  /* Cles USB, cartes SD, disques externes */
  GList *mounts = g_volume_monitor_get_mounts(volumes), *vols = g_volume_monitor_get_volumes(volumes);
  gboolean header = FALSE;
  for (GList *l = mounts; l; l = l->next) {
    GMount *m = l->data;
    if (g_mount_is_shadowed(m) || !(g_mount_can_unmount(m) || g_mount_can_eject(m))) continue;
    if (!header) { gtk_container_add(GTK_CONTAINER(nav), nav_section("Appareils")); header = TRUE; }
    GFile *root = g_mount_get_root(m);
    char *name = g_mount_get_name(m);
    gtk_container_add(GTK_CONTAINER(nav), nav_row("usb", name, root, NULL, m));
    g_free(name); g_object_unref(root);
  }
  for (GList *l = vols; l; l = l->next) {
    GVolume *v = l->data;
    GMount *m = g_volume_get_mount(v);
    if (m) { g_object_unref(m); continue; }
    if (!g_volume_can_mount(v)) continue;
    if (!header) { gtk_container_add(GTK_CONTAINER(nav), nav_section("Appareils")); header = TRUE; }
    char *name = g_volume_get_name(v);
    gtk_container_add(GTK_CONTAINER(nav), nav_row("usb", name, NULL, v, NULL));
    g_free(name);
  }
  g_list_free_full(mounts, g_object_unref); g_list_free_full(vols, g_object_unref);

  gtk_container_add(GTK_CONTAINER(nav), nav_section(""));
  gtk_container_add(GTK_CONTAINER(nav), nav_row("trash", "Corbeille", trash, NULL, NULL));
  gtk_widget_show_all(nav);
  if (cur) update_nav_selection();
}

static void on_volumes_changed(GVolumeMonitor *vm, gpointer obj, gpointer d) { rebuild_nav(); }

/* ------------------------------------------------------------------ */
/* Notification (snackbar)                                             */
/* ------------------------------------------------------------------ */
static gulong snack_handler;

static gboolean snack_hide(gpointer d) {
  snack_timer = 0;
  gtk_revealer_set_reveal_child(GTK_REVEALER(snack_rev), FALSE);
  return G_SOURCE_REMOVE;
}

static void snack(const char *text, const char *action, GCallback cb, gpointer d, GDestroyNotify free_d) {
  gtk_label_set_text(GTK_LABEL(snack_l), text);
  if (snack_handler) { g_signal_handler_disconnect(snack_btn, snack_handler); snack_handler = 0; }
  gtk_widget_set_visible(snack_btn, action != NULL);
  if (action) {
    gtk_button_set_label(GTK_BUTTON(snack_btn), action);
    snack_handler = g_signal_connect_data(snack_btn, "clicked", cb, d, (GClosureNotify)(void (*)(void))free_d, 0);
  }
  gtk_revealer_set_reveal_child(GTK_REVEALER(snack_rev), TRUE);
  if (snack_timer) g_source_remove(snack_timer);
  snack_timer = g_timeout_add_seconds(action ? 6 : 4, snack_hide, NULL);
}

/* ------------------------------------------------------------------ */
/* Operations sur les fichiers (dans un fil : l'interface ne gele pas)  */
/* ------------------------------------------------------------------ */
static GList *selected_entries(void) {
  GList *sel = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(flow)), *r = NULL;
  for (GList *l = sel; l; l = l->next) {
    Entry *e = g_object_get_data(G_OBJECT(l->data), "entry");
    if (e) r = g_list_append(r, e);
  }
  g_list_free(sel);
  return r;
}

/* "Photo.jpg" -> "Photo (copie).jpg", "Photo (copie 2).jpg"... */
static GFile *unique_target(GFile *dir, const char *base, gboolean is_dir) {
  GFile *t = g_file_get_child(dir, base);
  if (!g_file_query_exists(t, NULL)) return t;
  g_object_unref(t);
  const char *dot = is_dir || base[0] == '.' ? NULL : strrchr(base, '.');
  char *stem = dot ? g_strndup(base, dot - base) : g_strdup(base);
  for (int n = 1;; n++) {
    char *name = n == 1 ? g_strdup_printf("%s (copie)%s", stem, dot ? dot : "")
                        : g_strdup_printf("%s (copie %d)%s", stem, n, dot ? dot : "");
    t = g_file_get_child(dir, name);
    g_free(name);
    if (!g_file_query_exists(t, NULL)) break;
    g_object_unref(t);
  }
  g_free(stem);
  return t;
}

static gboolean copy_recursive(GFile *src, GFile *dst, GError **err) {
  GFileType t = g_file_query_file_type(src, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
  if (t != G_FILE_TYPE_DIRECTORY)
    return g_file_copy(src, dst, G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA, NULL, NULL, NULL, err);
  if (!g_file_make_directory(dst, NULL, err)) return FALSE;
  GFileEnumerator *en = g_file_enumerate_children(src, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err);
  if (!en) return FALSE;
  gboolean ok = TRUE;
  GFileInfo *fi;
  while (ok && (fi = g_file_enumerator_next_file(en, NULL, err))) {
    GFile *s = g_file_get_child(src, g_file_info_get_name(fi)), *d = g_file_get_child(dst, g_file_info_get_name(fi));
    ok = copy_recursive(s, d, err);
    g_object_unref(s); g_object_unref(d); g_object_unref(fi);
  }
  g_object_unref(en);
  return ok && !(err && *err);
}

static gboolean delete_recursive(GFile *f, GError **err) {
  GFileType t = g_file_query_file_type(f, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
  if (t == G_FILE_TYPE_DIRECTORY) {
    GFileEnumerator *en = g_file_enumerate_children(f, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err);
    if (!en) return FALSE;
    GFileInfo *fi;
    gboolean ok = TRUE;
    while (ok && (fi = g_file_enumerator_next_file(en, NULL, err))) {
      GFile *c = g_file_get_child(f, g_file_info_get_name(fi));
      ok = delete_recursive(c, err);
      g_object_unref(c); g_object_unref(fi);
    }
    g_object_unref(en);
    if (!ok) return FALSE;
  }
  return g_file_delete(f, NULL, err);
}

typedef enum { OP_COPY, OP_MOVE, OP_TRASH, OP_DELETE, OP_RESTORE } OpKind;
typedef struct {
  OpKind kind;
  GList *files;        /* GFile* sources */
  GList *origs;        /* OP_RESTORE : chemins d'origine (char*) */
  GFile *dest;         /* dossier cible (copie/deplacement) */
  int done, failed;
  char *error;
  GList *trashed;      /* OP_TRASH : chemins mis a la corbeille (pour Annuler) */
} Op;

static void op_free(gpointer p) {
  Op *o = p;
  g_list_free_full(o->files, g_object_unref);
  g_list_free_full(o->origs, g_free);
  g_list_free_full(o->trashed, g_free);
  if (o->dest) g_object_unref(o->dest);
  g_free(o->error); g_free(o);
}

static void op_fail(Op *o, GError *err) {
  o->failed++;
  if (!o->error && err) o->error = g_strdup(err->message);
}

static void op_thread(GTask *task, gpointer src, gpointer d, GCancellable *c) {
  Op *o = d;
  GList *orig = o->origs;
  for (GList *l = o->files; l; l = l->next, orig = orig ? orig->next : NULL) {
    GFile *f = l->data;
    GError *err = NULL;
    gboolean ok = FALSE;
    if (o->kind == OP_COPY || o->kind == OP_MOVE) {
      char *base = g_file_get_basename(f);
      gboolean is_dir = g_file_query_file_type(f, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) == G_FILE_TYPE_DIRECTORY;
      GFile *parent = g_file_get_parent(f);
      if (o->kind == OP_MOVE && parent && g_file_equal(parent, o->dest)) ok = TRUE;   /* deja ici */
      else {
        GFile *t = unique_target(o->dest, base, is_dir);
        if (o->kind == OP_MOVE) {
          ok = g_file_move(f, t, G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA, NULL, NULL, NULL, &err);
          /* Dossier vers un autre disque : copie puis suppression */
          if (!ok && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE)) {
            g_clear_error(&err);
            ok = copy_recursive(f, t, &err) && delete_recursive(f, &err);
          }
        } else ok = copy_recursive(f, t, &err);
        g_object_unref(t);
      }
      if (parent) g_object_unref(parent);
      g_free(base);
    } else if (o->kind == OP_TRASH) {
      ok = g_file_trash(f, NULL, &err);
      if (ok) { char *p = g_file_get_path(f); if (p) o->trashed = g_list_append(o->trashed, p); }
    } else if (o->kind == OP_DELETE) {
      ok = g_file_has_uri_scheme(f, "trash") ? g_file_delete(f, NULL, &err) : delete_recursive(f, &err);
    } else if (o->kind == OP_RESTORE && orig) {
      GFile *t = g_file_new_for_path(orig->data), *parent = g_file_get_parent(t);
      if (parent) g_file_make_directory_with_parents(parent, NULL, NULL);
      ok = g_file_move(f, t, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &err);
      if (parent) g_object_unref(parent);
      g_object_unref(t);
    }
    if (ok) o->done++; else op_fail(o, err);
    g_clear_error(&err);
  }
  g_task_return_boolean(task, TRUE);
}

static void run_op(Op *o);

/* Annuler une mise a la corbeille : on retrouve les elements par leur
   emplacement d'origine et on les remet en place. */
static void undo_trash(GtkButton *b, gpointer d) {
  GList *paths = d;
  GFileEnumerator *en = g_file_enumerate_children(trash, "standard::name,trash::orig-path,trash::deletion-date", 0, NULL, NULL);
  if (!en) return;
  Op *o = g_new0(Op, 1);
  o->kind = OP_RESTORE;
  GFileInfo *fi;
  while ((fi = g_file_enumerator_next_file(en, NULL, NULL))) {
    const char *op = g_file_info_get_attribute_byte_string(fi, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
    if (op && g_list_find_custom(paths, op, (GCompareFunc)g_strcmp0)) {
      o->files = g_list_append(o->files, g_file_enumerator_get_child(en, fi));
      o->origs = g_list_append(o->origs, g_strdup(op));
    }
    g_object_unref(fi);
  }
  g_object_unref(en);
  gtk_revealer_set_reveal_child(GTK_REVEALER(snack_rev), FALSE);
  if (o->files) run_op(o); else op_free(o);
}

static void free_path_list(gpointer d) { g_list_free_full(d, g_free); }

static const char *plural(int n, const char *one, const char *many) { return n > 1 ? many : one; }

static void op_done(GObject *src, GAsyncResult *res, gpointer d) {
  Op *o = g_task_get_task_data(G_TASK(res));
  char *msg = NULL;
  int n = o->done;
  if (o->failed && !o->done) msg = g_strdup_printf("Échec : %s", o->error ? o->error : "erreur inconnue");
  else switch (o->kind) {
    case OP_COPY:    msg = g_strdup_printf("%d %s", n, plural(n, "élément copié", "éléments copiés")); break;
    case OP_MOVE:    msg = g_strdup_printf("%d %s", n, plural(n, "élément déplacé", "éléments déplacés")); break;
    case OP_DELETE:  msg = g_strdup_printf("%d %s", n, plural(n, "élément supprimé définitivement", "éléments supprimés définitivement")); break;
    case OP_RESTORE: msg = g_strdup_printf("%d %s", n, plural(n, "élément restauré", "éléments restaurés")); break;
    case OP_TRASH:   msg = g_strdup_printf("%d %s", n, plural(n, "élément mis à la corbeille", "éléments mis à la corbeille")); break;
  }
  if (o->failed && o->done) {
    char *m2 = g_strdup_printf("%s, %d en échec", msg, o->failed);
    g_free(msg); msg = m2;
  }
  if (o->kind == OP_TRASH && o->trashed) {
    snack(msg, "Annuler", G_CALLBACK(undo_trash), o->trashed, free_path_list);
    o->trashed = NULL;
  } else snack(msg, NULL, NULL, NULL, NULL);
  g_free(msg);
  load_dir();
  rebuild_nav();   /* un dossier usuel a pu disparaitre ou revenir */
}

static void run_op(Op *o) {
  GTask *task = g_task_new(NULL, NULL, op_done, NULL);
  g_task_set_task_data(task, o, op_free);
  g_task_run_in_thread(task, op_thread);
  g_object_unref(task);
}

static Op *op_from_selection(OpKind kind) {
  GList *sel = selected_entries();
  if (!sel) return NULL;
  Op *o = g_new0(Op, 1);
  o->kind = kind;
  for (GList *l = sel; l; l = l->next) {
    Entry *e = l->data;
    o->files = g_list_append(o->files, g_object_ref(e->file));
    if (kind == OP_RESTORE) o->origs = g_list_append(o->origs, g_strdup(e->orig_path));
  }
  g_list_free(sel);
  return o;
}

/* ------------------------------------------------------------------ */
/* Dialogues Material                                                  */
/* ------------------------------------------------------------------ */
typedef void (*DialogCb)(const char *text, gpointer d);
typedef struct { GtkWidget *w, *entry; DialogCb cb; gpointer d; } Dialog;

static void dialog_ok(GtkWidget *b, gpointer p) {
  Dialog *dg = p;
  char *t = g_strdup(dg->entry ? gtk_entry_get_text(GTK_ENTRY(dg->entry)) : "");
  if (dg->entry && !*g_strstrip(t)) { g_free(t); return; }
  dg->cb(t, dg->d);
  g_free(t);
  gtk_widget_destroy(dg->w);
}

static void dialog_cancel(GtkWidget *b, gpointer p) { gtk_widget_destroy(((Dialog *)p)->w); }

static gboolean dialog_key(GtkWidget *w, GdkEventKey *e, gpointer p) {
  if (e->keyval == GDK_KEY_Escape) { gtk_widget_destroy(w); return TRUE; }
  return FALSE;
}

/* initial : texte prerempli (NULL = pas de champ). select_stem : selectionne
   le nom sans l'extension, comme sur Android. */
static void dialog_open(const char *title, const char *text, const char *initial, gboolean select_stem,
                        const char *action, gboolean danger, DialogCb cb, gpointer d) {
  Dialog *dg = g_new0(Dialog, 1);
  dg->cb = cb; dg->d = d;
  dg->w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(dg->w, "dlg");
  gtk_window_set_transient_for(GTK_WINDOW(dg->w), GTK_WINDOW(win));
  gtk_window_set_modal(GTK_WINDOW(dg->w), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dg->w), FALSE);
  gtk_window_set_title(GTK_WINDOW(dg->w), title);
  GtkWidget *tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_window_set_titlebar(GTK_WINDOW(dg->w), tb);   /* cadre dessine par nous, sans barre */
  g_object_set_data_full(G_OBJECT(dg->w), "dlg", dg, g_free);
  g_signal_connect(dg->w, "key-press-event", G_CALLBACK(dialog_key), NULL);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  add_class(v, "dlg-body");
  gtk_widget_set_size_request(v, 360, -1);
  gtk_container_add(GTK_CONTAINER(v), label(title, "dlg-title", 0));
  if (text) {
    GtkWidget *t = label(text, "dlg-text", 0);
    gtk_label_set_line_wrap(GTK_LABEL(t), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(t), 40);
    gtk_container_add(GTK_CONTAINER(v), t);
  }
  if (initial) {
    dg->entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(dg->entry), initial);
    g_signal_connect(dg->entry, "activate", G_CALLBACK(dialog_ok), dg);
    gtk_container_add(GTK_CONTAINER(v), dg->entry);
  }
  GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(bb, GTK_ALIGN_END);
  GtkWidget *cancel = gtk_button_new_with_label("Annuler"), *ok = gtk_button_new_with_label(action);
  add_class(cancel, "tbtn"); add_class(ok, danger ? "tbtn" : "fbtn");
  if (danger) add_class(ok, "danger");
  g_signal_connect(cancel, "clicked", G_CALLBACK(dialog_cancel), dg);
  g_signal_connect(ok, "clicked", G_CALLBACK(dialog_ok), dg);
  gtk_container_add(GTK_CONTAINER(bb), cancel);
  gtk_container_add(GTK_CONTAINER(bb), ok);
  gtk_container_add(GTK_CONTAINER(v), bb);
  gtk_container_add(GTK_CONTAINER(dg->w), v);
  gtk_widget_show_all(dg->w);
  if (dg->entry) {
    gtk_widget_grab_focus(dg->entry);
    const char *dot = select_stem ? strrchr(initial, '.') : NULL;
    int end = dot && dot != initial ? (int)g_utf8_pointer_to_offset(initial, dot) : -1;
    gtk_editable_select_region(GTK_EDITABLE(dg->entry), 0, end);
  }
}

static void do_new_folder(const char *name, gpointer d) {
  GFile *f = g_file_get_child_for_display_name(cur, name, NULL);
  GError *err = NULL;
  if (!f || !g_file_make_directory(f, NULL, &err)) {
    snack(err ? err->message : "Nom de dossier invalide", NULL, NULL, NULL, NULL);
    g_clear_error(&err);
  }
  if (f) g_object_unref(f);
}

static void new_folder(void) {
  if (in_trash()) return;
  GFile *t = unique_target(cur, "Nouveau dossier", TRUE);
  char *name = g_file_get_basename(t);
  dialog_open("Nouveau dossier", NULL, name, FALSE, "Créer", FALSE, do_new_folder, NULL);
  g_free(name); g_object_unref(t);
}

static void do_rename(const char *name, gpointer d) {
  GFile *f = d;
  GError *err = NULL;
  GFile *r = g_file_set_display_name(f, name, NULL, &err);
  if (!r) { snack(err->message, NULL, NULL, NULL, NULL); g_clear_error(&err); }
  else g_object_unref(r);
  g_object_unref(f);
}

/* Dossiers standards du dossier personnel : ni supprimes ni renommes
   (comme Telechargements sur ChromeOS) ; raccourcis et applis en dependent. */
static const char *STD_DIRS[] = {"Documents", "Images", "Musique", "Téléchargements", "Vidéos"};
static gboolean is_protected(GFile *f) {
  GFile *par = g_file_get_parent(f);
  gboolean r = FALSE;
  if (par && g_file_equal(par, home)) {
    char *b = g_file_get_basename(f);
    for (guint i = 0; i < G_N_ELEMENTS(STD_DIRS); i++) if (!g_strcmp0(b, STD_DIRS[i])) r = TRUE;
    g_free(b);
  }
  if (par) g_object_unref(par);
  return r;
}
static gboolean selection_protected(void) {
  GList *sel = selected_entries();
  gboolean r = FALSE;
  for (GList *l = sel; l && !r; l = l->next) r = is_protected(((Entry *)l->data)->file);
  g_list_free(sel);
  return r;
}
static void ensure_std_dirs(void) {
  for (guint i = 0; i < G_N_ELEMENTS(STD_DIRS); i++) {
    char *p = g_build_filename(g_get_home_dir(), STD_DIRS[i], NULL);
    g_mkdir_with_parents(p, 0755);
    g_free(p);
  }
}

static void rename_selected(void) {
  if (!in_trash() && selection_protected()) { snack("Ce dossier fait partie du système : il ne peut pas être renommé.", NULL, NULL, NULL, NULL); return; }
  GList *sel = selected_entries();
  if (sel && !sel->next && !in_trash()) {
    Entry *e = sel->data;
    dialog_open("Renommer", NULL, e->name, e->type != G_FILE_TYPE_DIRECTORY, "Renommer", FALSE,
                do_rename, g_object_ref(e->file));
  }
  g_list_free(sel);
}

static void do_delete(const char *t, gpointer d) { run_op(d); }

static void delete_selected(gboolean permanent) {
  if (!in_trash() && selection_protected()) { snack("Ce dossier fait partie du système : il ne peut pas être supprimé.", NULL, NULL, NULL, NULL); return; }
  if (in_trash() || permanent) {
    Op *o = op_from_selection(OP_DELETE);
    if (!o) return;
    int n = g_list_length(o->files);
    char *t = g_strdup_printf("%d %s définitivement. Cette action est irréversible.", n,
                              plural(n, "élément sera supprimé", "éléments seront supprimés"));
    dialog_open("Supprimer définitivement ?", t, NULL, FALSE, "Supprimer", TRUE, do_delete, o);
    g_free(t);
  } else {
    Op *o = op_from_selection(OP_TRASH);
    if (o) run_op(o);
  }
}

static void do_empty_trash(const char *t, gpointer d) {
  Op *o = g_new0(Op, 1);
  o->kind = OP_DELETE;
  for (guint i = 0; entries && i < entries->len; i++)
    o->files = g_list_append(o->files, g_object_ref(((Entry *)entries->pdata[i])->file));
  if (o->files) run_op(o); else op_free(o);
}

static void empty_trash(void) {
  if (!entries || !entries->len) return;
  dialog_open("Vider la corbeille ?", "Tous les éléments de la corbeille seront supprimés définitivement.",
              NULL, FALSE, "Vider", TRUE, do_empty_trash, NULL);
}

static void restore_selected(void) { Op *o = op_from_selection(OP_RESTORE); if (o) run_op(o); }

static void clip_set(gboolean cut) {
  GList *sel = selected_entries();
  if (!sel) return;
  g_list_free_full(clip, g_object_unref); clip = NULL;
  for (GList *l = sel; l; l = l->next) clip = g_list_append(clip, g_object_ref(((Entry *)l->data)->file));
  clip_cut = cut;
  int n = g_list_length(clip);
  char *m = g_strdup_printf("%d %s", n, cut ? plural(n, "élément coupé", "éléments coupés")
                                            : plural(n, "élément copié dans le presse-papiers", "éléments copiés dans le presse-papiers"));
  snack(m, NULL, NULL, NULL, NULL);
  g_free(m); g_list_free(sel);
}

static void paste(void) {
  if (!clip || in_trash()) return;
  Op *o = g_new0(Op, 1);
  o->kind = clip_cut ? OP_MOVE : OP_COPY;
  for (GList *l = clip; l; l = l->next) o->files = g_list_append(o->files, g_object_ref(l->data));
  o->dest = g_object_ref(cur);
  if (clip_cut) { g_list_free_full(clip, g_object_unref); clip = NULL; }
  run_op(o);
}


/* ------------------------------------------------------------------ */
/* Glisser-deposer : entre dossiers, vers / depuis les autres applis    */
/* (Chromium : envoi de fichiers sur un site, par exemple)              */
/* ------------------------------------------------------------------ */
static const GtkTargetEntry URI_TARGET[] = {{"text/uri-list", 0, 0}};
static gboolean is_protected(GFile *f);
static GList *selected_entries(void);

static void on_drag_begin(GtkWidget *w, GdkDragContext *ctx, gpointer d) {
  GtkFlowBoxChild *c = GTK_FLOW_BOX_CHILD(gtk_widget_get_ancestor(w, GTK_TYPE_FLOW_BOX_CHILD));
  if (!gtk_flow_box_child_is_selected(c)) {
    gtk_flow_box_unselect_all(GTK_FLOW_BOX(flow));
    gtk_flow_box_select_child(GTK_FLOW_BOX(flow), c);
  }
  Entry *e = g_object_get_data(G_OBJECT(c), "entry");
  if (e) {
    GtkIconTheme *th = gtk_icon_theme_get_default();
    gtk_drag_set_icon_name(ctx, e->type == G_FILE_TYPE_DIRECTORY ? "folder" : "text-x-generic", 0, 0);
    (void)th;
  }
}
static void on_drag_get(GtkWidget *w, GdkDragContext *ctx, GtkSelectionData *sd, guint info, guint t, gpointer d) {
  GList *sel = selected_entries();
  int n = g_list_length(sel), i = 0;
  char **uris = g_new0(char *, n + 1);
  for (GList *l = sel; l; l = l->next) uris[i++] = g_file_get_uri(((Entry *)l->data)->file);
  gtk_selection_data_set_uris(sd, uris);
  g_strfreev(uris); g_list_free(sel);
}
static void setup_drag(GtkWidget *child) {
  if (in_trash()) return;
  gtk_drag_source_set(child, GDK_BUTTON1_MASK, URI_TARGET, 1, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect(child, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
  g_signal_connect(child, "drag-data-get", G_CALLBACK(on_drag_get), NULL);
}

static void on_drop(GtkWidget *w, GdkDragContext *ctx, gint x, gint y, GtkSelectionData *sd, guint info, guint t, gpointer d) {
  char **uris = gtk_selection_data_get_uris(sd);
  if (!uris || in_trash()) { g_strfreev(uris); gtk_drag_finish(ctx, FALSE, FALSE, t); return; }
  /* Depot sur un dossier de la grille : dans ce dossier ; sinon ici */
  GFile *dest = g_object_ref(cur);
  GtkFlowBoxChild *c = w == gtk_widget_get_parent(flow) ? gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(flow), x, y) : NULL;
  Entry *te = c ? g_object_get_data(G_OBJECT(c), "entry") : NULL;
  if (te && te->type == G_FILE_TYPE_DIRECTORY) { g_object_unref(dest); dest = g_object_ref(te->file); }
  /* Depuis Fichiers : deplacer ; depuis une autre appli : copier */
  gboolean internal = gtk_drag_get_source_widget(ctx) != NULL;
  Op *o = g_new0(Op, 1);
  o->kind = internal ? OP_MOVE : OP_COPY;
  o->dest = dest;
  for (char **u = uris; *u; u++) {
    GFile *f = g_file_new_for_uri(*u);
    if (g_file_equal(f, dest) || (internal && is_protected(f))) { g_object_unref(f); continue; }
    o->files = g_list_append(o->files, f);
  }
  g_strfreev(uris);
  if (o->files) run_op(o); else op_free(o);
  gtk_drag_finish(ctx, TRUE, FALSE, t);
}
/* Depot sur un dossier du panneau (Documents, Telechargements, cle USB,
   Corbeille) */
static void on_nav_drop(GtkWidget *w, GdkDragContext *ctx, gint x, gint y, GtkSelectionData *sd, guint info, guint t, gpointer d) {
  GFile *dest = g_object_get_data(G_OBJECT(w), "file");
  char **uris = gtk_selection_data_get_uris(sd);
  if (!dest || !uris) { g_strfreev(uris); gtk_drag_finish(ctx, FALSE, FALSE, t); return; }
  gboolean internal = gtk_drag_get_source_widget(ctx) != NULL, to_trash = g_file_equal(dest, trash);
  Op *o = g_new0(Op, 1);
  o->kind = to_trash ? OP_TRASH : internal ? OP_MOVE : OP_COPY;
  if (!to_trash) o->dest = g_object_ref(dest);
  for (char **u = uris; *u; u++) {
    GFile *f = g_file_new_for_uri(*u);
    if (g_file_equal(f, dest) || (internal && is_protected(f))) { g_object_unref(f); continue; }
    o->files = g_list_append(o->files, f);
  }
  g_strfreev(uris);
  if (o->files) run_op(o); else op_free(o);
  gtk_drag_finish(ctx, TRUE, FALSE, t);
}

static void setup_drop(GtkWidget *w) {
  gtk_drag_dest_set(w, GTK_DEST_DEFAULT_ALL, URI_TARGET, 1, GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect(w, "drag-data-received", G_CALLBACK(on_drop), NULL);
}

static void open_entry(Entry *e) {
  if (e->type == G_FILE_TYPE_DIRECTORY || e->type == G_FILE_TYPE_MOUNTABLE) { navigate(e->file, TRUE); return; }
  if (in_trash()) { snack("Restaurez l'élément pour l'ouvrir.", NULL, NULL, NULL, NULL); return; }
  char *uri = g_file_get_uri(e->file);
  GError *err = NULL;
  GdkAppLaunchContext *ctx = gdk_display_get_app_launch_context(gdk_display_get_default());
  if (!g_app_info_launch_default_for_uri(uri, G_APP_LAUNCH_CONTEXT(ctx), &err)) {
    char *m = g_strdup_printf("Aucune appli pour ouvrir « %s »", e->name);
    snack(m, NULL, NULL, NULL, NULL);
    g_free(m); g_clear_error(&err);
  }
  g_object_unref(ctx); g_free(uri);
}

static void toggle_hidden(void) {
  show_hidden = !show_hidden;
  gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(flow));
  update_empty();
}

static void toggle_view(void) {
  list_mode = !list_mode;
  icon_set(view_ic, list_mode ? "gridview" : "listview", R_ON);
  gtk_widget_set_tooltip_text(view_btn, list_mode ? "Affichage en grille" : "Affichage en liste");
  rebuild_view();
}

/* ------------------------------------------------------------------ */
/* Menu contextuel                                                     */
/* ------------------------------------------------------------------ */
typedef void (*Action)(void);
static GtkWidget *menu, *more_btn;

static void on_mitem(GtkMenuItem *mi, gpointer d) { ((Action)d)(); }

static void a_open(void) { GList *s = selected_entries(); if (s) open_entry(s->data); g_list_free(s); }
static void a_cut(void) {
  if (selection_protected()) { snack("Ce dossier fait partie du système : copiez-le plutôt.", NULL, NULL, NULL, NULL); return; }
  clip_set(TRUE);
}
static void a_copy(void) { clip_set(FALSE); }
static void a_trash(void) { delete_selected(FALSE); }
static void a_delete(void) { delete_selected(TRUE); }
/* Image : fond d'ecran, comme le clic droit de ChromeOS */
static void a_wallpaper(void) {
  GList *sel = selected_entries();
  if (sel) {
    char *p = g_file_get_path(((Entry *)sel->data)->file);
    const char *argv[] = {"lumina-set-wallpaper", p, NULL};
    int st; g_free(run_argv(argv, &st));
    snack(st == 0 ? "Fond d'écran modifié" : "Impossible d'utiliser cette image", NULL, NULL, NULL, NULL);
    g_free(p);
  }
  g_list_free(sel);
}

static void menu_add(const char *text, Action a, gboolean danger) {
  GtkWidget *mi = gtk_menu_item_new_with_label(text);
  if (danger) add_class(mi, "danger");
  g_signal_connect(mi, "activate", G_CALLBACK(on_mitem), (gpointer)a);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
}


/* Ouvrir avec une autre appli que celle par defaut */
static void on_open_with(GtkMenuItem *mi, gpointer app) {
  GList *sel = selected_entries();
  GList *files = NULL;
  for (GList *l = sel; l; l = l->next) files = g_list_append(files, ((Entry *)l->data)->file);
  GdkAppLaunchContext *ctx = gdk_display_get_app_launch_context(gdk_display_get_default());
  g_app_info_launch(G_APP_INFO(app), files, G_APP_LAUNCH_CONTEXT(ctx), NULL);
  g_object_unref(ctx); g_list_free(files); g_list_free(sel);
}

/* Archives (zip, tar, 7z, rar...) : bsdtar, comme le fait ChromeOS */
static gboolean is_archive(Entry *e) {
  const char *t = e->ctype ? e->ctype : "";
  return strstr(t, "zip") || strstr(t, "x-tar") || strstr(t, "compressed-tar") || strstr(t, "x-7z") ||
         strstr(t, "x-rar") || strstr(t, "x-xz") || strstr(t, "gzip") || strstr(t, "bzip");
}
static void archive_done(GObject *o, GAsyncResult *r, gpointer msg) {
  gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(o), r, NULL);
  snack(ok ? msg : "L'opération a échoué", NULL, NULL, NULL, NULL);
  g_object_unref(o); g_free(msg);
  load_dir();
}
static void a_extract(void) {
  GList *sel = selected_entries();
  if (sel) {
    Entry *e = sel->data;
    char *src = g_file_get_path(e->file), *dir = g_file_get_path(cur);
    /* Dossier du nom de l'archive, sans ecraser un dossier existant */
    char *base = g_strdup(e->name), *dot = strchr(base[0] == '.' ? base + 1 : base, '.');
    if (dot) *dot = 0;
    char *dest = g_build_filename(dir, base, NULL);
    for (int i = 2; g_file_test(dest, G_FILE_TEST_EXISTS) && i < 100; i++) { g_free(dest); char *n = g_strdup_printf("%s (%d)", base, i); dest = g_build_filename(dir, n, NULL); g_free(n); }
    g_mkdir_with_parents(dest, 0755);
    GSubprocess *sp = g_subprocess_new(G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL, "bsdtar", "-xf", src, "-C", dest, NULL);
    snack("Extraction…", NULL, NULL, NULL, NULL);
    if (sp) g_subprocess_wait_check_async(sp, NULL, archive_done, g_strdup("Archive extraite"));
    g_free(src); g_free(dir); g_free(base); g_free(dest);
  }
  g_list_free(sel);
}
static void a_compress(void) {
  GList *sel = selected_entries();
  if (!sel) return;
  char *dir = g_file_get_path(cur);
  const char *first = ((Entry *)sel->data)->name;
  char *name = sel->next ? g_strdup("Archive.zip") : g_strdup_printf("%s.zip", first);
  char *dest = g_build_filename(dir, name, NULL);
  for (int i = 2; g_file_test(dest, G_FILE_TEST_EXISTS) && i < 100; i++) {
    g_free(dest); char *n = sel->next ? g_strdup_printf("Archive (%d).zip", i) : g_strdup_printf("%s (%d).zip", first, i);
    dest = g_build_filename(dir, n, NULL); g_free(n);
  }
  GPtrArray *argv = g_ptr_array_new();
  g_ptr_array_add(argv, "bsdtar"); g_ptr_array_add(argv, "-a"); g_ptr_array_add(argv, "-cf"); g_ptr_array_add(argv, dest);
  g_ptr_array_add(argv, "-C"); g_ptr_array_add(argv, dir);
  for (GList *l = sel; l; l = l->next) g_ptr_array_add(argv, ((Entry *)l->data)->name);
  g_ptr_array_add(argv, NULL);
  GSubprocess *sp = g_subprocess_newv((const char *const *)argv->pdata, G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
  snack("Compression…", NULL, NULL, NULL, NULL);
  if (sp) g_subprocess_wait_check_async(sp, NULL, archive_done, g_strdup("Archive ZIP créée"));
  g_ptr_array_free(argv, TRUE);
  g_free(dir); g_free(name); g_free(dest); g_list_free(sel);
}

/* Menu Material : sous le pointeur (clic droit) ou sous le bouton "plus".
   Le contenu depend du contexte : selection, vide, corbeille. */
static void show_menu(GdkEvent *ev) {
  if (menu) gtk_widget_destroy(menu);
  menu = gtk_menu_new();
  gtk_menu_attach_to_widget(GTK_MENU(menu), win, NULL);
  gtk_menu_set_reserve_toggle_size(GTK_MENU(menu), FALSE);   /* pas de marge pour des cases a cocher */
  GList *sel = selected_entries();
  int n = g_list_length(sel);
  g_list_free(sel);
  if (in_trash()) {
    if (n) { menu_add("Restaurer", restore_selected, FALSE); menu_add("Supprimer définitivement", a_delete, TRUE); }
    else menu_add("Vider la corbeille", empty_trash, TRUE);
  } else if (n) {
    if (n == 1) menu_add("Ouvrir", a_open, FALSE);
    if (n == 1) {
      GList *one = selected_entries();
      Entry *e = one ? one->data : NULL;
      if (e && e->ctype && g_str_has_prefix(e->ctype, "image/")) menu_add("Définir comme fond d'écran", a_wallpaper, FALSE);
      if (e && e->type != G_FILE_TYPE_DIRECTORY && e->ctype) {
        /* Ouvrir avec : les applis qui savent ouvrir ce type */
        GList *apps = g_app_info_get_all_for_type(e->ctype);
        if (apps) {
          GtkWidget *sub = gtk_menu_new(), *mi = gtk_menu_item_new_with_label("Ouvrir avec");
          gtk_menu_set_reserve_toggle_size(GTK_MENU(sub), FALSE);
          for (GList *l = apps; l; l = l->next) {
            if (!g_app_info_should_show(l->data)) continue;
            GtkWidget *ai = gtk_menu_item_new_with_label(g_app_info_get_name(l->data));
            g_signal_connect_data(ai, "activate", G_CALLBACK(on_open_with), g_object_ref(l->data),
                                  (GClosureNotify)(void (*)(void))g_object_unref, 0);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), ai);
          }
          gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), sub);
          gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
          g_list_free_full(apps, g_object_unref);
        }
      }
      if (e && is_archive(e)) menu_add("Extraire ici", a_extract, FALSE);
      g_list_free(one);
    }
    gboolean prot = selection_protected();
    if (!prot) menu_add("Couper", a_cut, FALSE);
    menu_add("Copier", a_copy, FALSE);
    menu_add("Compresser en ZIP", a_compress, FALSE);
    if (n == 1 && !prot) menu_add("Renommer", rename_selected, FALSE);
    if (!prot) menu_add("Mettre à la corbeille", a_trash, FALSE);
  } else {
    menu_add("Nouveau dossier", new_folder, FALSE);
    if (clip) menu_add("Coller", paste, FALSE);
    menu_add(show_hidden ? "Masquer les fichiers cachés" : "Afficher les fichiers cachés", toggle_hidden, FALSE);
  }
  gtk_widget_show_all(menu);
  if (ev) gtk_menu_popup_at_pointer(GTK_MENU(menu), ev);
  else gtk_menu_popup_at_widget(GTK_MENU(menu), more_btn, GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, NULL);
}

static gboolean on_flow_press(GtkWidget *w, GdkEventButton *ev, gpointer d) {
  if (ev->type != GDK_BUTTON_PRESS) return FALSE;
  GtkFlowBoxChild *c = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(flow), (int)ev->x, (int)ev->y);
  if (ev->button == 3) {
    if (c && !gtk_flow_box_child_is_selected(c)) {
      gtk_flow_box_unselect_all(GTK_FLOW_BOX(flow));
      gtk_flow_box_select_child(GTK_FLOW_BOX(flow), c);
    } else if (!c) gtk_flow_box_unselect_all(GTK_FLOW_BOX(flow));
    show_menu((GdkEvent *)ev);
    return TRUE;
  }
  if (ev->button == 1 && !c) gtk_flow_box_unselect_all(GTK_FLOW_BOX(flow));
  return FALSE;
}

static void on_child_activated(GtkFlowBox *fb, GtkFlowBoxChild *c, gpointer d) {
  Entry *e = g_object_get_data(G_OBJECT(c), "entry");
  if (e && pick_mode && e->type != G_FILE_TYPE_DIRECTORY) {
    if (pick_save) gtk_entry_set_text(GTK_ENTRY(pick_name), e->name);
    else { gtk_flow_box_unselect_all(fb); gtk_flow_box_select_child(fb, c); }
    pick_finish();
    return;
  }
  if (e) open_entry(e);
}

static void on_more(GtkButton *b, gpointer d) {
  gtk_flow_box_unselect_all(GTK_FLOW_BOX(flow));
  show_menu(NULL);
}

/* ------------------------------------------------------------------ */
/* Clavier                                                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Apercu rapide (Espace) : image, debut d'un texte, ou fiche du fichier */
/* ------------------------------------------------------------------ */
static GtkWidget *ql_win;
static gboolean ql_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  if (e->keyval == GDK_KEY_Escape || e->keyval == GDK_KEY_space) { gtk_widget_destroy(ql_win); ql_win = NULL; return TRUE; }
  return FALSE;
}
static void ql_open(GtkButton *b, gpointer e) { gtk_widget_destroy(ql_win); ql_win = NULL; open_entry(e); }
static void quick_look(void) {
  if (ql_win) { gtk_widget_destroy(ql_win); ql_win = NULL; return; }
  GList *sel = selected_entries();
  Entry *e = sel && !sel->next ? sel->data : NULL;
  g_list_free(sel);
  if (!e || e->type == G_FILE_TYPE_DIRECTORY) return;
  char *path = g_file_get_path(e->file);
  ql_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(ql_win, "qlwin");
  gtk_window_set_transient_for(GTK_WINDOW(ql_win), GTK_WINDOW(win));
  gtk_window_set_modal(GTK_WINDOW(ql_win), TRUE);
  gtk_window_set_title(GTK_WINDOW(ql_win), e->name);
  gtk_window_set_titlebar(GTK_WINDOW(ql_win), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  g_signal_connect(ql_win, "key-press-event", G_CALLBACK(ql_key), NULL);
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  add_class(v, "ql");
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *t = label(e->name, "qltitle", 0);
  gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_set_hexpand(t, TRUE);
  gtk_container_add(GTK_CONTAINER(h), t);
  GtkWidget *ob = gtk_button_new_with_label("Ouvrir");
  add_class(ob, "fbtn");
  g_signal_connect(ob, "clicked", G_CALLBACK(ql_open), e);
  gtk_container_add(GTK_CONTAINER(h), ob);
  gtk_container_add(GTK_CONTAINER(v), h);
  GtkWidget *body = NULL;
  if (path && e->ctype && g_str_has_prefix(e->ctype, "image/")) {
    int iw = 0, ih = 0; gdk_pixbuf_get_file_info(path, &iw, &ih);
    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(path, iw > 0 && iw < 760 ? iw : 760, ih > 0 && ih < 500 ? ih : 500, TRUE, NULL);
    if (pb) { GdkPixbuf *o = gdk_pixbuf_apply_embedded_orientation(pb); body = gtk_image_new_from_pixbuf(o); g_object_unref(o); g_object_unref(pb); }
  } else if (path && e->ctype && (g_content_type_is_a(e->ctype, "text/plain") || g_str_has_prefix(e->ctype, "text/"))) {
    char *c = NULL; gsize n = 0;
    GFile *f = e->file; GFileInputStream *in = g_file_read(f, NULL, NULL);
    if (in) { c = g_malloc0(8193); g_input_stream_read_all(G_INPUT_STREAM(in), c, 8192, &n, NULL, NULL); g_object_unref(in); }
    if (c && g_utf8_validate(c, n, NULL)) {
      GtkWidget *tv = gtk_text_view_new();
      gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
      gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
      gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), c, n);
      body = gtk_scrolled_window_new(NULL, NULL);
      gtk_widget_set_size_request(body, 640, 400);
      gtk_container_add(GTK_CONTAINER(body), tv);
    }
    g_free(c);
  }
  if (!body) {
    body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(body), entry_icon(e, 96));
    char *ty = e->ctype ? g_content_type_get_description(e->ctype) : g_strdup("Fichier");
    char *sz = g_format_size(e->size), *info = g_strdup_printf("%s · %s", ty, sz);
    gtk_container_add(GTK_CONTAINER(body), label(info, "meta", 0.5));
    gtk_widget_set_size_request(body, 360, 200);
    g_free(ty); g_free(sz); g_free(info);
  }
  gtk_container_add(GTK_CONTAINER(v), body);
  gtk_container_add(GTK_CONTAINER(v), label("Espace ou Échap pour fermer", "meta", 0.5));
  gtk_container_add(GTK_CONTAINER(ql_win), v);
  gtk_widget_show_all(ql_win);
  g_free(path);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer d) {
  guint k = ev->keyval;
  gboolean ctrl = ev->state & GDK_CONTROL_MASK, alt = ev->state & GDK_MOD1_MASK, shift = ev->state & GDK_SHIFT_MASK;
  gboolean typing = gtk_widget_has_focus(search_entry);
  if (k == GDK_KEY_Escape) {
    if (*gtk_entry_get_text(GTK_ENTRY(search_entry))) { gtk_entry_set_text(GTK_ENTRY(search_entry), ""); return TRUE; }
    gtk_flow_box_unselect_all(GTK_FLOW_BOX(flow)); gtk_widget_grab_focus(flow); return TRUE;
  }
  if (ctrl && k == GDK_KEY_f) { gtk_widget_grab_focus(search_entry); return TRUE; }
  if (k == GDK_KEY_space && !typing && !pick_mode) { quick_look(); return TRUE; }
  if (ctrl && k == GDK_KEY_h) { toggle_hidden(); return TRUE; }
  if (ctrl && shift && (k == GDK_KEY_N || k == GDK_KEY_n)) { new_folder(); return TRUE; }
  if (alt && k == GDK_KEY_Left) { go_back(); return TRUE; }
  if (alt && k == GDK_KEY_Right) { go_fwd(); return TRUE; }
  if (alt && k == GDK_KEY_Up) { go_up(); return TRUE; }
  if (typing) return FALSE;
  if (ctrl && k == GDK_KEY_a) { gtk_flow_box_select_all(GTK_FLOW_BOX(flow)); return TRUE; }
  if (ctrl && k == GDK_KEY_c) { clip_set(FALSE); return TRUE; }
  if (ctrl && k == GDK_KEY_x) { clip_set(TRUE); return TRUE; }
  if (ctrl && k == GDK_KEY_v) { paste(); return TRUE; }
  if (k == GDK_KEY_Delete) { delete_selected(shift); return TRUE; }
  if (k == GDK_KEY_F2) { rename_selected(); return TRUE; }
  if (k == GDK_KEY_BackSpace) { go_back(); return TRUE; }
  return FALSE;
}

static void on_search_changed(GtkEditable *e, gpointer d) {
  gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(flow));
  update_empty();
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
static GtkWidget *round_btn(const char *glyph, const char *tip, GCallback cb, Icon **ic_out) {
  GtkWidget *b = gtk_button_new();
  add_class(b, "round");
  gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(b, tip);
  Icon *ic = icon_new(glyph, R_ON, 20);
  gtk_container_add(GTK_CONTAINER(b), ic->img);
  g_signal_connect_swapped(b, "clicked", cb, NULL);
  if (ic_out) *ic_out = ic;
  return b;
}


/* ------------------------------------------------------------------ */
/* Mode « choisir » : Fichiers sert de selecteur pour Chromium et les   */
/* applis (portail FileChooser du shell), comme sur ChromeOS. Le choix  */
/* est ecrit sur la sortie standard (une adresse file:// par ligne).    */
/* ------------------------------------------------------------------ */

static gboolean pick_match(Entry *e) {
  if (!pick_filter || e->type == G_FILE_TYPE_DIRECTORY) return TRUE;
  if (pick_dir) return FALSE;
  char **pats = g_strsplit(pick_filter, ";", -1);
  gboolean ok = FALSE;
  char *low = g_utf8_strdown(e->name, -1);
  for (char **p = pats; *p && !ok; p++) {
    if (strchr(*p, '/')) ok = e->ctype && g_content_type_is_mime_type(e->ctype, *p);
    else { char *lp = g_utf8_strdown(*p, -1); ok = g_pattern_match_simple(lp, low); g_free(lp); }
  }
  g_free(low); g_strfreev(pats);
  return ok;
}

static void pick_output(GList *files) {
  for (GList *l = files; l; l = l->next) { char *u = g_file_get_uri(l->data); printf("%s\n", u); g_free(u); }
  fflush(stdout);
  exit(0);
}

static void pick_finish(void) {
  GList *out = NULL;
  if (pick_save) {
    const char *n = gtk_entry_get_text(GTK_ENTRY(pick_name));
    if (!*n || strchr(n, '/')) { snack("Saisissez un nom de fichier", NULL, NULL, NULL, NULL); return; }
    GFile *f = g_file_get_child(cur, n);
    if (g_file_query_exists(f, NULL)) {
      GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                            "« %s » existe déjà. Le remplacer ?", n);
      gtk_dialog_add_buttons(GTK_DIALOG(m), "Annuler", GTK_RESPONSE_CANCEL, "Remplacer", GTK_RESPONSE_ACCEPT, NULL);
      int r = gtk_dialog_run(GTK_DIALOG(m)); gtk_widget_destroy(m);
      if (r != GTK_RESPONSE_ACCEPT) { g_object_unref(f); return; }
    }
    out = g_list_append(out, f);
  } else {
    GList *sel = selected_entries();
    for (GList *l = sel; l; l = l->next) {
      Entry *e = l->data;
      gboolean isdir = e->type == G_FILE_TYPE_DIRECTORY;
      if (isdir == pick_dir) out = g_list_append(out, g_object_ref(e->file));
    }
    g_list_free(sel);
    if (!out && pick_dir) out = g_list_append(out, g_object_ref(cur));
    if (!out) { snack(pick_dir ? "Choisissez un dossier" : "Choisissez un fichier", NULL, NULL, NULL, NULL); return; }
  }
  pick_output(out);
}
static void on_pick(GtkButton *b, gpointer d) { pick_finish(); }
static void on_pick_cancel(GtkButton *b, gpointer d) { exit(1); }

static void pick_parse(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--pick")) pick_mode = TRUE;
    else if (!strcmp(argv[i], "--multiple")) pick_multi = TRUE;
    else if (!strcmp(argv[i], "--directory")) pick_dir = TRUE;
    else if (!strcmp(argv[i], "--save") && i + 1 < argc) pick_save = argv[++i];
    else if (!strcmp(argv[i], "--title") && i + 1 < argc) pick_title = argv[++i];
    else if (!strcmp(argv[i], "--filter") && i + 1 < argc) pick_filter = argv[++i];
    else if (!strcmp(argv[i], "--folder") && i + 1 < argc) { i++; }
    else if (!strcmp(argv[i], "--select") && i + 1 < argc) select_path = g_strdup(argv[++i]);
  }
}
static const char *pick_folder(int argc, char **argv) {
  for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--folder")) return argv[i + 1];
  return NULL;
}

/* Barres du selecteur : bouton principal et nom du fichier a enregistrer */
static void pick_build(GtkWidget *hb, GtkWidget *inner) {
  const char *lbl = pick_save ? "Enregistrer" : pick_dir ? "Sélectionner" : "Ouvrir";
  pick_btn = gtk_button_new_with_label(lbl);
  add_class(pick_btn, "fbtn");
  gtk_widget_set_valign(pick_btn, GTK_ALIGN_CENTER);
  g_signal_connect(pick_btn, "clicked", G_CALLBACK(on_pick), NULL);
  GtkWidget *cancel = gtk_button_new_with_label("Annuler");
  add_class(cancel, "tbtn");
  gtk_widget_set_valign(cancel, GTK_ALIGN_CENTER);
  g_signal_connect(cancel, "clicked", G_CALLBACK(on_pick_cancel), NULL);
  if (pick_save) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    add_class(bar, "pickbar");
    gtk_container_add(GTK_CONTAINER(bar), label("Nom", "meta", 0));
    pick_name = gtk_entry_new();
    add_class(pick_name, "pickname");
    gtk_entry_set_text(GTK_ENTRY(pick_name), pick_save);
    gtk_widget_set_hexpand(pick_name, TRUE);
    g_signal_connect_swapped(pick_name, "activate", G_CALLBACK(pick_finish), NULL);
    gtk_container_add(GTK_CONTAINER(bar), pick_name);
    gtk_container_add(GTK_CONTAINER(bar), cancel);
    gtk_container_add(GTK_CONTAINER(bar), pick_btn);
    gtk_container_add(GTK_CONTAINER(inner), bar);
  } else {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    add_class(bar, "pickbar");
    GtkWidget *hint = label(pick_dir ? "Ouvrez le dossier voulu, puis Sélectionner" :
                            pick_multi ? "Ctrl + clic pour choisir plusieurs fichiers" : "Double-clic pour ouvrir", "meta", 0);
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_container_add(GTK_CONTAINER(bar), hint);
    gtk_container_add(GTK_CONTAINER(bar), cancel);
    gtk_container_add(GTK_CONTAINER(bar), pick_btn);
    gtk_container_add(GTK_CONTAINER(inner), bar);
  }
  (void)hb;
}

static void build_window(void) {
  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-files");
  gtk_window_set_default_size(GTK_WINDOW(win), 980, 620);
  gtk_window_set_icon_name(GTK_WINDOW(win), "system-file-manager");
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

  /* Barre du haut : navigation, chemin, recherche, affichage, fenetre */
  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,maximize,close");
  back_btn = round_btn("back", "Précédent (Alt+←)", G_CALLBACK(go_back), NULL);
  fwd_btn = round_btn("fwd", "Suivant (Alt+→)", G_CALLBACK(go_fwd), NULL);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), back_btn);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), fwd_btn);
  crumbs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_valign(crumbs, GTK_ALIGN_CENTER);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), crumbs);
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

  more_btn = round_btn("more", "Plus d'options", G_CALLBACK(on_more), NULL);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), more_btn);
  view_btn = round_btn("listview", "Affichage en liste", G_CALLBACK(toggle_view), &view_ic);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), view_btn);
  GtkWidget *s = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  add_class(s, "search");
  gtk_widget_set_valign(s, GTK_ALIGN_CENTER);
  gtk_container_add(GTK_CONTAINER(s), icon_new("search", R_ON_VAR, 18)->img);
  search_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Rechercher dans ce dossier");
  g_signal_connect(search_entry, "changed", G_CALLBACK(on_search_changed), NULL);
  gtk_container_add(GTK_CONTAINER(s), search_entry);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), s);
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  /* Corps : navigation | contenu */
  GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *navsc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(navsc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request(navsc, 232, -1);
  add_class(navsc, "nav");
  nav = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav), GTK_SELECTION_SINGLE);
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(nav), TRUE);
  g_signal_connect(nav, "row-activated", G_CALLBACK(on_nav_row), NULL);
  gtk_container_add(GTK_CONTAINER(navsc), nav);
  /* Largeur fixe : sinon la liste reclame la moitie de la fenetre. */
  gtk_widget_set_hexpand(navsc, FALSE);
  gtk_box_pack_start(GTK_BOX(body), navsc, FALSE, FALSE, 0);

  GtkWidget *overlay = gtk_overlay_new();
  add_class(overlay, "content");
  gtk_widget_set_hexpand(overlay, TRUE);
  GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  flow = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_MULTIPLE);
  gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(flow), FALSE);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 4);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 4);
  gtk_widget_set_valign(flow, GTK_ALIGN_START);
  gtk_flow_box_set_filter_func(GTK_FLOW_BOX(flow), flow_filter, NULL, NULL);
  g_signal_connect(flow, "child-activated", G_CALLBACK(on_child_activated), NULL);
  /* Clic droit dans le vide sous les fichiers : la grille ne couvre que
     ses elements, on capte donc les clics sur la zone qui l'entoure (les
     coordonnees sont les memes, la grille etant en haut a gauche). */
  GtkWidget *eb = gtk_event_box_new();
  gtk_widget_add_events(eb, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(eb, "button-press-event", G_CALLBACK(on_flow_press), NULL);
  gtk_container_add(GTK_CONTAINER(eb), flow);
  setup_drop(eb);
  gtk_container_add(GTK_CONTAINER(scroll), eb);
  gtk_container_add(GTK_CONTAINER(inner), scroll);

  empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_vexpand(empty_box, TRUE);
  gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
  GtkWidget *eic = icon_new("folder", R_ON_VAR, 64)->img;
  gtk_widget_set_opacity(eic, 0.35);
  gtk_container_add(GTK_CONTAINER(empty_box), eic);
  empty_l = label("", "empty", 0.5);
  gtk_container_add(GTK_CONTAINER(empty_box), empty_l);
  /* Dossier vide : clic droit (Nouveau dossier, Coller) et depot de
     fichiers aussi sur ce message */
  GtkWidget *eeb = gtk_event_box_new();
  gtk_widget_set_vexpand(eeb, TRUE);
  gtk_widget_add_events(eeb, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(eeb, "button-press-event", G_CALLBACK(on_flow_press), NULL);
  gtk_container_add(GTK_CONTAINER(eeb), empty_box);
  setup_drop(eeb);
  gtk_container_add(GTK_CONTAINER(inner), eeb);
  gtk_container_add(GTK_CONTAINER(overlay), inner);

  snack_rev = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(snack_rev), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
  gtk_widget_set_halign(snack_rev, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(snack_rev, GTK_ALIGN_END);
  GtkWidget *sb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  add_class(sb, "snack");
  snack_l = label("", NULL, 0);
  snack_btn = gtk_button_new_with_label("");
  gtk_container_add(GTK_CONTAINER(sb), snack_l);
  gtk_container_add(GTK_CONTAINER(sb), snack_btn);
  gtk_container_add(GTK_CONTAINER(snack_rev), sb);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), snack_rev);

  if (pick_mode) pick_build(NULL, inner);
  gtk_box_pack_start(GTK_BOX(body), overlay, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(win), body);
  gtk_widget_show_all(win);
  gtk_widget_hide(empty_box);
  gtk_widget_set_no_show_all(snack_btn, TRUE);
}

static void on_theme_changed(void) { if (entries) rebuild_view(); }

int main(int argc, char **argv) {
  g_set_prgname("lumina-files");   /* nom de fenetre : rattache a son icone d'etagere */
  gtk_init(&argc, &argv);
  pick_parse(argc, argv);
  lumina_ui_init(CSS, on_theme_changed);

  home = g_file_new_for_path(g_get_home_dir());
  ensure_std_dirs();
  trash = g_file_new_for_uri("trash:///");
  volumes = g_volume_monitor_get();
  g_signal_connect(volumes, "mount-added", G_CALLBACK(on_volumes_changed), NULL);
  g_signal_connect(volumes, "mount-removed", G_CALLBACK(on_volumes_changed), NULL);
  g_signal_connect(volumes, "volume-added", G_CALLBACK(on_volumes_changed), NULL);
  g_signal_connect(volumes, "volume-removed", G_CALLBACK(on_volumes_changed), NULL);

  build_window();
  rebuild_nav();
  const char *pf = pick_mode ? pick_folder(argc, argv) : NULL;
  GFile *start = pf ? g_file_new_for_path(pf) : (argc > 1 && argv[1][0] != '-') ? g_file_new_for_commandline_arg(argv[1]) : g_object_ref(home);
  if (pick_mode) {
    gtk_window_set_title(GTK_WINDOW(win), pick_title ? pick_title : pick_save ? "Enregistrer" : "Choisir un fichier");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), pick_multi ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_SINGLE);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
  }
  navigate(start, FALSE);
  g_object_unref(start);
  gtk_widget_set_sensitive(back_btn, FALSE);
  gtk_widget_set_sensitive(fwd_btn, FALSE);
  gtk_widget_grab_focus(flow);
  gtk_main();
  return pick_mode ? 1 : 0;   /* fenetre fermee sans choix : annule */
}

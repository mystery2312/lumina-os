/*
 * lumina-gallery : la Galerie de LuminaOS (comme celle de ChromeOS).
 * Images (zoom, rotation, fond d'ecran, suppression), videos et musique
 * (lecture GStreamer : pause, avancer, volume, plein ecran). Les fleches
 * passent au fichier precedent / suivant du meme dossier.
 *
 * Usage : lumina-gallery [fichier|dossier]
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <gst/gst.h>
#include <string.h>
#include <math.h>
#include <glib/gstdio.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-gallery { background: #101312; }\n"
  "window.lumina-gallery.csd decoration { border-radius: 16px; box-shadow: 0 6px 22px alpha(black, 0.3); }\n"
  "window.lumina-gallery.maximized decoration, window.lumina-gallery.fullscreen decoration { border-radius: 0; }\n"
  ".header { background: #1a1d1c; color: #e6e9e7; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  "window.maximized .header { border-radius: 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 32px; min-height: 32px; color: #c4c9c6; }\n"
  ".header button.titlebutton:hover { background: alpha(white, 0.10); }\n"
  ".apptitle { font-size: 15px; color: #e6e9e7; padding-left: 8px; }\n"
  ".dim { color: #9aa29e; font-size: 12.5px; }\n"
  ".round { background: transparent; color: #e6e9e7; border-radius: 18px; min-width: 36px; min-height: 36px; padding: 0; }\n"
  ".round:hover { background: alpha(white, 0.10); }\n"
  ".nav { background: alpha(black, 0.45); border-radius: 24px; min-width: 48px; min-height: 48px; padding: 0; margin: 16px; }\n"
  ".nav:hover { background: alpha(black, 0.65); }\n"
  ".tbtn { background: transparent; color: @pri; border-radius: 16px; padding: 6px 12px; font-weight: 500; }\n"
  ".tbtn:hover { background: alpha(white, 0.08); }\n"
  ".controls { background: alpha(#1a1d1c, 0.92); border-radius: 24px; padding: 6px 14px; margin: 16px 40px; color: #e6e9e7; }\n"
  ".controls scale trough { background: alpha(white, 0.2); min-height: 4px; border-radius: 2px; border: none; }\n"
  ".controls scale highlight { background: @pri; border-radius: 2px; border: none; }\n"
  ".controls scale slider { background: @pri; min-width: 14px; min-height: 14px; border-radius: 7px; border: none; box-shadow: none; margin: -5px; }\n"
  ".time { font-size: 12.5px; color: #c4c9c6; font-feature-settings: 'tnum'; }\n"
  ".audio { color: #e6e9e7; }\n"
  ".audio .big { font-size: 22px; }\n"
  ".empty { color: #9aa29e; font-size: 15px; }\n";

typedef enum { K_NONE, K_IMAGE, K_VIDEO, K_AUDIO } Kind;

static GtkWidget *win, *hb, *title_l, *info_l, *stack, *img_area, *video_box, *audio_box, *audio_title, *empty_l;
static GtkWidget *video_page, *controls, *play_b, *seek, *time_l, *vol, *prev_b, *next_b, *img_tools, *wall_b;
static Icon *play_ic;
static GPtrArray *files;     /* chemins du dossier, du meme genre */
static int idx = -1;
static GdkPixbuf *pix;       /* image courante (orientation EXIF appliquee) */
static double zoom = 0;      /* 0 : ajuster a la fenetre */
static int rot;
static GstElement *player, *vsink;
static GtkWidget *vwidget;
static guint pos_timer;
static gboolean seeking;

static Kind kind_of(const char *path) {
  char *ct = g_content_type_guess(path, NULL, 0, NULL), *mt = g_content_type_get_mime_type(ct);
  Kind k = K_NONE;
  if (mt && g_str_has_prefix(mt, "image/")) k = K_IMAGE;
  else if (mt && g_str_has_prefix(mt, "video/")) k = K_VIDEO;
  else if (mt && g_str_has_prefix(mt, "audio/")) k = K_AUDIO;
  g_free(ct); g_free(mt);
  return k;
}

static gint by_name(gconstpointer a, gconstpointer b) {
  return g_utf8_collate(*(char *const *)a, *(char *const *)b);
}

/* Fichiers du dossier, pour passer de l'un a l'autre */
static void scan_folder(const char *path) {
  if (files) g_ptr_array_free(files, TRUE);
  files = g_ptr_array_new_with_free_func(g_free);
  char *dir = g_file_test(path, G_FILE_TEST_IS_DIR) ? g_strdup(path) : g_path_get_dirname(path);
  GDir *d = g_dir_open(dir, 0, NULL);
  const char *n;
  while (d && (n = g_dir_read_name(d))) {
    if (n[0] == '.') continue;
    char *p = g_build_filename(dir, n, NULL);
    if (kind_of(p) != K_NONE && g_file_test(p, G_FILE_TEST_IS_REGULAR)) g_ptr_array_add(files, p); else g_free(p);
  }
  if (d) g_dir_close(d);
  g_ptr_array_sort(files, by_name);
  idx = files->len ? 0 : -1;
  for (guint i = 0; i < files->len; i++) if (!strcmp(files->pdata[i], path)) idx = i;
  g_free(dir);
}

static const char *cur_path(void) { return idx >= 0 && (guint)idx < files->len ? files->pdata[idx] : NULL; }

static char *fmt_time(gint64 ns) {
  gint64 s = ns / GST_SECOND;
  return s >= 3600 ? g_strdup_printf("%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60))
                   : g_strdup_printf("%d:%02d", (int)(s / 60), (int)(s % 60));
}

/* ---------------- Lecture (GStreamer) ---------------- */
static void stop_media(void) {
  if (pos_timer) { g_source_remove(pos_timer); pos_timer = 0; }
  if (player) gst_element_set_state(player, GST_STATE_NULL);
}

static gboolean pos_tick(gpointer d) {
  gint64 pos = 0, dur = 0;
  if (!player || seeking) return G_SOURCE_CONTINUE;
  if (gst_element_query_position(player, GST_FORMAT_TIME, &pos) && gst_element_query_duration(player, GST_FORMAT_TIME, &dur) && dur > 0) {
    gtk_range_set_range(GTK_RANGE(seek), 0, dur / (double)GST_SECOND);
    g_signal_handlers_block_matched(seek, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, GINT_TO_POINTER(1));
    gtk_range_set_value(GTK_RANGE(seek), pos / (double)GST_SECOND);
    g_signal_handlers_unblock_matched(seek, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, GINT_TO_POINTER(1));
    char *a = fmt_time(pos), *b = fmt_time(dur), *t = g_strdup_printf("%s / %s", a, b);
    gtk_label_set_text(GTK_LABEL(time_l), t);
    g_free(a); g_free(b); g_free(t);
  }
  return G_SOURCE_CONTINUE;
}

static void set_playing(gboolean on) {
  if (!player) return;
  gst_element_set_state(player, on ? GST_STATE_PLAYING : GST_STATE_PAUSED);
  icon_set(play_ic, on ? "pause" : "play", R_ON);
}
static gboolean is_playing(void) {
  GstState s = GST_STATE_NULL;
  if (player) gst_element_get_state(player, &s, NULL, 0);
  return s == GST_STATE_PLAYING;
}
static void on_play(GtkWidget *w, gpointer d) { set_playing(!is_playing()); }
static void on_seek(GtkRange *r, gpointer d) {
  if (player) gst_element_seek_simple(player, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                      (gint64)(gtk_range_get_value(r) * GST_SECOND));
}
static void on_vol(GtkRange *r, gpointer d) { if (player) g_object_set(player, "volume", gtk_range_get_value(r), NULL); }

static gboolean on_bus(GstBus *bus, GstMessage *m, gpointer d) {
  if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS) {
    gst_element_seek_simple(player, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, 0);
    set_playing(FALSE);
  } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
    GError *e = NULL; gst_message_parse_error(m, &e, NULL);
    gtk_label_set_text(GTK_LABEL(info_l), "Format non pris en charge");
    g_clear_error(&e);
    stop_media();
  }
  return TRUE;
}

static gboolean init_player(void) {
  if (player) return TRUE;
  player = gst_element_factory_make("playbin", NULL);
  vsink = gst_element_factory_make("gtksink", NULL);
  if (!player || !vsink) return FALSE;
  g_object_get(vsink, "widget", &vwidget, NULL);
  g_object_set(player, "video-sink", vsink, NULL);
  gtk_widget_set_hexpand(vwidget, TRUE); gtk_widget_set_vexpand(vwidget, TRUE);
  gtk_container_add(GTK_CONTAINER(video_box), vwidget);
  gtk_widget_show(vwidget);
  GstBus *bus = gst_element_get_bus(player);
  gst_bus_add_watch(bus, on_bus, NULL);
  gst_object_unref(bus);
  return TRUE;
}

/* ---------------- Images ---------------- */
static gboolean draw_image(GtkWidget *w, cairo_t *cr, gpointer d) {
  if (!pix) return FALSE;
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
  double pw = gdk_pixbuf_get_width(pix), ph = gdk_pixbuf_get_height(pix);
  gboolean side = rot % 180 != 0;
  double bw = side ? ph : pw, bh = side ? pw : ph;
  double k = zoom > 0 ? zoom : MIN(1.0, MIN((W - 24) / bw, (H - 24) / bh));
  cairo_translate(cr, W / 2, H / 2);
  cairo_rotate(cr, rot * M_PI / 180);
  cairo_scale(cr, k, k);
  gdk_cairo_set_source_pixbuf(cr, pix, -pw / 2, -ph / 2);
  cairo_pattern_set_filter(cairo_get_source(cr), k < 1 ? CAIRO_FILTER_GOOD : CAIRO_FILTER_BEST);
  cairo_paint(cr);
  return FALSE;
}
static double fit_zoom(void) {
  double W = gtk_widget_get_allocated_width(img_area), H = gtk_widget_get_allocated_height(img_area);
  double pw = gdk_pixbuf_get_width(pix), ph = gdk_pixbuf_get_height(pix);
  if (rot % 180) { double t = pw; pw = ph; ph = t; }
  return MIN(1.0, MIN((W - 24) / pw, (H - 24) / ph));
}
static void do_zoom(double f) {
  if (!pix) return;
  double z = zoom > 0 ? zoom : fit_zoom();
  zoom = CLAMP(z * f, 0.05, 16);
  gtk_widget_queue_draw(img_area);
}
static void on_zoom_in(GtkWidget *w, gpointer d) { do_zoom(1.25); }
static void on_zoom_out(GtkWidget *w, gpointer d) { do_zoom(0.8); }
static void on_rotate(GtkWidget *w, gpointer d) { rot = (rot + 90) % 360; gtk_widget_queue_draw(img_area); }
static gboolean on_scroll(GtkWidget *w, GdkEventScroll *e, gpointer d) {
  double dx, dy;
  if (e->direction == GDK_SCROLL_UP) do_zoom(1.15);
  else if (e->direction == GDK_SCROLL_DOWN) do_zoom(0.87);
  else if (gdk_event_get_scroll_deltas((GdkEvent *)e, &dx, &dy)) do_zoom(dy < 0 ? 1.08 : 0.93);
  return TRUE;
}

/* ---------------- Affichage d'un fichier ---------------- */
static void show_current(void) {
  stop_media();
  g_clear_object(&pix);
  zoom = 0; rot = 0;
  const char *p = cur_path();
  gtk_widget_set_sensitive(prev_b, idx > 0);
  gtk_widget_set_sensitive(next_b, files && idx >= 0 && (guint)idx + 1 < files->len);
  if (!p) {
    gtk_label_set_text(GTK_LABEL(title_l), "Galerie");
    gtk_label_set_text(GTK_LABEL(info_l), "");
    gtk_stack_set_visible_child(GTK_STACK(stack), empty_l);
    gtk_widget_hide(controls); gtk_widget_hide(img_tools);
    return;
  }
  char *base = g_path_get_basename(p);
  gtk_label_set_text(GTK_LABEL(title_l), base);
  gtk_window_set_title(GTK_WINDOW(win), base);
  Kind k = kind_of(p);
  GStatBuf st; char *sz = g_stat(p, &st) == 0 ? g_format_size(st.st_size) : g_strdup("");
  if (k == K_IMAGE) {
    GdkPixbuf *raw = gdk_pixbuf_new_from_file(p, NULL);
    if (raw) { pix = gdk_pixbuf_apply_embedded_orientation(raw); g_object_unref(raw); }
    char *i = pix ? g_strdup_printf("%d × %d · %s · %d sur %u", gdk_pixbuf_get_width(pix), gdk_pixbuf_get_height(pix), sz, idx + 1, files->len)
                  : g_strdup("Image illisible");
    gtk_label_set_text(GTK_LABEL(info_l), i);
    g_free(i);
    gtk_stack_set_visible_child(GTK_STACK(stack), img_area);
    gtk_widget_hide(controls); gtk_widget_show(img_tools);
    gtk_widget_queue_draw(img_area);
  } else {
    char *i = g_strdup_printf("%s · %d sur %u", sz, idx + 1, files->len);
    gtk_label_set_text(GTK_LABEL(info_l), i);
    g_free(i);
    gtk_widget_hide(img_tools);
    if (!init_player()) {
      gtk_label_set_text(GTK_LABEL(info_l), "Lecteur vidéo indisponible");
    } else {
      char *uri = g_filename_to_uri(p, NULL, NULL);
      g_object_set(player, "uri", uri, NULL);
      g_free(uri);
      gtk_label_set_text(GTK_LABEL(audio_title), base);
      gtk_stack_set_visible_child(GTK_STACK(stack), k == K_VIDEO ? video_page : audio_box);
      gtk_widget_show(controls);
      gtk_label_set_text(GTK_LABEL(time_l), "0:00");
      set_playing(TRUE);
      pos_timer = g_timeout_add(250, pos_tick, NULL);
    }
  }
  g_free(sz); g_free(base);
}

static void go(int delta) {
  if (!files || idx < 0) return;
  int n = idx + delta;
  if (n < 0 || (guint)n >= files->len) return;
  idx = n;
  show_current();
}
static void on_prev(GtkWidget *w, gpointer d) { go(-1); }
static void on_next(GtkWidget *w, gpointer d) { go(1); }

static void on_wallpaper(GtkWidget *w, gpointer d) {
  const char *p = cur_path();
  if (!p) return;
  const char *argv[] = {"lumina-set-wallpaper", p, NULL};
  int st; g_free(run_argv(argv, &st));
  gtk_label_set_text(GTK_LABEL(info_l), st == 0 ? "Défini comme fond d'écran" : "Impossible de définir le fond d'écran");
}

static void on_delete(GtkWidget *w, gpointer d) {
  const char *p = cur_path();
  if (!p) return;
  GFile *f = g_file_new_for_path(p);
  if (g_file_trash(f, NULL, NULL)) {
    g_ptr_array_remove_index(files, idx);
    if ((guint)idx >= files->len) idx = (int)files->len - 1;
    show_current();
  }
  g_object_unref(f);
}

static void on_open_folder(GtkWidget *w, gpointer d) {
  const char *p = cur_path();
  if (!p) return;
  char *dir = g_path_get_dirname(p);
  const char *argv[] = {"lumina-files", dir, NULL};
  spawn_async(argv);
  g_free(dir);
}

static void on_open(GtkWidget *w, gpointer d) {
  GtkFileChooserNative *fc = gtk_file_chooser_native_new("Ouvrir", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN, "Ouvrir", "Annuler");
  GtkFileFilter *f = gtk_file_filter_new();
  gtk_file_filter_set_name(f, "Images, vidéos et musique");
  gtk_file_filter_add_mime_type(f, "image/*"); gtk_file_filter_add_mime_type(f, "video/*"); gtk_file_filter_add_mime_type(f, "audio/*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), f);
  const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
  if (pics) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fc), pics);
  if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
    char *p = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    scan_folder(p); show_current();
    g_free(p);
  }
  g_object_unref(fc);
}

static gboolean fullscreen;
static void toggle_fullscreen(void) {
  fullscreen = !fullscreen;
  if (fullscreen) { gtk_window_fullscreen(GTK_WINDOW(win)); gtk_widget_hide(hb); }
  else { gtk_window_unfullscreen(GTK_WINDOW(win)); gtk_widget_show(hb); }
}
static void on_full(GtkWidget *w, gpointer d) { toggle_fullscreen(); }
static gboolean on_dbl(GtkWidget *w, GdkEventButton *e, gpointer d) {
  if (e->type == GDK_2BUTTON_PRESS) { toggle_fullscreen(); return TRUE; }
  return FALSE;
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  switch (e->keyval) {
    case GDK_KEY_Left: if (gtk_widget_get_visible(controls) && player) break; go(-1); return TRUE;
    case GDK_KEY_Right: if (gtk_widget_get_visible(controls) && player) break; go(1); return TRUE;
    case GDK_KEY_Page_Up: go(-1); return TRUE;
    case GDK_KEY_Page_Down: go(1); return TRUE;
    case GDK_KEY_space: if (gtk_widget_get_visible(controls)) { on_play(NULL, NULL); return TRUE; } go(1); return TRUE;
    case GDK_KEY_f: case GDK_KEY_F11: toggle_fullscreen(); return TRUE;
    case GDK_KEY_Escape: if (fullscreen) { toggle_fullscreen(); return TRUE; } break;
    case GDK_KEY_plus: case GDK_KEY_equal: case GDK_KEY_KP_Add: do_zoom(1.25); return TRUE;
    case GDK_KEY_minus: case GDK_KEY_KP_Subtract: do_zoom(0.8); return TRUE;
    case GDK_KEY_0: zoom = 0; gtk_widget_queue_draw(img_area); return TRUE;
    case GDK_KEY_r: on_rotate(NULL, NULL); return TRUE;
    case GDK_KEY_Delete: on_delete(NULL, NULL); return TRUE;
  }
  /* Video : fleches = reculer / avancer de 10 s */
  if (player && gtk_widget_get_visible(controls) && (e->keyval == GDK_KEY_Left || e->keyval == GDK_KEY_Right)) {
    gint64 pos = 0;
    gst_element_query_position(player, GST_FORMAT_TIME, &pos);
    pos += (e->keyval == GDK_KEY_Right ? 10 : -10) * GST_SECOND;
    gst_element_seek_simple(player, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, MAX(0, pos));
    return TRUE;
  }
  return FALSE;
}

static GtkWidget *rbtn(const char *glyph, const char *tip, GCallback cb, const char *cls) {
  GtkWidget *b = gtk_button_new();
  add_class(b, cls ? cls : "round");
  gtk_widget_set_tooltip_text(b, tip);
  gtk_container_add(GTK_CONTAINER(b), icon_new(glyph, R_ON, 20)->img);
  g_signal_connect(b, "clicked", cb, NULL);
  return b;
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-gallery");
  gst_init(&argc, &argv);
  gtk_init(&argc, &argv);
  /* Galerie toujours sombre (comme ChromeOS) : les icones en clair */
  lumina_ui_init(CSS, NULL);
  lumina_ui_set_dark_local(TRUE);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-gallery");
  gtk_window_set_default_size(GTK_WINDOW(win), 1000, 680);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

  hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,maximize,close");
  GtkWidget *tv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  title_l = label("Galerie", "apptitle", 0);
  gtk_label_set_ellipsize(GTK_LABEL(title_l), PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_max_width_chars(GTK_LABEL(title_l), 40);
  info_l = label("", "dim", 0);
  gtk_widget_set_margin_start(info_l, 8);
  gtk_container_add(GTK_CONTAINER(tv), title_l);
  gtk_container_add(GTK_CONTAINER(tv), info_l);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), tv);
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), rbtn("fullscreen", "Plein écran (F)", G_CALLBACK(on_full), NULL));
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), rbtn("folder", "Ouvrir le dossier", G_CALLBACK(on_open_folder), NULL));
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), rbtn("file", "Ouvrir un fichier", G_CALLBACK(on_open), NULL));
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), rbtn("trash", "Mettre à la corbeille (Suppr)", G_CALLBACK(on_delete), NULL));
  img_tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  wall_b = gtk_button_new_with_label("Définir comme fond d'écran");
  add_class(wall_b, "tbtn");
  g_signal_connect(wall_b, "clicked", G_CALLBACK(on_wallpaper), NULL);
  gtk_container_add(GTK_CONTAINER(img_tools), wall_b);
  gtk_container_add(GTK_CONTAINER(img_tools), rbtn("rotate", "Faire pivoter (R)", G_CALLBACK(on_rotate), NULL));
  gtk_container_add(GTK_CONTAINER(img_tools), rbtn("zoomout", "Réduire (−)", G_CALLBACK(on_zoom_out), NULL));
  gtk_container_add(GTK_CONTAINER(img_tools), rbtn("zoom", "Agrandir (+)", G_CALLBACK(on_zoom_in), NULL));
  gtk_widget_show_all(img_tools);
  gtk_widget_set_no_show_all(img_tools, TRUE);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), img_tools);
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  GtkWidget *ov = gtk_overlay_new();
  stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  img_area = gtk_drawing_area_new();
  gtk_widget_add_events(img_area, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
  g_signal_connect(img_area, "draw", G_CALLBACK(draw_image), NULL);
  g_signal_connect(img_area, "scroll-event", G_CALLBACK(on_scroll), NULL);
  g_signal_connect(img_area, "button-press-event", G_CALLBACK(on_dbl), NULL);
  gtk_stack_add_named(GTK_STACK(stack), img_area, "image");
  video_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *veb = gtk_event_box_new();
  g_signal_connect(veb, "button-press-event", G_CALLBACK(on_dbl), NULL);
  gtk_container_add(GTK_CONTAINER(veb), video_box);
  gtk_stack_add_named(GTK_STACK(stack), veb, "video");
  video_page = veb;
  audio_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  add_class(audio_box, "audio");
  gtk_widget_set_valign(audio_box, GTK_ALIGN_CENTER);
  Icon *mi = icon_new("music", R_ON, 96);
  gtk_container_add(GTK_CONTAINER(audio_box), mi->img);
  audio_title = label("", "big", 0.5);
  gtk_container_add(GTK_CONTAINER(audio_box), audio_title);
  gtk_stack_add_named(GTK_STACK(stack), audio_box, "audio");
  empty_l = label("Ouvrez une image, une vidéo ou une musique.", "empty", 0.5);
  gtk_stack_add_named(GTK_STACK(stack), empty_l, "empty");
  gtk_container_add(GTK_CONTAINER(ov), stack);

  prev_b = rbtn("back", "Précédent (←)", G_CALLBACK(on_prev), "nav");
  gtk_widget_set_halign(prev_b, GTK_ALIGN_START); gtk_widget_set_valign(prev_b, GTK_ALIGN_CENTER);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), prev_b);
  next_b = rbtn("fwd", "Suivant (→)", G_CALLBACK(on_next), "nav");
  gtk_widget_set_halign(next_b, GTK_ALIGN_END); gtk_widget_set_valign(next_b, GTK_ALIGN_CENTER);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), next_b);

  controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  add_class(controls, "controls");
  gtk_widget_set_valign(controls, GTK_ALIGN_END);
  GtkWidget *pb = gtk_button_new();
  add_class(pb, "round");
  play_ic = icon_new("pause", R_ON, 22);
  gtk_container_add(GTK_CONTAINER(pb), play_ic->img);
  g_signal_connect(pb, "clicked", G_CALLBACK(on_play), NULL);
  play_b = pb;
  gtk_container_add(GTK_CONTAINER(controls), pb);
  seek = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 1);
  gtk_scale_set_draw_value(GTK_SCALE(seek), FALSE);
  gtk_widget_set_hexpand(seek, TRUE);
  g_signal_connect(seek, "value-changed", G_CALLBACK(on_seek), GINT_TO_POINTER(1));
  gtk_container_add(GTK_CONTAINER(controls), seek);
  time_l = label("0:00", "time", 0);
  gtk_container_add(GTK_CONTAINER(controls), time_l);
  gtk_container_add(GTK_CONTAINER(controls), icon_new("vol", R_ON, 18)->img);
  vol = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.05);
  gtk_scale_set_draw_value(GTK_SCALE(vol), FALSE);
  gtk_widget_set_size_request(vol, 90, -1);
  gtk_range_set_value(GTK_RANGE(vol), 1);
  g_signal_connect(vol, "value-changed", G_CALLBACK(on_vol), NULL);
  gtk_container_add(GTK_CONTAINER(controls), vol);
  gtk_widget_show_all(controls);           /* contenu visible ; la barre, */
  gtk_widget_hide(controls);               /* elle, seulement pendant une lecture */
  gtk_widget_set_no_show_all(controls, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), controls);
  gtk_container_add(GTK_CONTAINER(win), ov);
  gtk_widget_show_all(win);

  if (argc > 1) {
    GFile *f = g_file_new_for_commandline_arg(argv[1]);
    char *p = g_file_get_path(f);
    if (p) scan_folder(p);
    g_free(p); g_object_unref(f);
  }
  show_current();
  gtk_main();
  stop_media();
  return 0;
}

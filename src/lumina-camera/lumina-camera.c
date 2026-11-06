/*
 * lumina-camera : la Camera de LuminaOS. Apercu de la webcam (GStreamer,
 * v4l2), photo avec compte a rebours facultatif, enregistrement video.
 * Photos dans Images > Camera, videos dans Videos > Camera.
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-camera { background: #000000; }\n"
  "window.lumina-camera.csd decoration { border-radius: 16px; box-shadow: 0 6px 22px alpha(black, 0.3); }\n"
  ".header { background: #111413; color: #e6e9e7; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 30px; min-height: 30px; color: #c4c9c6; }\n"
  ".apptitle { font-size: 15px; color: #e6e9e7; padding-left: 8px; }\n"
  ".bar { background: #111413; padding: 14px; }\n"
  ".shutter { background: #ffffff; border-radius: 36px; min-width: 72px; min-height: 72px; padding: 0; border: 4px solid alpha(white, 0.4); }\n"
  ".shutter:hover { background: #e8eaed; }\n"
  ".shutter.rec { background: #d93025; }\n"
  ".mode { background: transparent; color: #c4c9c6; border-radius: 16px; padding: 6px 14px; font-weight: 500; }\n"
  ".mode.on { background: alpha(white, 0.14); color: #ffffff; }\n"
  ".round { background: alpha(white, 0.08); color: #e6e9e7; border-radius: 22px; min-width: 44px; min-height: 44px; padding: 0; }\n"
  ".round:hover { background: alpha(white, 0.16); }\n"
  ".msg { color: #e6e9e7; font-size: 15px; }\n"
  ".count { color: #ffffff; font-size: 96px; font-weight: 300; }\n"
  ".flash { background: white; }\n"
  ".thumb { border-radius: 22px; }\n";

static GtkWidget *win, *preview_box, *msg_l, *count_l, *shutter, *mode_photo, *mode_video, *timer_b, *thumb, *flash;
static GstElement *campipe, *vsink, *valve, *rec_bin;
static gboolean video_mode, recording;
static int timer_s, countdown;
static char *rec_path;

static char *out_dir(gboolean video) {
  const char *base = g_get_user_special_dir(video ? G_USER_DIRECTORY_VIDEOS : G_USER_DIRECTORY_PICTURES);
  char *d = g_build_filename(base ? base : g_get_home_dir(), "Caméra", NULL);
  g_mkdir_with_parents(d, 0755);
  return d;
}
static char *stamp_name(const char *ext) {
  GDateTime *n = g_date_time_new_now_local();
  char *s = g_date_time_format(n, "%Y-%m-%d %H.%M.%S");
  char *r = g_strdup_printf("%s %s.%s", video_mode ? "Vidéo" : "Photo", s, ext);
  g_free(s); g_date_time_unref(n);
  return r;
}

static void set_thumb(GdkPixbuf *pb) {
  GdkPixbuf *t = gdk_pixbuf_scale_simple(pb, 44, 44 * gdk_pixbuf_get_height(pb) / MAX(1, gdk_pixbuf_get_width(pb)), GDK_INTERP_BILINEAR);
  gtk_image_set_from_pixbuf(GTK_IMAGE(gtk_bin_get_child(GTK_BIN(thumb))), t);
  g_object_unref(t);
}

static gboolean unflash(gpointer d) { gtk_widget_hide(flash); return G_SOURCE_REMOVE; }

static void take_photo(void) {
  GstSample *s = NULL;
  g_object_get(vsink, "last-sample", &s, NULL);
  if (!s) { gtk_label_set_text(GTK_LABEL(msg_l), "Aucune image de la caméra"); return; }
  GstCaps *caps = gst_caps_new_simple("image/png", NULL, NULL);
  GError *err = NULL;
  GstSample *png = gst_video_convert_sample(s, caps, GST_SECOND * 3, &err);
  gst_caps_unref(caps);
  if (png) {
    GstBuffer *buf = gst_sample_get_buffer(png);
    GstMapInfo mi;
    if (gst_buffer_map(buf, &mi, GST_MAP_READ)) {
      char *dir = out_dir(FALSE), *name = stamp_name("png"), *path = g_build_filename(dir, name, NULL);
      g_file_set_contents(path, (const char *)mi.data, mi.size, NULL);
      GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, NULL);
      if (pb) { set_thumb(pb); g_object_set_data_full(G_OBJECT(thumb), "path", g_strdup(path), g_free); g_object_unref(pb); }
      gst_buffer_unmap(buf, &mi);
      g_free(dir); g_free(name); g_free(path);
    }
    gst_sample_unref(png);
    gtk_widget_show(flash);
    g_timeout_add(120, unflash, NULL);
  } else {
    gtk_label_set_text(GTK_LABEL(msg_l), err ? err->message : "Photo impossible");
    g_clear_error(&err);
  }
  gst_sample_unref(s);
}

/* Enregistrement : une branche "valve -> encodeur -> fichier" ouverte a la demande */
static void start_rec(void) {
  char *dir = out_dir(TRUE), *name = stamp_name("webm");
  g_free(rec_path); rec_path = g_build_filename(dir, name, NULL);
  GstElement *mux = gst_bin_get_by_name(GST_BIN(rec_bin), "sink");
  g_object_set(mux, "location", rec_path, NULL);
  gst_object_unref(mux);
  gst_element_set_state(rec_bin, GST_STATE_PLAYING);
  g_object_set(valve, "drop", FALSE, NULL);
  recording = TRUE;
  set_class(shutter, "rec", TRUE);
  gtk_label_set_text(GTK_LABEL(msg_l), "● Enregistrement…");
  g_free(dir); g_free(name);
}
static void stop_rec(void) {
  g_object_set(valve, "drop", TRUE, NULL);
  GstPad *pad = gst_element_get_static_pad(rec_bin, "sink");
  gst_pad_send_event(pad, gst_event_new_eos());
  gst_object_unref(pad);
  recording = FALSE;
  set_class(shutter, "rec", FALSE);
  char *t = g_strdup_printf("Vidéo enregistrée dans Vidéos › Caméra");
  gtk_label_set_text(GTK_LABEL(msg_l), t);
  g_free(t);
}

static gboolean count_tick(gpointer d) {
  if (--countdown > 0) {
    char *t = g_strdup_printf("%d", countdown);
    gtk_label_set_text(GTK_LABEL(count_l), t); g_free(t);
    return G_SOURCE_CONTINUE;
  }
  gtk_widget_hide(count_l);
  if (video_mode) start_rec(); else take_photo();
  return G_SOURCE_REMOVE;
}

static void on_shutter(GtkWidget *w, gpointer d) {
  if (!campipe) return;
  if (video_mode && recording) { stop_rec(); return; }
  if (timer_s) {
    countdown = timer_s + 1;
    gtk_widget_show(count_l);
    count_tick(NULL);
    g_timeout_add_seconds(1, count_tick, NULL);
  } else if (video_mode) start_rec();
  else take_photo();
}

static void on_mode(GtkWidget *w, gpointer d) {
  if (recording) stop_rec();
  video_mode = GPOINTER_TO_INT(d);
  set_class(mode_photo, "on", !video_mode);
  set_class(mode_video, "on", video_mode);
  gtk_label_set_text(GTK_LABEL(msg_l), "");
}
static void on_timer(GtkWidget *w, gpointer d) {
  timer_s = timer_s == 0 ? 3 : timer_s == 3 ? 10 : 0;
  char *t = timer_s ? g_strdup_printf("%d s", timer_s) : g_strdup("Retardateur");
  gtk_button_set_label(GTK_BUTTON(timer_b), t);
  g_free(t);
}
static void on_thumb(GtkWidget *w, gpointer d) {
  const char *p = g_object_get_data(G_OBJECT(thumb), "path");
  char *dir = out_dir(FALSE);
  const char *argv[] = {"lumina-gallery", p ? p : dir, NULL};
  spawn_async(argv);
  g_free(dir);
}

static gboolean on_bus(GstBus *b, GstMessage *m, gpointer d) {
  if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
    GError *e = NULL; gst_message_parse_error(m, &e, NULL);
    if (GST_MESSAGE_SRC(m) && GST_OBJECT_PARENT(GST_MESSAGE_SRC(m)) != GST_OBJECT(rec_bin)) {
      gtk_label_set_text(GTK_LABEL(msg_l), "Aucune caméra détectée, ou elle est utilisée par une autre appli.");
      gtk_widget_set_sensitive(shutter, FALSE);
    }
    g_clear_error(&e);
  } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ELEMENT && rec_bin && GST_MESSAGE_SRC(m) == GST_OBJECT(rec_bin)) {
  }
  return TRUE;
}

static void build_pipeline(void) {
  if (!g_file_test("/dev/video0", G_FILE_TEST_EXISTS)) {
    gtk_label_set_text(GTK_LABEL(msg_l), "Aucune caméra détectée sur ce PC.");
    gtk_widget_set_sensitive(shutter, FALSE);
    return;
  }
  GError *err = NULL;
  campipe = gst_parse_launch(
    "v4l2src ! videoconvert ! videoflip method=horizontal-flip ! tee name=t "
    "t. ! queue leaky=downstream max-size-buffers=2 ! videoconvert ! gtksink name=view "
    "t. ! queue ! valve name=valve drop=true ! videoconvert ! "
    "bin. ( name=rec vp8enc deadline=1 cpu-used=8 ! webmmux ! filesink name=sink async=false )", &err);
  if (!campipe) {
    /* Sans encodeur video : photos seulement */
    g_clear_error(&err);
    campipe = gst_parse_launch("v4l2src ! videoconvert ! videoflip method=horizontal-flip ! videoconvert ! gtksink name=view", &err);
  }
  if (!campipe) {
    gtk_label_set_text(GTK_LABEL(msg_l), err ? err->message : "Caméra indisponible");
    g_clear_error(&err);
    return;
  }
  vsink = gst_bin_get_by_name(GST_BIN(campipe), "view");
  valve = gst_bin_get_by_name(GST_BIN(campipe), "valve");
  rec_bin = gst_bin_get_by_name(GST_BIN(campipe), "rec");
  if (!valve) gtk_widget_set_sensitive(mode_video, FALSE);
  GtkWidget *w = NULL;
  g_object_get(vsink, "widget", &w, NULL);
  gtk_widget_set_hexpand(w, TRUE); gtk_widget_set_vexpand(w, TRUE);
  gtk_container_add(GTK_CONTAINER(preview_box), w);
  gtk_widget_show(w);
  GstBus *bus = gst_element_get_bus(campipe);
  gst_bus_add_watch(bus, on_bus, NULL);
  gst_object_unref(bus);
  gst_element_set_state(campipe, GST_STATE_PLAYING);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  if (e->keyval == GDK_KEY_space || e->keyval == GDK_KEY_Return) { on_shutter(NULL, NULL); return TRUE; }
  return FALSE;
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-camera");
  gst_init(&argc, &argv);
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, NULL);
  lumina_ui_set_dark_local(TRUE);
  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-camera");
  gtk_window_set_title(GTK_WINDOW(win), "Caméra");
  gtk_window_set_default_size(GTK_WINDOW(win), 860, 620);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,maximize,close");
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), label("Caméra", "apptitle", 0));
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *ov = gtk_overlay_new();
  preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(preview_box, TRUE);
  gtk_container_add(GTK_CONTAINER(ov), preview_box);
  msg_l = label("", "msg", 0.5);
  gtk_widget_set_valign(msg_l, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(msg_l, 14);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), msg_l);
  count_l = label("", "count", 0.5);
  gtk_widget_set_no_show_all(count_l, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), count_l);
  flash = gtk_event_box_new();
  add_class(flash, "flash");
  gtk_widget_set_no_show_all(flash, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), flash);
  gtk_container_add(GTK_CONTAINER(v), ov);

  GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  add_class(bar, "bar");
  timer_b = gtk_button_new_with_label("Retardateur");
  add_class(timer_b, "mode");
  g_signal_connect(timer_b, "clicked", G_CALLBACK(on_timer), NULL);
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_hexpand(left, TRUE);
  gtk_widget_set_valign(left, GTK_ALIGN_CENTER);
  mode_photo = gtk_button_new_with_label("Photo");
  add_class(mode_photo, "mode"); add_class(mode_photo, "on");
  g_signal_connect(mode_photo, "clicked", G_CALLBACK(on_mode), GINT_TO_POINTER(0));
  mode_video = gtk_button_new_with_label("Vidéo");
  add_class(mode_video, "mode");
  g_signal_connect(mode_video, "clicked", G_CALLBACK(on_mode), GINT_TO_POINTER(1));
  gtk_container_add(GTK_CONTAINER(left), mode_photo);
  gtk_container_add(GTK_CONTAINER(left), mode_video);
  gtk_container_add(GTK_CONTAINER(left), timer_b);
  gtk_container_add(GTK_CONTAINER(bar), left);
  shutter = gtk_button_new();
  add_class(shutter, "shutter");
  gtk_widget_set_tooltip_text(shutter, "Prendre (Espace)");
  g_signal_connect(shutter, "clicked", G_CALLBACK(on_shutter), NULL);
  gtk_container_add(GTK_CONTAINER(bar), shutter);
  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_hexpand(right, TRUE);
  gtk_widget_set_halign(right, GTK_ALIGN_END);
  gtk_widget_set_valign(right, GTK_ALIGN_CENTER);
  thumb = gtk_button_new();
  add_class(thumb, "round");
  gtk_widget_set_tooltip_text(thumb, "Ouvrir dans la Galerie");
  gtk_container_add(GTK_CONTAINER(thumb), icon_new("gallery", R_ON, 20)->img);
  g_signal_connect(thumb, "clicked", G_CALLBACK(on_thumb), NULL);
  gtk_container_add(GTK_CONTAINER(right), thumb);
  gtk_container_add(GTK_CONTAINER(bar), right);
  gtk_container_add(GTK_CONTAINER(v), bar);
  gtk_container_add(GTK_CONTAINER(win), v);
  gtk_widget_show_all(win);
  build_pipeline();
  gtk_main();
  if (recording) stop_rec();
  if (campipe) gst_element_set_state(campipe, GST_STATE_NULL);
  return 0;
}

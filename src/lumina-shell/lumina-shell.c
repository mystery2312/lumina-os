/*
 * lumina-shell : le bureau de LuminaOS.
 *
 * Fond d'ecran, barre d'etat, etagere, tiroir d'applications et
 * reglages rapides, dans un seul processus GTK3 +
 * gtk-layer-shell (~25 Mo de RAM). Le compositeur (labwc) gere les
 * fenetres ; ce programme dessine tout ce qui les entoure.
 *
 * Toutes les valeurs affichees sont lues dans le systeme : /proc,
 * /sys, iwctl, wpctl, brightnessctl. Un element dont la source n'existe
 * pas sur la machine (pas de batterie, pas de retroeclairage, pas de
 * Waydroid...) est masque plutot que simule.
 *
 * Commande a distance : SIGUSR1 bascule le tiroir, SIGUSR2 les reglages.
 */
#include "lumina-ui.h"
#include <glib/gstdio.h>
#include <sys/statvfs.h>
#include "lumina-sys.h"
#include <gtk-layer-shell.h>
#include <gio/gdesktopappinfo.h>
#include <gdk/gdkwayland.h>
#include "foreign-toplevel.h"
#include <glib-unix.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Batterie : glyphe genere, le remplissage suit le niveau. */
static char *battery_svg(int pct) {
  double w = 1.0 + 11.0 * CLAMP(pct, 0, 100) / 100.0;
  return g_strdup_printf("<rect x='2' y='7' width='17' height='10' rx='2.5'/><path d='M21.5 11v2'/>"
                         "<rect x='4' y='9' width='%.1f' height='6' rx='1' fill='currentColor' stroke='none'/>", w);
}

/* ------------------------------------------------------------------ */
/* Style                                                               */
/* ------------------------------------------------------------------ */
static const char *BASE_CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "window.lumina { background: transparent; }\n"
  /* Menu du clic droit (etagere, tiroir) */
  "menu, .menu { background: @c_high; border: none; border-radius: 12px; padding: 6px 0; min-width: 200px; }\n"
  "window.popup decoration, .csd.popup decoration { border-radius: 12px; box-shadow: 0 3px 12px alpha(black, 0.20); }\n"
  "menuitem { color: @on; padding: 9px 20px; min-height: 0; }\n"
  "menuitem:hover { background: alpha(@on, 0.08); color: @on; }\n"
  "menuitem label { font-size: 13.5px; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  /* Barre d'etat fine, transparente sur le fond d'ecran ; opaque, un cran
     plus foncee que le cadre des fenetres, quand une fenetre est agrandie. */
  ".bar { color: @on; font-size: 12px; font-weight: 500; padding: 0 4px; transition: background-color 180ms; }\n"
  ".bar.solid { background: @bar_solid; }\n"
  ".bar .date { color: @on_var; font-weight: 400; }\n"
  ".bar.light:not(.solid) { color: #ffffff; text-shadow: 0 1px 2px alpha(black, 0.5); }\n"
  ".bar.light:not(.solid) .date { color: alpha(white, 0.85); }\n"
  ".launch { background: transparent; color: @on; border-radius: 11px; padding: 1px 9px; min-height: 0; }\n"
  ".launch:hover { background: alpha(@on, 0.10); }\n"
  ".launch.open { background: @pri_c; }\n"
  ".clockbtn { background: transparent; color: inherit; border-radius: 11px; padding: 1px 8px; min-height: 0; }\n"
  ".clockbtn:hover { background: alpha(@on, 0.10); }\n"
  ".bar.light:not(.solid) .clockbtn:hover { background: alpha(white, 0.16); }\n"
  "calendar { background: transparent; color: @on; border: none; padding: 4px; font-size: 13px; }\n"
  "calendar.header { background: transparent; border: none; }\n"
  "calendar.button { color: @on_var; background: transparent; }\n"
  "calendar:selected { background: @pri; color: @on_pri; border-radius: 16px; }\n"
  "calendar.highlight { color: @pri; font-weight: 600; }\n"
  "calendar:indeterminate { color: alpha(@on, 0.35); }\n"
  ".caltitle { font-size: 15px; font-weight: 500; }\n"
  ".media { background: @c_high; border-radius: 20px; padding: 10px 12px 10px 16px; }\n"
  ".media .mtitle { font-size: 13px; font-weight: 500; }\n"
  ".media .martist { color: @on_var; font-size: 12px; }\n"
  ".mbtn { background: transparent; border-radius: 18px; min-width: 36px; min-height: 36px; padding: 0; }\n"
  ".mbtn:hover { background: alpha(@on, 0.08); }\n"
  ".tray { color: @on; background: transparent; border-radius: 11px; padding: 1px 10px; min-height: 0; }\n"
  ".tray:hover, .tray.open { background: alpha(@on, 0.10); }\n"
  /* Etagere pleine largeur facon ChromeOS : applis epinglees et ouvertes,
     un point sous celles qui ont une fenetre, un trait sous l'active. */
  ".shelf { background: alpha(@c, 0.92); border-top: 1px solid alpha(@on, 0.06); padding: 5px 8px 3px 8px; }\n"
  ".slot { background: transparent; border-radius: 12px; padding: 2px; min-width: 40px; min-height: 40px; }\n"
  ".slot:hover { background: alpha(@on, 0.08); }\n"
  ".ind { min-width: 4px; min-height: 3px; border-radius: 2px; background: transparent; }\n"
  ".ind.on { background: @on_var; }\n"
  ".ind.active { background: @pri; min-width: 14px; }\n"
  ".ico { border-radius: 12px; min-width: 36px; min-height: 36px; }\n"
  ".ico.big { border-radius: 14px; min-width: 46px; min-height: 46px; }\n"
  ".ico.pri { background: @pri_c; color: @on_pri_c; }\n"
  ".ico.ter { background: @ter_c; color: @on_ter_c; }\n"
  ".ico.sec { background: @sec_c; color: @on_sec_c; }\n"
  ".ico .letter { font-size: 18px; font-weight: 600; }\n"
  ".panel { background: @c_low; color: @on; border-radius: 28px; padding: 20px; margin: 24px;\n"
  "         box-shadow: 0 4px 18px alpha(black, 0.20), 0 1px 3px alpha(black, 0.12); }\n"
  ".panel.attached { margin-top: 8px; }\n"
  ".search { background: @c_high; border-radius: 23px; padding: 0 8px 0 16px; min-height: 46px; }\n"
  ".search entry { background: transparent; border: none; box-shadow: none; color: @on; font-size: 15px;\n"
  "                caret-color: @pri; min-height: 40px; }\n"
  ".section { color: @on_var; font-size: 12px; font-weight: 500; }\n"
  ".app { background: transparent; color: @on; border-radius: 16px; padding: 10px 2px 8px; }\n"
  ".app:hover, .app:focus { background: @c; }\n"
  ".app .name { font-size: 12px; }\n"
  ".app .kind { color: @on_var; font-size: 10.5px; }\n"
  ".empty { color: @on_var; font-size: 13px; padding: 24px; }\n"
  ".qs-time { font-size: 24px; }\n"
  ".qs-date { color: @on_var; font-size: 12.5px; }\n"
  ".tile { background: @c_high; color: @on; border-radius: 20px; padding: 10px 14px; min-height: 40px; }\n"
  ".tile:hover { background: @c_top; }\n"
  ".tile.on { background: @pri_c; color: @on_pri_c; }\n"
  ".tile:disabled { opacity: 0.55; }\n"
  ".tile .title { font-weight: 500; font-size: 13px; }\n"
  ".tile .sub { font-size: 11.5px; opacity: 0.8; }\n"
  ".nets { background: @c_high; border-radius: 20px; padding: 6px; }\n"
  ".net { background: transparent; color: @on; border-radius: 14px; padding: 8px 12px; }\n"
  ".net:hover { background: @c_top; }\n"
  ".net .sub { color: @on_var; font-size: 11.5px; }\n"
  ".net.current .title { color: @pri; font-weight: 500; }\n"
  ".pass { background: @c_low; border-radius: 14px; padding: 4px 10px; margin: 2px 6px 6px 6px; }\n"
  ".pass entry { background: transparent; border: none; box-shadow: none; color: @on; caret-color: @pri; }\n"
  ".textbtn { background: transparent; color: @pri; border-radius: 14px; padding: 6px 12px; font-weight: 500; }\n"
  ".textbtn:hover { background: alpha(@pri, 0.10); }\n"
  ".hint { color: @on_var; font-size: 11.5px; padding: 4px 12px; }\n"
  ".slider-row { background: @c_high; border-radius: 22px; padding: 0 14px; min-height: 44px; }\n"
  "scale trough { background: @c_top; min-height: 6px; border-radius: 3px; border: none; }\n"
  "scale highlight { background: @pri; border-radius: 3px; border: none; }\n"
  "scale slider { background: @pri; min-width: 18px; min-height: 18px; border-radius: 9px;\n"
  "               border: none; box-shadow: none; margin: -6px; }\n"
  ".mem { background: @c_high; border-radius: 20px; padding: 14px 16px; }\n"
  ".mem .muted { color: @on_var; font-size: 12.5px; }\n"
  ".mem .strong { font-size: 12.5px; font-weight: 500; }\n"
  ".legend { color: @on_var; font-size: 11.5px; }\n"
  ".upd { background: @c_high; border-radius: 20px; padding: 6px 8px 6px 16px; min-height: 36px; }\n"
  ".upd.ready { background: @pri_c; color: @on_pri_c; }\n"
  ".upd label { font-size: 12.5px; }\n"
  ".upd button { background: @pri; color: @on_pri; border-radius: 16px; padding: 4px 14px; font-weight: 500; }\n"
  ".pwr { background: @c_high; color: @on; border-radius: 20px; padding: 9px 14px; }\n"
  ".pwr:hover { background: @c_top; }\n"
  /* Vue d'ensemble, presse-papiers, emojis, notifications, lanceur */
  ".overview { padding: 36px 48px 28px 48px; }\n"
  ".desks { background: alpha(@surface, 0.55); border-radius: 22px; padding: 10px 14px; }\n"
  ".deskbtn { background: transparent; border-radius: 14px; padding: 4px; color: @on; }\n"
  ".deskbtn .deskthumb { border-radius: 10px; }\n"
  ".deskbtn:hover { background: alpha(@on, 0.08); }\n"
  ".deskbtn.current { box-shadow: inset 0 0 0 3px @pri; }\n"
  ".deskname { font-size: 12.5px; font-weight: 500; }\n"
  ".deskadd { background: @pri_c; border-radius: 24px; min-width: 48px; min-height: 48px; padding: 0; margin: 25px 6px 0 6px; }\n"
  ".deskadd:hover { background: @pri; }\n"
  ".ovcard { background: @c_low; border-radius: 20px; min-width: 260px;\n"
  "          box-shadow: 0 2px 10px alpha(black, 0.16); }\n"
  ".ovcard.current { box-shadow: 0 0 0 3px @pri, 0 2px 10px alpha(black, 0.16); }\n"
  ".ovhead { padding: 8px 8px 4px 12px; }\n"
  ".ovtitle { font-size: 13px; font-weight: 500; }\n"
  ".ovclose { background: transparent; border-radius: 14px; min-width: 28px; min-height: 28px; padding: 0; }\n"
  ".ovclose:hover { background: alpha(@on, 0.10); }\n"
  ".ovbody { background: @c_high; border-radius: 0 0 20px 20px; min-height: 150px; }\n"
  ".ovbody:hover { background: @c_top; }\n"
  ".ovempty { color: #ffffff; font-size: 15px; text-shadow: 0 1px 3px alpha(black, 0.5); }\n"
  ".ovhint { color: @on_var; font-size: 12px; }\n"
  ".ptitle { font-size: 15px; font-weight: 500; }\n"
  ".clipitem { background: @c_high; color: @on; border-radius: 12px; padding: 10px 14px; }\n"
  ".clipitem:hover { background: @c_top; }\n"
  ".clipimg { color: @pri; font-weight: 500; }\n"
  ".emoji { background: transparent; border-radius: 12px; font-size: 22px; padding: 4px; min-height: 40px; }\n"
  ".emoji:hover { background: @c_high; }\n"
  ".notifs { background: @c_high; border-radius: 20px; padding: 8px 10px 10px 14px; }\n"
  ".notifs .muted { color: @on_var; font-size: 12.5px; }\n"
  ".notif { background: @c_low; color: @on; border-radius: 14px; padding: 8px 12px; }\n"
  ".notif:hover { background: @c; }\n"
  ".notif .napp { color: @on_var; font-size: 11px; }\n"
  ".notif .nsum { font-size: 13px; font-weight: 500; }\n"
  ".notif .nbody { color: @on_var; font-size: 12px; }\n"
  ".recbtn { background: #ba1a1a; color: #ffffff; border-radius: 11px; padding: 1px 10px; min-height: 0; font-weight: 500; }\n"
  ".calc { background: @pri_c; color: @on_pri_c; border-radius: 18px; padding: 10px 16px; }\n"
  ".calc .calcres { font-size: 20px; font-weight: 500; }\n"
  ".calc .kind { color: @on_pri_c; font-size: 12px; }\n"
  ".fileres { background: transparent; color: @on; border-radius: 14px; padding: 7px 12px; }\n"
  ".fileres:hover { background: @c; }\n"
  ".fileres .kind { color: @on_var; font-size: 11.5px; }\n"
  ".shelf.side { border-top: none; padding: 8px 4px; }\n";

/* ------------------------------------------------------------------ */
/* Petits utilitaires systeme                                          */
/* ------------------------------------------------------------------ */
static char *fmt_go(double bytes) {               /* 1,12 Go */
  char *s = g_strdup_printf("%.2f Go", bytes / 1073741824.0);
  for (char *p = s; *p; p++) if (*p == '.') *p = ',';
  return s;
}

/* Dates en francais sans dependre des locales (economise ~20 Mo). */
static const char *DAY_S[] = {"", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
static const char *DAY_L[] = {"", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
static const char *MON_S[] = {"", "janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."};
static const char *MON_L[] = {"", "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre"};

/* ------------------------------------------------------------------ */
/* Etat global                                                         */
/* ------------------------------------------------------------------ */
static GtkWidget *drawer, *qs, *scrim, *bar_win, *shelf_win;
/* Un fond d'ecran par ecran branche (fenetres layer-shell) */
static GPtrArray *walls;
static GtkWidget *bar_box, *qs_scroll;
static void fit_qs(void);
static gboolean wall_light, bar_solid_now;   /* photo sombre en haut ; barre opaque */
static void update_bar_light(void);
/* lumina-extras.c */
static void extras_close(void);
static void extras_bar(GtkWidget *h);
static void extras_bar_end(GtkWidget *h);
static void extras_qs(GtkWidget *panel, GtkWidget *grid);
static void refresh_extras_qs(void);
static void fill_notifs(void);
static void extras_search_changed(const char *q);
static void extras_drawer(GtkWidget *v);
static void extras_shelf(GtkWidget *box);
static void extras_shelf_button(GtkWidget *b, const char *key, gboolean has_app);
static void register_filechooser(GDBusConnection *c);
static GtkWidget *clock_l, *date_l, *tray_btn, *batt_l, *launch_btn;
static Icon *tray_net_ic, *tray_batt_ic;
static GtkWidget *search_entry, *flow, *flow_empty, *flow_scroll;
static GtkWidget *qs_time, *qs_date;
static GtkWidget *wifi_tile, *wifi_sub, *nets_rev, *nets_box, *wifi_power_btn;
static Icon *wifi_ic;
static GtkWidget *dark_tile, *dark_sub, *android_tile, *android_sub;
static GtkWidget *bright_row, *bright_scale, *vol_row, *vol_scale;
static GtkWidget *mem_used_l, *mem_total_l, *mem_gauge, *zram_l;
static GtkWidget *upd_row, *upd_l, *upd_btn;
static double mem_u = 0, mem_z = 0;
static char *wifi_dev;
static guint bright_timer, vol_timer;

/* ------------------------------------------------------------------ */
/* Fond d'ecran                                                        */
/* ------------------------------------------------------------------ */
static void blob(cairo_t *cr, double cx, double cy, double rx, double ry, const char *hex) {
  double r, g, b; hex_rgb(hex, &r, &g, &b);
  cairo_save(cr);
  cairo_translate(cr, cx, cy); cairo_scale(cr, rx, ry);
  cairo_pattern_t *p = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
  cairo_pattern_add_color_stop_rgba(p, 0, r, g, b, 1);
  cairo_pattern_add_color_stop_rgba(p, 0.7, r, g, b, 0);
  cairo_set_source(cr, p);
  cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI); cairo_fill(cr);
  cairo_pattern_destroy(p);
  cairo_restore(cr);
}

/* Photo choisie dans Reglages > Affichage : chargee une fois, a la taille
   du plus grand ecran (une photo de 24 Mpx en pleine resolution couterait
   pres de 100 Mo de memoire). */
static GdkPixbuf *wall_pb;
static void load_wallpaper(void) {
  g_clear_object(&wall_pb);
  char *path = lumina_ui_wallpaper_path();
  if (!path) return;
  GdkDisplay *dpy = gdk_display_get_default();
  int mw = 1280, mh = 800;
  for (int i = 0; i < gdk_display_get_n_monitors(dpy); i++) {
    GdkRectangle g; GdkMonitor *m = gdk_display_get_monitor(dpy, i);
    gdk_monitor_get_geometry(m, &g);
    int sc = gdk_monitor_get_scale_factor(m);
    mw = MAX(mw, g.width * sc); mh = MAX(mh, g.height * sc);
  }
  GdkPixbuf *raw = gdk_pixbuf_new_from_file(path, NULL);
  if (raw) {
    GdkPixbuf *o = gdk_pixbuf_apply_embedded_orientation(raw);
    g_object_unref(raw);
    int w = gdk_pixbuf_get_width(o), h = gdk_pixbuf_get_height(o);
    double k = MAX((double)mw / w, (double)mh / h);
    if (k < 1) wall_pb = gdk_pixbuf_scale_simple(o, MAX(1, (int)(w * k)), MAX(1, (int)(h * k)), GDK_INTERP_BILINEAR);
    else wall_pb = g_object_ref(o);
    g_object_unref(o);
  }
  g_free(path);
}

/* Barre transparente sur une photo sombre : blanc ; opaque : couleurs du theme */
static void update_bar_light(void) {
  gboolean light = wall_light && !bar_solid_now;
  if (bar_box) set_class(bar_box, "light", light);
  if (light != lumina_bar_light) { lumina_bar_light = light; lumina_ui_rerender(); }
}

/* Photo sombre derriere la barre d'etat (bande du haut) : heure et icones
   en blanc, comme sur ChromeOS. Pas de voile sur la photo. */
static gboolean wall_top_dark(void) {
  if (!wall_pb) return FALSE;
  int w = gdk_pixbuf_get_width(wall_pb), h = gdk_pixbuf_get_height(wall_pb);
  int rs = gdk_pixbuf_get_rowstride(wall_pb), nc = gdk_pixbuf_get_n_channels(wall_pb);
  const guchar *px = gdk_pixbuf_read_pixels(wall_pb);
  int rows = MAX(1, h / 25);
  double sum = 0; long n = 0;
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < w; x += 4) {
      const guchar *q = px + y * rs + x * nc;
      sum += 0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2]; n++;
    }
  return n && sum / n < 140;
}

static gboolean draw_wall(GtkWidget *w, cairo_t *cr, gpointer d) {
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), r, g, b;
  if (wall_pb) {
    /* "Remplir" : la photo couvre l'ecran, centree, sans deformation. */
    double pw = gdk_pixbuf_get_width(wall_pb), ph = gdk_pixbuf_get_height(wall_pb);
    double k = MAX(W / pw, H / ph);
    cairo_save(cr);
    cairo_translate(cr, (W - pw * k) / 2, (H - ph * k) / 2);
    cairo_scale(cr, k, k);
    gdk_cairo_set_source_pixbuf(cr, wall_pb, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
    return FALSE;
  }
  hex_rgb(P->wall_c, &r, &g, &b);
  cairo_set_source_rgb(cr, r, g, b); cairo_paint(cr);
  blob(cr, W * 0.18, H * 0.88, W * 0.60, H * 0.70, P->wall_a);
  blob(cr, W * 0.88, H * 0.12, W * 0.55, H * 0.65, P->wall_b);
  return FALSE;
}

/* ------------------------------------------------------------------ */
/* Fenetres layer-shell                                                */
/* ------------------------------------------------------------------ */
/* Sous Wayland, GTK3 reutilise ses tampons sans effacer les zones
   transparentes : un texte qui change (l'heure) laisserait des residus.
   On vide donc la surface avant chaque dessin. */
static gboolean clear_surface(GtkWidget *w, cairo_t *cr, gpointer d) {
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
  return FALSE;
}

static GtkWidget *layer_window(GtkLayerShellLayer layer, const char *ns) {
  GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_layer_init_for_window(GTK_WINDOW(w));
  gtk_layer_set_layer(GTK_WINDOW(w), layer);
  gtk_layer_set_namespace(GTK_WINDOW(w), ns);
  GdkVisual *v = gdk_screen_get_rgba_visual(gtk_widget_get_screen(w));
  if (v) gtk_widget_set_visual(w, v);
  gtk_widget_set_app_paintable(w, TRUE);
  g_signal_connect(w, "draw", G_CALLBACK(clear_surface), NULL);
  add_class(w, "lumina");
  return w;
}

static void anchor(GtkWidget *w, gboolean t, gboolean b, gboolean l, gboolean r) {
  gtk_layer_set_anchor(GTK_WINDOW(w), GTK_LAYER_SHELL_EDGE_TOP, t);
  gtk_layer_set_anchor(GTK_WINDOW(w), GTK_LAYER_SHELL_EDGE_BOTTOM, b);
  gtk_layer_set_anchor(GTK_WINDOW(w), GTK_LAYER_SHELL_EDGE_LEFT, l);
  gtk_layer_set_anchor(GTK_WINDOW(w), GTK_LAYER_SHELL_EDGE_RIGHT, r);
}

/* ------------------------------------------------------------------ */
/* Panneaux : un seul ouvert a la fois, au-dessus d'un voile cliquable  */
/* ------------------------------------------------------------------ */
static void refresh_qs(void);
static void schedule_shelf(void);

static void close_panels(void) {
  extras_close();
  gtk_widget_hide(drawer); gtk_widget_hide(qs); gtk_widget_hide(scrim);
  set_class(launch_btn, "open", FALSE); set_class(tray_btn, "open", FALSE);
}

static void open_panel(GtkWidget *p) {
  if (p == qs) fit_qs();
  close_panels();
  gtk_widget_show(scrim);          /* mappe en premier : reste dessous */
  gtk_widget_show(p);
  if (p == drawer) {
    set_class(launch_btn, "open", TRUE);
    gtk_entry_set_text(GTK_ENTRY(search_entry), "");
    gtk_widget_grab_focus(search_entry);
  } else {
    set_class(tray_btn, "open", TRUE);
    refresh_qs();
  }
}

static void toggle_panel(GtkWidget *p) {
  if (gtk_widget_get_visible(p)) close_panels(); else open_panel(p);
}

static gboolean on_scrim_press(GtkWidget *w, GdkEvent *e, gpointer d) { close_panels(); return TRUE; }

static gboolean on_panel_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  if (e->keyval == GDK_KEY_Escape) { close_panels(); return TRUE; }
  return FALSE;
}

static gboolean on_sigusr1(gpointer d) { toggle_panel(drawer); return G_SOURCE_CONTINUE; }

/* SIGHUP : de nouvelles icones sont arrivees (lumina-fetch-icons). */
static void on_apps_changed(GAppInfoMonitor *m, gpointer d);
static gboolean on_sighup(gpointer d) {
  /* rescan_if_needed ne regarde que la date du dossier du theme, pas celle
     de 128x128/apps ou arrivent les icones : on reimpose le chemin de
     recherche, ce qui force GTK a tout relire. */
  GtkIconTheme *th = gtk_icon_theme_get_default();
  gchar **path = NULL; gint n = 0;
  gtk_icon_theme_get_search_path(th, &path, &n);
  gtk_icon_theme_set_search_path(th, (const gchar **)path, n);
  g_strfreev(path);
  on_apps_changed(NULL, NULL);
  return G_SOURCE_CONTINUE;
}
static gboolean on_sigusr2(gpointer d) { toggle_panel(qs); return G_SOURCE_CONTINUE; }

/* ------------------------------------------------------------------ */
/* Applications                                                        */
/* ------------------------------------------------------------------ */
static const char *TONES[] = {"pri", "ter", "sec"};

static const char *app_tone(GDesktopAppInfo *a) {
  char *t = g_desktop_app_info_get_string(a, "X-Lumina-Tone");
  const char *r = NULL;
  for (guint i = 0; i < 3; i++) if (g_strcmp0(t, TONES[i]) == 0) r = TONES[i];
  g_free(t);
  if (!r) r = TONES[g_str_hash(g_app_info_get_id(G_APP_INFO(a))) % 3];
  return r;
}


/* L'icone de l'appli existe-t-elle vraiment ? (sinon GTK afficherait
   son pictogramme "image manquante") */
static gboolean icon_available(GIcon *gi, int px) {
  if (!gi) return FALSE;
  if (G_IS_FILE_ICON(gi)) {
    GFile *f = g_file_icon_get_file(G_FILE_ICON(gi));
    return g_file_query_exists(f, NULL);
  }
  GtkIconInfo *info = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_default(), gi, px, 0);
  if (!info) return FALSE;
  g_object_unref(info);
  return TRUE;
}

/* Icones rondes, comme le lanceur de ChromeOS :
   - vraie icone de l'appli (PWA, Android, Linux, icone officielle des
     applis web telechargee par lumina-fetch-icons) posee sur un disque
     blanc ;
   - sinon (applis LuminaOS, hors ligne) : disque de couleur vive
     (X-Lumina-Color) et pictogramme blanc ;
   - fenetre d'une appli inconnue : disque gris et initiale. */
typedef struct { GdkPixbuf *pb; double r, g, b; gboolean filled; int sc; char letter[8]; } RoundIcon;

static void round_icon_free(gpointer p) { RoundIcon *ri = p; g_clear_object(&ri->pb); g_free(ri); }

static gboolean draw_round_icon(GtkWidget *w, cairo_t *cr, gpointer d) {
  RoundIcon *ri = d;
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
  double R = MIN(W, H) / 2 - 1, cx = W / 2, cy = H / 2;
  /* ombre douce */
  cairo_set_source_rgba(cr, 0, 0, 0, 0.14);
  cairo_arc(cr, cx, cy + 0.8, R, 0, 2 * M_PI); cairo_fill(cr);
  if (ri->filled) {
    cairo_pattern_t *g = cairo_pattern_create_linear(0, cy - R, 0, cy + R);
    cairo_pattern_add_color_stop_rgb(g, 0, MIN(1, ri->r * 1.12 + 0.04), MIN(1, ri->g * 1.12 + 0.04), MIN(1, ri->b * 1.12 + 0.04));
    cairo_pattern_add_color_stop_rgb(g, 1, ri->r * 0.9, ri->g * 0.9, ri->b * 0.9);
    cairo_set_source(cr, g);
    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI); cairo_fill(cr);
    cairo_pattern_destroy(g);
  } else {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI); cairo_fill(cr);
  }
  if (ri->pb) {
    int sc = ri->sc > 0 ? ri->sc : 1;   /* pixbuf rendu a sc fois la taille */
    double pw = gdk_pixbuf_get_width(ri->pb) / (double)sc, ph = gdk_pixbuf_get_height(ri->pb) / (double)sc;
    cairo_save(cr);
    cairo_arc(cr, cx, cy, R, 0, 2 * M_PI); cairo_clip(cr);
    cairo_translate(cr, cx - pw / 2, cy - ph / 2);
    cairo_scale(cr, 1.0 / sc, 1.0 / sc);
    gdk_cairo_set_source_pixbuf(cr, ri->pb, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else if (ri->letter[0]) {
    cairo_set_source_rgb(cr, 1, 1, 1);
    PangoLayout *l = gtk_widget_create_pango_layout(w, ri->letter);
    char *f = g_strdup_printf("Roboto Medium %d", (int)(R * 0.9));
    PangoFontDescription *fd = pango_font_description_from_string(f);
    pango_layout_set_font_description(l, fd);
    int lw, lh; pango_layout_get_pixel_size(l, &lw, &lh);
    cairo_move_to(cr, cx - lw / 2.0, cy - lh / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd); g_free(f); g_object_unref(l);
  }
  return FALSE;
}

static GtkWidget *round_widget(RoundIcon *ri, int px) {
  GtkWidget *a = gtk_drawing_area_new();
  gtk_widget_set_size_request(a, px, px);
  gtk_widget_set_halign(a, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(a, GTK_ALIGN_CENTER);
  g_signal_connect_data(a, "draw", G_CALLBACK(draw_round_icon), ri, (GClosureNotify)(void (*)(void))round_icon_free, 0);
  return a;
}

/* Pictogramme LuminaOS en blanc, en pixbuf (rendu SVG) */
static GdkPixbuf *glyph_pixbuf(const char *name, int px) {
  const char *body = glyph_svg(name);
  if (!body) return NULL;
  GString *b = g_string_new(body);
  g_string_replace(b, "currentColor", "#ffffff", 0);
  char *svg = g_strdup_printf("<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 24 24' "
                              "fill='none' stroke='#ffffff' stroke-width='2' stroke-linecap='round' "
                              "stroke-linejoin='round'>%s</svg>", px, px, b->str);
  g_string_free(b, TRUE);
  GdkPixbufLoader *l = gdk_pixbuf_loader_new_with_type("svg", NULL);
  GdkPixbuf *pb = NULL;
  if (l && gdk_pixbuf_loader_write(l, (const guchar *)svg, strlen(svg), NULL) && gdk_pixbuf_loader_close(l, NULL))
    pb = g_object_ref(gdk_pixbuf_loader_get_pixbuf(l));
  if (l) g_object_unref(l);
  g_free(svg);
  return pb;
}

static GtkWidget *app_icon(GDesktopAppInfo *a, gboolean big) {
  int px = big ? 48 : 38, sc = 2;   /* rendu a 2x : net sur les ecrans HiDPI */
  RoundIcon *ri = g_new0(RoundIcon, 1);
  ri->sc = sc;
  GIcon *gi = g_app_info_get_icon(G_APP_INFO(a));
  if (icon_available(gi, px)) {
    int ip = (int)(px * 0.70) * sc;
    GtkIconInfo *info = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_default(), gi, ip, GTK_ICON_LOOKUP_FORCE_SIZE);
    if (info) { ri->pb = gtk_icon_info_load_icon(info, NULL); g_object_unref(info); }
  }
  if (!ri->pb) {
    char *col = g_desktop_app_info_get_string(a, "X-Lumina-Color");
    const char *tone = app_tone(a);
    hex_rgb(col ? col : !strcmp(tone, "ter") ? P->ter : !strcmp(tone, "sec") ? "#5f6368" : P->pri, &ri->r, &ri->g, &ri->b);
    g_free(col);
    ri->filled = TRUE;
    char *g = g_desktop_app_info_get_string(a, "X-Lumina-Glyph");
    if (g) ri->pb = glyph_pixbuf(g, (int)(px * 0.56) * sc);
    g_free(g);
    if (!ri->pb) g_unichar_to_utf8(g_unichar_toupper(g_utf8_get_char(g_app_info_get_name(G_APP_INFO(a)))), ri->letter);
  }
  return round_widget(ri, px);
}

/* Type affiche sous le nom. Les PWA de Chromium ont un WMClass "crx_..."
   et un identifiant "chrome-..." ; les applis Waydroid un identifiant
   "waydroid.<paquet>.desktop". */
static char *app_kind(GDesktopAppInfo *a) {
  char *k = g_desktop_app_info_get_string(a, "X-Lumina-Kind");
  if (k) return k;
  const char *id = g_app_info_get_id(G_APP_INFO(a)), *wm = g_desktop_app_info_get_startup_wm_class(a);
  if (id && g_str_has_prefix(id, "waydroid.")) return g_strdup("Android");
  if ((wm && g_str_has_prefix(wm, "crx_")) || (id && (g_str_has_prefix(id, "chrome-") || g_str_has_prefix(id, "chromium-"))))
    return g_strdup("Web");
  return g_strdup("Linux");
}

static void launch(GAppInfo *a) {
  /* Une appli Android ne peut se lancer que si Android tourne :
     lumina-android le demarre a la demande, puis ouvre l'appli. */
  const char *id = g_app_info_get_id(a);
  if (id && g_str_has_prefix(id, "waydroid.") && g_str_has_suffix(id, ".desktop")) {
    char *pkg = g_strndup(id + 9, strlen(id) - 9 - 8);
    const char *argv[] = {"lumina-android", pkg, NULL};
    spawn_async(argv);
    g_free(pkg);
    return;
  }
  GdkAppLaunchContext *ctx = gdk_display_get_app_launch_context(gdk_display_get_default());
  GError *err = NULL;
  if (!g_app_info_launch(a, NULL, G_APP_LAUNCH_CONTEXT(ctx), &err)) {
    g_warning("lancement de %s impossible : %s", g_app_info_get_id(a), err->message);
    g_error_free(err);
  }
  g_object_unref(ctx);
}

static void on_app_clicked(GtkButton *b, gpointer d) { close_panels(); launch(G_APP_INFO(d)); }

static gint by_name(gconstpointer a, gconstpointer b) {
  return g_utf8_collate(g_app_info_get_name(*(GAppInfo **)a), g_app_info_get_name(*(GAppInfo **)b));
}

static gboolean app_visible(GAppInfo *a) {
  if (!G_IS_DESKTOP_APP_INFO(a) || !g_app_info_should_show(a)) return FALSE;
  /* Pas de terminal dans l'image : les apps console seraient inutilisables. */
  return !g_desktop_app_info_get_boolean(G_DESKTOP_APP_INFO(a), "Terminal");
}

static gboolean flow_filter(GtkFlowBoxChild *c, gpointer d) {
  const char *q = gtk_entry_get_text(GTK_ENTRY(search_entry));
  if (!*q) return TRUE;
  GAppInfo *a = g_object_get_data(G_OBJECT(c), "app");
  char *n = g_utf8_casefold(g_app_info_get_name(a), -1), *k = g_utf8_casefold(q, -1);
  gboolean ok = strstr(n, k) != NULL;
  g_free(n); g_free(k);
  return ok;
}

static GAppInfo *first_match(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(flow));
  GAppInfo *r = NULL;
  for (GList *l = kids; l && !r; l = l->next)
    if (flow_filter(l->data, NULL)) r = g_object_get_data(G_OBJECT(l->data), "app");
  g_list_free(kids);
  return r;
}

static void on_search_changed(GtkEditable *e, gpointer d) {
  gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(flow));
  gtk_widget_set_visible(flow_empty, first_match() == NULL);
  extras_search_changed(gtk_entry_get_text(GTK_ENTRY(search_entry)));
}

/* Entree : premiere appli trouvee, sinon recherche sur le Web. */
static void on_search_activate(GtkEntry *e, gpointer d) {
  const char *q = gtk_entry_get_text(e);
  GAppInfo *a = first_match();
  if (*q && a) { close_panels(); launch(a); return; }
  if (!*q) return;
  char *esc = g_uri_escape_string(q, NULL, FALSE);
  char *url = g_strdup_printf("https://duckduckgo.com/?q=%s", esc);
  const char *argv[] = {"lumina-browser", url, NULL};
  spawn_async(argv);
  g_free(esc); g_free(url);
  close_panels();
}

static gboolean on_app_menu(GtkWidget *w, GdkEventButton *e, gpointer key);

static void fill_drawer(void) {
  GList *all = g_app_info_get_all();
  GPtrArray *apps = g_ptr_array_new();
  for (GList *l = all; l; l = l->next) if (app_visible(l->data)) g_ptr_array_add(apps, l->data);
  g_ptr_array_sort(apps, by_name);

  for (guint i = 0; i < apps->len; i++) {
    GDesktopAppInfo *a = apps->pdata[i];
    GtkWidget *btn = gtk_button_new();
    add_class(btn, "app");
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_container_add(GTK_CONTAINER(v), app_icon(a, TRUE));
    GtkWidget *n = label(g_app_info_get_name(G_APP_INFO(a)), "name", 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(n), 12);
    gtk_container_add(GTK_CONTAINER(v), n);
    char *kind = app_kind(a);
    gtk_container_add(GTK_CONTAINER(v), label(kind, "kind", 0.5));
    g_free(kind);
    gtk_container_add(GTK_CONTAINER(btn), v);
    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_app_clicked), g_object_ref(a),
                          (GClosureNotify)(void (*)(void))g_object_unref, 0);
    /* Clic droit : epingler a l'etagere / retirer */
    g_signal_connect_data(btn, "button-press-event", G_CALLBACK(on_app_menu),
                          g_strdup(g_app_info_get_id(G_APP_INFO(a))), (GClosureNotify)(void (*)(void))g_free, 0);
    gtk_flow_box_insert(GTK_FLOW_BOX(flow), btn, -1);
    GtkWidget *child = gtk_widget_get_parent(btn);
    gtk_widget_set_can_focus(child, FALSE);
    g_object_set_data(G_OBJECT(child), "app", a);
  }
  /* GTK3 ne deduit pas la hauteur d'un GtkFlowBox dans un defilement :
     on la calcule (rangees de 5, ~110 px chacune), plafonnee a 4 rangees. */
  int rows = (apps->len + 4) / 5;
  if (flow_scroll)
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(flow_scroll), MIN(MAX(rows, 1), 4) * 122);
  gtk_widget_show_all(flow);
  g_ptr_array_free(apps, TRUE);
  g_list_free_full(all, g_object_unref);   /* les boutons gardent leur reference */
}

/* PWA installee depuis Chromium, appli Android ajoutee par Waydroid,
   paquet Debian : tous deposent un fichier .desktop, que GIO surveille. */
static void on_apps_changed(GAppInfoMonitor *m, gpointer d) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(flow));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  fill_drawer();
  on_search_changed(NULL, NULL);
  schedule_shelf();   /* une appli epinglee vient peut-etre d'etre installee */
}

/* ------------------------------------------------------------------ */
/* Barre d'etat                                                        */
/* ------------------------------------------------------------------ */
/* Premiere batterie trouvee : -1 si la machine n'en a pas. */
static int battery(gboolean *charging) {
  GDir *d = g_dir_open("/sys/class/power_supply", 0, NULL);
  int pct = -1;
  for (const char *n; d && (n = g_dir_read_name(d)) && pct < 0;) {
    char *tp = g_strdup_printf("/sys/class/power_supply/%s/type", n), *t = read_file(tp);
    if (g_strcmp0(t, "Battery") == 0) {
      char *cp = g_strdup_printf("/sys/class/power_supply/%s/capacity", n), *c = read_file(cp);
      char *sp = g_strdup_printf("/sys/class/power_supply/%s/status", n), *s = read_file(sp);
      if (c) pct = atoi(c);
      if (charging) *charging = g_strcmp0(s, "Charging") == 0 || g_strcmp0(s, "Full") == 0;
      g_free(cp); g_free(c); g_free(sp); g_free(s);
    }
    g_free(tp); g_free(t);
  }
  if (d) g_dir_close(d);
  return pct;
}

/* Espace disque presque plein : prevenir avant que les applis echouent
   (Chromium, Linux, mises a jour ont besoin d'ecrire). Une alerte par
   seuil franchi, pas a chaque verification. */
static gboolean disk_check(gpointer d) {
  static int warned = 0;          /* 0 : rien ; 1 : bas ; 2 : critique */
  struct statvfs v;
  if (statvfs("/var/lib/lumina", &v) != 0 || !v.f_blocks) return G_SOURCE_CONTINUE;
  double freeb = (double)v.f_bavail * v.f_frsize, tot = (double)v.f_blocks * v.f_frsize;
  int lvl = (freeb < 300e6 || freeb < tot * 0.02) ? 2 : (freeb < 1e9 || freeb < tot * 0.06) ? 1 : 0;
  if (lvl > warned) {
    char *f = g_format_size((guint64)freeb);
    char *body = g_strdup_printf("Il reste %s. Supprimez des fichiers (Téléchargements, Corbeille) ou des applis "
                                 "Linux dans Réglages › Stockage.", f);
    const char *argv[] = {"notify-send", "-a", "LuminaOS", "-u", lvl == 2 ? "critical" : "normal",
                          lvl == 2 ? "Espace disque presque épuisé" : "Espace disque bientôt plein", body, NULL};
    spawn_async(argv);
    g_free(f); g_free(body);
  }
  warned = lvl;
  return G_SOURCE_CONTINUE;
}

static gboolean tick(gpointer d) {
  GDateTime *now = g_date_time_new_now_local();
  char *t = g_date_time_format(now, "%H:%M");
  int dow = g_date_time_get_day_of_week(now), day = g_date_time_get_day_of_month(now), mon = g_date_time_get_month(now);
  char *ds = g_strdup_printf("%s %d %s", DAY_S[dow], day, MON_S[mon]);
  char *dl = g_strdup_printf("%s %d %s", DAY_L[dow], day, MON_L[mon]);
  gtk_label_set_text(GTK_LABEL(clock_l), t);
  gtk_label_set_text(GTK_LABEL(date_l), ds);
  gtk_label_set_text(GTK_LABEL(qs_time), t);
  gtk_label_set_text(GTK_LABEL(qs_date), dl);
  g_free(t); g_free(ds); g_free(dl); g_date_time_unref(now);

  /* Icone reseau : Wi-Fi s'il y a une carte, sinon cable. */
  if (wifi_dev) icon_set(tray_net_ic, net_wifi_up(wifi_dev) ? "wifi" : "wifi-off", R_BAR);
  else icon_set(tray_net_ic, net_wired_up(wifi_dev) ? "wired" : "wifi-off", R_BAR);

  gboolean ch = FALSE;
  int pct = battery(&ch);
  gtk_widget_set_visible(tray_batt_ic->img, pct >= 0);
  gtk_widget_set_visible(batt_l, pct >= 0);
  if (pct >= 0) {
    char *body = battery_svg(pct), *g = g_strconcat("svg:", body, NULL);
    icon_set(tray_batt_ic, g, R_BAR);
    char *s = g_strdup_printf(ch ? "%d %% ⚡" : "%d %%", pct);
    gtk_label_set_text(GTK_LABEL(batt_l), s);
    g_free(body); g_free(g); g_free(s);
  }
  return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Reglages rapides : Wi-Fi                                            */
/* ------------------------------------------------------------------ */
static void fill_networks(void);

static void on_connect_done(GObject *src, GAsyncResult *res, gpointer d) {
  GSubprocess *sp = G_SUBPROCESS(src);
  g_subprocess_wait_finish(sp, res, NULL);
  gboolean ok = g_subprocess_get_successful(sp);
  GtkWidget *pass_box = d;
  if (!ok && pass_box) {
    /* Reseau protege inconnu : on demande la cle. */
    gtk_widget_show_all(pass_box);
    GtkWidget *entry = g_object_get_data(G_OBJECT(pass_box), "entry");
    gtk_widget_grab_focus(entry);
  } else {
    refresh_qs();
    if (gtk_revealer_get_reveal_child(GTK_REVEALER(nets_rev))) fill_networks();
  }
  g_object_unref(sp);
}

static void connect_net(const char *ssid, const char *pass, GtkWidget *pass_box) {
  net_wifi_connect(wifi_dev, ssid, pass, on_connect_done, pass ? NULL : pass_box);
  gtk_label_set_text(GTK_LABEL(wifi_sub), "Connexion…");
}

static void on_net_clicked(GtkButton *b, gpointer d) {
  connect_net(g_object_get_data(G_OBJECT(b), "ssid"), NULL, d);
}

static void on_pass_activate(GtkEntry *e, gpointer d) {
  connect_net(d, gtk_entry_get_text(e), NULL);
  gtk_widget_hide(gtk_widget_get_parent(GTK_WIDGET(e)));
}

static const char *signal_word(int s) { return s >= 4 ? "Excellent" : s >= 3 ? "Bon" : s >= 2 ? "Moyen" : "Faible"; }

static void fill_networks(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(nets_box));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);

  gboolean powered = net_wifi_powered(wifi_dev);
  gtk_button_set_label(GTK_BUTTON(wifi_power_btn), powered ? "Désactiver le Wi-Fi" : "Activer le Wi-Fi");
  if (!powered) { gtk_container_add(GTK_CONTAINER(nets_box), label("Le Wi-Fi est désactivé.", "hint", 0)); }
  else {
    GPtrArray *nets = net_wifi_list(wifi_dev);
    if (nets->len == 0) gtk_container_add(GTK_CONTAINER(nets_box), label("Recherche des réseaux…", "hint", 0));
    for (guint i = 0; i < nets->len && i < 8; i++) {
      Net *n = nets->pdata[i];
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
      GtkWidget *b = gtk_button_new();
      add_class(b, "net");
      if (n->current) add_class(b, "current");
      GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      GtkWidget *t = label(n->ssid, "title", 0);
      gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
      gtk_widget_set_hexpand(t, TRUE);
      gtk_container_add(GTK_CONTAINER(h), t);
      char *sub = g_strdup_printf("%s · %s", g_strcmp0(n->security, "open") == 0 ? "Ouvert" : "Protégé",
                                  n->current ? "Connecté" : signal_word(n->signal));
      gtk_container_add(GTK_CONTAINER(h), label(sub, "sub", 1));
      g_free(sub);
      gtk_container_add(GTK_CONTAINER(b), h);
      g_object_set_data_full(G_OBJECT(b), "ssid", g_strdup(n->ssid), g_free);

      GtkWidget *pass = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
      add_class(pass, "pass");
      GtkWidget *e = gtk_entry_new();
      gtk_entry_set_visibility(GTK_ENTRY(e), FALSE);
      gtk_entry_set_placeholder_text(GTK_ENTRY(e), "Mot de passe du réseau, puis Entrée");
      gtk_widget_set_hexpand(e, TRUE);
      gtk_container_add(GTK_CONTAINER(pass), e);
      g_object_set_data(G_OBJECT(pass), "entry", e);
      g_signal_connect_data(e, "activate", G_CALLBACK(on_pass_activate), g_strdup(n->ssid),
                            (GClosureNotify)(void (*)(void))g_free, 0);
      gtk_widget_set_no_show_all(pass, TRUE);

      g_signal_connect(b, "clicked", G_CALLBACK(on_net_clicked), pass);
      gtk_container_add(GTK_CONTAINER(row), b);
      gtk_container_add(GTK_CONTAINER(row), pass);
      gtk_container_add(GTK_CONTAINER(nets_box), row);
    }
    g_ptr_array_free(nets, TRUE);
  }
  gtk_widget_show_all(nets_box);
}

static gboolean rescan_done(gpointer d) { if (gtk_widget_get_visible(qs)) fill_networks(); return G_SOURCE_REMOVE; }

static void on_wifi_tile(GtkButton *b, gpointer d) {
  gboolean open = !gtk_revealer_get_reveal_child(GTK_REVEALER(nets_rev));
  gtk_revealer_set_reveal_child(GTK_REVEALER(nets_rev), open);
  if (open) {
    fill_networks();
    net_wifi_scan(wifi_dev);
    g_timeout_add(3500, rescan_done, NULL);
  }
}

static void on_wifi_power(GtkButton *b, gpointer d) {
  net_wifi_power(wifi_dev, !net_wifi_powered(wifi_dev));
  refresh_qs();
  fill_networks();
}

/* ------------------------------------------------------------------ */
/* Reglages rapides : theme, Android, curseurs, memoire                 */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Portail org.freedesktop.portal.Settings (theme clair/sombre)          */
/* Chromium, les applis GTK4/libadwaita et Qt lisent la preference       */
/* clair/sombre via ce service D-Bus standard et suivent ses changements */
/* en direct. Le paquet xdg-desktop-portal le fournirait, mais pese      */
/* plusieurs demons : on n'en implemente que cette cle.                 */
/* ------------------------------------------------------------------ */
static GDBusConnection *portal_bus;

static const char PORTAL_XML[] =
  "<node><interface name='org.freedesktop.portal.Settings'>"
  "<method name='Read'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
  "<method name='ReadOne'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
  "<method name='ReadAll'><arg type='as' direction='in'/><arg type='a{sa{sv}}' direction='out'/></method>"
  "<signal name='SettingChanged'><arg type='s'/><arg type='s'/><arg type='v'/></signal>"
  "<property name='version' type='u' access='read'/>"
  "</interface></node>";

/* color-scheme : 1 = prefere le sombre, 2 = prefere le clair */
static GVariant *scheme(void) { return g_variant_new_uint32(dark ? 1 : 2); }

/* Reglages lus par les applis via le portail : theme clair/sombre, couleur
   d'accent, et disposition des boutons de fenetre. Sans cette derniere,
   GTK (donc Chromium) applique celle de GNOME, "appmenu:close" : les
   fenetres de Chromium n'avaient que le bouton Fermer. */
static GVariant *portal_value(const char *ns, const char *key) {
  if (!strcmp(ns, "org.freedesktop.appearance")) {
    if (!strcmp(key, "color-scheme")) return scheme();
    if (!strcmp(key, "accent-color")) {
      double r, g, b; hex_rgb(P->pri, &r, &g, &b);
      return g_variant_new("(ddd)", r, g, b);
    }
  } else if (!strcmp(ns, "org.gnome.desktop.wm.preferences")) {
    if (!strcmp(key, "button-layout")) return g_variant_new_string(":minimize,maximize,close");
  } else if (!strcmp(ns, "org.gnome.desktop.interface")) {
    if (!strcmp(key, "color-scheme")) return g_variant_new_string(dark ? "prefer-dark" : "default");
    if (!strcmp(key, "font-name")) return g_variant_new_string("Roboto 10");
    if (!strcmp(key, "gtk-theme")) return g_variant_new_string("Lumina");
    /* Accessibilite : texte plus grand, grand curseur */
    if (!strcmp(key, "text-scaling-factor")) {
      char *p = g_build_filename(g_get_user_config_dir(), "lumina", "bigtext", NULL);
      double f = g_file_test(p, G_FILE_TEST_EXISTS) ? 1.25 : 1.0;
      g_free(p);
      return g_variant_new_double(f);
    }
    if (!strcmp(key, "cursor-size")) {
      char *p = g_build_filename(g_get_user_config_dir(), "lumina", "bigcursor", NULL);
      int c = g_file_test(p, G_FILE_TEST_EXISTS) ? 48 : 24;
      g_free(p);
      return g_variant_new_int32(c);
    }
  }
  return NULL;
}
static const char *PORTAL_KEYS[][2] = {
  {"org.freedesktop.appearance", "color-scheme"}, {"org.freedesktop.appearance", "accent-color"},
  {"org.gnome.desktop.wm.preferences", "button-layout"},
  {"org.gnome.desktop.interface", "color-scheme"}, {"org.gnome.desktop.interface", "font-name"},
  {"org.gnome.desktop.interface", "text-scaling-factor"}, {"org.gnome.desktop.interface", "cursor-size"},
  {"org.gnome.desktop.interface", "gtk-theme"},
};

static void portal_call(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                        const char *method, GVariant *params, GDBusMethodInvocation *inv, gpointer d) {
  if (!strcmp(method, "ReadAll")) {
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));
    for (guint i = 0; i < G_N_ELEMENTS(PORTAL_KEYS); i++) {
      if (i && !strcmp(PORTAL_KEYS[i][0], PORTAL_KEYS[i - 1][0])) continue;
      GVariantBuilder ns;
      g_variant_builder_init(&ns, G_VARIANT_TYPE("a{sv}"));
      for (guint j = i; j < G_N_ELEMENTS(PORTAL_KEYS) && !strcmp(PORTAL_KEYS[j][0], PORTAL_KEYS[i][0]); j++)
        g_variant_builder_add(&ns, "{sv}", PORTAL_KEYS[j][1], portal_value(PORTAL_KEYS[j][0], PORTAL_KEYS[j][1]));
      g_variant_builder_add(&all, "{sa{sv}}", PORTAL_KEYS[i][0], &ns);
    }
    g_dbus_method_invocation_return_value(inv, g_variant_new("(a{sa{sv}})", &all));
    return;
  }
  const char *ns = NULL, *key = NULL;
  g_variant_get(params, "(&s&s)", &ns, &key);
  GVariant *val = portal_value(ns, key);
  if (!val) {
    g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.portal.Error.NotFound", "Requested setting not found");
    return;
  }
  /* L'ancienne methode Read renvoie la valeur enveloppee deux fois */
  GVariant *v = !strcmp(method, "Read") ? g_variant_new_variant(val) : val;
  g_dbus_method_invocation_return_value(inv, g_variant_new("(v)", v));
}

static GVariant *portal_prop(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                             const char *prop, GError **err, gpointer d) {
  return g_variant_new_uint32(2);
}

static const GDBusInterfaceVTable portal_vtable = { portal_call, portal_prop, NULL };

static void on_portal_bus(GDBusConnection *c, const char *name, gpointer d) {
  GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(PORTAL_XML, NULL);
  g_dbus_connection_register_object(c, "/org/freedesktop/portal/desktop", info->interfaces[0],
                                    &portal_vtable, NULL, NULL, NULL);
  g_dbus_node_info_unref(info);
  portal_bus = c;
  register_filechooser(c);
}

static void init_portal(void) {
  g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.portal.Desktop", G_BUS_NAME_OWNER_FLAGS_NONE,
                 on_portal_bus, NULL, NULL, NULL, NULL);
}

/* Theme change (ici ou par une autre appli LuminaOS) : le fond d'ecran, la
   tuile des reglages rapides, les notifications et les applis abonnees au
   portail (Chromium...) suivent. */
/* Recharge la photo seulement si le fichier a change */
static void refresh_wallpaper(void) {
  static gint64 wall_mtime = -1;
  char *wp = lumina_ui_wallpaper_path();
  gint64 mt = 0;
  if (wp) { GStatBuf st; if (g_stat(wp, &st) == 0) mt = (gint64)st.st_mtime * 1000000 + st.st_size; }
  if (mt != wall_mtime) { wall_mtime = mt; load_wallpaper(); }
  g_free(wp);
  wall_light = wall_top_dark();
  update_bar_light();
}

static void on_theme_changed(void) {
  refresh_wallpaper();
  if (walls) for (guint i = 0; i < walls->len; i++) gtk_widget_queue_draw(g_ptr_array_index(walls, i));
  if (qs && gtk_widget_get_visible(qs)) refresh_qs();
  if (portal_bus)
    g_dbus_connection_emit_signal(portal_bus, NULL, "/org/freedesktop/portal/desktop",
                                  "org.freedesktop.portal.Settings", "SettingChanged",
                                  g_variant_new("(ssv)", "org.freedesktop.appearance", "color-scheme", scheme()), NULL);
  if (portal_bus)
    g_dbus_connection_emit_signal(portal_bus, NULL, "/org/freedesktop/portal/desktop",
                                  "org.freedesktop.portal.Settings", "SettingChanged",
                                  g_variant_new("(ssv)", "org.freedesktop.appearance", "accent-color",
                                                portal_value("org.freedesktop.appearance", "accent-color")), NULL);
  const char *argv[] = {"makoctl", "mode", dark ? "-a" : "-r", "dark", NULL};
  spawn_async(argv);
}

static void on_dark_tile(GtkButton *b, gpointer d) { lumina_ui_set_dark(!dark); }

static void on_android_tile(GtkButton *b, gpointer d) {
  const char *argv[] = {"lumina-android", NULL};
  spawn_async(argv);
  close_panels();
}

static gboolean bright_apply(gpointer d) {
  char *v = g_strdup_printf("%d%%", (int)gtk_range_get_value(GTK_RANGE(bright_scale)));
  const char *argv[] = {"brightnessctl", "-q", "set", v, NULL};
  spawn_async(argv); g_free(v);
  bright_timer = 0; return G_SOURCE_REMOVE;
}
static void on_bright(GtkRange *r, gpointer d) { if (!bright_timer) bright_timer = g_timeout_add(80, bright_apply, NULL); }

static gboolean vol_apply(gpointer d) {
  char *v = g_strdup_printf("%.2f", gtk_range_get_value(GTK_RANGE(vol_scale)) / 100.0);
  const char *argv[] = {"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", v, NULL};
  spawn_async(argv); g_free(v);
  vol_timer = 0; return G_SOURCE_REMOVE;
}
static void on_vol(GtkRange *r, gpointer d) { if (!vol_timer) vol_timer = g_timeout_add(80, vol_apply, NULL); }

/* -1 si pas de retroeclairage pilotable (PC de bureau, VM). */
static int brightness_pct(void) {
  GDir *d = g_dir_open("/sys/class/backlight", 0, NULL);
  gboolean any = d && g_dir_read_name(d);
  if (d) g_dir_close(d);
  if (!any) return -1;
  const char *argv[] = {"brightnessctl", "-m", NULL};
  char *out = run_argv(argv, NULL);
  int pct = -1;
  if (out) { char **f = g_strsplit(out, ",", -1); if (g_strv_length(f) >= 4) pct = atoi(f[3]); g_strfreev(f); g_free(out); }
  return pct;
}

static int volume_pct(void) {
  const char *argv[] = {"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL};
  int st = -1;
  char *out = run_argv(argv, &st);
  int pct = -1;
  if (out && st == 0 && g_str_has_prefix(out, "Volume:")) pct = (int)lround(g_ascii_strtod(out + 7, NULL) * 100);
  g_free(out);
  return pct;
}

/* RAM : /proc/meminfo ; zram : /sys/block/zram0/mm_stat
   (orig_data_size compr_data_size mem_used_total ...). La memoire tenue
   par zram fait partie de la RAM utilisee : on l'isole dans la jauge. */
static void read_mem(double *total, double *used, double *zram_mem, double *zram_orig) {
  *total = *used = *zram_mem = *zram_orig = 0;
  char *mi = read_file("/proc/meminfo");
  double avail = 0;
  if (mi) {
    char **lines = g_strsplit(mi, "\n", -1);
    for (char **p = lines; *p; p++) {
      if (g_str_has_prefix(*p, "MemTotal:")) *total = g_ascii_strtod(*p + 9, NULL) * 1024;
      if (g_str_has_prefix(*p, "MemAvailable:")) avail = g_ascii_strtod(*p + 13, NULL) * 1024;
    }
    g_strfreev(lines); g_free(mi);
  }
  *used = *total - avail;
  char *zs = read_file("/sys/block/zram0/mm_stat");
  if (zs) {
    char **f = g_strsplit_set(zs, " \t", -1), *v[8] = {0}; int n = 0;
    for (char **p = f; *p && n < 8; p++) if (**p) v[n++] = *p;
    if (n >= 3) { *zram_orig = g_ascii_strtod(v[0], NULL); *zram_mem = g_ascii_strtod(v[2], NULL); }
    g_strfreev(f); g_free(zs);
  }
}

static gboolean draw_gauge(GtkWidget *w, cairo_t *cr, gpointer d) {
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), r, g, b;
  cairo_new_sub_path(cr);
  cairo_arc(cr, W - H / 2, H / 2, H / 2, -M_PI / 2, M_PI / 2);
  cairo_arc(cr, H / 2, H / 2, H / 2, M_PI / 2, 3 * M_PI / 2);
  cairo_close_path(cr);
  cairo_clip(cr);
  hex_rgb(P->c_top, &r, &g, &b); cairo_set_source_rgb(cr, r, g, b); cairo_paint(cr);
  hex_rgb(P->pri, &r, &g, &b); cairo_set_source_rgb(cr, r, g, b);
  cairo_rectangle(cr, 0, 0, W * mem_u, H); cairo_fill(cr);
  hex_rgb(P->ter, &r, &g, &b); cairo_set_source_rgb(cr, r, g, b);
  cairo_rectangle(cr, W * mem_u, 0, W * mem_z, H); cairo_fill(cr);
  return FALSE;
}

static void refresh_mem(void) {
  double total, used, zmem, zorig;
  read_mem(&total, &used, &zmem, &zorig);
  if (total <= 0) return;
  mem_u = MAX(0, used - zmem) / total;
  mem_z = zmem / total;
  char *u = fmt_go(used), *t = fmt_go(total), *tt = g_strdup_printf("sur %s", t);
  gtk_label_set_text(GTK_LABEL(mem_used_l), u);
  gtk_label_set_text(GTK_LABEL(mem_total_l), tt);
  char *z;
  /* zram garde quelques Ko de gestion interne meme vide : sous 1 Mo,
     le ratio de compression n'a pas de sens. */
  if (zmem >= 1048576) {
    char *ratio = g_strdup_printf("%.1f", zorig / zmem);
    for (char *p = ratio; *p; p++) if (*p == '.') *p = ',';
    z = g_strdup_printf("Compressée (zram) · %.0f Mo, ×%s", zmem / 1048576.0, ratio);
    g_free(ratio);
  } else z = g_strdup("Compressée (zram) · 0 Mo");
  gtk_label_set_text(GTK_LABEL(zram_l), z);
  gtk_widget_queue_draw(mem_gauge);
  g_free(u); g_free(t); g_free(tt); g_free(z);
}

static gboolean mem_tick(gpointer d) { if (gtk_widget_get_visible(qs)) refresh_mem(); return G_SOURCE_CONTINUE; }

static void refresh_qs(void) {
  refresh_extras_qs();
  fill_notifs();
  tick(NULL);
  if (wifi_dev) {
    gboolean on = net_wifi_powered(wifi_dev);
    char *ssid = on ? net_wifi_ssid(wifi_dev) : NULL;
    gtk_label_set_text(GTK_LABEL(wifi_sub), !on ? "Désactivé" : ssid ? ssid : "Non connecté");
    set_class(wifi_tile, "on", ssid != NULL);
    icon_set(wifi_ic, on ? "wifi" : "wifi-off", ssid ? R_ON_PRI_C : R_ON);
    g_free(ssid);
  }

  set_class(dark_tile, "on", dark);
  gtk_label_set_text(GTK_LABEL(dark_sub), dark ? "Activé" : "Désactivé");

  /* Meme seuil que lumina-android --install : 4 Go (3,5 Go visibles). */
  char *wd = g_find_program_in_path("waydroid");
  double total, used, zmem, zorig;
  read_mem(&total, &used, &zmem, &zorig);
  gboolean enough = total >= 3500.0 * 1048576;
  gtk_widget_set_sensitive(android_tile, wd != NULL);
  gtk_widget_set_tooltip_text(android_tile, wd ? NULL : enough
      ? "Installation : sudo lumina-android --install"
      : "Android demande au moins 4 Go de RAM : il ralentirait tout le système sur cette machine.");
  gtk_label_set_text(GTK_LABEL(android_sub), wd ? "Ouvrir" : enough ? "Non installé" : "Requiert 4 Go");
  g_free(wd);

  int bp = brightness_pct();
  gtk_widget_set_visible(bright_row, bp >= 0);
  if (bp >= 0) {
    g_signal_handlers_block_by_func(bright_scale, on_bright, NULL);
    gtk_range_set_value(GTK_RANGE(bright_scale), bp);
    g_signal_handlers_unblock_by_func(bright_scale, on_bright, NULL);
  }
  /* Version installee, et mise a jour prete a l'emploi au redemarrage */
  char *ver = read_file("/etc/lumina/version"), *st = read_file("/run/lumina-update/status");
  gboolean ready = st && g_str_has_prefix(st, "ready ");
  char *txt;
  if (ready) txt = g_strdup_printf("Mise à jour %s prête", st + 6);
  else if (st && g_str_has_prefix(st, "downloading ")) txt = g_strdup_printf("Téléchargement de la version %s…", st + 12);
  else if (st && g_str_has_prefix(st, "error ")) txt = g_strdup_printf("LuminaOS %s · %s", ver ? ver : "?", st + 6);
  else txt = g_strdup_printf("LuminaOS %s%s", ver ? ver : "?", st && g_str_has_prefix(st, "uptodate") ? " · à jour" : "");
  gtk_label_set_text(GTK_LABEL(upd_l), txt);
  set_class(upd_row, "ready", ready);
  gtk_widget_set_visible(upd_btn, ready);
  g_free(ver); g_free(st); g_free(txt);

  int vp = volume_pct();
  gtk_widget_set_visible(vol_row, vp >= 0);
  if (vp >= 0) {
    g_signal_handlers_block_by_func(vol_scale, on_vol, NULL);
    gtk_range_set_value(GTK_RANGE(vol_scale), vp);
    g_signal_handlers_unblock_by_func(vol_scale, on_vol, NULL);
  }
  refresh_mem();
}

static void on_power(GtkButton *b, gpointer d) {
  if (!strcmp(d, "lock")) {
    close_panels();
    /* Sans mot de passe, lumina-lock ne fait rien : on le dit */
    char *a = read_file("/var/lib/lumina/config/account.conf");
    gboolean pw = a && strstr(a, "HAS_PASSWORD=1");
    g_free(a);
    const char *argv[] = {"lumina-lock", NULL};
    const char *nv[] = {"notify-send", "-a", "LuminaOS", "Aucun mot de passe",
                        "Définissez un mot de passe ou un code PIN dans Réglages › Compte pour verrouiller l'écran.", NULL};
    spawn_async(pw ? argv : nv);
    return;
  }
  if (!strcmp(d, "logout")) {
    const char *argv[] = {"labwc", "--exit", NULL};
    spawn_async(argv);
    return;
  }
  const char *argv[] = {"systemctl", d, NULL};
  spawn_async(argv);
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
static void on_open_settings(GtkButton *b, gpointer d) {
  close_panels();
  const char *argv[] = {"lumina-settings", NULL};
  spawn_async(argv);
}

static GtkWidget *tile(const char *glyph, const char *title, GtkWidget **sub_out, Icon **ic_out) {
  GtkWidget *b = gtk_button_new();
  add_class(b, "tile");
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  Icon *ic = icon_new(glyph, R_ON, 20);
  gtk_container_add(GTK_CONTAINER(h), ic->img);
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
  gtk_container_add(GTK_CONTAINER(v), label(title, "title", 0));
  GtkWidget *sub = label("", "sub", 0);
  gtk_label_set_ellipsize(GTK_LABEL(sub), PANGO_ELLIPSIZE_END);
  gtk_container_add(GTK_CONTAINER(v), sub);
  gtk_container_add(GTK_CONTAINER(h), v);
  gtk_container_add(GTK_CONTAINER(b), h);
  gtk_widget_set_hexpand(b, TRUE);
  if (sub_out) *sub_out = sub;
  if (ic_out) *ic_out = ic;
  return b;
}

static GtkWidget *slider_row(const char *glyph, GtkWidget **scale_out, GCallback cb) {
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  add_class(h, "slider-row");
  gtk_container_add(GTK_CONTAINER(h), icon_new(glyph, R_ON_VAR, 20)->img);
  GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
  gtk_scale_set_draw_value(GTK_SCALE(s), FALSE);
  gtk_widget_set_hexpand(s, TRUE);
  g_signal_connect(s, "value-changed", cb, NULL);
  gtk_container_add(GTK_CONTAINER(h), s);
  *scale_out = s;
  return h;
}

/* ------------------------------------------------------------------ */
/* Plusieurs ecrans (bureau etendu, comme ChromeOS) : un fond d'ecran    */
/* sur chacun ; barre d'etat, etagere et panneaux sur l'ecran principal */
/* (le premier : l'ecran integre d'un portable). Tout est replace quand */
/* un ecran est branche ou debranche.                                   */
/* ------------------------------------------------------------------ */
static GdkMonitor *main_monitor(void) {
  return gdk_display_get_monitor(gdk_display_get_default(), 0);
}

/* Clic droit sur le bureau : comme ChromeOS, acces au fond d'ecran */
static void on_wall_menu(GtkMenuItem *m, gpointer d) {
  const char *argv[] = {"lumina-settings", "affichage", NULL};
  spawn_async(argv);
}
static void menu_done(GtkMenuShell *m, gpointer d);
static gboolean on_wall_press(GtkWidget *w, GdkEventButton *e, gpointer d) {
  if (e->type == GDK_BUTTON_PRESS && e->button == GDK_BUTTON_SECONDARY) {
    close_panels();
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *mi = gtk_menu_item_new_with_label("Fond d'écran et style…");
    g_signal_connect(mi, "activate", G_CALLBACK(on_wall_menu), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    gtk_widget_show_all(menu);
    g_signal_connect(menu, "deactivate", G_CALLBACK(menu_done), NULL);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)e);
    return TRUE;
  }
  return on_scrim_press(w, (GdkEvent *)e, d);
}

static void build_wallpapers(void) {
  if (!walls) walls = g_ptr_array_new_with_free_func((GDestroyNotify)gtk_widget_destroy);
  g_ptr_array_set_size(walls, 0);
  GdkDisplay *dpy = gdk_display_get_default();
  int n = gdk_display_get_n_monitors(dpy);
  for (int i = 0; i < n; i++) {
    GtkWidget *w = layer_window(GTK_LAYER_SHELL_LAYER_BACKGROUND, "lumina-wallpaper");
    gtk_layer_set_monitor(GTK_WINDOW(w), gdk_display_get_monitor(dpy, i));
    anchor(w, TRUE, TRUE, TRUE, TRUE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(w), -1);
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(area, "draw", G_CALLBACK(draw_wall), NULL);
    g_signal_connect(area, "button-press-event", G_CALLBACK(on_wall_press), NULL);
    gtk_container_add(GTK_CONTAINER(w), area);
    gtk_widget_show_all(w);
    g_ptr_array_add(walls, w);
  }
}

static gboolean relayout_monitors(gpointer d) {
  build_wallpapers();
  GdkMonitor *m = main_monitor();
  GtkWidget *ws[] = {bar_win, shelf_win, scrim, drawer, qs};
  for (guint i = 0; i < G_N_ELEMENTS(ws); i++) {
    if (!ws[i]) continue;
    gtk_layer_set_monitor(GTK_WINDOW(ws[i]), m);
    /* Surface perdue avec un ecran debranche : on la recree */
    if (gtk_widget_get_visible(ws[i]) && (ws[i] == bar_win || ws[i] == shelf_win)) {
      gtk_widget_hide(ws[i]); gtk_widget_show(ws[i]);
    }
  }
  return G_SOURCE_REMOVE;
}

static guint relayout_pending;
static gboolean relayout_cb(gpointer d) { relayout_pending = 0; return relayout_monitors(d); }

static void on_monitors_changed(GdkDisplay *dpy, GdkMonitor *mon, gpointer d) {
  /* Plusieurs signaux d'affilee au branchement : un seul replacement */
  if (relayout_pending) g_source_remove(relayout_pending);
  relayout_pending = g_timeout_add(300, relayout_cb, NULL);
}

static void on_tray_clicked(GtkButton *b, gpointer d) { toggle_panel(qs); }
static void calendar_toggle(void);
static void on_clock_clicked(GtkButton *b, gpointer d) { calendar_toggle(); }

static void on_launch_clicked(GtkButton *b, gpointer d) { toggle_panel(drawer); }

/* Barre d'etat fine : lanceur et heure a gauche, reglages rapides a droite. */

static void build_bar(void) {
  GtkWidget *w = bar_win = layer_window(GTK_LAYER_SHELL_LAYER_TOP, "lumina-bar");
  gtk_layer_set_monitor(GTK_WINDOW(w), main_monitor());
  anchor(w, TRUE, FALSE, TRUE, TRUE);
  gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(w));
  GtkWidget *h = bar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  add_class(h, "bar");
  gtk_widget_set_size_request(h, -1, 28);

  launch_btn = gtk_button_new();
  add_class(launch_btn, "launch");
  gtk_widget_set_valign(launch_btn, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(launch_btn, "Applications (touche Windows)");
  gtk_container_add(GTK_CONTAINER(launch_btn), icon_new("grid", R_BAR, 15)->img);
  g_signal_connect(launch_btn, "clicked", G_CALLBACK(on_launch_clicked), NULL);
  gtk_container_add(GTK_CONTAINER(h), launch_btn);

  /* Heure et date : un clic ouvre le calendrier (comme ChromeOS) */
  clock_l = label("", NULL, 0); date_l = label("", "date", 0);
  GtkWidget *cb = gtk_button_new();
  add_class(cb, "clockbtn");
  gtk_widget_set_valign(cb, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(cb, "Calendrier");
  GtkWidget *ch = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_container_add(GTK_CONTAINER(ch), clock_l);
  gtk_container_add(GTK_CONTAINER(ch), date_l);
  gtk_container_add(GTK_CONTAINER(cb), ch);
  g_signal_connect(cb, "clicked", G_CALLBACK(on_clock_clicked), NULL);
  gtk_container_add(GTK_CONTAINER(h), cb);
  GtkWidget *sp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(sp, TRUE);
  gtk_container_add(GTK_CONTAINER(h), sp);

  extras_bar_end(h);
  extras_bar(h);      /* vue d'ensemble / bureaux, a cote des reglages rapides */
  tray_btn = gtk_button_new();
  add_class(tray_btn, "tray");
  gtk_widget_set_valign(tray_btn, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(tray_btn, "Réglages rapides (Windows + A)");
  GtkWidget *t = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
  tray_net_ic = icon_new("wifi", R_BAR, 14);
  tray_batt_ic = icon_new("svg:", R_BAR, 14);
  batt_l = label("", NULL, 0);
  gtk_container_add(GTK_CONTAINER(t), tray_net_ic->img);
  gtk_container_add(GTK_CONTAINER(t), tray_batt_ic->img);
  gtk_container_add(GTK_CONTAINER(t), batt_l);
  gtk_container_add(GTK_CONTAINER(tray_btn), t);
  g_signal_connect(tray_btn, "clicked", G_CALLBACK(on_tray_clicked), NULL);
  gtk_container_add(GTK_CONTAINER(h), tray_btn);

  gtk_container_add(GTK_CONTAINER(w), h);
  gtk_widget_show_all(w);
  update_bar_light();
}

/* ------------------------------------------------------------------ */
/* Fenetres ouvertes : protocole wlr-foreign-toplevel-management        */
/* labwc annonce chaque fenetre, son appli, et ses etats (agrandie,     */
/* reduite, active, plein ecran) ; on peut aussi l'activer ou la        */
/* reduire. C'est ce qui alimente l'etagere et la teinte de la barre.   */
/* ------------------------------------------------------------------ */
typedef struct {
  struct zwlr_foreign_toplevel_handle_v1 *h;
  char *app_id, *title;
  gboolean maximized, minimized, activated, fullscreen;
  guint64 last_active;           /* ordre d'activation */
} Toplevel;

static GList *toplevels;
static struct wl_seat *seat;
static guint64 activation_seq;
static GtkWidget *shelf_apps;
static guint shelf_idle;

static gboolean refresh_shelf(gpointer d);
static void schedule_shelf(void) { if (!shelf_idle) shelf_idle = g_idle_add(refresh_shelf, NULL); }

static void tl_title(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *t) {
  Toplevel *tl = d; g_free(tl->title); tl->title = g_strdup(t);
}
static void tl_app_id(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, const char *a) {
  Toplevel *tl = d; g_free(tl->app_id); tl->app_id = g_strdup(a);
}
static void tl_output_enter(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {}
static void tl_output_leave(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {}
static void tl_state(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *states) {
  Toplevel *tl = d;
  gboolean was_active = tl->activated;
  tl->maximized = tl->minimized = tl->activated = tl->fullscreen = FALSE;
  uint32_t *st;
  wl_array_for_each(st, states) {
    switch (*st) {
      case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:  tl->maximized = TRUE; break;
      case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:  tl->minimized = TRUE; break;
      case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:  tl->activated = TRUE; break;
      case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN: tl->fullscreen = TRUE; break;
    }
  }
  if (tl->activated && !was_active) tl->last_active = ++activation_seq;
}
static void tl_done(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
  Toplevel *tl = d;
  g_debug("fenetre : app_id=%s titre=%s%s%s", tl->app_id, tl->title,
          tl->maximized ? " [agrandie]" : "", tl->activated ? " [active]" : "");
  schedule_shelf();
}
static void tl_closed(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {
  Toplevel *tl = d;
  toplevels = g_list_remove(toplevels, tl);
  zwlr_foreign_toplevel_handle_v1_destroy(h);
  g_free(tl->app_id); g_free(tl->title); g_free(tl);
  schedule_shelf();
}
static void tl_parent(void *d, struct zwlr_foreign_toplevel_handle_v1 *h,
                      struct zwlr_foreign_toplevel_handle_v1 *parent) {}

static const struct zwlr_foreign_toplevel_handle_v1_listener tl_listener = {
  tl_title, tl_app_id, tl_output_enter, tl_output_leave, tl_state, tl_done, tl_closed, tl_parent };

static void mgr_toplevel(void *d, struct zwlr_foreign_toplevel_manager_v1 *m,
                         struct zwlr_foreign_toplevel_handle_v1 *h) {
  Toplevel *tl = g_new0(Toplevel, 1);
  tl->h = h;
  toplevels = g_list_append(toplevels, tl);
  zwlr_foreign_toplevel_handle_v1_add_listener(h, &tl_listener, tl);
}
static void mgr_finished(void *d, struct zwlr_foreign_toplevel_manager_v1 *m) {
  zwlr_foreign_toplevel_manager_v1_destroy(m);
}
static const struct zwlr_foreign_toplevel_manager_v1_listener mgr_listener = { mgr_toplevel, mgr_finished };

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
  if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
    struct zwlr_foreign_toplevel_manager_v1 *m =
      wl_registry_bind(r, name, &zwlr_foreign_toplevel_manager_v1_interface, MIN(ver, 3));
    zwlr_foreign_toplevel_manager_v1_add_listener(m, &mgr_listener, NULL);
  }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

static void init_toplevels(void) {
  GdkDisplay *gd = gdk_display_get_default();
  if (!GDK_IS_WAYLAND_DISPLAY(gd)) return;
  struct wl_display *dpy = gdk_wayland_display_get_wl_display(gd);
  seat = gdk_wayland_seat_get_wl_seat(gdk_display_get_default_seat(gd));
  struct wl_registry *reg = wl_display_get_registry(dpy);
  wl_registry_add_listener(reg, &reg_listener, NULL);
  wl_display_roundtrip(dpy);
}

/* ------------------------------------------------------------------ */
/* Etagere                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
  GDesktopAppInfo *app;   /* NULL : fenetre d'une appli inconnue */
  char *key;              /* identifiant .desktop, ou app_id de la fenetre */
  GList *wins;            /* Toplevel* de cette appli */
} ShelfItem;

static GPtrArray *shelf_items;

static void shelf_item_free(gpointer p) {
  ShelfItem *it = p;
  if (it->app) g_object_unref(it->app);
  g_free(it->key); g_list_free(it->wins); g_free(it);
}

/* Nom que Chromium donne a la fenetre d'une appli web ouverte par
   "lumina-webapp URL" : chrome-<domaine>_<chemin, / devenus _>-Default.
   Ex. https://mail.google.com -> chrome-mail.google.com__-Default
       file:///home/lumina/   -> chrome-__home_lumina_-Default */
static char *webapp_window_id(GAppInfo *a) {
  const char *cmd = g_app_info_get_commandline(a);
  if (!cmd || !g_str_has_prefix(cmd, "lumina-webapp ")) return NULL;
  char **argv = g_strsplit(cmd + 14, " ", 2);
  GUri *u = argv[0] ? g_uri_parse(argv[0], G_URI_FLAGS_NONE, NULL) : NULL;
  g_strfreev(argv);
  if (!u) return NULL;
  const char *host = g_uri_get_host(u), *path = g_uri_get_path(u);
  char *p = g_strdup(path && *path ? path : "/");
  g_strdelimit(p, "/", '_');
  char *id = g_strdup_printf("chrome-%s_%s-Default", host ? host : "", p);
  g_free(p); g_uri_unref(u);
  return id;
}

/* Rapprochement approximatif, quand aucune regle exacte n'a trouve : beaucoup
   d'applis Linux annoncent une fenetre "mousepad" pour une entree
   "org.xfce.mousepad.desktop" sans StartupWMClass. On compare le nom de la
   fenetre au dernier segment de l'identifiant, a l'icone et au programme
   lance (celui lance DANS le conteneur, pour une appli Distrobox). */
static gboolean fuzzy_match(GAppInfo *a, const char *app_id) {
  gboolean hit = FALSE;
  const char *id = g_app_info_get_id(a);
  if (id) {
    char *base = g_strdup(id);
    if (g_str_has_suffix(base, ".desktop")) base[strlen(base) - 8] = 0;
    const char *last = strrchr(base, '.');
    hit = !g_ascii_strcasecmp(last ? last + 1 : base, app_id) ||
          !g_ascii_strcasecmp(g_str_has_prefix(base, "linux-") ? base + 6 : base, app_id);
    g_free(base);
  }
  char *icon = G_IS_DESKTOP_APP_INFO(a) ? g_desktop_app_info_get_string(G_DESKTOP_APP_INFO(a), "Icon") : NULL;
  if (!hit && icon) {
    const char *last = strrchr(icon, '.');
    hit = !g_ascii_strcasecmp(icon, app_id) || (last && !g_ascii_strcasecmp(last + 1, app_id));
  }
  g_free(icon);
  const char *cmd = g_app_info_get_commandline(a);
  if (!hit && cmd) {
    const char *inner = strstr(cmd, " -- ");
    char *rest = g_strstrip(g_strdup(inner ? inner + 4 : cmd));
    char **argv = g_strsplit(rest, " ", 2);
    if (argv[0] && *argv[0]) {
      char *bn = g_path_get_basename(argv[0]);
      hit = !g_ascii_strcasecmp(bn, app_id);
      g_free(bn);
    }
    g_strfreev(argv);
    g_free(rest);
  }
  return hit;
}

/* Appli d'une fenetre, parmi les applis visibles : nom de fenetre d'appli
   web, StartupWMClass, puis identifiant .desktop. Les entrees masquees
   (ex. chromium.desktop, remplacee par "Navigateur") sont ignorees. */
/* Identifiant d'appli Chromium (32 lettres a-p) contenu dans un nom de
   fenetre ou de fichier : "chrome-<id>-Default", "crx_<id>",
   "chromium-<id>-Default.desktop"... */
static gboolean crx_id(const char *s, char out[33]) {
  if (!s) return FALSE;
  for (const char *p = s; *p; p++) {
    int n = 0;
    while (p[n] >= 'a' && p[n] <= 'p' && n < 32) n++;
    if (n == 32 && (p[n] < 'a' || p[n] > 'z')) { memcpy(out, p, 32); out[32] = 0; return TRUE; }
    if (n) p += n - 1;
  }
  return FALSE;
}

static GDesktopAppInfo *app_for_window(const char *app_id, GList *all) {
  if (!app_id || !*app_id) return NULL;
  /* PWA installee : Chromium ne nomme pas sa fenetre comme son raccourci
     (chrome-ID-Default contre chromium-ID-Default.desktop / crx_ID). On
     rapproche par l'identifiant d'appli. */
  char want[33];
  if ((g_str_has_prefix(app_id, "chrome-") || g_str_has_prefix(app_id, "chromium-") || g_str_has_prefix(app_id, "crx_"))
      && crx_id(app_id, want)) {
    for (GList *l = all; l; l = l->next) {
      if (!G_IS_DESKTOP_APP_INFO(l->data)) continue;
      char got[33];
      const char *wm = g_desktop_app_info_get_startup_wm_class(l->data), *id = g_app_info_get_id(l->data);
      if ((crx_id(wm, got) && !strcmp(got, want)) || (crx_id(id, got) && !strcmp(got, want)))
        return g_object_ref(l->data);
    }
  }
  for (GList *l = all; l; l = l->next) {
    if (!G_IS_DESKTOP_APP_INFO(l->data) || !g_app_info_should_show(l->data)) continue;
    const char *wm = g_desktop_app_info_get_startup_wm_class(l->data);
    char *wid = webapp_window_id(l->data);
    gboolean hit = (wm && g_ascii_strcasecmp(wm, app_id) == 0) || g_strcmp0(wid, app_id) == 0;
    g_free(wid);
    if (hit) return g_object_ref(l->data);
  }
  /* Identifiant .desktop, puis entree publiee depuis le conteneur Linux
     (Distrobox la nomme "linux-<appli>.desktop"). */
  const char *forms[] = {"%s.desktop", "linux-%s.desktop"};
  for (guint i = 0; i < G_N_ELEMENTS(forms); i++) {
    char *id = g_strdup_printf(forms[i], app_id);
    GDesktopAppInfo *a = g_desktop_app_info_new(id);
    g_free(id);
    if (a && g_app_info_should_show(G_APP_INFO(a))) return a;
    g_clear_object(&a);
  }
  for (GList *l = all; l; l = l->next)
    if (G_IS_DESKTOP_APP_INFO(l->data) && g_app_info_should_show(l->data) && fuzzy_match(l->data, app_id))
      return g_object_ref(l->data);
  return NULL;
}

static ShelfItem *shelf_find(const char *key) {
  for (guint i = 0; i < shelf_items->len; i++) {
    ShelfItem *it = shelf_items->pdata[i];
    if (g_strcmp0(it->key, key) == 0) return it;
  }
  return NULL;
}

static ShelfItem *shelf_add(GDesktopAppInfo *a, const char *key) {
  ShelfItem *it = g_new0(ShelfItem, 1);
  it->app = a; it->key = g_strdup(key);
  g_ptr_array_add(shelf_items, it);
  return it;
}

static Toplevel *most_recent(GList *wins) {
  Toplevel *best = NULL;
  for (GList *l = wins; l; l = l->next) {
    Toplevel *t = l->data;
    if (!best || t->last_active > best->last_active) best = t;
  }
  return best;
}

static Toplevel *least_recent(GList *wins) {
  Toplevel *best = NULL;
  for (GList *l = wins; l; l = l->next) {
    Toplevel *t = l->data;
    if (!best || t->last_active < best->last_active) best = t;
  }
  return best;
}

static void activate(Toplevel *t) {
  if (t->minimized) zwlr_foreign_toplevel_handle_v1_unset_minimized(t->h);
  if (seat) zwlr_foreign_toplevel_handle_v1_activate(t->h, seat);
}

/* Comme sur ChromeOS : appli fermee -> lancer ; fenetre en arriere-plan ->
   la ramener ; fenetre deja active -> la reduire, ou passer a la suivante
   s'il y en a plusieurs. */
static void on_shelf_clicked(GtkButton *b, gpointer d) {
  ShelfItem *it = d;
  close_panels();
  if (!it->wins) { if (it->app) launch(G_APP_INFO(it->app)); return; }
  Toplevel *top = most_recent(it->wins);
  if (top->activated && !top->minimized) {
    if (g_list_length(it->wins) > 1) activate(least_recent(it->wins));
    else zwlr_foreign_toplevel_handle_v1_set_minimized(top->h);
  } else {
    activate(top);
  }
}

static GtkWidget *letter_icon(const char *name) {
  RoundIcon *ri = g_new0(RoundIcon, 1);
  ri->filled = TRUE;
  hex_rgb("#5f6368", &ri->r, &ri->g, &ri->b);
  g_unichar_to_utf8(g_unichar_toupper(g_utf8_get_char(name && *name ? name : "?")), ri->letter);
  return round_widget(ri, 38);
}


/* Applis epinglees : ~/.config/lumina/shelf (une par ligne, dans l'ordre),
   cree au premier changement ; d'ici la, la liste par defaut du systeme. */
static char *pins_path(void) { return g_build_filename(g_get_user_config_dir(), "lumina", "shelf", NULL); }

static GPtrArray *read_pins(void) {
  char *p = pins_path(), *conf = read_file(p);
  if (!conf) conf = read_file("/etc/lumina/dock.conf");
  GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
  char **ids = conf ? g_strsplit(conf, "\n", -1) : NULL;
  for (char **i = ids; i && *i; i++) {
    char *id = g_strstrip(*i);
    if (*id && *id != '#') g_ptr_array_add(a, g_strdup(id));
  }
  g_strfreev(ids); g_free(conf); g_free(p);
  return a;
}

static gboolean is_pinned(const char *id) {
  GPtrArray *a = read_pins();
  gboolean r = FALSE;
  for (guint i = 0; i < a->len && !r; i++) r = !g_strcmp0(a->pdata[i], id);
  g_ptr_array_free(a, TRUE);
  return r;
}

static void set_pinned(const char *id, gboolean on) {
  GPtrArray *a = read_pins();
  GString *out = g_string_new("# Applis epinglees dans l'etagere, dans l'ordre.\n");
  for (guint i = 0; i < a->len; i++)
    if (g_strcmp0(a->pdata[i], id)) g_string_append_printf(out, "%s\n", (char *)a->pdata[i]);
  if (on) g_string_append_printf(out, "%s\n", id);
  char *p = pins_path(), *dir = g_path_get_dirname(p);
  g_mkdir_with_parents(dir, 0755);
  g_file_set_contents(p, out->str, -1, NULL);
  g_free(p); g_free(dir); g_string_free(out, TRUE); g_ptr_array_free(a, TRUE);
  schedule_shelf();
}

static void on_menu_pin(GtkMenuItem *m, gpointer key) { set_pinned(key, TRUE); }
static void on_menu_unpin(GtkMenuItem *m, gpointer key) { set_pinned(key, FALSE); }
static void on_menu_new(GtkMenuItem *m, gpointer key) {
  GDesktopAppInfo *a = g_desktop_app_info_new(key);
  close_panels();
  if (a) { launch(G_APP_INFO(a)); g_object_unref(a); }
}
static void on_menu_uninstall(GtkMenuItem *m, gpointer key) {
  close_panels();
  const char *argv[] = {"lumina-uninstall", key, NULL};
  spawn_async(argv);
}
static ShelfItem *shelf_find(const char *key);
static void on_menu_close(GtkMenuItem *m, gpointer key) {
  ShelfItem *it = shelf_find(key);
  if (it) for (GList *l = it->wins; l; l = l->next) zwlr_foreign_toplevel_handle_v1_close(((Toplevel *)l->data)->h);
}

static gboolean menu_destroy_idle(gpointer m) { gtk_widget_destroy(m); return G_SOURCE_REMOVE; }
static void menu_done(GtkMenuShell *m, gpointer d) { g_idle_add(menu_destroy_idle, m); }

/* Menu du clic droit, sur l'etagere comme dans le tiroir (facon ChromeOS). */
static gboolean on_app_menu(GtkWidget *w, GdkEventButton *e, gpointer key) {
  if (e->type != GDK_BUTTON_PRESS || e->button != GDK_BUTTON_SECONDARY) return FALSE;
  GtkWidget *menu = gtk_menu_new();
  add_class(menu, "appmenu");
  char *k = g_strdup(key);
  g_object_set_data_full(G_OBJECT(menu), "key", k, g_free);
  GDesktopAppInfo *a = g_desktop_app_info_new(k);
  ShelfItem *it = shelf_find(k);
  GtkWidget *mi;
  if (a) {
    mi = gtk_menu_item_new_with_label(it && it->wins ? "Nouvelle fenêtre" : "Ouvrir");
    g_signal_connect(mi, "activate", G_CALLBACK(on_menu_new), k);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    gboolean pinned = is_pinned(k);
    mi = gtk_menu_item_new_with_label(pinned ? "Retirer de l'étagère" : "Épingler à l'étagère");
    g_signal_connect(mi, "activate", G_CALLBACK(pinned ? on_menu_unpin : on_menu_pin), k);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    /* Applis web installees, applis Linux et Android : desinstallables */
    if (g_str_has_prefix(k, "chrome-") || g_str_has_prefix(k, "chromium-") || g_str_has_prefix(k, "linux-") ||
        g_str_has_prefix(k, "waydroid.")) {
      mi = gtk_menu_item_new_with_label("Désinstaller");
      g_signal_connect(mi, "activate", G_CALLBACK(on_menu_uninstall), k);
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    g_object_unref(a);
  }
  if (it && it->wins) {
    mi = gtk_menu_item_new_with_label(g_list_length(it->wins) > 1 ? "Fermer toutes les fenêtres" : "Fermer");
    g_signal_connect(mi, "activate", G_CALLBACK(on_menu_close), k);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
  }
  gtk_widget_show_all(menu);
  /* "deactivate" arrive AVANT "activate" de l'element choisi : detruire le
     menu tout de suite annulerait le choix. On le detruit juste apres. */
  g_signal_connect(menu, "deactivate", G_CALLBACK(menu_done), NULL);
  gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)e);
  return TRUE;
}

static gboolean refresh_shelf(gpointer d) {
  shelf_idle = 0;

  /* Les boutons pointent sur les elements : on les detruit d'abord. */
  GList *kids = gtk_container_get_children(GTK_CONTAINER(shelf_apps));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  g_ptr_array_set_size(shelf_items, 0);

  /* 1. Applis epinglees, dans l'ordre choisi par l'utilisateur */
  GPtrArray *pins = read_pins();
  for (guint i = 0; i < pins->len; i++) {
    const char *id = pins->pdata[i];
    if (shelf_find(id)) continue;
    GDesktopAppInfo *a = g_desktop_app_info_new(id);
    if (a) shelf_add(a, id);
  }
  g_ptr_array_free(pins, TRUE);

  /* 2. Fenetres ouvertes : rattachees a leur appli, sinon ajoutees a la suite */
  GList *all = g_app_info_get_all();
  gboolean any_max = FALSE;
  for (GList *l = toplevels; l; l = l->next) {
    Toplevel *t = l->data;
    if (t->maximized && !t->minimized) any_max = TRUE;
    GDesktopAppInfo *a = app_for_window(t->app_id, all);
    const char *key = a ? g_app_info_get_id(G_APP_INFO(a)) : t->app_id ? t->app_id : "";
    ShelfItem *it = shelf_find(key);
    if (it) { if (a) g_object_unref(a); }
    else it = shelf_add(a, key);
    it->wins = g_list_append(it->wins, t);
  }
  g_list_free_full(all, g_object_unref);

  /* Barre d'etat opaque des qu'une fenetre est agrandie. */
  set_class(bar_box, "solid", any_max);
  if (any_max != bar_solid_now) { bar_solid_now = any_max; update_bar_light(); }

  for (guint i = 0; i < shelf_items->len; i++) {
    ShelfItem *it = shelf_items->pdata[i];
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *b = gtk_button_new();
    add_class(b, "slot");
    Toplevel *top = it->wins ? most_recent(it->wins) : NULL;
    const char *name = it->app ? g_app_info_get_name(G_APP_INFO(it->app))
                               : top && top->title ? top->title : it->key;
    gtk_widget_set_tooltip_text(b, name);
    gtk_container_add(GTK_CONTAINER(b), it->app ? app_icon(it->app, FALSE) : letter_icon(name));
    g_signal_connect(b, "clicked", G_CALLBACK(on_shelf_clicked), it);
    g_signal_connect_data(b, "button-press-event", G_CALLBACK(on_app_menu), g_strdup(it->key),
                          (GClosureNotify)(void (*)(void))g_free, 0);
    extras_shelf_button(b, it->key, it->app != NULL);
    gtk_container_add(GTK_CONTAINER(v), b);

    GtkWidget *ind = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    add_class(ind, "ind");
    gboolean active = FALSE;
    for (GList *l = it->wins; l; l = l->next) if (((Toplevel *)l->data)->activated) active = TRUE;
    if (it->wins) add_class(ind, active ? "active" : "on");
    gtk_widget_set_halign(ind, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(v), ind);
    gtk_container_add(GTK_CONTAINER(shelf_apps), v);
  }
  gtk_widget_show_all(shelf_apps);
  return G_SOURCE_REMOVE;
}

static void build_shelf(void) {
  GtkWidget *w = shelf_win = layer_window(GTK_LAYER_SHELL_LAYER_TOP, "lumina-shelf");
  gtk_layer_set_monitor(GTK_WINDOW(w), main_monitor());
  anchor(w, FALSE, TRUE, TRUE, TRUE);
  gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(w));
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  add_class(h, "shelf");
  shelf_apps = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_hexpand(shelf_apps, TRUE);
  gtk_widget_set_halign(shelf_apps, GTK_ALIGN_CENTER);
  gtk_container_add(GTK_CONTAINER(h), shelf_apps);
  gtk_container_add(GTK_CONTAINER(w), h);
  shelf_items = g_ptr_array_new_with_free_func(shelf_item_free);
  refresh_shelf(NULL);
  extras_shelf(h);
  gtk_widget_show_all(w);
}

static void build_scrim(void) {
  scrim = layer_window(GTK_LAYER_SHELL_LAYER_OVERLAY, "lumina-scrim");
  gtk_layer_set_monitor(GTK_WINDOW(scrim), main_monitor());
  anchor(scrim, TRUE, TRUE, TRUE, TRUE);
  gtk_layer_set_exclusive_zone(GTK_WINDOW(scrim), -1);
  GtkWidget *eb = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb), FALSE);
  g_signal_connect(eb, "button-press-event", G_CALLBACK(on_scrim_press), NULL);
  gtk_container_add(GTK_CONTAINER(scrim), eb);
  gtk_widget_show(eb);
}

static void build_drawer(void) {
  drawer = layer_window(GTK_LAYER_SHELL_LAYER_OVERLAY, "lumina-drawer");
  gtk_layer_set_monitor(GTK_WINDOW(drawer), main_monitor());
  /* Le lanceur est dans le coin haut gauche : le tiroir s'ouvre dessous. */
  anchor(drawer, TRUE, FALSE, TRUE, FALSE);
  gtk_layer_set_margin(GTK_WINDOW(drawer), GTK_LAYER_SHELL_EDGE_LEFT, -12);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(drawer), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
  g_signal_connect(drawer, "key-press-event", G_CALLBACK(on_panel_key), NULL);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  add_class(v, "panel"); add_class(v, "attached");
  gtk_widget_set_size_request(v, 560, -1);

  GtkWidget *s = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  add_class(s, "search");
  gtk_container_add(GTK_CONTAINER(s), icon_new("search", R_ON_VAR, 20)->img);
  search_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Rechercher une appli, un fichier, calculer…");
  gtk_widget_set_hexpand(search_entry, TRUE);
  g_signal_connect(search_entry, "changed", G_CALLBACK(on_search_changed), NULL);
  g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activate), NULL);
  gtk_container_add(GTK_CONTAINER(s), search_entry);
  gtk_container_add(GTK_CONTAINER(v), s);
  extras_drawer(v);

  gtk_container_add(GTK_CONTAINER(v), label("Toutes les applications", "section", 0));

  flow = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 5);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 5);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 4);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 4);
  gtk_flow_box_set_filter_func(GTK_FLOW_BOX(flow), flow_filter, NULL, NULL);

  GtkWidget *sc = flow_scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(sc), flow);
  fill_drawer();
  g_signal_connect(g_app_info_monitor_get(), "changed", G_CALLBACK(on_apps_changed), NULL);
  gtk_container_add(GTK_CONTAINER(v), sc);

  flow_empty = label("Aucune appli ne correspond. Entrée lance la recherche sur le Web.", "empty", 0.5);
  gtk_container_add(GTK_CONTAINER(v), flow_empty);

  gtk_container_add(GTK_CONTAINER(drawer), v);
  gtk_widget_show_all(v);
  gtk_widget_hide(flow_empty);
}

static void build_qs(void) {
  qs = layer_window(GTK_LAYER_SHELL_LAYER_OVERLAY, "lumina-quick-settings");
  gtk_layer_set_monitor(GTK_WINDOW(qs), main_monitor());
  anchor(qs, TRUE, FALSE, FALSE, TRUE);
  gtk_layer_set_margin(GTK_WINDOW(qs), GTK_LAYER_SHELL_EDGE_TOP, 0);
  gtk_layer_set_margin(GTK_WINDOW(qs), GTK_LAYER_SHELL_EDGE_RIGHT, -12);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(qs), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
  g_signal_connect(qs, "key-press-event", G_CALLBACK(on_panel_key), NULL);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  add_class(v, "panel"); add_class(v, "attached");
  gtk_widget_set_size_request(v, 380, -1);

  GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  qs_time = label("", "qs-time", 0); qs_date = label("", "qs-date", 1);
  gtk_widget_set_hexpand(qs_date, TRUE);
  gtk_widget_set_valign(qs_date, GTK_ALIGN_BASELINE);
  gtk_widget_set_valign(qs_time, GTK_ALIGN_BASELINE);
  gtk_container_add(GTK_CONTAINER(head), qs_time);
  gtk_container_add(GTK_CONTAINER(head), qs_date);
  GtkWidget *gear = gtk_button_new();
  add_class(gear, "tray");
  gtk_widget_set_tooltip_text(gear, "Tous les réglages");
  gtk_container_add(GTK_CONTAINER(gear), icon_new("set", R_ON_VAR, 18)->img);
  g_signal_connect(gear, "clicked", G_CALLBACK(on_open_settings), NULL);
  gtk_container_add(GTK_CONTAINER(head), gear);
  gtk_container_add(GTK_CONTAINER(v), head);

  /* Wi-Fi : tuile pleine largeur, la liste des reseaux se deplie dessous. */
  if (wifi_dev) {
    wifi_tile = tile("wifi", "Wi-Fi", &wifi_sub, &wifi_ic);
    gtk_container_add(GTK_CONTAINER(gtk_bin_get_child(GTK_BIN(wifi_tile))), icon_new("chev", R_ON_VAR, 16)->img);
    gtk_widget_set_hexpand(gtk_widget_get_parent(wifi_sub), TRUE);
    g_signal_connect(wifi_tile, "clicked", G_CALLBACK(on_wifi_tile), NULL);
    gtk_container_add(GTK_CONTAINER(v), wifi_tile);

    nets_rev = gtk_revealer_new();
    GtkWidget *nv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    add_class(nv, "nets");
    nets_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(nv), nets_box);
    wifi_power_btn = gtk_button_new_with_label("");
    add_class(wifi_power_btn, "textbtn");
    gtk_widget_set_halign(wifi_power_btn, GTK_ALIGN_END);
    g_signal_connect(wifi_power_btn, "clicked", G_CALLBACK(on_wifi_power), NULL);
    gtk_container_add(GTK_CONTAINER(nv), wifi_power_btn);
    gtk_container_add(GTK_CONTAINER(nets_rev), nv);
    gtk_container_add(GTK_CONTAINER(v), nets_rev);
  }

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  dark_tile = tile("moon", "Thème sombre", &dark_sub, NULL);
  g_signal_connect(dark_tile, "clicked", G_CALLBACK(on_dark_tile), NULL);
  android_tile = tile("android", "Apps Android", &android_sub, NULL);
  g_signal_connect(android_tile, "clicked", G_CALLBACK(on_android_tile), NULL);
  gtk_grid_attach(GTK_GRID(grid), dark_tile, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), android_tile, 1, 0, 1, 1);
  gtk_container_add(GTK_CONTAINER(v), grid);
  extras_qs(v, grid);

  bright_row = slider_row("sun", &bright_scale, G_CALLBACK(on_bright));
  vol_row = slider_row("vol", &vol_scale, G_CALLBACK(on_vol));
  /* Contenu affiche d'abord, puis la rangee soustraite a show_all :
     c'est refresh_qs qui decide si la machine a cet appareil. */
  gtk_widget_show_all(bright_row); gtk_widget_show_all(vol_row);
  gtk_widget_set_no_show_all(bright_row, TRUE);
  gtk_widget_set_no_show_all(vol_row, TRUE);
  gtk_container_add(GTK_CONTAINER(v), bright_row);
  gtk_container_add(GTK_CONTAINER(v), vol_row);

  GtkWidget *mem = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  add_class(mem, "mem");
  GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *ml = label("Mémoire", "muted", 0);
  gtk_widget_set_hexpand(ml, TRUE);
  mem_used_l = label("", "strong", 1); mem_total_l = label("", "muted", 1);
  gtk_container_add(GTK_CONTAINER(top), ml);
  gtk_container_add(GTK_CONTAINER(top), mem_used_l);
  gtk_container_add(GTK_CONTAINER(top), mem_total_l);
  gtk_container_add(GTK_CONTAINER(mem), top);
  mem_gauge = gtk_drawing_area_new();
  gtk_widget_set_size_request(mem_gauge, -1, 8);
  g_signal_connect(mem_gauge, "draw", G_CALLBACK(draw_gauge), NULL);
  gtk_container_add(GTK_CONTAINER(mem), mem_gauge);
  GtkWidget *lg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  add_class(lg, "legend");
  GtkWidget *l1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *l2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_container_add(GTK_CONTAINER(l1), icon_new("svg:<rect x='6' y='6' width='12' height='12' rx='3' fill='currentColor' stroke='none'/>", R_PRI, 10)->img);
  gtk_container_add(GTK_CONTAINER(l1), label("Utilisée", NULL, 0));
  gtk_container_add(GTK_CONTAINER(l2), icon_new("svg:<rect x='6' y='6' width='12' height='12' rx='3' fill='currentColor' stroke='none'/>", R_TER, 10)->img);
  zram_l = label("", NULL, 0);
  gtk_container_add(GTK_CONTAINER(l2), zram_l);
  gtk_container_add(GTK_CONTAINER(lg), l1);
  gtk_container_add(GTK_CONTAINER(lg), l2);
  gtk_container_add(GTK_CONTAINER(mem), lg);
  gtk_container_add(GTK_CONTAINER(v), mem);

  upd_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  add_class(upd_row, "upd");
  upd_l = label("", NULL, 0);
  gtk_label_set_ellipsize(GTK_LABEL(upd_l), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(upd_l, TRUE);
  gtk_container_add(GTK_CONTAINER(upd_row), upd_l);
  upd_btn = gtk_button_new_with_label("Redémarrer");
  g_signal_connect(upd_btn, "clicked", G_CALLBACK(on_power), (gpointer)"reboot");
  gtk_container_add(GTK_CONTAINER(upd_row), upd_btn);
  gtk_widget_show_all(upd_row);
  gtk_widget_set_no_show_all(upd_btn, TRUE);
  gtk_container_add(GTK_CONTAINER(v), upd_row);

  GtkWidget *pw = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_set_homogeneous(GTK_BOX(pw), TRUE);
  /* Verrouiller, se deconnecter (retour a l'ecran de connexion),
     redemarrer, eteindre : icones, nom en info-bulle */
  const char *acts[][3] = {{"lock", "Verrouiller (Windows + L)", "lock"}, {"user", "Se déconnecter", "logout"},
                           {"reboot", "Redémarrer", "reboot"}, {"power", "Éteindre", "poweroff"}};
  for (int i = 0; i < 4; i++) {
    GtkWidget *b = gtk_button_new();
    add_class(b, "pwr");
    gtk_widget_set_tooltip_text(b, acts[i][1]);
    gtk_container_add(GTK_CONTAINER(b), icon_new(acts[i][0], R_ON, 20)->img);
    g_signal_connect(b, "clicked", G_CALLBACK(on_power), (gpointer)acts[i][2]);
    gtk_container_add(GTK_CONTAINER(pw), b);
  }
  gtk_container_add(GTK_CONTAINER(v), pw);

  /* Petits ecrans (1366 x 768...) : le volet ne depasse jamais l'ecran,
     son contenu defile. Hauteur maxi recalculee a chaque ouverture. */
  qs_scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(qs_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(qs_scroll), TRUE);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(qs_scroll), TRUE);
  gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(qs_scroll), TRUE);
  gtk_container_add(GTK_CONTAINER(qs_scroll), v);
  gtk_container_add(GTK_CONTAINER(qs), qs_scroll);
  gtk_widget_show_all(qs_scroll);
}

static void fit_qs(void) {
  if (!qs_scroll) return;
  /* Sous Wayland, GDK ne connait pas la zone de travail : on retire
     nous-memes la barre d'etat et l'etagere. */
  GdkRectangle g;
  gdk_monitor_get_geometry(main_monitor(), &g);
  int bar = bar_win ? gtk_widget_get_allocated_height(bar_win) : 28;
  int shelf = shelf_win ? gtk_widget_get_allocated_height(shelf_win) : 56;
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(qs_scroll), MAX(300, g.height - bar - shelf - 4));
}

#include "lumina-extras.c"

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "--action")) return action_client(argv[2]);
  gtk_init(&argc, &argv);
  lumina_ui_init(BASE_CSS, on_theme_changed);
  refresh_wallpaper();
  init_portal();

  wifi_dev = net_wifi_device();

  build_wallpapers();
  g_signal_connect(gdk_display_get_default(), "monitor-added", G_CALLBACK(on_monitors_changed), NULL);
  g_signal_connect(gdk_display_get_default(), "monitor-removed", G_CALLBACK(on_monitors_changed), NULL);
  build_bar();
  build_shelf();
  build_scrim();
  build_drawer();
  build_qs();
  extras_init();

  init_toplevels();
  tick(NULL);
  g_timeout_add_seconds(5, tick, NULL);
  g_timeout_add_seconds(120, disk_check, NULL);
  g_timeout_add_seconds(2, mem_tick, NULL);
  g_unix_signal_add(SIGUSR1, on_sigusr1, NULL);
  g_unix_signal_add(SIGUSR2, on_sigusr2, NULL);
  g_unix_signal_add(SIGHUP, on_sighup, NULL);

  gtk_main();
  return 0;
}

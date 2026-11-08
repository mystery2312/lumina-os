/*
 * lumina-ui : ce que toutes les applis LuminaOS partagent pour se
 * ressembler. Palette tonale Material 3, glyphes, rendu des icones, et
 * theme clair/sombre commun (~/.config/lumina/theme) applique en direct a
 * toutes les applis ouvertes quand l'une d'elles le change.
 */
#include "lumina-ui.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib/gstdio.h>

/* ------------------------------------------------------------------ */
/* Palette tonale Material 3, graine #0F7A63                           */
/* ------------------------------------------------------------------ */

static const Palette LIGHT = {
  "#f5fbf7", "#eff5f1", "#e9efeb", "#e3eae5", "#dee4e0", "#171d1a", "#404945", "#bfc9c3",
  "#006b57", "#ffffff", "#a1f2d7", "#002019", "#cde8de", "#06201a",
  "#4a6179", "#d1e4ff", "#041d33", "#9fd8c4", "#bcd3ef", "#e6efe9", "#d5ddd8" };
static const Palette DARK = {
  "#0f1512", "#171d1a", "#1b211e", "#252b28", "#303633", "#dee4e0", "#bfc9c3", "#404945",
  "#85d6bc", "#00382c", "#005141", "#a1f2d7", "#334b43", "#cde8de",
  "#b1c8e8", "#324960", "#d1e4ff", "#0e4a3c", "#24364c", "#121a17", "#1d2320" };

static Palette GEN_LIGHT, GEN_DARK;
static gboolean use_gen;
const Palette *P = &LIGHT;
gboolean dark = FALSE;
gboolean lumina_bar_light = FALSE;



/* ------------------------------------------------------------------ */
/* Couleur d'accent choisie (Reglages > Affichage), facon Material You */
/* ------------------------------------------------------------------ */
/* Palette tonale recalculee depuis une couleur "graine" : la teinte de la
   graine, a des niveaux de luminance fixes (les "tons" Material, soit la
   L* de CIELAB). Ce n'est pas l'espace HCT exact de Google, mais LCh(ab)
   en est proche et suffit a des palettes harmonieuses et lisibles. */
static double lin(double c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double gam(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055; }
static double lab_f(double t) { return t > 216.0 / 24389 ? cbrt(t) : (24389.0 / 27 * t + 16) / 116; }
static double lab_fi(double t) { return t * t * t > 216.0 / 24389 ? t * t * t : (116 * t - 16) / (24389.0 / 27); }

static void rgb_to_lch(double r, double g, double b, double *L, double *C, double *H) {
  r = lin(r); g = lin(g); b = lin(b);
  double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
  double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
  double fx = lab_f(x), fy = lab_f(y), fz = lab_f(z);
  double A = 500 * (fx - fy), B = 200 * (fy - fz);
  *L = 116 * fy - 16; *C = hypot(A, B); *H = atan2(B, A) * 180 / M_PI;
  if (*H < 0) *H += 360;
}

/* Ton (L*), chroma, teinte -> "#rrggbb", chroma reduite si hors gamut sRGB */
static void tone_hex(char out[8], double h, double c, double t) {
  for (;; c = c > 1 ? c - 1 : 0) {
    double A = c * cos(h * M_PI / 180), B = c * sin(h * M_PI / 180);
    double fy = (t + 16) / 116, fx = fy + A / 500, fz = fy - B / 200;
    double x = lab_fi(fx) * 0.95047, y = t > 8 ? fy * fy * fy : t / (24389.0 / 27), z = lab_fi(fz) * 1.08883;
    double r = 3.2406 * x - 1.5372 * y - 0.4986 * z;
    double g = -0.9689 * x + 1.8758 * y + 0.0415 * z;
    double b = 0.0557 * x - 0.2040 * y + 1.0570 * z;
    if ((r >= -0.001 && r <= 1.001 && g >= -0.001 && g <= 1.001 && b >= -0.001 && b <= 1.001) || c <= 0) {
      g_snprintf(out, 8, "#%02x%02x%02x", (int)lround(CLAMP(gam(CLAMP(r, 0, 1)), 0, 1) * 255),
                 (int)lround(CLAMP(gam(CLAMP(g, 0, 1)), 0, 1) * 255), (int)lround(CLAMP(gam(CLAMP(b, 0, 1)), 0, 1) * 255));
      return;
    }
  }
}

static char GEN_HEX[2][21][8];

static void build_palettes(double h, double c) {
  /* Schema "tonal spot" : primaire franche, secondaire et neutres discrets,
     tertiaire decalee de 60 degres. Une graine grise donne un theme gris. */
  gboolean grey = c < 10;
  double pc = grey ? 4 : 40, sc = grey ? 3 : 14, tc = grey ? 4 : 26, nc = grey ? 1 : 4, nvc = grey ? 2 : 8;
  double th = fmod(h + 60, 360);
  struct { double h, c, tl, td; } R[21] = {
    /* surface    */ {h, nc, 98, 6},   /* c_low  */ {h, nc, 96, 10}, /* c      */ {h, nc, 94, 12},
    /* c_high     */ {h, nc, 92, 17},  /* c_top  */ {h, nc, 90, 22}, /* on     */ {h, nc, 10, 90},
    /* on_var     */ {h, nvc, 30, 80}, /* outl_v */ {h, nvc, 80, 30},
    /* pri        */ {h, pc, 40, 80},  /* on_pri */ {h, pc, 100, 20}, /* pri_c */ {h, pc, 90, 30},
    /* on_pri_c   */ {h, pc, 10, 90},  /* sec_c  */ {h, sc, 90, 30},  /* on_sec_c */ {h, sc, 10, 90},
    /* ter        */ {th, tc, 40, 80}, /* ter_c  */ {th, tc, 90, 30}, /* on_ter_c */ {th, tc, 10, 90},
    /* wall_a     */ {h, pc, 82, 28},  /* wall_b */ {th, tc, 84, 22}, /* wall_c */ {h, nc, 93, 8},
    /* bar_solid  */ {h, nc, 87, 14} };
  const char **L = (const char **)&GEN_LIGHT, **D = (const char **)&GEN_DARK;
  for (int i = 0; i < 21; i++) {
    tone_hex(GEN_HEX[0][i], R[i].h, R[i].c, R[i].tl);
    tone_hex(GEN_HEX[1][i], R[i].h, R[i].c, R[i].td);
    L[i] = GEN_HEX[0][i]; D[i] = GEN_HEX[1][i];
  }
}

static char *conf_file(const char *name) { return g_build_filename(g_get_user_config_dir(), "lumina", name, NULL); }


/* ------------------------------------------------------------------ */
/* Fonds d'ecran LuminaOS : dessines (aucune image a telecharger), aux  */
/* couleurs du theme ; ~/.config/lumina/wallstyle contient le numero.   */
/* ------------------------------------------------------------------ */
const char *LUMINA_WALL_NAMES[LUMINA_WALL_COUNT] = {"Aube", "Aurore", "Vagues", "Galets", "Nuit"};

int lumina_ui_wall_style(void) {
  char *p = conf_file("wallstyle"), *t = NULL;
  g_file_get_contents(p, &t, NULL, NULL);
  int v = t ? atoi(t) : 0;
  g_free(p); g_free(t);
  return CLAMP(v, 0, LUMINA_WALL_COUNT - 1);
}
void lumina_ui_set_wall_style(int v) {
  char *p = conf_file("wallstyle"), *t = g_strdup_printf("%d\n", v);
  g_file_set_contents(p, t, -1, NULL);
  g_free(p); g_free(t);
}

static void wblob(cairo_t *cr, double cx, double cy, double rx, double ry, const char *hex, double a) {
  double r, g, b; hex_rgb(hex, &r, &g, &b);
  cairo_save(cr);
  cairo_translate(cr, cx, cy); cairo_scale(cr, rx, ry);
  cairo_pattern_t *p = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
  cairo_pattern_add_color_stop_rgba(p, 0, r, g, b, a);
  cairo_pattern_add_color_stop_rgba(p, 0.7, r, g, b, 0);
  cairo_set_source(cr, p);
  cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI); cairo_fill(cr);
  cairo_pattern_destroy(p);
  cairo_restore(cr);
}
static void wfill(cairo_t *cr, const char *hex) { double r, g, b; hex_rgb(hex, &r, &g, &b); cairo_set_source_rgb(cr, r, g, b); cairo_paint(cr); }

void lumina_ui_draw_wallpaper(cairo_t *cr, double W, double H, int style) {
  double r, g, b;
  switch (style) {
    case 1: {  /* Aurore : grand degrade diagonal primaire -> tertiaire */
      cairo_pattern_t *p = cairo_pattern_create_linear(0, H, W, 0);
      hex_rgb(P->wall_a, &r, &g, &b); cairo_pattern_add_color_stop_rgb(p, 0, r, g, b);
      hex_rgb(P->wall_c, &r, &g, &b); cairo_pattern_add_color_stop_rgb(p, 0.55, r, g, b);
      hex_rgb(P->wall_b, &r, &g, &b); cairo_pattern_add_color_stop_rgb(p, 1, r, g, b);
      cairo_set_source(cr, p); cairo_paint(cr); cairo_pattern_destroy(p);
      wblob(cr, W * 0.7, H * 0.35, W * 0.45, H * 0.5, P->pri_c, 0.55);
      break;
    }
    case 2: {  /* Vagues : bandes ondulees superposees */
      wfill(cr, P->wall_c);
      const char *cols[] = {P->wall_b, P->wall_a, P->pri_c, P->sec_c};
      for (int i = 0; i < 4; i++) {
        hex_rgb(cols[i], &r, &g, &b);
        cairo_set_source_rgba(cr, r, g, b, 0.55 + 0.1 * i);
        double base = H * (0.45 + 0.13 * i), amp = H * (0.06 - 0.008 * i);
        cairo_move_to(cr, 0, H);
        for (double x = 0; x <= W; x += W / 60) cairo_line_to(cr, x, base + amp * sin(x / W * 2 * G_PI * (1.2 + 0.3 * i) + i));
        cairo_line_to(cr, W, H); cairo_close_path(cr); cairo_fill(cr);
      }
      break;
    }
    case 3: {  /* Galets : disques flous a la Material You */
      wfill(cr, P->wall_c);
      static const double G[][4] = {{0.15, 0.25, 0.22, 0}, {0.55, 0.15, 0.16, 1}, {0.85, 0.55, 0.26, 2},
                                    {0.35, 0.75, 0.2, 1}, {0.7, 0.9, 0.14, 0}, {0.05, 0.85, 0.12, 2}};
      const char *cols[] = {P->wall_a, P->wall_b, P->pri_c};
      for (guint i = 0; i < G_N_ELEMENTS(G); i++) {
        double R = MIN(W, H) * G[i][2];
        hex_rgb(cols[(int)G[i][3]], &r, &g, &b);
        cairo_set_source_rgba(cr, r, g, b, 0.85);
        cairo_arc(cr, W * G[i][0], H * G[i][1], R, 0, 2 * G_PI); cairo_fill(cr);
      }
      break;
    }
    case 4: {  /* Nuit : sombre, une lueur de la couleur du theme */
      wfill(cr, "#0d1210");
      wblob(cr, W * 0.2, H * 0.9, W * 0.7, H * 0.8, DARK.wall_a, 0.9);
      wblob(cr, W * 0.9, H * 0.1, W * 0.5, H * 0.6, P->ter, 0.35);
      break;
    }
    default:   /* Aube : le fond d'origine */
      wfill(cr, P->wall_c);
      wblob(cr, W * 0.18, H * 0.88, W * 0.60, H * 0.70, P->wall_a, 1);
      wblob(cr, W * 0.88, H * 0.12, W * 0.55, H * 0.65, P->wall_b, 1);
  }
}

char *lumina_ui_wallpaper_path(void) {
  char *p = conf_file("wallpaper");
  if (g_file_test(p, G_FILE_TEST_IS_REGULAR)) return p;
  g_free(p);
  return NULL;
}

char *lumina_ui_accent(void) {
  char *p = conf_file("accent"), *t = NULL;
  g_file_get_contents(p, &t, NULL, NULL);
  g_free(p);
  if (t) g_strstrip(t);
  if (!t || !*t) { g_free(t); return g_strdup(LUMINA_DEFAULT_ACCENT); }
  return t;
}

/* Couleur dominante d'une photo : la teinte la plus representee parmi les
   pixels assez colores (ponderes par leur saturation). */
gboolean lumina_ui_image_hue(const char *path, double *h_out) {
  GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(path, 48, 48, NULL);
  if (!pb) return FALSE;
  double bins[36] = {0}, hs[36] = {0};
  int w = gdk_pixbuf_get_width(pb), hh = gdk_pixbuf_get_height(pb), rs = gdk_pixbuf_get_rowstride(pb);
  int nc = gdk_pixbuf_get_n_channels(pb);
  const guchar *px = gdk_pixbuf_read_pixels(pb);
  for (int y = 0; y < hh; y++)
    for (int x = 0; x < w; x++) {
      const guchar *q = px + y * rs + x * nc;
      double L, C, H;
      rgb_to_lch(q[0] / 255.0, q[1] / 255.0, q[2] / 255.0, &L, &C, &H);
      if (C < 12 || L < 15 || L > 92) continue;
      int bi = (int)(H / 10) % 36;
      bins[bi] += C; hs[bi] += C * H;
    }
  g_object_unref(pb);
  int best = -1;
  for (int i = 0; i < 36; i++) if (bins[i] > 0 && (best < 0 || bins[i] > bins[best])) best = i;
  if (best < 0) return FALSE;
  *h_out = hs[best] / bins[best];
  return TRUE;
}

/* Palettes selon ~/.config/lumina/accent : "#rrggbb", ou "wallpaper" */
static void load_accent(void) {
  char *a = lumina_ui_accent();
  double h = 0, c = 0, L;
  use_gen = FALSE;
  if (!g_ascii_strcasecmp(a, "wallpaper")) {
    char *w = lumina_ui_wallpaper_path();
    if (w && lumina_ui_image_hue(w, &h)) { c = 40; use_gen = TRUE; }
    g_free(w);
  } else if (a[0] == '#' && strlen(a) == 7 && g_ascii_strcasecmp(a, LUMINA_DEFAULT_ACCENT)) {
    double r, g, b;
    hex_rgb(a, &r, &g, &b);
    rgb_to_lch(r, g, b, &L, &c, &h);
    use_gen = TRUE;
  }
  if (use_gen) build_palettes(h, c);
  g_free(a);
}

/* Contraste eleve (Reglages > Accessibilite) : textes secondaires et
   contours aussi marques que le texte principal. */
static Palette HC;
static const Palette *pick(gboolean d) {
  const Palette *base = use_gen ? (d ? &GEN_DARK : &GEN_LIGHT) : (d ? &DARK : &LIGHT);
  char *p = conf_file("contrast");
  gboolean hc = g_file_test(p, G_FILE_TEST_EXISTS);
  g_free(p);
  if (!hc) return base;
  HC = *base;
  HC.on_var = HC.on; HC.outline_var = HC.on;
  HC.surface = d ? "#000000" : "#ffffff"; HC.c_low = HC.surface;
  return &HC;
}

const char *role_color(int r) {
  switch (r) {
    case R_ON:       return P->on;
    case R_ON_VAR:   return P->on_var;
    case R_ON_PRI_C: return P->on_pri_c;
    case R_ON_SEC_C: return P->on_sec_c;
    case R_ON_TER_C: return P->on_ter_c;
    case R_TER:      return P->ter;
    case R_BAR:      return lumina_bar_light ? "#ffffff" : P->on;
    case R_ON_PRI:   return P->on_pri;
    default:         return P->pri;
  }
}

/* ------------------------------------------------------------------ */
/* Glyphes 24 px, trait 1.8 : la meme famille que la maquette          */
/* ------------------------------------------------------------------ */
typedef struct { const char *name, *svg; } Glyph;
static const Glyph GLYPHS[] = {
  {"web",   "<circle cx='12' cy='12' r='9'/><path d='M3 12h18M12 3c2.6 2.8 2.6 15.2 0 18M12 3c-2.6 2.8-2.6 15.2 0 18'/>"},
  {"files", "<path d='M3 6.5A2.5 2.5 0 0 1 5.5 4h4l2 2.5h7A2.5 2.5 0 0 1 21 9v8.5a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z'/>"},
  {"mail",  "<rect x='2.5' y='5' width='19' height='14' rx='2.5'/><path d='m3 7 9 6.2L21 7'/>"},
  {"store", "<path d='M4.5 8h15l-1.2 11.5a1.8 1.8 0 0 1-1.8 1.5h-9a1.8 1.8 0 0 1-1.8-1.5z'/><path d='M8.5 8a3.5 3.5 0 0 1 7 0'/><path d='M10.5 12.3v4.4l3.8-2.2z' fill='currentColor' stroke='none'/>"},
  {"set",   "<path d='M4 7h10M18 7h2M4 17h4M12 17h8'/><circle cx='16' cy='7' r='2.2'/><circle cx='10' cy='17' r='2.2'/>"},
  {"ai",    "<path d='M12 2.8 13.9 9l6.2 1.9-6.2 1.9L12 19l-1.9-6.2L3.9 10.9 10.1 9z'/>"},
  {"docs",  "<path d='M6 2.5h8l4.5 4.5v14.5H6z'/><path d='M14 2.5V7h4.5M9 12h6M9 15.5h6'/>"},
  {"cal",   "<rect x='3' y='5' width='18' height='16' rx='2.5'/><path d='M3 10h18M8 2.5v4.5M16 2.5v4.5'/>"},
  {"photo", "<rect x='3' y='4' width='18' height='16' rx='2.5'/><circle cx='8.8' cy='9.5' r='1.9'/><path d='m3.5 18 5-5 4.5 4.5 3-2.5 4.5 4'/>"},
  {"vid",   "<rect x='2' y='5' width='20' height='14' rx='4'/><path d='M10.3 9.1v5.8L15.4 12z' fill='currentColor' stroke='none'/>"},
  {"map",   "<path d='M12 21.5s7-6.1 7-11a7 7 0 1 0-14 0c0 4.9 7 11 7 11z'/><circle cx='12' cy='10.2' r='2.6'/>"},
  {"drive", "<path d='M9.3 3h5.4l6.3 11H15z'/><path d='M9.3 3 3 14l3 5 6.3-11z'/><path d='M6 19h12l2.9-5H9z'/>"},
  {"term",  "<rect x='2.5' y='4' width='19' height='16' rx='2.5'/><path d='m7 9.5 3 2.5-3 2.5M12.5 15h4.5'/>"},
  {"grid",  "<g fill='currentColor' stroke='none'><circle cx='6' cy='6' r='2.2'/><circle cx='12' cy='6' r='2.2'/><circle cx='18' cy='6' r='2.2'/><circle cx='6' cy='12' r='2.2'/><circle cx='12' cy='12' r='2.2'/><circle cx='18' cy='12' r='2.2'/><circle cx='6' cy='18' r='2.2'/><circle cx='12' cy='18' r='2.2'/><circle cx='18' cy='18' r='2.2'/></g>"},
  {"search","<circle cx='10.5' cy='10.5' r='6.5'/><path d='m15.4 15.4 4.1 4.1'/>"},
  {"wifi",  "<path d='M12 20.5 1.2 7.6A16.5 16.5 0 0 1 12 3.5c4.1 0 7.9 1.5 10.8 4.1z' fill='currentColor' stroke='none'/>"},
  {"wifi-off","<path d='M12 20.5 1.2 7.6A16.5 16.5 0 0 1 12 3.5c4.1 0 7.9 1.5 10.8 4.1z'/><path d='m3 3 18 18'/>"},
  {"wired", "<rect x='8' y='3' width='8' height='6' rx='1.5'/><path d='M12 9v4M5 13h14M5 13v4M19 13v4M12 13v4'/><rect x='3' y='17' width='4' height='4' rx='1'/><rect x='10' y='17' width='4' height='4' rx='1'/><rect x='17' y='17' width='4' height='4' rx='1'/>"},
  {"moon",  "<path d='M20 14.5A8.5 8.5 0 1 1 9.5 4a7 7 0 0 0 10.5 10.5z'/>"},
  {"android","<path d='M5 11a7 7 0 0 1 14 0v7H5z'/><path d='m7 5.5-1.5-2M17 5.5l1.5-2'/><circle cx='9.3' cy='8.8' r='.9' fill='currentColor' stroke='none'/><circle cx='14.7' cy='8.8' r='.9' fill='currentColor' stroke='none'/>"},
  {"sun",   "<circle cx='12' cy='12' r='4'/><path d='M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4'/>"},
  {"vol",   "<path d='M4 9.5h3.5L12 5v14l-4.5-4.5H4z'/><path d='M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11'/>"},
  {"power", "<path d='M12 3v8'/><path d='M7 6.3a7.5 7.5 0 1 0 10 0'/>"},
  {"reboot","<path d='M20 11a8 8 0 1 0-2.3 5.7M20 5v6h-6'/>"},
  {"lock",  "<rect x='5' y='10.5' width='14' height='10' rx='2.5'/><path d='M8 10.5V8a4 4 0 0 1 8 0v2.5'/>"},
  {"chev",  "<path d='m9 6 6 6-6 6'/>"},
  /* --- Fichiers ------------------------------------------------------- */
  {"folder",  "<path d='M3 6.5A2.5 2.5 0 0 1 5.5 4h4l2 2.5h7A2.5 2.5 0 0 1 21 9v8.5a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z' fill='currentColor' stroke='none'/>"},
  {"file",    "<path d='M6 2.5h8l4.5 4.5v14.5H6z'/><path d='M14 2.5V7h4.5'/>"},
  {"pdf",     "<path d='M6 2.5h8l4.5 4.5v14.5H6z'/><path d='M14 2.5V7h4.5'/><path d='M8.5 16.5c2-1 4.5-5 3.5-7s-2.2 3 1 5.5c1.6 1.2 3 1 3 .3s-3.5-.6-7.5 1.2z'/>"},
  {"music",   "<path d='M9 18V5.5l11-2V16'/><circle cx='6.5' cy='18' r='2.5'/><circle cx='17.5' cy='16' r='2.5'/>"},
  {"archive", "<rect x='3' y='4' width='18' height='4.5' rx='1.2'/><path d='M4.5 8.5v10A1.5 1.5 0 0 0 6 20h12a1.5 1.5 0 0 0 1.5-1.5v-10M10 12.5h4'/>"},
  {"home",    "<path d='M3.5 10.5 12 3.5l8.5 7V19a1.5 1.5 0 0 1-1.5 1.5h-4v-6h-6v6H5A1.5 1.5 0 0 1 3.5 19z'/>"},
  {"download","<path d='M12 3.5v11M7.5 10l4.5 4.5 4.5-4.5M4.5 19.5h15'/>"},
  {"trash",   "<path d='M4 6.5h16M9.5 6.5V4.5h5v2M6 6.5l1 13a1.5 1.5 0 0 0 1.5 1.5h7A1.5 1.5 0 0 0 17 19.5l1-13M10 10.5v6M14 10.5v6'/>"},
  {"bt",      "<path d='m7 7 10 10-5 4V3l5 4L7 17'/>"},
  {"kbd",     "<rect x='2.5' y='6' width='19' height='12' rx='2.5'/><path d='M6 10h.01M9.5 10h.01M13 10h.01M16.5 10h.01M8 14h8'/>"},
  {"info",    "<circle cx='12' cy='12' r='9'/><path d='M12 11v5.5M12 7.8h.01'/>"},
  {"disk",    "<rect x='3' y='5' width='18' height='14' rx='3'/><path d='M3 14h18'/><circle cx='16.5' cy='16.5' r='.8' fill='currentColor' stroke='none'/>"},
  {"usb",     "<rect x='7' y='9' width='10' height='12' rx='2'/><path d='M9 9V3.5h6V9M10.5 5.5h.01M13.5 5.5h.01'/>"},
  {"eject",   "<path d='m5 14 7-8 7 8z'/><path d='M5 18.5h14'/>"},
  {"back",    "<path d='M19 12H5m6-6-6 6 6 6'/>"},
  {"fwd",     "<path d='M5 12h14m-6-6 6 6-6 6'/>"},
  {"up",      "<path d='M12 19V5m-6 6 6-6 6 6'/>"},
  {"down",    "<path d='M12 5v14m-6-6 6 6 6-6'/>"},
  {"play",    "<path d='M8 5.5v13l10.5-6.5z' fill='currentColor' stroke='none'/>"},
  {"pause",   "<path d='M7.5 5h3v14h-3zM13.5 5h3v14h-3z' fill='currentColor' stroke='none'/>"},
  {"prev",    "<path d='M6 5v14M19 5.5v13L9 12z'/>"},
  {"next",    "<path d='M18 5v14M5 5.5v13L15 12z'/>"},
  {"rotate",  "<path d='M20 11a8 8 0 1 0-2.3 5.7M20 5v6h-6'/>"},
  {"zoomout", "<circle cx='10.5' cy='10.5' r='6.5'/><path d='m15.4 15.4 4.1 4.1M8 10.5h5'/>"},
  {"fullscreen","<path d='M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5'/>"},
  {"camera",  "<path d='M3 8.5A2.5 2.5 0 0 1 5.5 6h2l1.5-2h6l1.5 2h2A2.5 2.5 0 0 1 21 8.5v9a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z'/><circle cx='12' cy='12.5' r='3.8'/>"},
  {"text",    "<path d='M6 2.5h8l4.5 4.5v14.5H6z'/><path d='M14 2.5V7h4.5M9 11h6M9 14.5h6M9 18h4'/>"},
  {"gallery", "<rect x='3' y='4' width='18' height='16' rx='2.5'/><circle cx='8.8' cy='9.5' r='1.9'/><path d='m3.5 18 5-5 4.5 4.5 3-2.5 4.5 4'/>"},
  {"wallpaper","<rect x='3' y='4' width='18' height='16' rx='2.5'/><path d='m3.5 17 5.5-5.5 4 4 2.5-2.5 5 5'/><circle cx='16' cy='8.5' r='1.5'/>"},
  {"more",    "<g fill='currentColor' stroke='none'><circle cx='12' cy='5.5' r='1.7'/><circle cx='12' cy='12' r='1.7'/><circle cx='12' cy='18.5' r='1.7'/></g>"},
  {"plus",    "<path d='M12 5v14M5 12h14'/>"},
  {"close",   "<path d='m6 6 12 12M18 6 6 18'/>"},
  {"listview","<path d='M8.5 6h12M8.5 12h12M8.5 18h12M3.5 6h.01M3.5 12h.01M3.5 18h.01'/>"},
  {"gridview","<rect x='3.5' y='3.5' width='7' height='7' rx='1.5'/><rect x='13.5' y='3.5' width='7' height='7' rx='1.5'/><rect x='3.5' y='13.5' width='7' height='7' rx='1.5'/><rect x='13.5' y='13.5' width='7' height='7' rx='1.5'/>"},
  {"restore", "<path d='M4 11a8 8 0 1 1 2.3 5.7M4 5v6h6'/>"},
  {"recent",  "<circle cx='12' cy='12' r='8.5'/><path d='M12 7.5V12l3 2'/>"},
  /* --- Bureau facon ChromeOS ------------------------------------------- */
  {"overview","<rect x='2.5' y='5' width='8' height='14' rx='2'/><rect x='13.5' y='5' width='8' height='14' rx='2'/>"},
  {"dnd",     "<circle cx='12' cy='12' r='9'/><path d='M7.5 12h9'/>"},
  {"night",   "<path d='M20 14.5A8.5 8.5 0 1 1 9.5 4a7 7 0 0 0 10.5 10.5z'/><path d='M17 3.5v3M15.5 5h3'/>"},
  {"record",  "<circle cx='12' cy='12' r='9'/><circle cx='12' cy='12' r='4' fill='currentColor' stroke='none'/>"},
  {"capture", "<path d='M4 8V6a2 2 0 0 1 2-2h2M16 4h2a2 2 0 0 1 2 2v2M20 16v2a2 2 0 0 1-2 2h-2M8 20H6a2 2 0 0 1-2-2v-2'/><circle cx='12' cy='12' r='3'/>"},
  {"clip",    "<rect x='5' y='4.5' width='14' height='16' rx='2.5'/><path d='M9 4.5V3.5h6v1M8.5 10h7M8.5 13.5h7M8.5 17h4'/>"},
  {"emoji",   "<circle cx='12' cy='12' r='9'/><path d='M8.5 14.5a4 4 0 0 0 7 0'/><path d='M9 9.5h.01M15 9.5h.01'/>"},
  {"bell",    "<path d='M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15z'/><path d='M10 20.5a2.2 2.2 0 0 0 4 0'/>"},
  {"calc",    "<rect x='5' y='3' width='14' height='18' rx='2.5'/><path d='M8.5 7.5h7M9 12h.01M12 12h.01M15 12h.01M9 15.5h.01M12 15.5h.01M15 15.5h.01'/>"},
  {"zoom",    "<circle cx='10.5' cy='10.5' r='6.5'/><path d='m15.4 15.4 4.1 4.1M10.5 8v5M8 10.5h5'/>"},
  {"print",   "<path d='M7 9V3.5h10V9'/><rect x='3.5' y='9' width='17' height='8' rx='2'/><path d='M7 14h10v6.5H7z'/>"},
  {"clock",   "<circle cx='12' cy='12' r='9'/><path d='M12 7v5l3.5 2'/>"},
  {"mouse",   "<rect x='6' y='3' width='12' height='18' rx='6'/><path d='M12 3v6'/>"},
  {"access",  "<circle cx='12' cy='4.5' r='1.8'/><path d='M4.5 8.5 12 10l7.5-1.5M12 10v4.5l-3.5 7M12 14.5l3.5 7'/>"},
  {"battery", "<rect x='3' y='7' width='16' height='10' rx='2.5'/><path d='M21 10.5v3'/><path d='M6.5 10v4' />"},
  {"shelf",   "<rect x='3' y='3.5' width='18' height='17' rx='2.5'/><path d='M3 16h18'/>"},
  {"reset",   "<path d='M4 11a8 8 0 1 1 2.3 5.7M4 5v6h6'/><path d='M12 8v4l2.5 1.5'/>"},
  {"user",    "<circle cx='12' cy='8.5' r='4'/><path d='M4.5 20.5a7.5 7.5 0 0 1 15 0'/>"},
  {"monitor", "<rect x='2.5' y='4' width='19' height='13' rx='2.5'/><path d='M8 20.5h8M12 17v3.5'/>"},
  /* Lumi : etincelles */
  {"grid4", "<rect x='4' y='4' width='7' height='7' rx='1.5'/><rect x='13' y='4' width='7' height='7' rx='1.5'/><rect x='4' y='13' width='7' height='7' rx='1.5'/><rect x='13' y='13' width='7' height='7' rx='1.5'/>"},
  {"mic", "<rect x='9' y='3' width='6' height='11.5' rx='3'/><path d='M5.5 11.5a6.5 6.5 0 0 0 13 0M12 18v3'/>"},
  {"scanscreen", "<path d='M4 8V5.5A1.5 1.5 0 0 1 5.5 4H8M16 4h2.5A1.5 1.5 0 0 1 20 5.5V8M20 16v2.5a1.5 1.5 0 0 1-1.5 1.5H16M8 20H5.5A1.5 1.5 0 0 1 4 18.5V16M8.5 10h7M8.5 14h5'/>"},
  {"sparkle", "<path d='M10.5 3.5c.7 4.4 2.6 6.3 7 7-4.4.7-6.3 2.6-7 7-.7-4.4-2.6-6.3-7-7 4.4-.7 6.3-2.6 7-7z'/><path d='M18.5 14.5c.3 1.9 1.1 2.7 3 3-1.9.3-2.7 1.1-3 3-.3-1.9-1.1-2.7-3-3 1.9-.3 2.7-1.1 3-3z'/>"},
  {"send",    "<path d='M21 3 3 10.5l7.2 2.8L13 21z'/><path d='M21 3 10.2 13.3'/>"},
  {"stop",    "<rect x='6.5' y='6.5' width='11' height='11' rx='2'/>"},
};

const char *glyph_svg(const char *name) {
  for (guint i = 0; i < G_N_ELEMENTS(GLYPHS); i++)
    if (g_strcmp0(GLYPHS[i].name, name) == 0) return GLYPHS[i].svg;
  return NULL;
}

static GPtrArray *icons;

static void icon_render(Icon *ic) {
  const char *body = g_str_has_prefix(ic->glyph, "svg:") ? ic->glyph + 4 : glyph_svg(ic->glyph);
  if (!body) return;
  const char *hex = role_color(ic->role);
  int scale = gtk_widget_get_scale_factor(ic->img);
  int px = ic->px * scale;

  GString *b = g_string_new(body);
  g_string_replace(b, "currentColor", hex, 0);
  char *svg = g_strdup_printf("<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 24 24' "
                              "fill='none' stroke='%s' stroke-width='1.8' stroke-linecap='round' "
                              "stroke-linejoin='round'>%s</svg>", px, px, hex, b->str);
  g_string_free(b, TRUE);

  GdkPixbufLoader *l = gdk_pixbuf_loader_new_with_type("svg", NULL);
  if (l && gdk_pixbuf_loader_write(l, (const guchar *)svg, strlen(svg), NULL) && gdk_pixbuf_loader_close(l, NULL)) {
    GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(l);
    cairo_surface_t *s = gdk_cairo_surface_create_from_pixbuf(pb, scale, NULL);
    gtk_image_set_from_surface(GTK_IMAGE(ic->img), s);
    cairo_surface_destroy(s);
  }
  if (l) g_object_unref(l);
  g_free(svg);
}

/* Le tiroir est reconstruit quand des applis sont installees : une icone
   detruite doit quitter la liste, sinon le changement de theme la
   re-rendrait apres liberation. */
static void icon_destroyed(GtkWidget *w, gpointer d) {
  Icon *ic = d;
  g_ptr_array_remove(icons, ic);
  g_free(ic->glyph); g_free(ic);
}

Icon *icon_new(const char *glyph, int role, int px) {
  Icon *ic = g_new0(Icon, 1);
  ic->img = gtk_image_new();
  ic->glyph = g_strdup(glyph);
  ic->role = role; ic->px = px;
  gtk_widget_set_size_request(ic->img, px, px);
  g_ptr_array_add(icons, ic);
  g_signal_connect(ic->img, "destroy", G_CALLBACK(icon_destroyed), ic);
  icon_render(ic);
  return ic;
}

void icon_set(Icon *ic, const char *glyph, int role) {
  if (g_strcmp0(ic->glyph, glyph) == 0 && ic->role == role) return;
  g_free(ic->glyph); ic->glyph = g_strdup(glyph); ic->role = role;
  icon_render(ic);
}

/* ------------------------------------------------------------------ */
/* Style et theme partage                                              */
/* ------------------------------------------------------------------ */
static GtkCssProvider *css;
static char *app_css;
static void (*theme_cb)(void);
static GFileMonitor *theme_mon;

static char *theme_path(void) { return g_build_filename(g_get_user_config_dir(), "lumina", "theme", NULL); }

static void apply_css(void) {
  GString *s = g_string_new(NULL);
#define DEF(n, v) g_string_append_printf(s, "@define-color %s %s;\n", n, v)
  DEF("surface", P->surface); DEF("c_low", P->c_low); DEF("c", P->c); DEF("c_high", P->c_high);
  DEF("c_top", P->c_top); DEF("on", P->on); DEF("on_var", P->on_var); DEF("outline_var", P->outline_var);
  DEF("pri", P->pri); DEF("on_pri", P->on_pri); DEF("pri_c", P->pri_c); DEF("on_pri_c", P->on_pri_c);
  DEF("sec_c", P->sec_c); DEF("on_sec_c", P->on_sec_c); DEF("ter", P->ter); DEF("ter_c", P->ter_c);
  DEF("on_ter_c", P->on_ter_c); DEF("bar_solid", P->bar_solid);
  DEF("err", dark ? "#ffb4ab" : "#ba1a1a");
#undef DEF
  /* Fluidite : survols et changements d'etat en douceur partout */
  g_string_append(s, "button, row, flowboxchild, entry, switch, .tile, .slot, .card {\n"
                     "  transition: background-color 160ms ease-out, color 160ms ease-out,\n"
                     "              box-shadow 200ms ease-out, opacity 160ms ease-out; }\n");
  g_string_append(s, app_css);
  gtk_css_provider_load_from_data(css, s->str, -1, NULL);
  g_string_free(s, TRUE);
}

static void apply_theme(gboolean on) {
  dark = on; P = pick(on);
  apply_css();
  for (guint i = 0; i < icons->len; i++) icon_render(icons->pdata[i]);
  g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", on, NULL);
  if (theme_cb) theme_cb();
}

static gboolean read_theme(void) {
  char *p = theme_path(), *t = NULL;
  g_file_get_contents(p, &t, NULL, NULL);
  gboolean d = t && g_str_has_prefix(g_strstrip(t), "dark");
  g_free(p); g_free(t);
  return d;
}

/* Une autre appli LuminaOS a change le theme, l'accent ou le fond
   d'ecran (fichiers de ~/.config/lumina) : on suit. */
static void on_theme_file(GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent ev, gpointer d) {
  if (ev != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && ev != G_FILE_MONITOR_EVENT_CREATED &&
      ev != G_FILE_MONITOR_EVENT_DELETED && ev != G_FILE_MONITOR_EVENT_MOVED_IN &&
      ev != G_FILE_MONITOR_EVENT_RENAMED) return;
  GFile *t = (ev == G_FILE_MONITOR_EVENT_RENAMED && o) ? o : f;
  char *n = g_file_get_basename(t);
  gboolean ours = !g_strcmp0(n, "theme") || !g_strcmp0(n, "accent") || !g_strcmp0(n, "wallpaper") ||
                  !g_strcmp0(n, "contrast") || !g_strcmp0(n, "wallstyle");
  g_free(n);
  if (!ours) return;
  load_accent();
  apply_theme(read_theme());
}

void lumina_ui_init(const char *css_text, void (*on_theme)(void)) {
  /* Ouvrir / Enregistrer : selecteur de LuminaOS (Fichiers, via le portail
     du shell) plutot que la boite de dialogue GTK */
  g_setenv("GTK_USE_PORTAL", "1", FALSE);
  icons = g_ptr_array_new();
  app_css = g_strdup(css_text);
  theme_cb = on_theme;
  css = gtk_css_provider_new();
  load_accent();
  dark = read_theme(); P = pick(dark);
  apply_css();
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", dark, NULL);

  char *p = theme_path(), *dir = g_path_get_dirname(p);
  g_mkdir_with_parents(dir, 0755);
  GFile *f = g_file_new_for_path(dir);
  theme_mon = g_file_monitor_directory(f, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
  if (theme_mon) g_signal_connect(theme_mon, "changed", G_CALLBACK(on_theme_file), NULL);
  g_object_unref(f); g_free(p); g_free(dir);
}

/* Appli toujours sombre (Galerie), sans toucher au reglage du systeme */
void lumina_ui_set_dark_local(gboolean on) { apply_theme(on); }

/* Copie du widget, reduite si elle est grande (photo de la Galerie),
   legerement transparente et tenue par son milieu. */
void lumina_ui_drag_ghost(GtkWidget *w, GdkDragContext *c, gpointer d) {
  int width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
  int sf = gtk_widget_get_scale_factor(w);
  if (width < 1 || height < 1) return;
  double f = MIN(1.0, 200.0 / MAX(width, height));
  int gw = MAX(1, (int)(width * f)), gh = MAX(1, (int)(height * f));
  cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gw * sf, gh * sf);
  cairo_surface_set_device_scale(s, sf, sf);
  cairo_t *cr = cairo_create(s);
  cairo_scale(cr, f, f);
  cairo_push_group(cr);
  gtk_widget_draw(w, cr);
  cairo_pop_group_to_source(cr);
  cairo_paint_with_alpha(cr, 0.85);
  cairo_destroy(cr);
  cairo_surface_set_device_offset(s, -gw * sf / 2.0, -gh * sf / 2.0);
  gtk_drag_set_icon_surface(c, s);
  cairo_surface_destroy(s);
}

void lumina_ui_rerender(void) { for (guint i = 0; i < icons->len; i++) icon_render(icons->pdata[i]); }

void lumina_ui_set_accent(const char *v) {
  char *p = conf_file("accent");
  if (!v || !g_ascii_strcasecmp(v, LUMINA_DEFAULT_ACCENT)) g_unlink(p);
  else g_file_set_contents(p, v, -1, NULL);
  g_free(p);
  load_accent();
  apply_theme(dark);
}

void lumina_ui_set_dark(gboolean on) {
  apply_theme(on);
  char *p = theme_path();
  g_file_set_contents(p, on ? "dark\n" : "light\n", -1, NULL);
  g_free(p);
}

/* ------------------------------------------------------------------ */
/* Petits utilitaires d'interface                                      */
/* ------------------------------------------------------------------ */
void add_class(GtkWidget *w, const char *c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

void set_class(GtkWidget *w, const char *c, gboolean on) {
  GtkStyleContext *sc = gtk_widget_get_style_context(w);
  if (on) gtk_style_context_add_class(sc, c); else gtk_style_context_remove_class(sc, c);
}

GtkWidget *label(const char *text, const char *cls, float xalign) {
  GtkWidget *l = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(l), xalign);
  if (cls) add_class(l, cls);
  return l;
}

void hex_rgb(const char *h, double *r, double *g, double *b) {
  unsigned v = strtoul(h + 1, NULL, 16);
  *r = ((v >> 16) & 255) / 255.0; *g = ((v >> 8) & 255) / 255.0; *b = (v & 255) / 255.0;
}

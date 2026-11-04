/*
 * lumina-ui : ce que toutes les applis LuminaOS partagent pour se
 * ressembler. Palette tonale Material 3, glyphes, rendu des icones, et
 * theme clair/sombre commun (~/.config/lumina/theme) applique en direct a
 * toutes les applis ouvertes quand l'une d'elles le change.
 */
#include "lumina-ui.h"
#include <stdlib.h>
#include <string.h>

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

const Palette *P = &LIGHT;
gboolean dark = FALSE;


const char *role_color(int r) {
  switch (r) {
    case R_ON:       return P->on;
    case R_ON_VAR:   return P->on_var;
    case R_ON_PRI_C: return P->on_pri_c;
    case R_ON_SEC_C: return P->on_sec_c;
    case R_ON_TER_C: return P->on_ter_c;
    case R_TER:      return P->ter;
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
  {"more",    "<g fill='currentColor' stroke='none'><circle cx='12' cy='5.5' r='1.7'/><circle cx='12' cy='12' r='1.7'/><circle cx='12' cy='18.5' r='1.7'/></g>"},
  {"plus",    "<path d='M12 5v14M5 12h14'/>"},
  {"close",   "<path d='m6 6 12 12M18 6 6 18'/>"},
  {"listview","<path d='M8.5 6h12M8.5 12h12M8.5 18h12M3.5 6h.01M3.5 12h.01M3.5 18h.01'/>"},
  {"gridview","<rect x='3.5' y='3.5' width='7' height='7' rx='1.5'/><rect x='13.5' y='3.5' width='7' height='7' rx='1.5'/><rect x='3.5' y='13.5' width='7' height='7' rx='1.5'/><rect x='13.5' y='13.5' width='7' height='7' rx='1.5'/>"},
  {"restore", "<path d='M4 11a8 8 0 1 1 2.3 5.7M4 5v6h6'/>"},
  {"recent",  "<circle cx='12' cy='12' r='8.5'/><path d='M12 7.5V12l3 2'/>"},
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
#undef DEF
  g_string_append(s, app_css);
  gtk_css_provider_load_from_data(css, s->str, -1, NULL);
  g_string_free(s, TRUE);
}

static void apply_theme(gboolean on) {
  dark = on; P = on ? &DARK : &LIGHT;
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

/* Une autre appli LuminaOS a change le theme : on suit. */
static void on_theme_file(GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent ev, gpointer d) {
  if (ev != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && ev != G_FILE_MONITOR_EVENT_CREATED &&
      ev != G_FILE_MONITOR_EVENT_DELETED) return;
  gboolean d2 = read_theme();
  if (d2 != dark) apply_theme(d2);
}

void lumina_ui_init(const char *css_text, void (*on_theme)(void)) {
  icons = g_ptr_array_new();
  app_css = g_strdup(css_text);
  theme_cb = on_theme;
  css = gtk_css_provider_new();
  dark = read_theme(); P = dark ? &DARK : &LIGHT;
  apply_css();
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", dark, NULL);

  char *p = theme_path(), *dir = g_path_get_dirname(p);
  g_mkdir_with_parents(dir, 0755);
  GFile *f = g_file_new_for_path(p);
  theme_mon = g_file_monitor_file(f, G_FILE_MONITOR_NONE, NULL, NULL);
  if (theme_mon) g_signal_connect(theme_mon, "changed", G_CALLBACK(on_theme_file), NULL);
  g_object_unref(f); g_free(p); g_free(dir);
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

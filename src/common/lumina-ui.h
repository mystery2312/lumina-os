/*
 * lumina-ui : palette, glyphes, icones et theme partages par les applis
 * LuminaOS (lumina-shell, lumina-files...). Voir lumina-ui.c.
 */
#ifndef LUMINA_UI_H
#define LUMINA_UI_H
#include <gtk/gtk.h>

/* Palette tonale Material 3, graine #0F7A63 */
typedef struct {
  const char *surface, *c_low, *c, *c_high, *c_top, *on, *on_var, *outline_var,
             *pri, *on_pri, *pri_c, *on_pri_c, *sec_c, *on_sec_c,
             *ter, *ter_c, *on_ter_c, *wall_a, *wall_b, *wall_c,
             *bar_solid;   /* un cran plus fonce que le cadre des fenetres */
} Palette;

extern const Palette *P;   /* palette en cours (claire ou sombre) */
extern gboolean dark;

/* Roles de couleur des icones : re-rendues a chaque changement de theme. */
enum { R_ON, R_ON_VAR, R_PRI, R_TER, R_ON_PRI_C, R_ON_SEC_C, R_ON_TER_C };
const char *role_color(int r);

/* Glyphes 24 px, trait 1.8 */
const char *glyph_svg(const char *name);

typedef struct {
  GtkWidget *img;
  char *glyph;     /* nom de glyphe, ou "svg:" + corps brut */
  int role, px;
} Icon;
Icon *icon_new(const char *glyph, int role, int px);
void icon_set(Icon *ic, const char *glyph, int role);

/* Installe le style (palette en @define-color + css de l'appli) et suit le
   theme commun ; on_theme est appele apres chaque changement. */
void lumina_ui_init(const char *css, void (*on_theme)(void));
void lumina_ui_set_dark(gboolean on);   /* applique et enregistre */

void add_class(GtkWidget *w, const char *c);
void set_class(GtkWidget *w, const char *c, gboolean on);
GtkWidget *label(const char *text, const char *cls, float xalign);
void hex_rgb(const char *h, double *r, double *g, double *b);
#endif

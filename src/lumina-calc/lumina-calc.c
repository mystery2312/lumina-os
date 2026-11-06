/*
 * lumina-calc : la Calculatrice de LuminaOS. Touches a l'ecran ou au
 * clavier, parentheses, pourcentage, puissance, racine ; historique des
 * calculs (cliquer pour reprendre un resultat).
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <string.h>
#include <math.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-calc.csd { border-radius: 0 0 16px 16px; background: @surface; }\n"
  "window.lumina-calc.csd decoration { border-radius: 16px; box-shadow: 0 6px 22px alpha(black, 0.18), 0 0 0 1px alpha(black, 0.06); }\n"
  ".header { background: @surface; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 30px; min-height: 30px; color: @on_var; }\n"
  ".header button.titlebutton:hover { background: alpha(@on, 0.08); }\n"
  ".apptitle { font-size: 15px; color: @on; padding-left: 8px; }\n"
  ".display { background: @c_low; border-radius: 24px; padding: 14px 20px; margin: 4px 14px 10px 14px; }\n"
  ".expr { font-size: 18px; color: @on_var; }\n"
  ".result { font-size: 38px; color: @on; }\n"
  ".history { color: @on_var; font-size: 12.5px; }\n"
  ".hist { background: transparent; color: @on_var; border-radius: 10px; padding: 2px 6px; }\n"
  ".hist:hover { background: alpha(@on, 0.06); }\n"
  ".key { background: @c_high; color: @on; border-radius: 22px; font-size: 20px; min-height: 52px; }\n"
  ".key:hover { background: @c_top; }\n"
  ".key.op { background: @sec_c; color: @on_sec_c; }\n"
  ".key.fn { background: @ter_c; color: @on_ter_c; font-size: 17px; }\n"
  ".key.eq { background: @pri; color: @on_pri; }\n"
  ".keys { margin: 0 14px 16px 14px; }\n";

static GtkWidget *expr_l, *res_l, *hist_box;
static GString *expr;
static gboolean just_done;

/* --- Evaluation : + - * / % ^ sqrt, parentheses, virgule ------------ */
typedef struct { const char *p; gboolean err; } C;
static double e_expr(C *c);
static void ws(C *c) { while (*c->p == ' ') c->p++; }
static double e_atom(C *c) {
  ws(c);
  if (g_str_has_prefix(c->p, "√")) { c->p += strlen("√"); double v = e_atom(c); if (v < 0) c->err = TRUE; return sqrt(v); }
  if (*c->p == '(') { c->p++; double v = e_expr(c); ws(c); if (*c->p == ')') c->p++; return v; }
  if (*c->p == '-' || g_str_has_prefix(c->p, "−")) { c->p += *c->p == '-' ? 1 : strlen("−"); return -e_atom(c); }
  char b[64]; int n = 0;
  while ((g_ascii_isdigit(*c->p) || *c->p == ',' || *c->p == '.') && n < 63) { b[n++] = *c->p == ',' ? '.' : *c->p; c->p++; }
  b[n] = 0;
  if (!n) { c->err = TRUE; return 0; }
  double v = g_ascii_strtod(b, NULL);
  ws(c);
  if (*c->p == '%') { c->p++; v /= 100; }
  return v;
}
static double e_pow(C *c) { double b = e_atom(c); ws(c); if (*c->p == '^') { c->p++; return pow(b, e_pow(c)); } return b; }
static double e_term(C *c) {
  double v = e_pow(c);
  for (;;) {
    ws(c);
    if (*c->p == '*' || g_str_has_prefix(c->p, "×")) { c->p += *c->p == '*' ? 1 : strlen("×"); v *= e_pow(c); }
    else if (*c->p == '/' || g_str_has_prefix(c->p, "÷")) {
      c->p += *c->p == '/' ? 1 : strlen("÷");
      double r = e_pow(c); if (r == 0) c->err = TRUE; else v /= r;
    } else return v;
  }
}
static double e_expr(C *c) {
  double v = e_term(c);
  for (;;) {
    ws(c);
    if (*c->p == '+') { c->p++; v += e_term(c); }
    else if (*c->p == '-' || g_str_has_prefix(c->p, "−")) { c->p += *c->p == '-' ? 1 : strlen("−"); v -= e_term(c); }
    else return v;
  }
}
static char *fmt(double v) {
  char *s = g_strdup_printf("%.12g", v);
  for (char *p = s; *p; p++) if (*p == '.') *p = ',';
  return s;
}
static char *eval(const char *q, gboolean *ok) {
  /* parentheses oubliees : on les ferme */
  GString *g = g_string_new(q);
  int open = 0;
  for (const char *p = q; *p; p++) open += *p == '(' ? 1 : *p == ')' ? -1 : 0;
  while (open-- > 0) g_string_append_c(g, ')');
  C c = {g->str, FALSE};
  double v = e_expr(&c);
  ws(&c);
  *ok = !c.err && !*c.p && !isnan(v) && !isinf(v);
  g_string_free(g, TRUE);
  return *ok ? fmt(v) : g_strdup("Erreur");
}

static void show(void) {
  gtk_label_set_text(GTK_LABEL(expr_l), expr->len ? expr->str : " ");
  gboolean ok;
  char *r = expr->len ? eval(expr->str, &ok) : g_strdup("0");
  if (expr->len && !ok) { g_free(r); r = g_strdup(" "); }
  if (!just_done) gtk_label_set_text(GTK_LABEL(res_l), r);
  g_free(r);
}

static void on_hist(GtkButton *b, gpointer d) {
  g_string_assign(expr, gtk_button_get_label(b) + 0);
  char *eq = strstr(expr->str, " = ");
  if (eq) g_string_erase(expr, 0, eq - expr->str + 3);
  just_done = FALSE; show();
}

static void equals(void) {
  if (!expr->len) return;
  gboolean ok;
  char *r = eval(expr->str, &ok);
  gtk_label_set_text(GTK_LABEL(res_l), r);
  if (ok) {
    char *h = g_strdup_printf("%s = %s", expr->str, r);
    GtkWidget *b = gtk_button_new_with_label(h);
    add_class(b, "hist");
    gtk_widget_set_halign(b, GTK_ALIGN_END);
    g_signal_connect(b, "clicked", G_CALLBACK(on_hist), NULL);
    gtk_box_pack_end(GTK_BOX(hist_box), b, FALSE, FALSE, 0);
    gtk_widget_show(b);
    GList *k = gtk_container_get_children(GTK_CONTAINER(hist_box));
    if (g_list_length(k) > 3) gtk_widget_destroy(g_list_first(k)->data);
    g_list_free(k);
    g_string_assign(expr, r);
    g_free(h);
    just_done = TRUE;
  }
  gtk_label_set_text(GTK_LABEL(expr_l), " ");
  g_free(r);
}

static void press(const char *k) {
  if (!strcmp(k, "=")) { equals(); return; }
  if (!strcmp(k, "C")) { g_string_truncate(expr, 0); just_done = FALSE; show(); return; }
  if (!strcmp(k, "⌫")) {
    if (expr->len) { const char *e = expr->str + expr->len, *p = g_utf8_prev_char(e); g_string_truncate(expr, p - expr->str); }
    just_done = FALSE; show(); return;
  }
  /* Apres un resultat : un chiffre recommence, un operateur continue */
  if (just_done && (g_ascii_isdigit(k[0]) || k[0] == ',' || k[0] == '(' || g_str_has_prefix(k, "√"))) g_string_truncate(expr, 0);
  just_done = FALSE;
  g_string_append(expr, k);
  show();
}
static void on_key_btn(GtkButton *b, gpointer d) { press(d); }

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  switch (e->keyval) {
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_equal: press("="); return TRUE;
    case GDK_KEY_BackSpace: press("⌫"); return TRUE;
    case GDK_KEY_Escape: case GDK_KEY_Delete: press("C"); return TRUE;
    case GDK_KEY_plus: case GDK_KEY_KP_Add: press("+"); return TRUE;
    case GDK_KEY_minus: case GDK_KEY_KP_Subtract: press("−"); return TRUE;
    case GDK_KEY_asterisk: case GDK_KEY_KP_Multiply: press("×"); return TRUE;
    case GDK_KEY_slash: case GDK_KEY_KP_Divide: press("÷"); return TRUE;
    case GDK_KEY_comma: case GDK_KEY_period: case GDK_KEY_KP_Decimal: case GDK_KEY_KP_Separator: press(","); return TRUE;
    case GDK_KEY_percent: press("%"); return TRUE;
    case GDK_KEY_parenleft: press("("); return TRUE;
    case GDK_KEY_parenright: press(")"); return TRUE;
    case GDK_KEY_asciicircum: press("^"); return TRUE;
  }
  guint32 u = gdk_keyval_to_unicode(e->keyval);
  if (u >= '0' && u <= '9') { char s[2] = {(char)u, 0}; press(s); return TRUE; }
  return FALSE;
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-calc");
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, NULL);
  expr = g_string_new(NULL);
  GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-calc");
  gtk_window_set_title(GTK_WINDOW(win), "Calculatrice");
  gtk_window_set_default_size(GTK_WINDOW(win), 360, 560);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,close");
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), label("Calculatrice", "apptitle", 0));
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *disp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  add_class(disp, "display");
  hist_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(hist_box, "history");
  gtk_container_add(GTK_CONTAINER(disp), hist_box);
  expr_l = label(" ", "expr", 1);
  gtk_label_set_ellipsize(GTK_LABEL(expr_l), PANGO_ELLIPSIZE_START);
  res_l = label("0", "result", 1);
  gtk_label_set_ellipsize(GTK_LABEL(res_l), PANGO_ELLIPSIZE_START);
  gtk_label_set_selectable(GTK_LABEL(res_l), TRUE);
  gtk_container_add(GTK_CONTAINER(disp), expr_l);
  gtk_container_add(GTK_CONTAINER(disp), res_l);
  gtk_container_add(GTK_CONTAINER(v), disp);

  static const char *K[6][4] = {
    {"C", "(", ")", "÷"}, {"√", "^", "%", "×"}, {"7", "8", "9", "−"},
    {"4", "5", "6", "+"}, {"1", "2", "3", "⌫"}, {"0", ",", "=", NULL},
  };
  GtkWidget *g = gtk_grid_new();
  add_class(g, "keys");
  gtk_grid_set_row_spacing(GTK_GRID(g), 8);
  gtk_grid_set_column_spacing(GTK_GRID(g), 8);
  gtk_grid_set_row_homogeneous(GTK_GRID(g), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(g), TRUE);
  gtk_widget_set_vexpand(g, TRUE);
  for (int r = 0; r < 6; r++)
    for (int c = 0; c < 4; c++) {
      const char *k = K[r][c];
      if (!k) continue;
      GtkWidget *b = gtk_button_new_with_label(k);
      add_class(b, "key");
      if (strstr("÷×−+", k) && strlen(k) > 0 && !g_ascii_isdigit(k[0]) && strcmp(k, "C") && strcmp(k, ",")) add_class(b, "op");
      if (!strcmp(k, "C") || !strcmp(k, "(") || !strcmp(k, ")") || !strcmp(k, "√") || !strcmp(k, "^") || !strcmp(k, "%") || !strcmp(k, "⌫")) add_class(b, "fn");
      if (!strcmp(k, "=")) add_class(b, "eq");
      g_signal_connect(b, "clicked", G_CALLBACK(on_key_btn), (gpointer)k);
      gtk_grid_attach(GTK_GRID(g), b, c, r, !strcmp(k, "=") ? 2 : 1, 1);
    }
  gtk_container_add(GTK_CONTAINER(v), g);
  gtk_container_add(GTK_CONTAINER(win), v);
  gtk_widget_show_all(win);
  gtk_main();
  return 0;
}

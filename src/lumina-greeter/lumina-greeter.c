/*
 * lumina-greeter : l'ecran de connexion de LuminaOS (lance par greetd, voir
 * lumina-greeter-session). Grande horloge, une carte par personne, mot de
 * passe ou code PIN, session invite, eteindre / redemarrer.
 *
 * Dialogue avec greetd sur $GREETD_SOCK : messages JSON precedes de leur
 * longueur (32 bits, ordre de la machine).
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <string.h>
#include <math.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.greeter { background: transparent; }\n"
  ".clock { font-size: 84px; font-weight: 300; color: #ffffff; text-shadow: 0 2px 12px alpha(black, 0.35); }\n"
  ".gdate { font-size: 18px; color: alpha(white, 0.9); text-shadow: 0 1px 6px alpha(black, 0.35); }\n"
  ".card { background: alpha(@surface, 0.80); color: @on; border-radius: 24px; padding: 18px 20px; min-width: 120px; }\n"
  ".card:hover { background: alpha(@surface, 0.92); }\n"
  ".card.sel { background: @surface; box-shadow: 0 0 0 3px @pri; }\n"
  ".cname { font-size: 14px; font-weight: 500; }\n"
  ".panel { background: alpha(@surface, 0.92); color: @on; border-radius: 28px; padding: 22px 24px; }\n"
  ".who { font-size: 20px; }\n"
  ".pw { background: @c_low; color: @on; border: 1px solid @outline_var; border-radius: 24px; padding: 6px 18px; min-height: 36px; caret-color: @pri; }\n"
  ".pw:focus { border: 2px solid @pri; padding: 5px 17px; }\n"
  ".go { background: @pri; color: @on_pri; border-radius: 24px; min-width: 48px; min-height: 48px; padding: 0 18px; font-weight: 500; font-size: 15px; }\n"
  ".go.arrow { padding: 0; min-width: 48px; font-size: 20px; }\n"
  ".go:disabled { opacity: 0.5; }\n"
  ".err { color: #ba1a1a; font-size: 13px; }\n"
  ".hint { color: @on_var; font-size: 12.5px; }\n"
  ".bottom { background: alpha(@surface, 0.80); border-radius: 22px; padding: 6px; }\n"
  ".bbtn { background: transparent; color: @on; border-radius: 18px; padding: 8px 14px; font-weight: 500; }\n"
  ".bbtn:hover { background: alpha(@on, 0.08); }\n";

typedef struct { char *user, *name; gboolean pw; } Person;
static GPtrArray *people;
static Person *sel;
static GtkWidget *win, *clock_l, *date_l, *cards, *panel, *who_l, *pw_e, *go_b, *err_l, *hint_l;
static GSocketConnection *conn;
static const char *DAYS[] = {"", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
static const char *MONTHS[] = {"", "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
                               "septembre", "octobre", "novembre", "décembre"};

/* ---------------- greetd ---------------- */
static char *json_str(const char *s) {
  GString *g = g_string_new("\"");
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') g_string_append_printf(g, "\\%c", *p);
    else if (*p < 0x20) g_string_append_printf(g, "\\u%04x", *p);
    else g_string_append_c(g, *p);
  }
  g_string_append_c(g, '"');
  return g_string_free(g, FALSE);
}

/* Envoie une requete, rend la reponse (JSON brut) ou NULL */
static char *greetd(const char *json) {
  if (!conn) {
    const char *path = g_getenv("GREETD_SOCK");
    if (!path) return NULL;
    GSocketClient *c = g_socket_client_new();
    GSocketAddress *a = g_unix_socket_address_new(path);
    conn = g_socket_client_connect(c, G_SOCKET_CONNECTABLE(a), NULL, NULL);
    g_object_unref(a); g_object_unref(c);
    if (!conn) return NULL;
  }
  GOutputStream *o = g_io_stream_get_output_stream(G_IO_STREAM(conn));
  GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
  guint32 len = strlen(json);
  if (!g_output_stream_write_all(o, &len, 4, NULL, NULL, NULL) ||
      !g_output_stream_write_all(o, json, len, NULL, NULL, NULL)) return NULL;
  guint32 rl = 0;
  if (!g_input_stream_read_all(in, &rl, 4, NULL, NULL, NULL) || rl > 1 << 20) return NULL;
  char *r = g_malloc0(rl + 1);
  if (!g_input_stream_read_all(in, r, rl, NULL, NULL, NULL)) { g_free(r); return NULL; }
  return r;
}
static gboolean is_type(const char *r, const char *t) {
  char *k = g_strdup_printf("\"type\":\"%s\"", t);
  char *k2 = g_strdup_printf("\"type\": \"%s\"", t);
  gboolean ok = r && (strstr(r, k) || strstr(r, k2));
  g_free(k); g_free(k2);
  return ok;
}
static void cancel_session(void) { g_free(greetd("{\"type\":\"cancel_session\"}")); }

static void fail(const char *msg) {
  gtk_label_set_text(GTK_LABEL(err_l), msg);
  gtk_widget_show(err_l);
  gtk_widget_set_sensitive(go_b, TRUE);
  gtk_widget_set_sensitive(pw_e, TRUE);
  gtk_entry_set_text(GTK_ENTRY(pw_e), "");
  gtk_widget_grab_focus(pw_e);
}

/* Ouverture de session : create_session, eventuellement le mot de passe,
   puis start_session. greetd lance alors la session et nous ferme. */
static void login(const char *user, const char *password) {
  gtk_widget_hide(err_l);
  gtk_widget_set_sensitive(go_b, FALSE);
  gtk_widget_set_sensitive(pw_e, FALSE);
  while (gtk_events_pending()) gtk_main_iteration();
  char *u = json_str(user), *req = g_strdup_printf("{\"type\":\"create_session\",\"username\":%s}", u);
  char *r = greetd(req);
  g_free(u); g_free(req);
  /* Questions de PAM : on repond le mot de passe aux questions secretes,
     rien aux messages d'information. */
  int guard = 0;
  while (r && is_type(r, "auth_message") && guard++ < 4) {
    gboolean secret = strstr(r, "secret") != NULL;
    char *p = json_str(secret ? (password ? password : "") : "");
    char *q = g_strdup_printf("{\"type\":\"post_auth_message_response\",\"response\":%s}", p);
    g_free(r); r = greetd(q);
    g_free(p); g_free(q);
  }
  if (!r) { fail("L'écran de connexion ne répond pas."); return; }
  if (!is_type(r, "success")) {
    g_free(r); cancel_session();
    fail(password && *password ? "Mot de passe incorrect" : "Connexion impossible");
    return;
  }
  g_free(r);
  r = greetd("{\"type\":\"start_session\",\"cmd\":[\"/usr/local/bin/lumina-session\"],\"env\":[]}");
  if (r && is_type(r, "success")) { g_free(r); gtk_main_quit(); return; }
  g_free(r); cancel_session();
  fail("La session n'a pas pu démarrer.");
}

/* ---------------- interface ---------------- */
static gboolean draw_bg(GtkWidget *w, cairo_t *cr, gpointer d) {
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), r, g, b;
  hex_rgb(P->wall_c, &r, &g, &b);
  cairo_set_source_rgb(cr, r, g, b); cairo_paint(cr);
  const char *cols[] = {P->wall_a, P->wall_b};
  double pos[][4] = {{0.18, 0.88, 0.60, 0.70}, {0.88, 0.12, 0.55, 0.65}};
  for (int i = 0; i < 2; i++) {
    hex_rgb(cols[i], &r, &g, &b);
    cairo_save(cr);
    cairo_translate(cr, W * pos[i][0], H * pos[i][1]); cairo_scale(cr, W * pos[i][2], H * pos[i][3]);
    cairo_pattern_t *p = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
    cairo_pattern_add_color_stop_rgba(p, 0, r, g, b, 1);
    cairo_pattern_add_color_stop_rgba(p, 0.7, r, g, b, 0);
    cairo_set_source(cr, p); cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI); cairo_fill(cr);
    cairo_pattern_destroy(p); cairo_restore(cr);
  }
  return FALSE;
}

static gboolean draw_avatar(GtkWidget *w, cairo_t *cr, gpointer d) {
  Person *p = d;
  double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), R = MIN(W, H) / 2, r, g, b;
  static const char *cols[] = {"#1a73e8", "#188038", "#e37400", "#a142f4", "#d93025", "#00897b"};
  hex_rgb(!strcmp(p->user, "invite") ? "#5f6368" : cols[g_str_hash(p->user) % 6], &r, &g, &b);
  cairo_set_source_rgb(cr, r, g, b);
  cairo_arc(cr, W / 2, H / 2, R, 0, 2 * M_PI); cairo_fill(cr);
  char first[8] = {0};
  g_unichar_to_utf8(g_unichar_toupper(g_utf8_get_char(p->name)), first);
  PangoLayout *l = gtk_widget_create_pango_layout(w, first);
  char *f = g_strdup_printf("Roboto Medium %d", (int)(R * 0.75));
  PangoFontDescription *fd = pango_font_description_from_string(f);
  pango_layout_set_font_description(l, fd);
  int lw, lh; pango_layout_get_pixel_size(l, &lw, &lh);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_move_to(cr, W / 2 - lw / 2.0, H / 2 - lh / 2.0);
  pango_cairo_show_layout(cr, l);
  pango_font_description_free(fd); g_free(f); g_object_unref(l);
  return FALSE;
}
static GtkWidget *avatar(Person *p, int px) {
  GtkWidget *a = gtk_drawing_area_new();
  gtk_widget_set_size_request(a, px, px);
  gtk_widget_set_halign(a, GTK_ALIGN_CENTER);
  g_signal_connect(a, "draw", G_CALLBACK(draw_avatar), p);
  return a;
}

static void refresh_cards(void) {
  GList *k = gtk_container_get_children(GTK_CONTAINER(cards));
  for (GList *l = k; l; l = l->next) set_class(l->data, "sel", g_object_get_data(G_OBJECT(l->data), "p") == sel);
  g_list_free(k);
}

static void choose(Person *p) {
  sel = p;
  refresh_cards();
  gtk_widget_hide(err_l);
  char *t = g_strdup_printf(!strcmp(p->user, "invite") ? "Session invité" : "Bonjour %s", p->name);
  gtk_label_set_text(GTK_LABEL(who_l), t);
  g_free(t);
  gtk_entry_set_text(GTK_ENTRY(pw_e), "");
  gtk_widget_set_visible(pw_e, p->pw);
  gtk_label_set_text(GTK_LABEL(hint_l), !strcmp(p->user, "invite")
                     ? "Rien n'est conservé : fichiers et historique disparaissent à la déconnexion."
                     : p->pw ? "Mot de passe ou code PIN" : "Aucun mot de passe sur ce compte");
  gtk_button_set_label(GTK_BUTTON(go_b), p->pw ? "→" : "Se connecter");
  gtk_widget_set_hexpand(go_b, !p->pw);
  set_class(go_b, "arrow", p->pw);
  gtk_widget_set_sensitive(go_b, TRUE);
  gtk_widget_set_sensitive(pw_e, TRUE);
  gtk_widget_show(panel);
  if (p->pw) gtk_widget_grab_focus(pw_e); else gtk_widget_grab_focus(go_b);
}
static void on_card(GtkButton *b, gpointer d) { choose(d); }
static void on_go(GtkWidget *w, gpointer d) { if (sel) login(sel->user, sel->pw ? gtk_entry_get_text(GTK_ENTRY(pw_e)) : ""); }

static void on_power(GtkButton *b, gpointer d) {
  const char *argv[] = {"systemctl", d, NULL};
  spawn_async(argv);
}

static gboolean tick(gpointer d) {
  GDateTime *n = g_date_time_new_now_local();
  char *t = g_date_time_format(n, "%H:%M");
  char *ds = g_strdup_printf("%s %d %s", DAYS[g_date_time_get_day_of_week(n)], g_date_time_get_day_of_month(n),
                             MONTHS[g_date_time_get_month(n)]);
  ds[0] = g_ascii_toupper(ds[0]);
  gtk_label_set_text(GTK_LABEL(clock_l), t);
  gtk_label_set_text(GTK_LABEL(date_l), ds);
  g_free(t); g_free(ds); g_date_time_unref(n);
  return G_SOURCE_CONTINUE;
}

static void load_people(void) {
  people = g_ptr_array_new();
  const char *argv[] = {"/usr/local/sbin/lumina-users", "list", NULL};
  char *out = run_argv(argv, NULL);
  char **lines = out ? g_strsplit(out, "\n", -1) : NULL;
  for (char **l = lines; l && *l; l++) {
    char **f = g_strsplit(*l, "\t", 3);
    if (g_strv_length(f) == 3) {
      Person *p = g_new0(Person, 1);
      p->user = g_strdup(f[0]); p->name = g_strdup(*f[1] ? f[1] : f[0]); p->pw = !strcmp(f[2], "1");
      g_ptr_array_add(people, p);
    }
    g_strfreev(f);
  }
  g_strfreev(lines); g_free(out);
  if (!people->len) {
    Person *p = g_new0(Person, 1);
    p->user = g_strdup("lumina"); p->name = g_strdup("LuminaOS"); p->pw = TRUE;
    g_ptr_array_add(people, p);
  }
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  if (e->keyval == GDK_KEY_Escape && sel) { sel = NULL; refresh_cards(); gtk_widget_hide(panel); return TRUE; }
  return FALSE;
}

int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, NULL);
  lumina_ui_set_dark_local(TRUE);
  load_people();

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "greeter");
  gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
  gtk_window_fullscreen(GTK_WINDOW(win));
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

  GtkWidget *ov = gtk_overlay_new();
  GtkWidget *bg = gtk_drawing_area_new();
  g_signal_connect(bg, "draw", G_CALLBACK(draw_bg), NULL);
  gtk_container_add(GTK_CONTAINER(ov), bg);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_halign(v, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(v, GTK_ALIGN_CENTER);
  clock_l = label("", "clock", 0.5);
  date_l = label("", "gdate", 0.5);
  gtk_container_add(GTK_CONTAINER(v), clock_l);
  gtk_container_add(GTK_CONTAINER(v), date_l);
  gtk_widget_set_margin_bottom(date_l, 24);

  cards = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_widget_set_halign(cards, GTK_ALIGN_CENTER);
  for (guint i = 0; i < people->len; i++) {
    Person *p = people->pdata[i];
    GtkWidget *b = gtk_button_new();
    add_class(b, "card");
    g_object_set_data(G_OBJECT(b), "p", p);
    GtkWidget *cv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(cv), avatar(p, 64));
    GtkWidget *n = label(p->name, "cname", 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(n), 14);
    gtk_container_add(GTK_CONTAINER(cv), n);
    gtk_container_add(GTK_CONTAINER(b), cv);
    g_signal_connect(b, "clicked", G_CALLBACK(on_card), p);
    gtk_container_add(GTK_CONTAINER(cards), b);
  }
  gtk_container_add(GTK_CONTAINER(v), cards);

  panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  add_class(panel, "panel");
  gtk_widget_set_size_request(panel, 380, -1);
  gtk_widget_set_halign(panel, GTK_ALIGN_CENTER);
  who_l = label("", "who", 0);
  gtk_container_add(GTK_CONTAINER(panel), who_l);
  hint_l = label("", "hint", 0);
  gtk_label_set_line_wrap(GTK_LABEL(hint_l), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(hint_l), 40);
  gtk_container_add(GTK_CONTAINER(panel), hint_l);
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  pw_e = gtk_entry_new();
  add_class(pw_e, "pw");
  gtk_entry_set_visibility(GTK_ENTRY(pw_e), FALSE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(pw_e), "Mot de passe");
  gtk_widget_set_hexpand(pw_e, TRUE);
  gtk_widget_set_no_show_all(pw_e, TRUE);
  g_signal_connect(pw_e, "activate", G_CALLBACK(on_go), NULL);
  gtk_container_add(GTK_CONTAINER(row), pw_e);
  go_b = gtk_button_new_with_label("→");
  add_class(go_b, "go");
  g_signal_connect(go_b, "clicked", G_CALLBACK(on_go), NULL);
  gtk_container_add(GTK_CONTAINER(row), go_b);
  gtk_widget_set_halign(row, GTK_ALIGN_FILL);
  gtk_container_add(GTK_CONTAINER(panel), row);
  err_l = label("", "err", 0);
  gtk_widget_set_no_show_all(err_l, TRUE);
  gtk_container_add(GTK_CONTAINER(panel), err_l);
  gtk_widget_show_all(panel);              /* contenu pret ; le cadre, lui, */
  gtk_widget_hide(panel);                  /* n'apparait qu'au choix d'un compte */
  gtk_widget_set_no_show_all(panel, TRUE);
  gtk_container_add(GTK_CONTAINER(v), panel);
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), v);

  GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  add_class(bottom, "bottom");
  gtk_widget_set_halign(bottom, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(bottom, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(bottom, 22);
  const char *pw[][3] = {{"reboot", "Redémarrer", "reboot"}, {"power", "Éteindre", "poweroff"}};
  for (int i = 0; i < 2; i++) {
    GtkWidget *b = gtk_button_new();
    add_class(b, "bbtn");
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(h), icon_new(pw[i][0], R_ON, 18)->img);
    gtk_container_add(GTK_CONTAINER(h), label(pw[i][1], NULL, 0));
    gtk_container_add(GTK_CONTAINER(b), h);
    g_signal_connect(b, "clicked", G_CALLBACK(on_power), (gpointer)pw[i][2]);
    gtk_container_add(GTK_CONTAINER(bottom), b);
  }
  gtk_overlay_add_overlay(GTK_OVERLAY(ov), bottom);
  gtk_container_add(GTK_CONTAINER(win), ov);

  tick(NULL);
  g_timeout_add_seconds(5, tick, NULL);
  gtk_widget_show_all(win);
  /* Une seule personne : directement son mot de passe */
  if (people->len == 1) choose(people->pdata[0]);
  gtk_main();
  return 0;
}

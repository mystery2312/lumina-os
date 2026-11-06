/*
 * lumina-tasks : le Gestionnaire de taches de LuminaOS (Windows + Echap).
 * Les programmes de la session regroupes par appli (Chromium, Linux,
 * applis LuminaOS...), avec memoire et processeur ; « Arreter » termine
 * l'appli choisie. En haut, la memoire et le processeur de tout le PC.
 */
#include "lumina-ui.h"
#include "lumina-sys.h"
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-tasks.csd { border-radius: 0 0 16px 16px; background: @surface; }\n"
  "window.lumina-tasks.csd decoration { border-radius: 16px; box-shadow: 0 6px 22px alpha(black, 0.18), 0 0 0 1px alpha(black, 0.06); }\n"
  ".header { background: @surface; border: none; box-shadow: none; padding: 2px 8px; min-height: 40px; border-radius: 16px 16px 0 0; }\n"
  ".header button.titlebutton { background: transparent; border-radius: 16px; min-width: 32px; min-height: 32px; color: @on_var; }\n"
  ".header button.titlebutton:hover { background: alpha(@on, 0.08); }\n"
  ".apptitle { font-size: 15px; color: @on; padding-left: 8px; }\n"
  ".summary { background: @c_low; border-radius: 20px; padding: 14px 18px; margin: 4px 14px 10px 14px; }\n"
  ".big { font-size: 22px; color: @on; }\n"
  ".muted { color: @on_var; font-size: 12.5px; }\n"
  ".list { background: @c_low; border-radius: 20px; margin: 0 14px; }\n"
  ".list row { padding: 8px 14px; border-radius: 14px; }\n"
  ".list row:selected { background: @sec_c; color: @on_sec_c; }\n"
  ".name { font-size: 14px; color: @on; }\n"
  ".num { font-size: 13px; color: @on_var; font-feature-settings: 'tnum'; }\n"
  ".head { color: @on_var; font-size: 12px; font-weight: 500; padding: 4px 28px; }\n"
  ".fbtn { background: @pri; color: @on_pri; border-radius: 20px; padding: 8px 20px; font-weight: 500; margin: 12px 14px 14px 0; }\n"
  ".fbtn:disabled { background: alpha(@on, 0.12); color: alpha(@on, 0.38); }\n";

/* Une appli = un groupe de processus (meme nom d'appli) */
typedef struct { char *name; GArray *pids; guint64 rss_kb, ticks, prev_ticks; double cpu; } Group;
static GHashTable *groups, *prev_ticks;   /* nom -> Group* ; pid -> ticks precedents */
static GtkWidget *list, *mem_l, *cpu_l, *end_b;
static guint64 prev_total, prev_idle;
static double cpu_total;
static long hz;

static void group_free(gpointer p) { Group *g = p; g_free(g->name); g_array_free(g->pids, TRUE); g_free(g); }

/* Nom affiche d'un processus : appli connue, sinon nom du programme */
static char *app_name(const char *comm, const char *cmdline) {
  static const char *MAP[][2] = {
    {"chromium", "Navigateur Chromium"}, {"chrome_crashpad", "Navigateur Chromium"},
    {"lumina-files", "Fichiers"}, {"lumina-settings", "Réglages"}, {"lumina-text", "Texte"},
    {"lumina-gallery", "Galerie"}, {"lumina-calc", "Calculatrice"}, {"lumina-camera", "Caméra"},
    {"lumina-tasks", "Gestionnaire de tâches"}, {"foot", "Terminal"}, {"podman", "Linux (conteneur)"},
    {"conmon", "Linux (conteneur)"}, {"distrobox", "Linux (conteneur)"}, {"lumina-shell", "Bureau LuminaOS"},
    {"labwc", "Bureau LuminaOS"}, {"mako", "Bureau LuminaOS"}, {"pipewire", "Son"}, {"wireplumber", "Son"},
    {"pipewire-pulse", "Son"}, {"waydroid", "Android"}, {"orca", "Lecteur d'écran"},
  };
  if (g_str_has_prefix(cmdline, "/usr/lib/chromium/")) return g_strdup("Navigateur Chromium");
  /* Services de fond de la session : regroupes */
  static const char *SYS[] = {"systemd", "(sd-pam)", "dbus", "gvfs", "udevadm", "at-spi", "mpris-proxy", "wl-paste",
                              "lumina-session", "lumina-display", "lumina-gestures", "lumina-fetch", "swayidle", "xdg-", "sh", "bash",
                              "find", "cat", "sleep", "wlsunset", "libinput", "awk", "gpg-agent"};
  for (guint i = 0; i < G_N_ELEMENTS(SYS); i++) if (g_str_has_prefix(comm, SYS[i])) return g_strdup("Services système");
  for (guint i = 0; i < G_N_ELEMENTS(MAP); i++) if (g_str_has_prefix(comm, MAP[i][0])) return g_strdup(MAP[i][1]);
  return g_strdup(comm);
}

static void read_cpu_total(guint64 *total, guint64 *idle) {
  char *s = read_file("/proc/stat");
  *total = *idle = 0;
  if (s && g_str_has_prefix(s, "cpu ")) {
    guint64 v[10] = {0};
    sscanf(s + 4, "%lu %lu %lu %lu %lu %lu %lu %lu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    for (int i = 0; i < 8; i++) *total += v[i];
    *idle = v[3] + v[4];
  }
  g_free(s);
}

static void scan(void) {
  if (groups) g_hash_table_destroy(groups);
  groups = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, group_free);
  guint64 total, idle;
  read_cpu_total(&total, &idle);
  guint64 dt = total > prev_total ? total - prev_total : 1;
  cpu_total = prev_total ? 100.0 * (1.0 - (double)(idle - prev_idle) / dt) : 0;
  GHashTable *new_ticks = g_hash_table_new(g_direct_hash, g_direct_equal);
  uid_t me = getuid();
  GDir *d = g_dir_open("/proc", 0, NULL);
  const char *n;
  while (d && (n = g_dir_read_name(d))) {
    if (!g_ascii_isdigit(n[0])) continue;
    int pid = atoi(n);
    char *sp = g_strdup_printf("/proc/%d/status", pid), *st = read_file(sp);
    g_free(sp);
    if (!st) continue;
    char *u = strstr(st, "\nUid:");
    if (!u || (uid_t)atoi(u + 5) != me) { g_free(st); continue; }
    char *rs = strstr(st, "\nVmRSS:");
    guint64 rss = rs ? g_ascii_strtoull(rs + 7, NULL, 10) : 0;
    g_free(st);
    if (!rss) continue;   /* threads noyau, zombies */
    char *cp = g_strdup_printf("/proc/%d/comm", pid), *comm = read_file(cp);
    char *lp = g_strdup_printf("/proc/%d/cmdline", pid), *cmd = NULL;
    g_file_get_contents(lp, &cmd, NULL, NULL);
    char *tp = g_strdup_printf("/proc/%d/stat", pid), *stat = read_file(tp);
    guint64 ticks = 0;
    char *rp = stat ? strrchr(stat, ')') : NULL;
    if (rp) { guint64 ut = 0, stt = 0; sscanf(rp + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &stt); ticks = ut + stt; }
    char *name = app_name(comm ? comm : "?", cmd ? cmd : "");
    Group *g = g_hash_table_lookup(groups, name);
    if (!g) { g = g_new0(Group, 1); g->name = name; g->pids = g_array_new(FALSE, FALSE, sizeof(int)); g_hash_table_insert(groups, g->name, g); }
    else g_free(name);
    g_array_append_val(g->pids, pid);
    g->rss_kb += rss;
    guint64 prev = GPOINTER_TO_SIZE(prev_ticks ? g_hash_table_lookup(prev_ticks, GINT_TO_POINTER(pid)) : NULL);
    if (prev && ticks >= prev) g->cpu += 100.0 * (ticks - prev) / dt * sysconf(_SC_NPROCESSORS_ONLN);
    g_hash_table_insert(new_ticks, GINT_TO_POINTER(pid), GSIZE_TO_POINTER(ticks ? ticks : 1));
    g_free(cp); g_free(comm); g_free(lp); g_free(cmd); g_free(tp); g_free(stat);
  }
  if (d) g_dir_close(d);
  if (prev_ticks) g_hash_table_destroy(prev_ticks);
  prev_ticks = new_ticks;
  prev_total = total; prev_idle = idle;
}

static gint by_mem(gconstpointer a, gconstpointer b) {
  const Group *x = *(Group *const *)a, *y = *(Group *const *)b;
  return x->rss_kb < y->rss_kb ? 1 : x->rss_kb > y->rss_kb ? -1 : 0;
}

static char *selected_name(void) {
  GtkListBoxRow *r = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
  return r ? g_strdup(g_object_get_data(G_OBJECT(r), "name")) : NULL;
}

static void fill(void) {
  char *keep = selected_name();
  GList *k = gtk_container_get_children(GTK_CONTAINER(list));
  for (GList *l = k; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(k);
  GPtrArray *a = g_ptr_array_new();
  GHashTableIter it; gpointer key, val;
  g_hash_table_iter_init(&it, groups);
  while (g_hash_table_iter_next(&it, &key, &val)) g_ptr_array_add(a, val);
  g_ptr_array_sort(a, by_mem);
  for (guint i = 0; i < a->len; i++) {
    Group *g = a->pdata[i];
    GtkWidget *r = gtk_list_box_row_new();
    g_object_set_data_full(G_OBJECT(r), "name", g_strdup(g->name), g_free);
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *nl = label(g->name, "name", 0);
    gtk_label_set_ellipsize(GTK_LABEL(nl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(nl, TRUE);
    gtk_container_add(GTK_CONTAINER(h), nl);
    char *mem = g_format_size(g->rss_kb * 1024), *cpu = g_strdup_printf("%.0f %%", g->cpu);
    GtkWidget *ml = label(mem, "num", 1), *cl = label(cpu, "num", 1);
    gtk_label_set_width_chars(GTK_LABEL(ml), 10); gtk_label_set_width_chars(GTK_LABEL(cl), 7);
    gtk_container_add(GTK_CONTAINER(h), ml);
    gtk_container_add(GTK_CONTAINER(h), cl);
    gtk_container_add(GTK_CONTAINER(r), h);
    gtk_container_add(GTK_CONTAINER(list), r);
    if (keep && !strcmp(keep, g->name)) gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(r));
    g_free(mem); g_free(cpu);
  }
  g_ptr_array_free(a, TRUE);
  g_free(keep);
  gtk_widget_show_all(list);

  char *mi = read_file("/proc/meminfo");
  guint64 tot = 0, avail = 0;
  if (mi) {
    char *t = strstr(mi, "MemTotal:"), *v = strstr(mi, "MemAvailable:");
    if (t) tot = g_ascii_strtoull(t + 9, NULL, 10);
    if (v) avail = g_ascii_strtoull(v + 13, NULL, 10);
  }
  g_free(mi);
  char *u = g_format_size((tot - avail) * 1024), *T = g_format_size(tot * 1024);
  char *m = g_strdup_printf("%s sur %s", u, T), *c = g_strdup_printf("%.0f %%", cpu_total);
  gtk_label_set_text(GTK_LABEL(mem_l), m);
  gtk_label_set_text(GTK_LABEL(cpu_l), c);
  g_free(u); g_free(T); g_free(m); g_free(c);
}

static gboolean tick(gpointer d) { scan(); fill(); return G_SOURCE_CONTINUE; }
static gboolean tick_once(gpointer d) { scan(); fill(); return G_SOURCE_REMOVE; }

static void on_sel(GtkListBox *lb, GtkListBoxRow *r, gpointer d) {
  const char *n = r ? g_object_get_data(G_OBJECT(r), "name") : NULL;
  /* Le bureau lui-meme ne s'arrete pas ici */
  gtk_widget_set_sensitive(end_b, n && strcmp(n, "Bureau LuminaOS") && strcmp(n, "Gestionnaire de tâches"));
}

static void on_end(GtkButton *b, gpointer d) {
  char *n = selected_name();
  Group *g = n ? g_hash_table_lookup(groups, n) : NULL;
  if (g) {
    for (guint i = 0; i < g->pids->len; i++) kill(g_array_index(g->pids, int, i), SIGTERM);
    g_timeout_add(400, tick_once, NULL);
  }
  g_free(n);
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-tasks");
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, NULL);
  hz = sysconf(_SC_CLK_TCK);
  GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-tasks");
  gtk_window_set_title(GTK_WINDOW(win), "Gestionnaire de tâches");
  gtk_window_set_default_size(GTK_WINDOW(win), 560, 600);
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  GtkWidget *hb = gtk_header_bar_new();
  add_class(hb, "header");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":minimize,maximize,close");
  gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), label("Gestionnaire de tâches", "apptitle", 0));
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);

  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *sum = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
  add_class(sum, "summary");
  GtkWidget *a = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2), *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_container_add(GTK_CONTAINER(a), label("Mémoire", "muted", 0));
  mem_l = label("", "big", 0);
  gtk_container_add(GTK_CONTAINER(a), mem_l);
  gtk_container_add(GTK_CONTAINER(b), label("Processeur", "muted", 0));
  cpu_l = label("", "big", 0);
  gtk_container_add(GTK_CONTAINER(b), cpu_l);
  gtk_container_add(GTK_CONTAINER(sum), a);
  gtk_container_add(GTK_CONTAINER(sum), b);
  gtk_container_add(GTK_CONTAINER(v), sum);
  GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  add_class(head, "head");
  GtkWidget *hn = label("Appli", NULL, 0); gtk_widget_set_hexpand(hn, TRUE);
  GtkWidget *hm = label("Mémoire", NULL, 1), *hc = label("Processeur", NULL, 1);
  gtk_label_set_width_chars(GTK_LABEL(hm), 10); gtk_label_set_width_chars(GTK_LABEL(hc), 9);
  gtk_container_add(GTK_CONTAINER(head), hn); gtk_container_add(GTK_CONTAINER(head), hm); gtk_container_add(GTK_CONTAINER(head), hc);
  gtk_container_add(GTK_CONTAINER(v), head);
  GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_vexpand(sc, TRUE);
  add_class(sc, "list");
  list = gtk_list_box_new();
  g_signal_connect(list, "row-selected", G_CALLBACK(on_sel), NULL);
  gtk_container_add(GTK_CONTAINER(sc), list);
  gtk_container_add(GTK_CONTAINER(v), sc);
  end_b = gtk_button_new_with_label("Arrêter l'appli");
  add_class(end_b, "fbtn");
  gtk_widget_set_halign(end_b, GTK_ALIGN_END);
  gtk_widget_set_sensitive(end_b, FALSE);
  g_signal_connect(end_b, "clicked", G_CALLBACK(on_end), NULL);
  gtk_container_add(GTK_CONTAINER(v), end_b);
  gtk_container_add(GTK_CONTAINER(win), v);
  gtk_widget_show_all(win);
  /* Premiere mesure tout de suite (processeur : a la suivante), puis
     toutes les 2 s. */
  scan();
  fill();
  g_timeout_add_seconds(2, tick, NULL);
  gtk_main();
  return 0;
}

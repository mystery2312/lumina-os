/*
 * lumina-sys : utilitaires systeme partages par les applis LuminaOS.
 * Le Wi-Fi passe par iwctl (iwd), deja present pour le reseau : aucun
 * demon supplementaire, rien en memoire quand on ne s'en sert pas.
 */
#include "lumina-sys.h"
#include <string.h>

char *read_file(const char *path) {
  char *c = NULL;
  if (!g_file_get_contents(path, &c, NULL, NULL)) return NULL;
  return g_strstrip(c);
}

/* Les sorties d'iwctl sont colorees : on retire les sequences ANSI. */
static char *strip_ansi(char *s) {
  char *r = s, *w = s;
  while (*r) {
    if (*r == '\x1b') { r++; if (*r == '[') { r++; while (*r && !g_ascii_isalpha(*r)) r++; if (*r) r++; } continue; }
    *w++ = *r++;
  }
  *w = 0;
  return s;
}

char *run_argv(const char *const *argv, int *status) {
  char *out = NULL; int st = -1;
  if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                    NULL, NULL, &out, NULL, &st, NULL)) { g_free(out); out = NULL; }
  if (status) *status = st;
  return out ? strip_ansi(out) : NULL;
}

void spawn_async(const char *const *argv) {
  g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Wi-Fi                                                               */
/* ------------------------------------------------------------------ */
void net_free(gpointer p) { Net *n = p; g_free(n->ssid); g_free(n->security); g_free(n); }

char *net_wifi_device(void) {
  GDir *d = g_dir_open("/sys/class/net", 0, NULL);
  char *r = NULL;
  for (const char *n; d && (n = g_dir_read_name(d)) && !r;) {
    char *p = g_strdup_printf("/sys/class/net/%s/wireless", n);
    if (g_file_test(p, G_FILE_TEST_IS_DIR)) r = g_strdup(n);
    g_free(p);
  }
  if (d) g_dir_close(d);
  return r;
}

static gboolean iface_up(const char *n) {
  char *p = g_strdup_printf("/sys/class/net/%s/operstate", n), *s = read_file(p);
  gboolean up = g_strcmp0(s, "up") == 0;
  g_free(p); g_free(s);
  return up;
}

gboolean net_wifi_up(const char *dev) { return dev && iface_up(dev); }

gboolean net_wired_up(const char *wifi_dev) {
  GDir *d = g_dir_open("/sys/class/net", 0, NULL);
  gboolean up = FALSE;
  for (const char *n; d && (n = g_dir_read_name(d)) && !up;) {
    if (!g_strcmp0(n, "lo") || !g_strcmp0(n, wifi_dev)) continue;
    up = iface_up(n);
  }
  if (d) g_dir_close(d);
  return up;
}

gboolean net_wifi_powered(const char *dev) {
  if (!dev) return FALSE;
  const char *argv[] = {"iwctl", "device", dev, "show", NULL};
  char *out = run_argv(argv, NULL);
  gboolean on = FALSE;
  char **lines = out ? g_strsplit(out, "\n", -1) : NULL;
  for (char **p = lines; p && *p; p++)
    if (strstr(*p, "Powered")) on = strstr(*p, " on") != NULL;
  g_strfreev(lines); g_free(out);
  return on;
}

char *net_wifi_ssid(const char *dev) {
  if (!dev) return NULL;
  const char *argv[] = {"iwctl", "station", dev, "show", NULL};
  char *out = run_argv(argv, NULL), *ssid = NULL;
  if (out) {
    char **lines = g_strsplit(out, "\n", -1);
    for (char **p = lines; *p; p++) {
      char *k = strstr(*p, "Connected network");
      if (k) { ssid = g_strdup(g_strstrip(k + strlen("Connected network"))); break; }
    }
    g_strfreev(lines); g_free(out);
  }
  return ssid;
}

/* "iwctl station X get-networks" : colonnes nom / securite / signal.
   Le nom peut contenir des espaces : on lit les deux derniers champs a
   droite, le reste est le nom. */
GPtrArray *net_wifi_list(const char *dev) {
  GPtrArray *r = g_ptr_array_new_with_free_func(net_free);
  if (!dev) return r;
  const char *argv[] = {"iwctl", "station", dev, "get-networks", NULL};
  char *out = run_argv(argv, NULL);
  if (!out) return r;
  char **lines = g_strsplit(out, "\n", -1);
  for (char **p = lines; *p; p++) {
    char *l = g_strstrip(*p);
    if (!*l || strstr(l, "Available networks") || strstr(l, "Network name") || g_str_has_prefix(l, "---") ||
        strstr(l, "No networks")) continue;
    gboolean cur = FALSE;
    if (*l == '>') { cur = TRUE; l = g_strchug(l + 1); }
    char *sig = strrchr(l, ' ');
    if (!sig) continue;
    *sig++ = 0; g_strchomp(l);
    char *sec = strrchr(l, ' ');
    if (!sec) continue;
    *sec++ = 0;
    Net *n = g_new0(Net, 1);
    n->ssid = g_strdup(g_strchomp(l));
    n->security = g_strdup(sec);
    for (char *c = sig; *c; c++) if (*c == '*') n->signal++;
    n->current = cur;
    g_ptr_array_add(r, n);
  }
  g_strfreev(lines); g_free(out);
  return r;
}

void net_wifi_scan(const char *dev) {
  if (!dev) return;
  const char *argv[] = {"iwctl", "station", dev, "scan", NULL};
  spawn_async(argv);
}

void net_wifi_power(const char *dev, gboolean on) {
  if (!dev) return;
  const char *argv[] = {"iwctl", "device", dev, "set-property", "Powered", on ? "on" : "off", NULL};
  g_free(run_argv(argv, NULL));
}

void net_wifi_connect(const char *dev, const char *ssid, const char *pass,
                      GAsyncReadyCallback cb, gpointer data) {
  GSubprocess *sp = pass
    ? g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                       "iwctl", "--passphrase", pass, "station", dev, "connect", ssid, NULL)
    : g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
                       "iwctl", "--dont-ask", "station", dev, "connect", ssid, NULL);
  if (sp) g_subprocess_wait_async(sp, NULL, cb, data);
}

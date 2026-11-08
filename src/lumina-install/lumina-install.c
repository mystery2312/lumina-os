/*
 * lumina-install : l'installateur de LuminaOS.
 *
 * Propose, au demarrage sur une cle USB, d'essayer LuminaOS ou de
 * l'installer sur un disque interne. L'ecriture du disque est faite par
 * /usr/local/sbin/lumina-install-disk (en administrateur, via pkexec),
 * dont on affiche la progression.
 *
 * Ecrans : accueil -> choix du disque -> confirmation -> progression -> fin.
 * Usage : lumina-install [--welcome]   (--welcome : ouvert au demarrage)
 */
#include "lumina-ui.h"
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

static const char *CSS =
  "* { font-family: 'Roboto', 'DejaVu Sans', sans-serif; }\n"
  "button { background-image: none; border: none; box-shadow: none; text-shadow: none; outline: none; }\n"
  "window.lumina-install.csd, window.lumina-install.csd decoration { border-radius: 28px; }\n"
  "window.lumina-install { background: @surface; }\n"
  "window.lumina-install .titlebar { background: transparent; min-height: 0; padding: 0; margin: 0;\n"
  "                                  border: none; box-shadow: none; }\n"
  ".page { padding: 36px 40px 28px 40px; }\n"
  ".hero { border-radius: 28px; background: @pri_c; color: @on_pri_c; min-width: 72px; min-height: 72px; }\n"
  ".hero.danger { background: #ffdad6; color: #410002; }\n"
  ".title { font-size: 28px; color: @on; }\n"
  ".text { font-size: 14px; color: @on_var; }\n"
  ".disk { background: @c_low; border-radius: 16px; padding: 14px 16px; color: @on; }\n"
  ".disk:hover { background: @c; }\n"
  ".disk:checked, .disk.sel { background: @sec_c; color: @on_sec_c; }\n"
  ".disk:disabled { opacity: 0.45; }\n"
  ".disk .name { font-size: 15px; font-weight: 500; }\n"
  ".disk .meta { font-size: 12.5px; color: @on_var; }\n"
  ".warn { background: #ffdad6; color: #410002; border-radius: 16px; padding: 14px 16px; }\n"
  ".tbtn { background: transparent; color: @pri; border-radius: 20px; padding: 10px 18px; font-weight: 500; }\n"
  ".tbtn:hover { background: alpha(@pri, 0.08); }\n"
  ".tbtn.danger { color: @err; }\n"
  ".fbtn { background: @pri; color: @on_pri; border-radius: 20px; padding: 10px 22px; font-weight: 500; }\n"
  ".fbtn:disabled { background: alpha(@on, 0.12); color: alpha(@on, 0.38); }\n"
  ".fbtn.danger { background: #ba1a1a; color: #ffffff; }\n"
  ".fbtn.danger:disabled { background: alpha(@on, 0.12); color: alpha(@on, 0.38); }\n"
  "checkbutton { color: @on; }\n"
  "progressbar trough { background: @c_top; min-height: 8px; border-radius: 4px; border: none; }\n"
  "progressbar progress { background: @pri; min-height: 8px; border-radius: 4px; border: none; }\n"
  ".step { font-size: 13.5px; color: @on_var; }\n";

typedef struct { char *path, *model, *contents; guint64 size; gboolean usable; char *why; } Disk;

static GtkWidget *win, *stack, *disk_box, *next_btn, *confirm_l, *confirm_chk, *erase_btn;
static GtkWidget *prog_bar, *prog_step, *end_title, *end_text, *end_hero_box, *end_btn;
static Icon *end_ic;
static GPtrArray *disks;
static Disk *chosen;
static gboolean done_ok, touched;
static char *last_error;

/* ------------------------------------------------------------------ */
/* Disques : lsblk -P (paires CLE="valeur"), sans dependance JSON        */
/* ------------------------------------------------------------------ */
static char *kv(const char *line, const char *key) {
  char *pat = g_strdup_printf("%s=\"", key);
  const char *p = strstr(line, pat);
  /* "PATH=" ne doit pas etre trouve dans "PKNAME=" : debut de ligne ou espace */
  while (p && p != line && p[-1] != ' ') p = strstr(p + 1, pat);
  char *r = NULL;
  if (p) {
    p += strlen(pat);
    const char *e = strchr(p, '"');
    r = e ? g_strndup(p, e - p) : NULL;
  }
  g_free(pat);
  return r ? r : g_strdup("");
}

static char *run(const char *cmd) {
  char *out = NULL;
  g_spawn_command_line_sync(cmd, &out, NULL, NULL, NULL);
  return out ? out : g_strdup("");
}

static void disk_free(gpointer p) {
  Disk *d = p;
  g_free(d->path); g_free(d->model); g_free(d->contents); g_free(d->why); g_free(d);
}

/* Contenu lisible d'un disque : "Windows (NTFS, 237 Go), DONNEES (ext4, 100 Go)" */
static char *describe(const char *path) {
  char *cmd = g_strdup_printf("lsblk -P -b -o FSTYPE,LABEL,SIZE,TYPE %s", path);
  char *out = run(cmd);
  GString *s = g_string_new(NULL);
  char **lines = g_strsplit(out, "\n", -1);
  for (char **l = lines; *l; l++) {
    char *type = kv(*l, "TYPE"), *fs = kv(*l, "FSTYPE"), *lab = kv(*l, "LABEL"), *sz = kv(*l, "SIZE");
    if (!strcmp(type, "part") && *fs) {
      char *size = g_format_size(g_ascii_strtoull(sz, NULL, 10));
      const char *name = *lab ? lab : !strcmp(fs, "ntfs") ? "Windows" : !strcmp(fs, "vfat") ? "Démarrage" : fs;
      g_string_append_printf(s, "%s%s (%s, %s)", s->len ? ", " : "", name, fs, size);
      g_free(size);
    }
    g_free(type); g_free(fs); g_free(lab); g_free(sz);
  }
  g_strfreev(lines); g_free(out); g_free(cmd);
  if (!s->len) g_string_assign(s, "Aucune partition reconnue");
  return g_string_free(s, FALSE);
}

static void scan_disks(void) {
  if (disks) g_ptr_array_free(disks, TRUE);
  disks = g_ptr_array_new_with_free_func(disk_free);
  char *live = run("lumina-is-live --disk");
  g_strstrip(live);
  char *out = run("lsblk -P -b -d -o PATH,SIZE,MODEL,TYPE,RO,TRAN");
  char **lines = g_strsplit(out, "\n", -1);
  for (char **l = lines; *l; l++) {
    if (!**l) continue;
    char *path = kv(*l, "PATH"), *type = kv(*l, "TYPE"), *ro = kv(*l, "RO");
    if (!strcmp(type, "disk") && !g_str_has_prefix(path, "/dev/zram") && strcmp(path, live)) {
      Disk *d = g_new0(Disk, 1);
      d->path = g_strdup(path);
      char *sz = kv(*l, "SIZE");
      d->size = g_ascii_strtoull(sz, NULL, 10);
      g_free(sz);
      char *model = kv(*l, "MODEL");
      d->model = *model ? g_strstrip(model) : (g_free(model), g_strdup("Disque"));
      d->contents = describe(path);
      d->usable = !strcmp(ro, "0") && d->size >= 12ULL * 1024 * 1024 * 1024;
      if (strcmp(ro, "0")) d->why = g_strdup("En lecture seule");
      else if (!d->usable) d->why = g_strdup("Trop petit : il faut au moins 12 Go");
      g_ptr_array_add(disks, d);
    }
    g_free(path); g_free(type); g_free(ro);
  }
  g_strfreev(lines); g_free(out); g_free(live);
}

/* ------------------------------------------------------------------ */
/* Pages                                                               */
/* ------------------------------------------------------------------ */
static GtkWidget *hero(const char *glyph, gboolean danger, Icon **ic_out) {
  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(b, "hero");
  if (danger) add_class(b, "danger");
  gtk_widget_set_halign(b, GTK_ALIGN_START);
  gtk_widget_set_valign(b, GTK_ALIGN_START);
  gtk_widget_set_size_request(b, 72, 72);
  Icon *ic = icon_new(glyph, danger ? R_ON : R_ON_PRI_C, 36);
  gtk_widget_set_valign(ic->img, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(ic->img, TRUE);
  gtk_widget_set_vexpand(b, FALSE);
  gtk_container_add(GTK_CONTAINER(b), ic->img);
  if (ic_out) *ic_out = ic;
  return b;
}

/* Logo LuminaOS (Aube), en version claire ou sombre selon le theme. */
static GtkWidget *logo(int px) {
  const char *f = dark ? "/usr/share/icons/hicolor/scalable/apps/luminaos-dark.svg"
                       : "/usr/share/icons/hicolor/scalable/apps/luminaos.svg";
  GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(f, px, px, NULL);
  GtkWidget *img = pb ? gtk_image_new_from_pixbuf(pb) : gtk_image_new();
  if (pb) g_object_unref(pb);
  gtk_widget_set_halign(img, GTK_ALIGN_START);
  return img;
}

static GtkWidget *page(void) {
  GtkWidget *p = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  add_class(p, "page");
  return p;
}

static GtkWidget *text(const char *t, const char *cls) {
  GtkWidget *l = label(t, cls, 0);
  gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(l), 56);
  return l;
}

static GtkWidget *buttons(void) {
  GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(bb, GTK_ALIGN_END);
  gtk_widget_set_valign(bb, GTK_ALIGN_END);
  gtk_widget_set_vexpand(bb, TRUE);
  return bb;
}

static GtkWidget *btn(const char *t, const char *cls, GCallback cb) {
  GtkWidget *b = gtk_button_new_with_label(t);
  add_class(b, cls);
  g_signal_connect(b, "clicked", cb, NULL);
  return b;
}

static void go(const char *name) { gtk_stack_set_visible_child_name(GTK_STACK(stack), name); }

static void on_try(void) { gtk_widget_destroy(win); }
static void on_back_welcome(void) { go("welcome"); }
static void on_back_disks(void) { go("disks"); }

static void on_disk_toggled(GtkToggleButton *b, gpointer d) {
  if (!gtk_toggle_button_get_active(b)) return;
  chosen = d;
  gtk_widget_set_sensitive(next_btn, TRUE);
}

static void fill_disks(void) {
  GList *kids = gtk_container_get_children(GTK_CONTAINER(disk_box));
  for (GList *l = kids; l; l = l->next) gtk_widget_destroy(l->data);
  g_list_free(kids);
  chosen = NULL;
  gtk_widget_set_sensitive(next_btn, FALSE);
  scan_disks();
  GtkWidget *group = NULL;
  for (guint i = 0; i < disks->len; i++) {
    Disk *d = disks->pdata[i];
    GtkWidget *rb = group ? gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(group)) : gtk_radio_button_new(NULL);
    if (!group) group = rb;
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(rb), FALSE);   /* carte cliquable, pas de pastille */
    add_class(rb, "disk");
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_add(GTK_CONTAINER(h), icon_new("disk", R_ON_VAR, 24)->img);
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    char *size = g_format_size(d->size);
    char *name = g_strdup_printf("%s · %s", d->model, size);
    gtk_container_add(GTK_CONTAINER(v), label(name, "name", 0));
    char *meta = g_strdup_printf("%s — %s", d->path, d->why ? d->why : d->contents);
    GtkWidget *ml = label(meta, "meta", 0);
    gtk_label_set_ellipsize(GTK_LABEL(ml), PANGO_ELLIPSIZE_END);
    gtk_container_add(GTK_CONTAINER(v), ml);
    gtk_container_add(GTK_CONTAINER(h), v);
    gtk_container_add(GTK_CONTAINER(rb), h);
    gtk_widget_set_sensitive(rb, d->usable);
    g_signal_connect(rb, "toggled", G_CALLBACK(on_disk_toggled), d);
    gtk_container_add(GTK_CONTAINER(disk_box), rb);
    g_free(size); g_free(name); g_free(meta);
  }
  /* Le premier bouton radio est actif par defaut : on ne preselectionne
     rien pour forcer un choix conscient du disque a effacer. */
  if (group) {
    GtkWidget *none = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(group));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(none), TRUE);
    g_object_ref_sink(none);
  }
  if (!disks->len)
    gtk_container_add(GTK_CONTAINER(disk_box), text("Aucun disque interne trouvé sur ce PC.", "text"));
  gtk_widget_show_all(disk_box);
}

static void on_install(void) { fill_disks(); go("disks"); }

static void on_confirm_toggled(GtkToggleButton *b, gpointer d) {
  gtk_widget_set_sensitive(erase_btn, gtk_toggle_button_get_active(b));
}

/* A cote de Windows ? L'outil regarde le disque sans le modifier. */
static gboolean install_beside, beside_bitlocker;
static GtkWidget *how_box, *beside_l, *beside_warn, *beside_chk, *beside_btn, *win_hint;
static guint64 beside_bytes;

static void on_beside_chk(GtkToggleButton *b, gpointer d) {
  gtk_widget_set_sensitive(beside_btn, !beside_bitlocker || gtk_toggle_button_get_active(b));
}

static void show_erase_confirm(void);
static void on_how_beside(void) {
  char *free_s = g_format_size(beside_bytes);
  char *t = g_strdup_printf("LuminaOS utilisera les %s d'espace libre de « %s ». Windows et ses fichiers ne sont pas modifiés. "
                            "Au démarrage du PC, LuminaOS s'ouvre et Windows reste disponible.", free_s, chosen->model);
  gtk_label_set_text(GTK_LABEL(beside_l), t);
  g_free(t); g_free(free_s);
  gtk_widget_set_visible(beside_warn, beside_bitlocker);
  gtk_widget_set_visible(beside_chk, beside_bitlocker);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(beside_chk), FALSE);
  gtk_widget_set_sensitive(beside_btn, !beside_bitlocker);
  install_beside = TRUE;
  go("beside");
}
static void on_how_erase(void) { show_erase_confirm(); }

static void on_next(void) {
  if (!chosen) return;
  install_beside = FALSE;
  char *out = NULL;
  const char *argv[] = {"pkexec", "/usr/local/sbin/lumina-install-disk", "--check-beside", chosen->path, NULL};
  g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, NULL, NULL);
  gboolean ok = out && g_str_has_prefix(out, "OK ");
  gboolean found_windows = out && g_str_has_prefix(out, "NO ") && (strstr(out, "Espace libre") || strstr(out, "EFI de Windows"));
  if (ok) {
    beside_bytes = g_ascii_strtoull(out + 3, NULL, 10);
    beside_bitlocker = strstr(out, "BITLOCKER") != NULL;
    g_free(out);
    gtk_widget_hide(win_hint);
    go("how");
    return;
  }
  /* Windows present mais pas de place : on l'explique avant d'effacer */
  gtk_widget_set_visible(win_hint, found_windows);
  g_free(out);
  show_erase_confirm();
}

static void show_erase_confirm(void) {
  char *size = g_format_size(chosen->size);
  char *t = g_strdup_printf("Tout le contenu de « %s » (%s, %s) sera effacé définitivement :\n%s",
                            chosen->model, chosen->path, size, chosen->contents);
  gtk_label_set_text(GTK_LABEL(confirm_l), t);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(confirm_chk), FALSE);
  gtk_widget_set_sensitive(erase_btn, FALSE);
  g_free(t); g_free(size);
  go("confirm");
}

/* ------------------------------------------------------------------ */
/* Installation                                                        */
/* ------------------------------------------------------------------ */
static void show_end(gboolean ok) {
  gtk_window_set_deletable(GTK_WINDOW(win), TRUE);
  if (ok) {
    gtk_label_set_text(GTK_LABEL(end_title), "LuminaOS est installé");
    gtk_label_set_text(GTK_LABEL(end_text), "Retirez la clé USB, puis redémarrez : le PC démarrera sur LuminaOS. "
                                            "La clé reste utilisable pour essayer ou installer LuminaOS ailleurs.");
    gtk_button_set_label(GTK_BUTTON(end_btn), "Redémarrer");
    icon_set(end_ic, "power", R_ON_PRI_C);
    set_class(end_hero_box, "danger", FALSE);
  } else {
    gtk_label_set_text(GTK_LABEL(end_title), "L'installation a échoué");
    char *t = g_strdup_printf("%s\n\n%s", last_error ? last_error : "Erreur inconnue.",
                              touched ? "Le disque est dans un état incomplet. Vous pouvez relancer l'installation."
                                      : "Le disque n'a pas été modifié.");
    gtk_label_set_text(GTK_LABEL(end_text), t);
    g_free(t);
    gtk_button_set_label(GTK_BUTTON(end_btn), "Fermer");
    icon_set(end_ic, "close", R_ON);
    set_class(end_hero_box, "danger", TRUE);
  }
  go("end");
}

static void handle_line(const char *l) {
  if (g_str_has_prefix(l, "PROGRESS ")) {
    char *end = NULL;
    long p = strtol(l + 9, &end, 10);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(prog_bar), CLAMP(p, 0, 100) / 100.0);
    if (end && *end == ' ') gtk_label_set_text(GTK_LABEL(prog_step), end + 1);
  } else if (g_str_has_prefix(l, "ERROR ")) {
    g_free(last_error); last_error = g_strdup(l + 6);
  } else if (!strcmp(l, "WRITING")) {
    touched = TRUE;
  } else if (!strcmp(l, "DONE")) {
    done_ok = TRUE;
  }
}

#define LOG_PATH "/run/lumina-install.log"
#define PID_PATH "/run/lumina-install.pid"
static goffset log_pos;
static guint poll_id;
static GPid child = 0;
static gboolean child_exited;
static int child_code;

/* L'installation tourne-t-elle ? (fichier pid du programme d'installation) */
static gboolean install_running(void) {
  char *t = NULL;
  gboolean alive = FALSE;
  if (g_file_get_contents(PID_PATH, &t, NULL, NULL)) {
    int pid = atoi(t);
    /* Le programme d'installation tourne en root : kill() repond EPERM a
       l'utilisateur, ce qui prouve justement que le processus existe. */
    alive = pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
  }
  g_free(t);
  return alive;
}

/* Lit les lignes ajoutees au journal depuis le dernier passage. */
static void read_log(void) {
  char *all = NULL;
  gsize len = 0;
  if (!g_file_get_contents(LOG_PATH, &all, &len, NULL)) return;
  if ((goffset)len < log_pos) log_pos = 0;          /* journal recree */
  char *p = all + log_pos, *nl;
  while ((nl = memchr(p, '\n', all + len - p))) {
    *nl = 0;
    handle_line(p);
    p = nl + 1;
  }
  log_pos = p - all;
  g_free(all);
}

static gboolean poll_log(gpointer d) {
  read_log();
  gboolean finished = done_ok || last_error || (child ? child_exited : !install_running());
  if (!finished) return G_SOURCE_CONTINUE;
  read_log();
  if (!done_ok && !last_error)
    last_error = g_strdup(child && (child_code == 126 || child_code == 127)
                          ? "Autorisation refusée par le système."
                          : "Le programme d'installation s'est arrêté.");
  poll_id = 0;
  show_end(done_ok);
  return G_SOURCE_REMOVE;
}

static void on_child_exit(GPid pid, gint status, gpointer d) {
  g_spawn_close_pid(pid);
  /* pkexec : 126 = autorisation refusee, 127 = non autorise / introuvable */
  child_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  child_exited = TRUE;
}

static void start_polling(void) {
  log_pos = 0;
  if (!poll_id) poll_id = g_timeout_add(500, poll_log, NULL);
}

static void on_erase(void) {
  done_ok = touched = FALSE;
  g_clear_pointer(&last_error, g_free);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(prog_bar), 0);
  gtk_label_set_text(GTK_LABEL(prog_step), "Démarrage");
  gtk_window_set_deletable(GTK_WINDOW(win), FALSE);   /* on ne ferme pas en pleine ecriture */
  go("progress");

  const char *argv_all[] = {"pkexec", "/usr/local/sbin/lumina-install-disk", chosen->path, NULL};
  const char *argv_beside[] = {"pkexec", "/usr/local/sbin/lumina-install-disk", "--beside", chosen->path, NULL};
  const char *const *argv = install_beside ? argv_beside : argv_all;
  GError *err = NULL;
  child_exited = FALSE;
  if (!g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                     G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &child, &err)) {
    last_error = g_strdup(err->message);
    g_clear_error(&err);
    show_end(FALSE);
    return;
  }
  g_child_watch_add(child, on_child_exit, NULL);
  start_polling();
}

static void on_end(void) {
  if (done_ok) {
    const char *argv[] = {"systemctl", "reboot", NULL};
    g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
  }
  gtk_widget_destroy(win);
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */
static void build(void) {
  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  add_class(win, "lumina-install");
  gtk_window_set_title(GTK_WINDOW(win), "Installer LuminaOS");
  gtk_window_set_default_size(GTK_WINDOW(win), 640, 520);
  gtk_window_set_titlebar(GTK_WINDOW(win), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

  /* Accueil */
  GtkWidget *p = page();
  gtk_container_add(GTK_CONTAINER(p), logo(88));
  gtk_container_add(GTK_CONTAINER(p), text("Bienvenue dans LuminaOS", "title"));
  gtk_container_add(GTK_CONTAINER(p), text("LuminaOS fonctionne en ce moment depuis la clé USB : rien n'a été modifié "
                                           "sur ce PC. Vous pouvez l'essayer tel quel, ou l'installer pour de bon "
                                           "sur le disque de ce PC.", "text"));
  gtk_container_add(GTK_CONTAINER(p), text("L'installation est plus rapide qu'une clé et libère le port USB. "
                                           "L'appli « Installer LuminaOS » reste disponible dans le tiroir "
                                           "tant que vous êtes sur la clé.", "text"));
  GtkWidget *bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn("Essayer LuminaOS", "tbtn", G_CALLBACK(on_try)));
  gtk_container_add(GTK_CONTAINER(bb), btn("Installer sur ce PC", "fbtn", G_CALLBACK(on_install)));
  gtk_container_add(GTK_CONTAINER(p), bb);
  gtk_stack_add_named(GTK_STACK(stack), p, "welcome");

  /* Choix du disque */
  p = page();
  gtk_container_add(GTK_CONTAINER(p), text("Choisissez le disque", "title"));
  gtk_container_add(GTK_CONTAINER(p), text("Si Windows est sur ce disque, vous pourrez le garder. La clé de démarrage "
                                           "n'apparaît pas dans la liste.", "text"));
  GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sc), TRUE);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sc), 240);
  disk_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_add(GTK_CONTAINER(sc), disk_box);
  gtk_container_add(GTK_CONTAINER(p), sc);
  bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn("Retour", "tbtn", G_CALLBACK(on_back_welcome)));
  next_btn = btn("Suivant", "fbtn", G_CALLBACK(on_next));
  gtk_container_add(GTK_CONTAINER(bb), next_btn);
  gtk_container_add(GTK_CONTAINER(p), bb);
  gtk_stack_add_named(GTK_STACK(stack), p, "disks");

  /* Windows est la : a cote, ou tout effacer */
  p = page();
  gtk_container_add(GTK_CONTAINER(p), hero("disk", FALSE, NULL));
  gtk_container_add(GTK_CONTAINER(p), text("Windows est installé sur ce disque", "title"));
  gtk_container_add(GTK_CONTAINER(p), text("Comment installer LuminaOS ?", "text"));
  how_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget *hb1 = btn("À côté de Windows (conseillé)", "fbtn", G_CALLBACK(on_how_beside));
  gtk_container_add(GTK_CONTAINER(how_box), hb1);
  GtkWidget *hb2 = btn("Effacer Windows et tout le disque", "tbtn", G_CALLBACK(on_how_erase));
  add_class(hb2, "danger");
  gtk_container_add(GTK_CONTAINER(how_box), hb2);
  gtk_container_add(GTK_CONTAINER(p), how_box);
  GtkWidget *hbb = buttons();
  gtk_container_add(GTK_CONTAINER(hbb), btn("Retour", "tbtn", G_CALLBACK(on_back_disks)));
  gtk_container_add(GTK_CONTAINER(p), hbb);
  gtk_stack_add_named(GTK_STACK(stack), p, "how");

  /* Confirmation : a cote de Windows */
  p = page();
  gtk_container_add(GTK_CONTAINER(p), hero("disk", FALSE, NULL));
  gtk_container_add(GTK_CONTAINER(p), text("Installer à côté de Windows ?", "title"));
  beside_l = text("", "text");
  gtk_container_add(GTK_CONTAINER(p), beside_l);
  beside_warn = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(beside_warn, "warn");
  gtk_container_add(GTK_CONTAINER(beside_warn),
                    text("Ce Windows est chiffré par BitLocker. Notez d'abord sa clé de récupération (aka.ms/myrecoverykey, depuis un autre appareil) : "
                         "sans elle, Windows pourrait rester inaccessible si le PC la demande. Pour ouvrir Windows, utilisez ensuite « Redémarrer sous Windows » dans LuminaOS.", NULL));
  gtk_container_add(GTK_CONTAINER(p), beside_warn);
  beside_chk = gtk_check_button_new_with_label("J'ai noté ma clé de récupération BitLocker");
  g_signal_connect(beside_chk, "toggled", G_CALLBACK(on_beside_chk), NULL);
  gtk_container_add(GTK_CONTAINER(p), beside_chk);
  gtk_widget_show_all(beside_warn);     /* d'abord le contenu : show_all ignore ensuite ce bloc */
  gtk_widget_set_no_show_all(beside_warn, TRUE);
  gtk_widget_set_no_show_all(beside_chk, TRUE);
  GtkWidget *bbb = buttons();
  gtk_container_add(GTK_CONTAINER(bbb), btn("Retour", "tbtn", G_CALLBACK(on_back_disks)));
  beside_btn = btn("Installer", "fbtn", G_CALLBACK(on_erase));
  gtk_container_add(GTK_CONTAINER(bbb), beside_btn);
  gtk_container_add(GTK_CONTAINER(p), bbb);
  gtk_stack_add_named(GTK_STACK(stack), p, "beside");

  /* Confirmation */
  p = page();
  gtk_container_add(GTK_CONTAINER(p), hero("trash", TRUE, NULL));
  gtk_container_add(GTK_CONTAINER(p), text("Effacer ce disque ?", "title"));
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  add_class(w, "warn");
  confirm_l = text("", NULL);
  gtk_container_add(GTK_CONTAINER(w), confirm_l);
  gtk_container_add(GTK_CONTAINER(p), w);
  win_hint = text("Pour garder Windows, annulez : réduisez sa partition d'au moins 24 Go dans « Gestion des disques » sous Windows, "
                  "puis relancez cette installation.", "text");
  gtk_widget_set_no_show_all(win_hint, TRUE);
  gtk_container_add(GTK_CONTAINER(p), win_hint);
  gtk_container_add(GTK_CONTAINER(p), text("Sauvegardez d'abord ce que vous voulez garder : "
                                           "l'opération est irréversible.", "text"));
  confirm_chk = gtk_check_button_new_with_label("J'ai compris que tout le contenu de ce disque sera perdu");
  g_signal_connect(confirm_chk, "toggled", G_CALLBACK(on_confirm_toggled), NULL);
  gtk_container_add(GTK_CONTAINER(p), confirm_chk);
  bb = buttons();
  gtk_container_add(GTK_CONTAINER(bb), btn("Retour", "tbtn", G_CALLBACK(on_back_disks)));
  erase_btn = btn("Effacer et installer", "fbtn", G_CALLBACK(on_erase));
  add_class(erase_btn, "danger");
  gtk_container_add(GTK_CONTAINER(bb), erase_btn);
  gtk_container_add(GTK_CONTAINER(p), bb);
  gtk_stack_add_named(GTK_STACK(stack), p, "confirm");

  /* Progression */
  p = page();
  gtk_container_add(GTK_CONTAINER(p), hero("download", FALSE, NULL));
  gtk_container_add(GTK_CONTAINER(p), text("Installation en cours", "title"));
  gtk_container_add(GTK_CONTAINER(p), text("Ne retirez pas la clé USB et n'éteignez pas le PC.", "text"));
  prog_bar = gtk_progress_bar_new();
  gtk_container_add(GTK_CONTAINER(p), prog_bar);
  prog_step = label("", "step", 0);
  gtk_container_add(GTK_CONTAINER(p), prog_step);
  gtk_stack_add_named(GTK_STACK(stack), p, "progress");

  /* Fin */
  p = page();
  end_hero_box = hero("power", FALSE, &end_ic);
  gtk_container_add(GTK_CONTAINER(p), end_hero_box);
  end_title = text("", "title");
  end_text = text("", "text");
  gtk_container_add(GTK_CONTAINER(p), end_title);
  gtk_container_add(GTK_CONTAINER(p), end_text);
  bb = buttons();
  end_btn = btn("", "fbtn", G_CALLBACK(on_end));
  gtk_container_add(GTK_CONTAINER(bb), end_btn);
  gtk_container_add(GTK_CONTAINER(p), bb);
  gtk_stack_add_named(GTK_STACK(stack), p, "end");

  /* Petits ecrans (netbook 1024x600, VM en 800x600) : la fenetre ne
     depasse pas l'espace libre, le contenu defile au besoin. */
  GtkWidget *outer = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outer), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(outer), TRUE);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(outer), TRUE);
  GdkMonitor *mon = gdk_display_get_monitor(gdk_display_get_default(), 0);
  if (mon) {
    GdkRectangle wa; gdk_monitor_get_workarea(mon, &wa);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(outer), MAX(300, wa.height - 140));
    gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(outer), MAX(400, wa.width - 40));
    if (wa.height - 140 < 520 || wa.width - 40 < 640)
      gtk_window_set_default_size(GTK_WINDOW(win), MIN(640, wa.width - 40), MIN(520, wa.height - 140));
  }
  gtk_container_add(GTK_CONTAINER(outer), stack);
  gtk_container_add(GTK_CONTAINER(win), outer);
  gtk_widget_show_all(win);
  go("welcome");
}

int main(int argc, char **argv) {
  g_set_prgname("lumina-install");
  gtk_init(&argc, &argv);
  lumina_ui_init(CSS, NULL);
  /* Rien a proposer sur un LuminaOS deja installe. */
  int st = 0;
  g_spawn_command_line_sync("lumina-is-live", NULL, NULL, &st, NULL);
  if (!g_spawn_check_wait_status(st, NULL)) {
    if (argc > 1 && !strcmp(argv[1], "--welcome")) return 0;
    GtkWidget *d = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                          "LuminaOS est déjà installé sur ce PC.");
    gtk_dialog_run(GTK_DIALOG(d));
    return 0;
  }
  build();
  char *log = NULL;
  gboolean finished = !install_running() && g_file_get_contents(LOG_PATH, &log, NULL, NULL) &&
                      strstr(log, "\nDONE\n") != NULL;
  g_free(log);
  if (finished) {
    /* Installation terminee pendant que la fenetre etait fermee */
    read_log();
    show_end(TRUE);
  } else if (install_running()) {
    gtk_window_set_deletable(GTK_WINDOW(win), FALSE);
    gtk_label_set_text(GTK_LABEL(prog_step), "Reprise de l'affichage…");
    go("progress");
    start_polling();
  }
  gtk_main();
  return 0;
}

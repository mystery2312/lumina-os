/*
 * lumina-index : retrouver un fichier par ce qu'il contient.
 *
 * Index plein texte (SQLite FTS5) du dossier personnel, dans
 * ~/.cache/lumina/index.db. Rien ne quitte le PC. L'indexation tourne en
 * priorite minimale (processeur et disque) et s'arrete si le disque se
 * remplit : elle ne doit jamais se sentir sur un vieux PC.
 *
 *   lumina-index watch                 tache de fond de la session
 *   lumina-index update                met l'index a jour une fois
 *   lumina-index search "mots" [N]     chemin <TAB> extrait (mots : \2...\3)
 *   lumina-index context "question" [MAX]   passages pour Lumi (@@ chemin)
 *   lumina-index extract FICHIER [MAX] texte d'un fichier
 *   lumina-index status                nombre de fichiers lus
 *   lumina-index clear                 efface l'index
 *
 * Desactivable : ~/.config/lumina/index-off (Reglages > Lumi).
 */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>

#define BODY_MAX    (200 * 1024)        /* texte garde par fichier */
#define TEXT_MAX    (1024 * 1024)       /* fichier texte lu au-dela : ignore */
#define DOC_MAX     (50 * 1024 * 1024)  /* PDF ou bureautique au-dela : ignore */
#define FILES_MAX   20000
#define DEPTH_MAX   12
#define FREE_MIN    (300LL * 1024 * 1024)  /* marge laissee sur le disque */
#define DB_MAX      (256LL * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* Extraction du texte                                                  */
/* ------------------------------------------------------------------ */
static const char *const TEXT_EXT[] = {
  "txt", "md", "markdown", "csv", "tsv", "log", "org", "tex", "rst", "srt",
  "c", "h", "cpp", "hpp", "py", "js", "ts", "sh", "rs", "go", "java", "php", "css", "sql", "json", "xml", "yaml", "yml", "ini", NULL
};

static char *ext_of(const char *path) {
  const char *b = strrchr(path, '/');
  const char *d = strrchr(b ? b : path, '.');
  return d && d[1] ? g_ascii_strdown(d + 1, -1) : g_strdup("");
}

static gboolean in_list(const char *s, const char *const *l) {
  for (; *l; l++) if (!strcmp(s, *l)) return TRUE;
  return FALSE;
}

/* Sortie d'une commande, limitee en temps et en taille (fichier piege). */
static char *run_capped(const char *pipeline, const char *path) {
  char *out = NULL;
  const char *argv[] = {"sh", "-c", pipeline, "sh", path, NULL};
  if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                    NULL, NULL, &out, NULL, NULL, NULL)) { g_free(out); return NULL; }
  return out;
}

/* Octets quelconques -> UTF-8 valide (les vieux .txt Windows sont en CP1252). */
static char *to_utf8(char *raw) {
  if (!raw) return NULL;
  if (g_utf8_validate(raw, -1, NULL)) return raw;
  char *c = g_convert_with_fallback(raw, -1, "UTF-8", "CP1252", " ", NULL, NULL, NULL);
  g_free(raw);
  if (c && !g_utf8_validate(c, -1, NULL)) { g_free(c); return NULL; }
  return c;
}

/* Retire les balises XML/HTML : une fin de paragraphe devient un saut de
   ligne, le contenu de script et style disparait, les entites sont decodees. */
static char *strip_tags(const char *x) {
  GString *o = g_string_sized_new(strlen(x) / 2);
  const char *p = x;
  while (*p) {
    if (*p == '<') {
      if (!g_ascii_strncasecmp(p, "<script", 7) || !g_ascii_strncasecmp(p, "<style", 6)) {
        const char *close = strcasestr(p, p[1] == 's' && (p[2] == 'c' || p[2] == 'C') ? "</script" : "</style");
        if (!close) break;
        p = close;
      }
      const char *e = strchr(p, '>');
      if (!e) break;
      gboolean para = !strncmp(p, "</w:p>", 6) || !strncmp(p, "</text:p>", 9) || !strncmp(p, "</a:p>", 6)
                   || !strncmp(p, "</si>", 5) || !g_ascii_strncasecmp(p, "</p>", 4) || !g_ascii_strncasecmp(p, "<br", 3)
                   || !g_ascii_strncasecmp(p, "</div>", 6) || !g_ascii_strncasecmp(p, "</li>", 5)
                   || !g_ascii_strncasecmp(p, "</tr>", 5) || !strncmp(p, "</table:table-row>", 18);
      gboolean cell = !strncmp(p, "</table:table-cell>", 19) || !g_ascii_strncasecmp(p, "</td>", 5);
      if (para) g_string_append_c(o, '\n'); else if (cell) g_string_append_c(o, ' ');
      p = e + 1;
    } else if (*p == '&') {
      static const struct { const char *e, *c; } E[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&nbsp;", " "},
        {"&agrave;", "à"}, {"&acirc;", "â"}, {"&eacute;", "é"}, {"&egrave;", "è"}, {"&ecirc;", "ê"}, {"&euml;", "ë"},
        {"&icirc;", "î"}, {"&iuml;", "ï"}, {"&ocirc;", "ô"}, {"&ugrave;", "ù"}, {"&ucirc;", "û"}, {"&ccedil;", "ç"},
        {"&Eacute;", "É"}, {"&Agrave;", "À"}, {"&oelig;", "œ"}, {"&euro;", "€"}, {"&rsquo;", "’"},
      };
      gboolean done = FALSE;
      for (guint i = 0; i < G_N_ELEMENTS(E) && !done; i++) {
        size_t n = strlen(E[i].e);
        if (!strncmp(p, E[i].e, n)) { g_string_append(o, E[i].c); p += n; done = TRUE; }
      }
      if (!done && p[1] == '#') {
        char *end = NULL;
        gulong v = (p[2] == 'x' || p[2] == 'X') ? strtoul(p + 3, &end, 16) : strtoul(p + 2, &end, 10);
        if (end && *end == ';' && v && g_unichar_validate((gunichar)v)) {
          g_string_append_unichar(o, (gunichar)v);
          p = end + 1;
          done = TRUE;
        }
      }
      if (!done) g_string_append_c(o, *p++);
    } else g_string_append_c(o, *p++);
  }
  return g_string_free(o, FALSE);
}

/* Pas de texte a lire : NULL. Coupe a BODY_MAX, sur une frontiere UTF-8. */
static char *extract_text(const char *path, goffset size) {
  char *e = ext_of(path), *txt = NULL;
  const char *zip = NULL;
  if (!strcmp(e, "docx")) zip = "word/document.xml";
  else if (!strcmp(e, "odt") || !strcmp(e, "ods") || !strcmp(e, "odp")) zip = "content.xml";
  else if (!strcmp(e, "pptx")) zip = "ppt/slides/slide*.xml";
  else if (!strcmp(e, "xlsx")) zip = "xl/sharedStrings.xml";

  if (!strcmp(e, "pdf")) {
    if (size <= DOC_MAX) txt = run_capped("timeout 40 pdftotext -enc UTF-8 -l 150 \"$1\" - | head -c 1000000", path);
  } else if (zip) {
    if (size <= DOC_MAX) {
      char *c = g_strdup_printf("timeout 20 bsdtar -xOf \"$1\" '%s' | head -c 6000000", zip);
      char *x = to_utf8(run_capped(c, path));
      g_free(c);
      if (x) { txt = strip_tags(x); g_free(x); }
    }
  } else if (!strcmp(e, "html") || !strcmp(e, "htm")) {
    char *x = NULL;
    if (size <= TEXT_MAX && g_file_get_contents(path, &x, NULL, NULL)) {
      x = to_utf8(x);
      if (x) { txt = strip_tags(x); g_free(x); }
    }
  } else if (in_list(e, TEXT_EXT) && size <= TEXT_MAX) {
    gsize n = 0;
    if (g_file_get_contents(path, &txt, &n, NULL) && memchr(txt, 0, n)) g_clear_pointer(&txt, g_free);
  }
  g_free(e);
  txt = to_utf8(txt);
  if (!txt) return NULL;
  if (strlen(txt) > BODY_MAX) {
    char *cut = txt + BODY_MAX;
    while (cut > txt && (*cut & 0xC0) == 0x80) cut--;
    *cut = 0;
  }
  if (!*g_strstrip(txt)) { g_free(txt); return NULL; }
  return txt;
}

/* ------------------------------------------------------------------ */
/* Base                                                                 */
/* ------------------------------------------------------------------ */
static char *db_path(void) { return g_build_filename(g_get_user_cache_dir(), "lumina", "index.db", NULL); }
static char *off_path(void) { return g_build_filename(g_get_user_config_dir(), "lumina", "index-off", NULL); }

static gboolean exec_ok(sqlite3 *db, const char *sql) { return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK; }

static sqlite3 *db_open(gboolean writable) {
  char *p = db_path();
  sqlite3 *db = NULL;
  if (writable) {
    char *d = g_path_get_dirname(p);
    g_mkdir_with_parents(d, 0700);
    g_free(d);
  }
  int rc = sqlite3_open_v2(p, &db, writable ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READWRITE, NULL);
  if (rc == SQLITE_OK && writable) chmod(p, 0600);
  g_free(p);
  if (rc != SQLITE_OK) { sqlite3_close(db); return NULL; }
  sqlite3_busy_timeout(db, 4000);
  exec_ok(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-2048; PRAGMA temp_store=FILE;");
  if (writable && !exec_ok(db,
      "CREATE TABLE IF NOT EXISTS files(id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, mtime INTEGER, size INTEGER, gen INTEGER);"
      "CREATE VIRTUAL TABLE IF NOT EXISTS docs USING fts5(body, tokenize='unicode61 remove_diacritics 2');")) {
    sqlite3_close(db);
    return NULL;
  }
  return db;
}

/* ------------------------------------------------------------------ */
/* Mise a jour                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
  sqlite3 *db;
  sqlite3_stmt *get, *touch, *add, *setf, *deldoc, *adddoc;
  gint64 gen;
  int count, pending;
  gboolean full;        /* disque ou index plein : on s'arrete */
  gboolean complete;    /* parcours entier : on peut retirer les disparus */
} Up;

static gboolean room_left(void) {
  char *p = db_path(), *wal = g_strconcat(p, "-wal", NULL);
  struct statvfs v;
  struct stat s;
  gboolean ok = TRUE;
  if (statvfs(p, &v) == 0 && (long long)v.f_bavail * v.f_frsize < FREE_MIN) ok = FALSE;
  long long total = 0;
  if (stat(p, &s) == 0) total += s.st_size;
  if (stat(wal, &s) == 0) total += s.st_size;
  if (total > DB_MAX) ok = FALSE;
  g_free(p); g_free(wal);
  return ok;
}

static void commit_point(Up *u, gboolean force) {
  if (!force && u->pending < 20) return;
  exec_ok(u->db, "COMMIT; BEGIN;");
  u->pending = 0;
  if (!room_left()) u->full = TRUE;
}

static void index_file(Up *u, const char *path, const struct stat *st) {
  gint64 id = 0;
  sqlite3_reset(u->get);
  sqlite3_bind_text(u->get, 1, path, -1, SQLITE_STATIC);
  if (sqlite3_step(u->get) == SQLITE_ROW) {
    id = sqlite3_column_int64(u->get, 0);
    if (sqlite3_column_int64(u->get, 1) == (gint64)st->st_mtime && sqlite3_column_int64(u->get, 2) == (gint64)st->st_size) {
      sqlite3_reset(u->get);
      sqlite3_reset(u->touch);
      sqlite3_bind_int64(u->touch, 1, u->gen);
      sqlite3_bind_int64(u->touch, 2, id);
      sqlite3_step(u->touch);
      u->pending++;
      commit_point(u, FALSE);
      return;
    }
  }
  sqlite3_reset(u->get);
  char *body = extract_text(path, st->st_size);
  if (id) {
    sqlite3_reset(u->setf);
    sqlite3_bind_int64(u->setf, 1, (gint64)st->st_mtime);
    sqlite3_bind_int64(u->setf, 2, (gint64)st->st_size);
    sqlite3_bind_int64(u->setf, 3, u->gen);
    sqlite3_bind_int64(u->setf, 4, id);
    sqlite3_step(u->setf);
    sqlite3_reset(u->deldoc);
    sqlite3_bind_int64(u->deldoc, 1, id);
    sqlite3_step(u->deldoc);
  } else {
    sqlite3_reset(u->add);
    sqlite3_bind_text(u->add, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(u->add, 2, (gint64)st->st_mtime);
    sqlite3_bind_int64(u->add, 3, (gint64)st->st_size);
    sqlite3_bind_int64(u->add, 4, u->gen);
    sqlite3_step(u->add);
    id = sqlite3_last_insert_rowid(u->db);
  }
  if (body) {
    sqlite3_reset(u->adddoc);
    sqlite3_bind_int64(u->adddoc, 1, id);
    sqlite3_bind_text(u->adddoc, 2, body, -1, SQLITE_STATIC);
    sqlite3_step(u->adddoc);
    sqlite3_reset(u->adddoc);
    g_free(body);
  }
  u->pending += 5;
  commit_point(u, FALSE);
  g_usleep(10 * 1000);     /* laisse respirer un disque lent */
}

static gboolean skip_dir(const char *n) {
  return !strcmp(n, "node_modules") || !strcmp(n, "__pycache__") || !strcmp(n, "venv") || !strcmp(n, "target");
}

static void walk(Up *u, const char *dir, int depth) {
  if (depth > DEPTH_MAX || u->full) return;
  GDir *d = g_dir_open(dir, 0, NULL);
  if (!d) return;
  const char *n;
  while (!u->full && u->count < FILES_MAX && (n = g_dir_read_name(d))) {
    if (n[0] == '.' || strpbrk(n, "\t\n")) continue;
    char *p = g_build_filename(dir, n, NULL);
    struct stat st;
    if (lstat(p, &st) == 0) {
      if (S_ISDIR(st.st_mode)) { if (!skip_dir(n)) walk(u, p, depth + 1); }
      else if (S_ISREG(st.st_mode)) { u->count++; index_file(u, p, &st); }
    }
    g_free(p);
  }
  g_dir_close(d);
}

/* Priorite la plus basse : processeur (SCHED_IDLE, nice 19) et disque. */
static void be_gentle(void) {
  struct sched_param sp = {0};
  sched_setscheduler(0, SCHED_IDLE, &sp);
  setpriority(PRIO_PROCESS, 0, 19);
  syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, 0, (3 << 13) /* IOPRIO_CLASS_IDLE */);
}

static gboolean is_off(void) {
  char *p = off_path();
  gboolean off = g_file_test(p, G_FILE_TEST_EXISTS);
  g_free(p);
  return off;
}

static int do_update(void) {
  if (is_off()) return 0;
  /* Pas de nouvelle base sur un disque deja presque plein */
  char *cache = g_build_filename(g_get_user_cache_dir(), "lumina", NULL);
  g_mkdir_with_parents(cache, 0700);
  struct statvfs v;
  if (statvfs(cache, &v) == 0 && (long long)v.f_bavail * v.f_frsize < FREE_MIN) { g_free(cache); return 0; }
  char *lock = g_build_filename(cache, "index.lock", NULL);
  g_free(cache);
  int fd = g_open(lock, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  g_free(lock);
  if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) { if (fd >= 0) close(fd); return 0; }

  sqlite3 *db = db_open(TRUE);
  if (!db) { close(fd); return 1; }
  Up u = {.db = db, .gen = g_get_real_time(), .complete = FALSE};
  sqlite3_prepare_v2(db, "SELECT id, mtime, size FROM files WHERE path=?", -1, &u.get, NULL);
  sqlite3_prepare_v2(db, "UPDATE files SET gen=? WHERE id=?", -1, &u.touch, NULL);
  sqlite3_prepare_v2(db, "INSERT INTO files(path, mtime, size, gen) VALUES(?,?,?,?)", -1, &u.add, NULL);
  sqlite3_prepare_v2(db, "UPDATE files SET mtime=?, size=?, gen=? WHERE id=?", -1, &u.setf, NULL);
  sqlite3_prepare_v2(db, "DELETE FROM docs WHERE rowid=?", -1, &u.deldoc, NULL);
  sqlite3_prepare_v2(db, "INSERT INTO docs(rowid, body) VALUES(?,?)", -1, &u.adddoc, NULL);
  exec_ok(db, "BEGIN;");
  if (room_left()) walk(&u, g_get_home_dir(), 0); else u.full = TRUE;
  u.complete = !u.full && u.count < FILES_MAX;
  commit_point(&u, TRUE);
  if (u.complete) {
    sqlite3_stmt *s;
    sqlite3_prepare_v2(db, "DELETE FROM docs WHERE rowid IN (SELECT id FROM files WHERE gen<>?)", -1, &s, NULL);
    sqlite3_bind_int64(s, 1, u.gen); sqlite3_step(s); sqlite3_finalize(s);
    sqlite3_prepare_v2(db, "DELETE FROM files WHERE gen<>?", -1, &s, NULL);
    sqlite3_bind_int64(s, 1, u.gen); sqlite3_step(s); sqlite3_finalize(s);
  }
  exec_ok(db, "COMMIT;");
  exec_ok(db, "PRAGMA wal_checkpoint(TRUNCATE);");
  sqlite3_finalize(u.get); sqlite3_finalize(u.touch); sqlite3_finalize(u.add);
  sqlite3_finalize(u.setf); sqlite3_finalize(u.deldoc); sqlite3_finalize(u.adddoc);
  sqlite3_close(db);
  close(fd);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Recherche                                                            */
/* ------------------------------------------------------------------ */
static const char *const STOP[] = {
  "le", "la", "les", "un", "une", "des", "du", "de", "et", "ou", "est", "sont", "que", "qui", "quoi", "quel", "quelle",
  "quels", "quelles", "dans", "sur", "pour", "par", "avec", "sans", "ce", "cet", "cette", "ces", "mon", "ma", "mes",
  "ton", "ta", "tes", "son", "sa", "ses", "notre", "nos", "votre", "vos", "leur", "leurs", "je", "tu", "il", "elle",
  "on", "nous", "vous", "ils", "elles", "au", "aux", "en", "ne", "pas", "plus", "ai", "as", "avons", "avez", "ont",
  "etait", "etre", "avoir", "fait", "faire", "ou", "quand", "comment", "pourquoi", "combien", "fichier", "fichiers",
  "document", "documents", "dit", "disent", "trouve", "trouver", "cherche", "chercher", "retrouve", "retrouver", "moi",
  "me", "peux", "peut", "parle", "parlent", "ca", "cela", "the", "and", "what", "where", NULL
};

/* Requete FTS5 : mots-prefixes joints par espace (ET) ou OR. Les mots ne
   contiennent que des lettres et des chiffres : aucune syntaxe ne passe. */
static char *build_query(const char *q, gboolean any) {
  GString *o = g_string_new(NULL), *w = g_string_new(NULL);
  for (const char *p = q;; p = g_utf8_next_char(p)) {
    gunichar c = *p ? g_utf8_get_char(p) : 0;
    if (c && g_unichar_isalnum(c)) { g_string_append_unichar(w, c); continue; }
    if (w->len && g_utf8_strlen(w->str, -1) >= 2) {
      char *low = g_utf8_strdown(w->str, -1), *plain = g_str_to_ascii(low, "C");
      if (!any || !in_list(plain, STOP)) {
        if (o->len) g_string_append(o, any ? " OR " : " ");
        g_string_append_printf(o, "\"%s\"*", low);
      }
      g_free(low); g_free(plain);
    }
    g_string_truncate(w, 0);
    if (!c) break;
  }
  g_string_free(w, TRUE);
  if (!o->len) { g_string_free(o, TRUE); return NULL; }
  return g_string_free(o, FALSE);
}

static int do_search(const char *q, int limit) {
  char *fts = build_query(q, FALSE);
  sqlite3 *db = fts ? db_open(FALSE) : NULL;
  if (!db) { g_free(fts); return 0; }
  sqlite3_stmt *s = NULL;
  if (sqlite3_prepare_v2(db,
      "SELECT f.path, snippet(docs, 0, char(2), char(3), '…', 14) FROM docs JOIN files f ON f.id = docs.rowid "
      "WHERE docs MATCH ? ORDER BY bm25(docs) LIMIT ?", -1, &s, NULL) == SQLITE_OK) {
    sqlite3_bind_text(s, 1, fts, -1, SQLITE_STATIC);
    sqlite3_bind_int(s, 2, limit);
    while (sqlite3_step(s) == SQLITE_ROW) {
      const char *path = (const char *)sqlite3_column_text(s, 0);
      if (!g_file_test(path, G_FILE_TEST_EXISTS)) continue;     /* supprime depuis */
      char *sn = g_strdup((const char *)sqlite3_column_text(s, 1));
      for (char *c = sn; *c; c++) if (*c == '\n' || *c == '\t' || *c == '\r') *c = ' ';
      printf("%s\t%s\n", path, sn);
      g_free(sn);
    }
    sqlite3_finalize(s);
  }
  sqlite3_close(db);
  g_free(fts);
  return 0;
}

/* Passages pour Lumi : pour chacun des meilleurs fichiers, le texte autour
   du premier mot trouve. MAX = caracteres au total (selon le modele). */
static int do_context(const char *q, int maxchars) {
  enum { NFILES = 4 };
  char *fts = build_query(q, TRUE);
  sqlite3 *db = fts ? db_open(FALSE) : NULL;
  if (!db) { g_free(fts); return 0; }
  sqlite3_stmt *s = NULL;
  int each = MAX(maxchars / NFILES, 200);
  if (sqlite3_prepare_v2(db,
      "SELECT f.path, highlight(docs, 0, char(2), char(3)) FROM docs JOIN files f ON f.id = docs.rowid "
      "WHERE docs MATCH ? ORDER BY bm25(docs) LIMIT ?", -1, &s, NULL) == SQLITE_OK) {
    sqlite3_bind_text(s, 1, fts, -1, SQLITE_STATIC);
    sqlite3_bind_int(s, 2, NFILES);
    while (sqlite3_step(s) == SQLITE_ROW) {
      const char *path = (const char *)sqlite3_column_text(s, 0);
      if (!g_file_test(path, G_FILE_TEST_EXISTS)) continue;
      char *b = g_strdup((const char *)sqlite3_column_text(s, 1));
      /* retire les marques ; `at` = position (octets) du premier mot trouve */
      char *first = strchr(b, 2);
      gsize at = first ? (gsize)(first - b) : 0, w = 0;
      for (char *c = b; *c; c++) if (*c != 2 && *c != 3) b[w++] = *c;
      b[w] = 0;
      /* fenetre autour : un quart avant, le reste apres */
      glong pos = g_utf8_pointer_to_offset(b, b + MIN(at, w));
      glong len = g_utf8_strlen(b, -1);
      glong from = MAX(0, pos - each / 4), to = MIN(len, from + each);
      char *x = g_utf8_substring(b, from, to);
      printf("@@ %s\n%s%s%s\n", path, from ? "…" : "", x, to < len ? "…" : "");
      g_free(x); g_free(b);
    }
    sqlite3_finalize(s);
  }
  sqlite3_close(db);
  g_free(fts);
  return 0;
}

static int do_extract(const char *path, int maxchars) {
  struct stat st;
  if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 1;
  char *t = extract_text(path, st.st_size);
  if (!t) return 2;
  if (maxchars > 0 && g_utf8_strlen(t, -1) > maxchars) *g_utf8_offset_to_pointer(t, maxchars) = 0;
  fputs(t, stdout);
  g_free(t);
  return 0;
}

static int do_status(void) {
  sqlite3 *db = db_open(FALSE);
  int n = 0;
  if (db) {
    sqlite3_stmt *s = NULL;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM docs", -1, &s, NULL) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
      n = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    sqlite3_close(db);
  }
  printf("%d\n", n);
  return 0;
}

static int do_clear(void) {
  char *p = db_path();
  static const char *const SUF[] = {"", "-wal", "-shm"};
  for (guint i = 0; i < G_N_ELEMENTS(SUF); i++) {
    char *f = g_strconcat(p, SUF[i], NULL);
    g_unlink(f);
    g_free(f);
  }
  g_free(p);
  return 0;
}

int main(int argc, char **argv) {
  const char *c = argc > 1 ? argv[1] : "";
  if (!strcmp(c, "update")) { be_gentle(); return do_update(); }
  if (!strcmp(c, "watch")) {
    be_gentle();
    g_usleep(G_USEC_PER_SEC * 90);       /* apres l'ouverture de session */
    for (;;) { do_update(); g_usleep(G_USEC_PER_SEC * 1200); }
  }
  if (!strcmp(c, "search") && argc > 2) return do_search(argv[2], argc > 3 ? atoi(argv[3]) : 6);
  if (!strcmp(c, "context") && argc > 2) return do_context(argv[2], argc > 3 ? atoi(argv[3]) : 3000);
  if (!strcmp(c, "extract") && argc > 2) return do_extract(argv[2], argc > 3 ? atoi(argv[3]) : 0);
  if (!strcmp(c, "status")) return do_status();
  if (!strcmp(c, "clear")) return do_clear();
  fprintf(stderr, "usage : lumina-index update|watch|search|context|extract|status|clear\n");
  return 2;
}

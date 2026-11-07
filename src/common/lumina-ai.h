/* Lumi, l'assistant de LuminaOS : acces commun a l'IA pour les applis.
 *
 * Deux modes, choisis dans Reglages > Lumi (~/.config/lumina/ai.conf) :
 *   - local  : modele sur ce PC (llama.cpp), sur un socket prive de
 *              l'utilisateur ; rien ne quitte le PC ;
 *   - remote : service compatible OpenAI choisi par l'utilisateur, avec sa
 *              cle (~/.config/lumina/ai.key, 0600).
 * Les reponses arrivent morceau par morceau (on_piece), puis on_done. */
#pragma once
#include <gio/gio.h>

typedef enum { LUMINA_AI_OFF, LUMINA_AI_LOCAL, LUMINA_AI_REMOTE } LuminaAiMode;

typedef struct {
  LuminaAiMode mode;
  char *model;         /* local : id du catalogue (leger, equilibre) */
  char *remote_url;    /* remote : https://.../v1 */
  char *remote_model;  /* remote : nom du modele */
} LuminaAiConfig;

LuminaAiConfig *lumina_ai_config_load(void);
void lumina_ai_config_save(const LuminaAiConfig *c);
void lumina_ai_config_free(LuminaAiConfig *c);
char *lumina_ai_key_load(void);
void lumina_ai_key_save(const char *key);

/* Chemin du modele local installe (NULL s'il est absent). */
char *lumina_ai_model_path(const char *id);

/* Pret a repondre ? Sinon *why recoit une explication en francais. */
gboolean lumina_ai_ready(char **why);

typedef void (*LuminaAiPiece)(const char *piece, gpointer d);
typedef void (*LuminaAiDone)(const char *error, gpointer d);   /* error NULL = succes */

/* Conversation : messages = suite de paires (role, contenu) terminee par
   NULL ; role = "system", "user" ou "assistant". */
void lumina_ai_chat(const char *const *messages, LuminaAiPiece on_piece, LuminaAiDone on_done,
                    gpointer d, GCancellable *cancel);

/* Idem, avec la progression de la lecture de la demande (0..1) par le
   moteur local, avant le premier mot de la reponse : utile sur un PC lent. */
typedef void (*LuminaAiProgress)(double fraction, gpointer d);
void lumina_ai_chat_progress(const char *const *messages, LuminaAiProgress on_progress, LuminaAiPiece on_piece,
                             LuminaAiDone on_done, gpointer d, GCancellable *cancel);

/* Raccourci : une consigne systeme + un texte. */
void lumina_ai_ask(const char *system, const char *user, LuminaAiPiece on_piece, LuminaAiDone on_done,
                   gpointer d, GCancellable *cancel);

/* Utilitaires JSON minimaux (exposes pour les tests). */
char *lumina_ai_json_escape(const char *s);
char *lumina_ai_json_string_at(const char *json, const char *key);

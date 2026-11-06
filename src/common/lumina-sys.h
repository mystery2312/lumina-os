/*
 * lumina-sys : utilitaires systeme partages par les applis LuminaOS
 * (fichiers, commandes, Wi-Fi via iwd). Voir lumina-sys.c.
 */
#ifndef LUMINA_SYS_H
#define LUMINA_SYS_H
#include <gio/gio.h>

char *read_file(const char *path);                       /* contenu sans blancs autour, ou NULL */
char *run_argv(const char *const *argv, int *status);    /* sortie, sans couleurs ANSI */
void spawn_async(const char *const *argv);
char *battery_eta(void);                                 /* « environ 3 h 10 restantes » ou NULL */

/* Wi-Fi (iwd). dev : interface, ex. "wlan0" ; NULL si pas de carte. */
typedef struct { char *ssid, *security; int signal; gboolean current; } Net;
void net_free(gpointer p);
char *net_wifi_device(void);
gboolean net_wifi_up(const char *dev);                   /* associe et lien actif */
gboolean net_wired_up(const char *wifi_dev);             /* une autre interface active */
gboolean net_wifi_powered(const char *dev);
char *net_wifi_ssid(const char *dev);                    /* reseau connecte, ou NULL */
GPtrArray *net_wifi_list(const char *dev);               /* Net*, du meilleur au moins bon */
void net_wifi_scan(const char *dev);
void net_wifi_power(const char *dev, gboolean on);
/* Connexion asynchrone. pass NULL : reseau ouvert ou deja connu (echoue
   sans demander si une cle est necessaire). Le rappel recoit le
   GSubprocess : g_subprocess_get_successful() apres wait_finish. */
void net_wifi_connect(const char *dev, const char *ssid, const char *pass,
                      GAsyncReadyCallback cb, gpointer data);
#endif

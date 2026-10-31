/* Mode Transfert (entrée TRANSFERT de l'écran titre) : la montre redémarre sans le jeu, se connecte au Wi-Fi, règle
   l'heure (NTP), met à jour les fichiers du jeu depuis le serveur de l'Atelier, puis redémarre dans le jeu. Le Wi-Fi
   n'est jamais démarré en jeu ni sur le cadran. */
#ifndef TRANSFERT_H
#define TRANSFERT_H

/* Redémarre la montre en mode Transfert (cfg.reglages du moteur, SDX TRANSFERT) */
void transfert_lancer(void);
/* Au démarrage : 1 si le mode Transfert a été demandé (la demande est alors effacée) */
int transfert_demande(void);
/* Le mode Transfert : tâche à part (pile plus grande que celle de app_main), redémarre dans le jeu à la fin */
void transfert_demarrer(void);
/* Anneau de chargement du jeu (part de 0 à 1), puis tampon rendu avant les réservations du moteur */
void anneau_demarrage(float part);
void anneau_demarrage_fin(void);
/* Réseau Wi-Fi gardé dans la NVS (SDX WIFI, depuis le PC) ; 1 si c'est fait */
int transfert_wifi(const char *ssid, const char *mdp);

#endif

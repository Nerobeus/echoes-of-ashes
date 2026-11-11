/* Applications de la montre : l'accueil (partition « montre »), Echoes of Ashes (« echoes »), le PC DOS (« pcdos »,
   tiny386, pcdos/ du dépôt) et le Game Boy (« gameboy », gnuboy, gameboy/ du dépôt). Lancer une application = la
   choisir pour le prochain démarrage (esp_ota_set_boot_partition), puis redémarrer. */
#ifndef APPS_H
#define APPS_H

/* 1 si la partition de cette application contient un programme valide */
int app_presente(const char *nom);
/* Redémarre dans cette application ; ne revient pas (0 si elle n'est pas là) */
int app_lancer(const char *nom);
/* Mise en veille d'Echoes, du PC DOS ou du Game Boy : l'app montre démarre écran éteint et s'endort aussitôt ;
   reprise : drapeau posé pour l'application qui reprendra au prochain lancement (« reprise » : Echoes, ou NULL). Ne
   revient pas (0 : pas d'app montre) */
int app_veille(const char *reprise);
/* 1 si ce drapeau était posé (« veille », « reprise ») ; il est effacé */
int app_drapeau(const char *cle);
/* Valeur gardée (« monde ») : lue sans l'effacer, -1 si elle n'est pas posée */
int app_valeur(const char *cle);
/* Redémarre dans Echoes (nom : « echoes ») avec le monde choisi : 0 Echoes, 1 l'île du jeu des héroïnes (main.c : leger_monde) */
int app_lancer_monde(const char *nom, int monde);

#endif

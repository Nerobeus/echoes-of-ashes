/*
 * Mise à jour de l'app Android depuis le serveur de l'Atelier (node serveur.js, même réseau Wi-Fi) : au lancement, le
 * fil de mise à jour cherche le serveur, puis télécharge monde.pak et musique.bin s'ils sont plus récents que ceux de
 * l'APK et du dernier téléchargement (gardé dans le dossier privé de l'app). Sans serveur : la copie la plus récente.
 */
#ifndef MAJ_ANDROID_H
#define MAJ_ANDROID_H

#include <android/asset_manager.h>
#include <stddef.h>

enum { MAJ_RECHERCHE, MAJ_TELECHARGE, MAJ_FINI };
extern volatile int maj_phase;          /* MAJ_* : lu par l'écran d'attente */
extern volatile float maj_part;         /* part téléchargée, de 0 à 1 */

/* Lance le fil de mise à jour (dir : dossier privé de l'app, am : assets de l'APK) */
void maj_lancer(const char *dir, AAssetManager *am);
/* Une fois maj_phase à MAJ_FINI : 1 si le fichier téléchargé name ("monde.pak", "musique.bin") est la copie à prendre
   (son chemin dans path), 0 pour celle de l'APK, -1 s'il n'y en a aucune (édition enfant choisie sans monde_enfant.pak
   téléchargé : celle de l'APK est 18+) */
int maj_fichier(const char *name, char *path, size_t n);
/* 1 si la copie de l'APK de name peut servir (absente, ou 18+ quand l'édition enfant est choisie : 0) */
int maj_apk(const char *name);

#endif

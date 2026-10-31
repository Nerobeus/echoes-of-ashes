/*
 * Réglages de la montre, gardés dans <dossier du jeu>/montre.txt (lignes « clé=valeur ») : volume (%), luminosité (1 à
 * 5, comme le menu Système du jeu : 64 + 48 × (n - 1), 255 au plus), veille (secondes sans action, 0 : jamais). Lus au
 * démarrage ; réécrits 2 s après le dernier changement, qu'il vienne de la page RÉGLAGES, du cadran SON du jeu ou de
 * la luminosité d'une partie chargée. Le PC DOS lit le même fichier (volume). MANETTE (Poké Ball Plus, manette.c) :
 * gardée en NVS (espace « echoes »), l'app montre réécrivant montre.txt avec ses seules clés.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sd.h"
#include "veille.h"
#include "horloge.h"
#include "manette.h"
#include "reglages.h"

static const char *TAG = "reglages";
#define FICHIER SD_DIR "/montre.txt"
#define ECRIRE_US 2000000

static const int VEILLES[] = { 30, 60, 120, 300, 0 };
#define NVEILLES (int)(sizeof VEILLES / sizeof VEILLES[0])
static int volume = CONFIG_ECHOES_VOLUME, lum = 4, veille = CONFIG_ECHOES_VEILLE_S;
static int64_t change_t;   /* dernier changement pas encore écrit (0 : rien) */

static int lum_cmd(int n) { const int v = 64 + 48 * (n - 1); return v > 255 ? 255 : v; }
int reglages_volume(void) { return volume; }
int reglages_luminosite(void) { return lum_cmd(lum); }

void reglages_lire(void) {
    FILE *f = fopen(FICHIER, "r");
    if (f) {
        char l[64];
        int v;
        while (fgets(l, sizeof l, f)) {
            if (sscanf(l, "volume=%d", &v) == 1) volume = v < 0 ? 0 : v > 100 ? 100 : v;
            else if (sscanf(l, "luminosite=%d", &v) == 1) lum = v < 1 ? 1 : v > 5 ? 5 : v;
            else if (sscanf(l, "veille=%d", &v) == 1) veille = v < 0 ? 0 : v;
        }
        fclose(f);
    }
    veille_delai(veille);
    ESP_LOGI(TAG, "volume %d %%, luminosité %d, veille %d s%s", volume, lum, veille, f ? "" : " (valeurs par défaut)");
}

static void ecrire(void) {
    FILE *f = fopen(FICHIER, "w");
    if (!f) { ESP_LOGW(TAG, FICHIER " : écriture impossible"); return; }
    fprintf(f, "volume=%d\nluminosite=%d\nveille=%d\n", volume, lum, veille);
    fclose(f);
}
static void change(void) { change_t = esp_timer_get_time(); if (!change_t) change_t = 1; }

void reglages_luminosite_vue(int level) {
    int n = (level - 64 + 24) / 48 + 1;
    n = n < 1 ? 1 : n > 5 ? 5 : n;
    if (n != lum) { lum = n; change(); }
}

void reglages_suivre(int64_t now) {
    const int v = (int)(eng_volume() * 100 + 0.5f);   /* son du jeu (cadran SON) : arrondi au %, noté s'il a bougé */
    if (v != volume) { volume = v; change(); }
    if (change_t && now - change_t >= ECRIRE_US) { change_t = 0; ecrire(); }
}

/* ---------- Page RÉGLAGES ---------- */
static char txt[24];
static const char *v_volume(void) { snprintf(txt, sizeof txt, "%d %%", volume); return txt; }
static void c_volume(int s) {
    volume += 10 * s;
    volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    eng_set_volume(volume / 100.0f);
    change();
}
static const char *v_lum(void) { snprintf(txt, sizeof txt, "%d / 5", lum); return txt; }
extern void eng_luminosite_montre(int level);   /* main.c : commande de l'écran */
static void c_lum(int s) {
    lum += s;
    lum = lum < 1 ? 1 : lum > 5 ? 5 : lum;
    eng_luminosite_montre(lum_cmd(lum));
    change();
}
static const char *v_veille(void) {
    if (!veille) return "JAMAIS";
    if (veille < 60) snprintf(txt, sizeof txt, "%d s", veille); else snprintf(txt, sizeof txt, "%d min", veille / 60);
    return txt;
}
static void c_veille(int s) {
    int k = 0;
    while (k < NVEILLES - 1 && VEILLES[k] != veille) k++;
    k = (k + s + NVEILLES) % NVEILLES;
    veille = VEILLES[k];
    veille_delai(veille);
    change();
}

/* Manette Bluetooth : NON, RECHERCHE, ou le niveau de sa batterie. L'activer redémarre la montre si la mémoire du
   Bluetooth a été rendue au démarrage (manette coupée) : main.c ne la garde que pour la manette */
static int manette = -1;
bool reglages_manette(void) {
    if (manette >= 0) return manette;
    manette = 0;
    nvs_flash_init();   /* sans effet si c'est déjà fait */
    nvs_handle_t h;
    int8_t v;
    if (nvs_open("echoes", NVS_READONLY, &h) == ESP_OK) { if (nvs_get_i8(h, "manette", &v) == ESP_OK) manette = !!v; nvs_close(h); }
    return manette;
}
void reglages_sans_manette(void) {
    manette = 0;
    nvs_handle_t h;
    if (nvs_open("echoes", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i8(h, "manette", 0); nvs_commit(h); nvs_close(h); }
}
static const char *v_manette(void) {
    if (!reglages_manette()) return "NON";
    if (!manette_connectee()) return "RECHERCHE";
    const int b = manette_batterie();
    if (b >= 0) snprintf(txt, sizeof txt, "PBP %d %%", b % 1000); else snprintf(txt, sizeof txt, "PBP");
    return txt;
}
/* Activée sans la mémoire du Bluetooth (rendue au démarrage) : redémarrage ; en partie, sauvegardée d'abord et reprise
   aussitôt (drapeau « reprise » d'apps.c, lu par main.c) ; pas le moment de sauver (combat, course, scène, dialogue) :
   la manette viendra au prochain démarrage */
static void c_manette(int s) {
    manette = !reglages_manette();
    nvs_handle_t h;
    if (nvs_open("echoes", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i8(h, "manette", (int8_t)manette); nvs_commit(h); nvs_close(h); }
    if (!manette) { manette_arreter(); return; }
    if (manette_active() || manette_demarrer()) return;
    if (eng_sauvegarder()) {
        if (nvs_open("apps", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "reprise", 1); nvs_commit(h); nvs_close(h); }
    } else if (!eng_ecran_titre()) { ESP_LOGW(TAG, "manette : partie impossible à sauver maintenant, active au prochain démarrage"); return; }
    ESP_LOGI(TAG, "manette : redémarrage (mémoire du Bluetooth rendue au démarrage)");
    horloge_garder();
    esp_restart();
}

const eng_reglage_t reglages_montre[] = {
    { "VOLUME", v_volume, c_volume },
    { "LUMINOSITÉ", v_lum, c_lum },
    { "VEILLE", v_veille, c_veille },
    { "MANETTE", v_manette, c_manette },
};
const int reglages_nombre = sizeof reglages_montre / sizeof reglages_montre[0];
/* En jeu, en bas du menu SYSTÈME (eng_config_t.regls_jeu) */
const eng_reglage_t reglages_jeu[] = { { "MANETTE", v_manette, c_manette } };
const int reglages_jeu_nombre = sizeof reglages_jeu / sizeof reglages_jeu[0];

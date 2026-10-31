/*
 * Mode Transfert : hors du jeu (la montre y redémarre, le moteur n'est pas chargé, toute la mémoire est libre), le Wi-Fi
 * règle l'heure et met à jour les fichiers du jeu, puis la montre redémarre dans le jeu. Rien de cela ne tourne en jeu
 * ni sur le cadran.
 *
 * 1. Wi-Fi : réseau gardé dans la NVS (espace « echoes », clés ssid et mdp, posés par SDX WIFI depuis le PC :
 *    engine/tools/envoi_sd.py --wifi, ou reçus du téléphone), sinon celui de menuconfig ; sans réseau ou sans
 *    connexion, le téléphone le donne par le Bluetooth (provisionner : app « ESP BLE Provisioning » d'Espressif).
 * 2. Heure : NTP (pool.ntp.org), convertie en heure locale (CONFIG_ECHOES_FUSEAU) pour horloge_regler.
 * 3. Serveur de l'Atelier : la dernière adresse (NVS, clé serveur), sinon « ECHOES? » diffusé en UDP sur le port du
 *    serveur, auquel serveur.js répond « ECHOES <port> » (comme pour les apps Android).
 * 4. Fichiers : /api/moteur/<nom> (HEAD : X-Mtime et taille ; data/enfant pour l'édition enfant) comparés à ce que
 *    wifi.txt a noté ; ceux qui ont changé sont tous téléchargés en <nom>.part, puis mis en place ensemble (un
 *    téléchargement raté ne change rien) ; le paquet léger (monde_esp32.pak, du même export que monde.pak) est
 *    ensuite écrit dans la partition « monde ». Les entrées du manifeste de envoi_sd.py sont oubliées (sdx_oublier).
 *
 * Écran : un anneau ; centre violet (attente du téléphone par le Bluetooth), bleu (Wi-Fi), cyan (heure), jaune (serveur), vert (fichiers : l'anneau suit la part
 * reçue), rouge (échec) ; anneau vert plein à la fin.
 */
#include <math.h>
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "lwip/sockets.h"
#include "board.h"
#include "horloge.h"
#include "lcd_co5300.h"
#include "sd.h"
#include "sdx.h"
#include "transfert.h"

static const char *TAG = "transfert";
#define MAGIC 0x54524E53u            /* « TRNS » */
RTC_NOINIT_ATTR static uint32_t demande;

#define BANDE 34
#define BLOC (32 * 1024)            /* morceau lu sur le réseau puis écrit sur la carte (SRAM interne : DMA) */
#define FICHIERS 5
#if CONFIG_ECHOES_ENFANT
#define URL_DIR "/api/moteur/enfant/"
#else
#define URL_DIR "/api/moteur/"
#endif
/* Ordre : le paquet léger en dernier (écrit en flash quand tout le reste est en place) */
static const char *const NOMS[FICHIERS] = { "histoire.scn", "interieurs.bin", "musique.bin", "monde.pak", "monde_esp32.pak" };
#define LEGER (FICHIERS - 1)

void transfert_lancer(void) {
    demande = MAGIC;
    horloge_garder();
    esp_restart();
}
int transfert_demande(void) {
    const int d = demande == MAGIC;
    demande = 0;
    return d;
}

static void nvs_pret(void) {
    static int fait;
    if (fait) return;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    fait = 1;
}
int transfert_wifi(const char *ssid, const char *mdp) {
    nvs_pret();
    nvs_handle_t h;
    if (nvs_open("echoes", NVS_READWRITE, &h) != ESP_OK) return 0;
    const int ok = nvs_set_str(h, "ssid", ssid) == ESP_OK && nvs_set_str(h, "mdp", mdp) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
static int nvs_lire(const char *cle, char *v, size_t n) {
    nvs_handle_t h;
    if (nvs_open("echoes", NVS_READONLY, &h) != ESP_OK) return 0;
    const int ok = nvs_get_str(h, cle, v, &n) == ESP_OK && v[0];
    nvs_close(h);
    return ok;
}
static void nvs_ecrire(const char *cle, const char *v) {
    nvs_handle_t h;
    if (nvs_open("echoes", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, cle, v); nvs_commit(h);
    nvs_close(h);
}

/* ---------- Écran : anneau de progression ---------- */
#define C_FOND   0x0000
#define C_PISTE  0x2104
#define C_BLEU   0x2B7F
#define C_CYAN   0x07FF
#define C_JAUNE  0xFEA0
#define C_VERT   0x2FE6
#define C_ROUGE  0xF8A2
#define C_VIOLET 0x981F
static uint16_t *bande;
static void ecran(float part, uint16_t centre, uint16_t anneau) {
    if (!bande) return;
    const float c = (LCD_W - 1) * 0.5f;
    for (int y0 = 0; y0 < LCD_H; y0 += BANDE) {
        const int h = LCD_H - y0 < BANDE ? LCD_H - y0 : BANDE;
        lcd_buffer_wait(0);
        for (int y = 0; y < h; y++) for (int x = 0; x < LCD_W; x++) {
            const float dx = x - c, dy = y0 + y - c, d = sqrtf(dx * dx + dy * dy);
            uint16_t v = C_FOND;
            if (d < 46) v = centre;
            else if (d > 196 && d < 216) {
                float a = atan2f(dx, -dy) / (2 * 3.14159265f);   /* 0 en haut, sens des aiguilles */
                if (a < 0) a += 1;
                v = a <= part ? anneau : C_PISTE;
            }
            bande[y * LCD_W + x] = (uint16_t)(v >> 8 | v << 8);
        }
        lcd_band_send(0, LCD_W, y0, h, bande, 0);
    }
    lcd_wait_all();
}

/* Démarrage du jeu (main.c) : le même anneau pendant le chargement, sinon l'écran reste noir plusieurs secondes */
void anneau_demarrage(float part) {
    if (!bande) bande = heap_caps_malloc(LCD_W * BANDE * 2, MALLOC_CAP_DMA);
    ecran(part, C_PISTE, C_BLEU);
}
void anneau_demarrage_fin(void) { free(bande); bande = NULL; }

/* ---------- Wi-Fi ---------- */
static EventGroupHandle_t ev;
#define EV_IP 1
#define EV_ECHEC 2
static int essais;
static volatile int prov, reseau_recu;   /* provisionnement Bluetooth en cours ; réseau reçu du téléphone */
static void evenement(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { if (!prov) esp_wifi_connect(); }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (prov) { if (reseau_recu) esp_wifi_connect(); }   /* le gestionnaire essaie le réseau reçu ; les échecs vont au téléphone */
        else if (++essais < 5) esp_wifi_connect(); else xEventGroupSetBits(ev, EV_ECHEC);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "adresse " IPSTR, IP2STR(&((ip_event_got_ip_t *)data)->ip_info.ip));
        xEventGroupSetBits(ev, EV_IP);
    } else if (base == WIFI_PROV_EVENT && id == WIFI_PROV_CRED_RECV) {
        const wifi_sta_config_t *c = data;
        char ssid[33] = "", mdp[65] = "";
        memcpy(ssid, c->ssid, sizeof c->ssid); memcpy(mdp, c->password, sizeof c->password);
        transfert_wifi(ssid, mdp);   /* gardé pour les fois suivantes */
        reseau_recu = 1;
        ESP_LOGI(TAG, "Bluetooth : réseau « %s » reçu du téléphone", ssid);
    } else if (base == WIFI_PROV_EVENT && id == WIFI_PROV_CRED_FAIL) {
        ESP_LOGW(TAG, "Bluetooth : connexion refusée (mot de passe ?), le téléphone peut renvoyer le réseau");
        reseau_recu = 0;
        wifi_prov_mgr_reset_sm_state_on_failure();
    } else if (base == WIFI_PROV_EVENT && id == WIFI_PROV_END) wifi_prov_mgr_deinit();
}
/* Pas de réseau, ou connexion impossible : le téléphone le donne par le Bluetooth (app « ESP BLE Provisioning »
   d'Espressif : appareil PROV_ECHOES, code CONFIG_ECHOES_BLE_CODE) ; le Bluetooth n'existe que le temps de cette
   attente, sa mémoire est rendue à la fin (FREE_BTDM) */
static int provisionner(void) {
    esp_wifi_stop();
    xEventGroupClearBits(ev, EV_IP | EV_ECHEC);
    prov = 1; reseau_recu = 0;
    esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, evenement, NULL);
    wifi_prov_mgr_config_t c = { .scheme = wifi_prov_scheme_ble, .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM };
    if (wifi_prov_mgr_init(c) != ESP_OK) { ESP_LOGE(TAG, "Bluetooth : provisionnement impossible"); return 0; }
    ecran(0, C_VIOLET, C_VIOLET);
    ESP_LOGI(TAG, "Bluetooth : app ESP BLE Provisioning, appareil PROV_ECHOES, code « %s » (3 min)", CONFIG_ECHOES_BLE_CODE);
    if (wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, CONFIG_ECHOES_BLE_CODE, "PROV_ECHOES", NULL) != ESP_OK) { wifi_prov_mgr_deinit(); return 0; }
    const EventBits_t b = xEventGroupWaitBits(ev, EV_IP, pdFALSE, pdFALSE, pdMS_TO_TICKS(180000));
    if (!(b & EV_IP)) { ESP_LOGE(TAG, "Bluetooth : rien reçu du téléphone"); wifi_prov_mgr_stop_provisioning(); }
    vTaskDelay(pdMS_TO_TICKS(1500));   /* le téléphone apprend le succès, puis le gestionnaire s'arrête (WIFI_PROV_END) */
    prov = 0;
    return (b & EV_IP) != 0;
}
static int wifi_connecter(void) {
    char ssid[33] = "", mdp[65] = "";
    if (!nvs_lire("ssid", ssid, sizeof ssid)) { snprintf(ssid, sizeof ssid, "%s", CONFIG_ECHOES_WIFI_SSID); snprintf(mdp, sizeof mdp, "%s", CONFIG_ECHOES_WIFI_MDP); }
    else nvs_lire("mdp", mdp, sizeof mdp);
    ev = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&ic) != ESP_OK) return 0;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, evenement, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, evenement, NULL);
    if (!ssid[0]) { ESP_LOGW(TAG, "pas de réseau Wi-Fi gardé"); return provisionner() && (esp_wifi_set_ps(WIFI_PS_NONE), 1); }
    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, ssid, strlen(ssid));
    memcpy(wc.sta.password, mdp, strlen(mdp));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    ESP_LOGI(TAG, "Wi-Fi : %s…", ssid);
    const EventBits_t b = xEventGroupWaitBits(ev, EV_IP | EV_ECHEC, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if (!(b & EV_IP)) { ESP_LOGW(TAG, "Wi-Fi : pas de connexion à %s", ssid); if (!provisionner()) return 0; }
    esp_wifi_set_ps(WIFI_PS_NONE);   /* débit plein pendant les téléchargements */
    return 1;
}

/* ---------- Heure ---------- */
static int heure_ntp(void) {
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&c);
    const int ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK;
    esp_netif_sntp_deinit();
    if (!ok) { ESP_LOGW(TAG, "heure : pas de réponse NTP"); return 0; }
    /* Heure locale du fuseau, gardée telle quelle (le système est en UTC0 : voir horloge.c) */
    const time_t t = time(NULL);
    struct tm l;
    setenv("TZ", CONFIG_ECHOES_FUSEAU, 1); tzset();
    localtime_r(&t, &l);
    setenv("TZ", "UTC0", 1); tzset();
    horloge_regler(&l);
    ESP_LOGI(TAG, "heure : %04d-%02d-%02d %02d:%02d:%02d", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min, l.tm_sec);
    return 1;
}

/* ---------- Serveur de l'Atelier ---------- */
static char hote[64];   /* « ip:port » */

static int http_tete(const char *nom, long long *mtime, long long *taille);
static int serveur_essayer(const char *h) {
    snprintf(hote, sizeof hote, "%s", h);
    long long m, t;
    return http_tete("histoire.scn", &m, &t);
}
static int serveur_chercher(void) {
    char h[64];
    if (nvs_lire("serveur", h, sizeof h) && serveur_essayer(h)) return 1;
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return 0;
    int un = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &un, sizeof un);
    struct timeval tv = { .tv_sec = 1 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(CONFIG_ECHOES_SERVEUR_PORT), .sin_addr.s_addr = htonl(INADDR_BROADCAST) };
    int trouve = 0;
    for (int k = 0; k < 6 && !trouve; k++) {
        sendto(s, "ECHOES?", 7, 0, (struct sockaddr *)&a, sizeof a);
        char r[32];
        struct sockaddr_in de;
        socklen_t dl = sizeof de;
        const int n = recvfrom(s, r, sizeof r - 1, 0, (struct sockaddr *)&de, &dl);
        int port;
        if (n > 0 && (r[n] = 0, sscanf(r, "ECHOES %d", &port) == 1)) {
            char ip[16];
            inet_ntoa_r(de.sin_addr, ip, sizeof ip);
            snprintf(h, sizeof h, "%s:%d", ip, port);
            trouve = serveur_essayer(h);
        }
    }
    close(s);
    if (trouve) { nvs_ecrire("serveur", hote); ESP_LOGI(TAG, "serveur de l'Atelier : %s", hote); }
    else ESP_LOGE(TAG, "serveur de l'Atelier introuvable (node serveur.js lancé, même réseau ?)");
    return trouve;
}

/* ---------- HTTP ---------- */
static long long ent_mtime, ent_taille;
static esp_err_t http_ev(esp_http_client_event_t *e) {
    if (e->event_id == HTTP_EVENT_ON_HEADER) {
        if (!strcasecmp(e->header_key, "X-Mtime")) ent_mtime = atoll(e->header_value);
        else if (!strcasecmp(e->header_key, "Content-Length")) ent_taille = atoll(e->header_value);
    }
    return ESP_OK;
}
static void url_de(char *u, int n, const char *nom) {
#if CONFIG_ECHOES_ENFANT
    if (!strcmp(nom, "musique.bin")) { snprintf(u, n, "http://%s/api/moteur/musique.bin", hote); return; }   /* commune */
#endif
    snprintf(u, n, "http://%s" URL_DIR "%s", hote, nom);
}
static int http_tete(const char *nom, long long *mtime, long long *taille) {
    char u[160];
    url_de(u, sizeof u, nom);
    esp_http_client_config_t c = { .url = u, .method = HTTP_METHOD_HEAD, .event_handler = http_ev, .timeout_ms = 4000 };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    ent_mtime = ent_taille = -1;
    const int ok = esp_http_client_perform(h) == ESP_OK && esp_http_client_get_status_code(h) == 200 && ent_mtime > 0;
    esp_http_client_cleanup(h);
    *mtime = ent_mtime; *taille = ent_taille;
    return ok;
}

/* ---------- Fichiers ---------- */
typedef struct { long long mtime, taille; } note_t;
static note_t notes[FICHIERS], serveur[FICHIERS];
static const char *const NOTES = SD_DIR "/wifi.txt";   /* « nom mtime taille » des fichiers reçus par le Wi-Fi */

static void notes_lire(void) {
    FILE *f = fopen(NOTES, "r");
    if (!f) return;
    char l[96], nom[40];
    long long m, t;
    while (fgets(l, sizeof l, f))
        if (sscanf(l, "%39s %lld %lld", nom, &m, &t) == 3)
            for (int k = 0; k < FICHIERS; k++) if (!strcmp(nom, NOMS[k])) { notes[k].mtime = m; notes[k].taille = t; }
    fclose(f);
}
static void notes_ecrire(void) {
    FILE *f = fopen(NOTES, "w");
    if (!f) return;
    for (int k = 0; k < FICHIERS; k++) if (notes[k].mtime > 0) fprintf(f, "%s %lld %lld\n", NOMS[k], notes[k].mtime, notes[k].taille);
    fclose(f);
}
static long long taille_de(const char *chemin) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    const long long t = ftell(f);
    fclose(f);
    return t;
}

static uint8_t *bloc;
static long long recu, total;   /* octets reçus de tous les fichiers à recevoir */
static int telecharger(int k) {
    char u[160], p[96];
    url_de(u, sizeof u, NOMS[k]);
    snprintf(p, sizeof p, SD_DIR "/%s.part", NOMS[k]);
    for (int essai = 0; essai < 3; essai++) {
        esp_http_client_config_t c = { .url = u, .event_handler = http_ev, .timeout_ms = 10000, .buffer_size = 4096 };
        esp_http_client_handle_t h = esp_http_client_init(&c);
        long long lu = 0;
        int ok = 0;
        FILE *f = NULL;
        if (esp_http_client_open(h, 0) == ESP_OK && esp_http_client_fetch_headers(h) >= 0 && esp_http_client_get_status_code(h) == 200
            && (f = fopen(p, "wb"))) {
            const int64_t t0 = esp_timer_get_time();
            int64_t vu = 0;
            for (;;) {
                const int n = esp_http_client_read(h, (char *)bloc, BLOC);
                if (n <= 0) break;
                if (fwrite(bloc, 1, n, f) != (size_t)n) break;
                lu += n;
                const int64_t now = esp_timer_get_time();
                if (now - vu > 500000) { vu = now; ecran(total > 0 ? (float)(recu + lu) / total : 0, C_VERT, C_VERT); }
            }
            ok = lu == serveur[k].taille;
            const float s = (esp_timer_get_time() - t0) / 1e6f;
            ESP_LOGI(TAG, "%s : %lld / %lld octets en %.1f s (%.2f Mo/s)", NOMS[k], lu, serveur[k].taille, s, s > 0 ? lu / s / 1048576 : 0);
        }
        if (f) { fflush(f); fsync(fileno(f)); fclose(f); }
        esp_http_client_cleanup(h);
        if (ok) { recu += lu; return 1; }
        ESP_LOGW(TAG, "%s : téléchargement raté (essai %d)", NOMS[k], essai + 1);
    }
    unlink(p);
    return 0;
}

/* Paquet léger reçu (monde_esp32.pak de la carte) écrit dans la partition « monde » */
static int flasher_leger(void) {
    const esp_partition_t *pt = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "monde");
    char p[96];
    snprintf(p, sizeof p, SD_DIR "/%s", NOMS[LEGER]);
    FILE *f = fopen(p, "rb");
    const long long t = taille_de(p);
    if (!pt || !f || t <= 0 || t > pt->size) { if (f) fclose(f); ESP_LOGE(TAG, "paquet léger : partition ou fichier absent"); return 0; }
    if (esp_partition_erase_range(pt, 0, (t + 4095) & ~4095) != ESP_OK) { fclose(f); return 0; }
    long long o = 0;
    int ok = 1;
    while (ok && o < t) {
        const int n = fread(bloc, 1, BLOC, f);
        if (n <= 0) { ok = 0; break; }
        ok = esp_partition_write(pt, o, bloc, n) == ESP_OK;
        o += n;
    }
    fclose(f);
    ESP_LOGI(TAG, "paquet léger : %lld octets écrits en flash%s", o, ok ? "" : " (ÉCHEC)");
    return ok;
}

static int fichiers(void) {
    notes_lire();
    int changes[FICHIERS] = { 0 }, n = 0;
    total = recu = 0;
    for (int k = 0; k < FICHIERS; k++) {
        if (!http_tete(NOMS[k], &serveur[k].mtime, &serveur[k].taille)) { ESP_LOGW(TAG, "%s : absent du serveur", NOMS[k]); continue; }
        char p[96];
        snprintf(p, sizeof p, SD_DIR "/%s", NOMS[k]);
        changes[k] = serveur[k].mtime != notes[k].mtime || serveur[k].taille != notes[k].taille || taille_de(p) != serveur[k].taille;
        if (changes[k]) { n++; total += serveur[k].taille; }
    }
    /* Le paquet léger et monde.pak vont ensemble : l'un sans l'autre, le jeu refuserait de démarrer */
    if (changes[LEGER] != changes[3] && serveur[LEGER].mtime > 0 && serveur[3].mtime > 0) {
        for (int k = 3; k <= LEGER; k++) if (!changes[k]) { changes[k] = 1; n++; total += serveur[k].taille; }
    }
    ESP_LOGI(TAG, "%d fichier(s) à recevoir, %.1f Mo", n, total / 1048576.0);
    if (!n) return 1;
    for (int k = 0; k < FICHIERS; k++) if (changes[k] && !telecharger(k)) {
        for (int j = 0; j < FICHIERS; j++) { char p[96]; snprintf(p, sizeof p, SD_DIR "/%s.part", NOMS[j]); unlink(p); }
        return 0;
    }
    /* Tout est reçu : mise en place ensemble */
    for (int k = 0; k < FICHIERS; k++) if (changes[k]) {
        char p[96], q[96];
        snprintf(p, sizeof p, SD_DIR "/%s.part", NOMS[k]);
        snprintf(q, sizeof q, SD_DIR "/%s", NOMS[k]);
        unlink(q);
        if (rename(p, q)) { ESP_LOGE(TAG, "%s : renommage impossible", NOMS[k]); return 0; }
        notes[k] = serveur[k];
        sdx_oublier(NOMS[k]);
    }
    notes_ecrire();
    if (changes[LEGER]) { if (!flasher_leger()) return 0; sdx_oublier("flash:monde_esp32.pak"); }
    return 1;
}

/* ---------- Le mode ---------- */
static void tache(void *arg) {
    bande = heap_caps_malloc(LCD_W * BANDE * 2, MALLOC_CAP_DMA);
    bloc = heap_caps_malloc(BLOC, MALLOC_CAP_DMA);
    lcd_set_brightness(0xD0);
    ESP_LOGI(TAG, "mode Transfert");
    nvs_pret();
    int ok = 0;
    ecran(0, C_BLEU, C_BLEU);
    if (wifi_connecter()) {
        ecran(0, C_CYAN, C_CYAN);
        heure_ntp();
        ecran(0, C_JAUNE, C_JAUNE);
        if (!sd_init()) ESP_LOGE(TAG, "pas de carte SD");
        else if (serveur_chercher()) ok = fichiers();
    }
    ecran(1, ok ? C_VERT : C_ROUGE, ok ? C_VERT : C_ROUGE);
    ESP_LOGI(TAG, "%s : retour au jeu", ok ? "transfert fini" : "ÉCHEC du transfert");
    vTaskDelay(pdMS_TO_TICKS(ok ? 1500 : 4000));
    esp_wifi_stop();
    horloge_garder();
    esp_restart();
}
void transfert_demarrer(void) {
    if (xTaskCreatePinnedToCore(tache, "transfert", 8192, NULL, 5, NULL, 0) != pdPASS) { ESP_LOGE(TAG, "tâche impossible"); esp_restart(); }
}

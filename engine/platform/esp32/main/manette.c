/*
 * Manette Bluetooth de la montre : la Poké Ball Plus (manette.h), pour le Game Boy (gameboy/) et Echoes. Protocole relevé dans pokeball-plus-4-windows (rna0,
 * licence MIT, github.com/rna0/pokeball-plus-4-windows) : la balle s'annonce « Pokemon PBP » (bouton du haut appuyé :
 * elle clignote en blanc) ; service 6675e16c-f36d-4567-bb55-6b51e27a23e5, caractéristique …e6 en notification. Trame
 * de 5 octets au moins : octet 1 = 1 bouton du haut, 2 clic du stick, 3 les deux ; X du stick = quartet bas de l'octet 3
 * et quartet haut de l'octet 2 (32 à 192), Y = octet 4 (36 à 180, petit en haut).
 *
 * État brut (boutons, stick ramené à -100..100), rangé dans un seul mot (lu par les autres tâches sans verrou) ; chaque
 * application en fait ses boutons (Game Boy : haut = A, clic = B, stick = croix). NimBLE en central (rôles central et observateur seulement, pile en PSRAM) :
 * recherche active jusqu'à la balle (nom annoncé, ou appareil déjà appairé ; sans filtre des doublons, par tranches de 10 s), connexion, service, caractéristique et son
 * descripteur 0x2902, abonnement ; si la balle exige le chiffrement, appairage « just works » (liaison gardée en NVS),
 * puis abonnement refait. Ensuite, la batterie (service standard 0x180F, niveau en % 0x2A19) : lue, puis suivie en
 * notification si la balle le permet. Déconnectée : la recherche reprend.
 *
 * Commandes (relevées par essais depuis le PC le 31/10/2026, rien de publié) : …e7 (rapport de sortie, écriture sans
 * réponse), trame [commande][longueur u16][données] ; réponses sur …e8 (non lues ici). 03 02 00 NN 00 joue l'activité
 * NN rangée dans la balle (lumière, vibration, son) : la 3 n'est qu'une légère vibration (16 et 17 ont des sons, 18 à
 * 22 sont les cris du Pokémon de la balle). Une commande qui arrive pendant la précédente (environ 1 s) est perdue.
 * Effets sur mesure (lumière et vibrations de 200 Hz écrites dans la base de la balle par tools/pokeball/pbp_effets.py) :
 * l'effet e (ENG_EFFET_* du moteur : validation, coup reçu, K.O., crash, combat, victoire, soin, niveau, défaite) joue
 * l'activité 70 + e ; lumière d'ambiance (activité 100, cyan de l'interface pendant 10 s) relancée à la fin de chaque
 * effet et toutes les 8 s (manette_tick), éteinte (activité 101) quand la montre lâche la balle ; à la connexion,
 * leurs octets d'état (flash 0x55046 à 0x55065, 00 = valide) sont lus par la
 * commande 0b, réponse sur …e8 ; sans eux (autre balle), la légère vibration de la balle (activité 13, sans lumière ; rien pour le soin et le niveau).
 */
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "manette.h"

static const char *TAG = "manette";
#define NOM "Pokemon PBP"
/* UUID de 128 bits, octets de poids faible d'abord (ordre de NimBLE) */
static const ble_uuid128_t U_SVC = BLE_UUID128_INIT(0xe5, 0x23, 0x7a, 0xe2, 0x51, 0x6b, 0x55, 0xbb, 0x67, 0x45, 0x6d, 0xf3, 0x6c, 0xe1, 0x75, 0x66);
static const ble_uuid128_t U_ENTREES = BLE_UUID128_INIT(0xe6, 0x23, 0x7a, 0xe2, 0x51, 0x6b, 0x55, 0xbb, 0x67, 0x45, 0x6d, 0xf3, 0x6c, 0xe1, 0x75, 0x66);
static const ble_uuid128_t U_COMMANDES = BLE_UUID128_INIT(0xe7, 0x23, 0x7a, 0xe2, 0x51, 0x6b, 0x55, 0xbb, 0x67, 0x45, 0x6d, 0xf3, 0x6c, 0xe1, 0x75, 0x66);
static const ble_uuid128_t U_REPONSES = BLE_UUID128_INIT(0xe8, 0x23, 0x7a, 0xe2, 0x51, 0x6b, 0x55, 0xbb, 0x67, 0x45, 0x6d, 0xf3, 0x6c, 0xe1, 0x75, 0x66);
#define ACT_VIBRE 13         /* activité de la balle : légère vibration, sans son ni lumière (la 3, même vibration, allume le
                                 motif 10 en boucle sans fin : la balle ne s'éteignait plus) */
#define ACT_EFFETS 70        /* activités des effets sur mesure : 70 à 70 + MANETTE_EFFETS - 1 (tools/pokeball) */
#define ACT_AMBIANCE 100     /* lumière d'ambiance : cyan de l'interface pendant 10 s (pas en boucle sans fin : la balle
                                 la jouerait encore, sans jamais se mettre en veille, après la perte de la montre) */
#define ACT_ETEINT 101       /* LED éteinte : jouée quand la montre lâche la balle */
#define AMBIANCE_US 8000000  /* l'ambiance est rejouée toutes les 8 s */
#define ETAT_PERSO (0x55000 + ACT_EFFETS)   /* octets d'état des activités 70 à 100 dans la flash de la balle */
#define ETAT_N (ACT_ETEINT - ACT_EFFETS + 1)
#define VIBRE_US 400000      /* une commande toutes les 0,4 s au plus (la balle perd celle qui arrive pendant la précédente) */
/* Durée de chaque effet (ms, lumière ou vibration la plus longue, marge comprise) : l'ambiance revient après */
static const uint16_t DUREE_MS[MANETTE_EFFETS] = { 500, 700, 1300, 1100, 1300, 1400, 1400, 1900, 2600, 400 };
/* Stick : le centre est relevé à chaque connexion (moyenne des CALIBRE premières trames sans bouton) : celui de la balle
   de l'utilisateur était vers 112 et 130 (pas 108 : le bas était presque tenu au repos). Plages : COURSE0 de part et
   d'autre du centre, puis élargies à mesure que le stick va plus loin : le bout réel de chaque direction vaut 100 %
   (avec les plages de pokeball-plus-4-windows, X 32 à 192 et Y 36 à 180, le haut de cette balle restait sous 80 % :
   Sera ne courait pas vers le haut) */
#define CALIBRE 8
#define COURSE0 40
static int x_min = 32, x_max = 192, y_min = 36, y_max = 180, x_c = 112, y_c = 108;
static int cal_n, cal_x, cal_y;

static bool demarre;
static uint8_t own_addr_type;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE, svc_fin, h_val, h_cmd, h_rep, h_rep_cccd, h_cccd, h_batt;
static volatile bool connectee, perso;   /* perso : la balle a les effets sur mesure */
static volatile bool ambiance, ambiance_due;   /* ambiance voulue ; à relancer à la fin de l'effet en cours */
static volatile int64_t fin_effet, der_cmd, der_ambiance;
static volatile int batterie = -1;
static volatile uint32_t etat;   /* boutons, x + 128, y + 128 : un octet chacun */
static volatile int64_t vue;
static volatile int mes_n, mes_ecart;   /* mesure : trames reçues et plus grand écart (µs) depuis manette_mesure */
static volatile int64_t mes_t;

static int evenement(struct ble_gap_event *e, void *arg);

/* Recherche active (le nom est souvent dans la réponse), sans filtre des doublons, par tranches de RECHERCHE_MS : le filtre
   du contrôleur ne rapporte une adresse qu'une fois par recherche, et la balle vue une première fois sans son nom
   (allumage, appairage) n'était plus jamais rapportée ; elle n'était trouvée qu'en relançant MANETTE */
#define RECHERCHE_MS 10000
static void chercher(void) {
    if (!demarre || conn != BLE_HS_CONN_HANDLE_NONE) return;
    struct ble_gap_disc_params p = { .filter_duplicates = 0, .passive = 0 };
    const int rc = ble_gap_disc(own_addr_type, RECHERCHE_MS, &p, evenement, NULL);
    if (rc && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "recherche : %d", rc);
}

static int borne(int v) { return v < -100 ? -100 : v > 100 ? 100 : v; }
static int axe(int v, int c, int mn, int mx) {   /* -100 à 100 depuis le centre, course propre à chaque côté */
    const int d = v - c, course = d < 0 ? c - mn : mx - c;
    return course > 8 ? borne(d * 100 / course) : 0;
}
static uint32_t ranger(int b, int x, int y) { return (uint32_t)b | (uint32_t)(x + 128) << 8 | (uint32_t)(y + 128) << 16; }
/* Trame de la balle → état ; « vue » change avec un bouton ou un cran du stick (quart de course) */
static void trame(const uint8_t *v, int n) {
    const int64_t t = esp_timer_get_time();
    if (mes_t && t - mes_t > mes_ecart) mes_ecart = (int)(t - mes_t);
    mes_t = t; mes_n++;
    if (n < 5) return;
    const int b = (v[1] == 1 || v[1] == 3 ? MANETTE_HAUT : 0) | (v[1] == 2 || v[1] == 3 ? MANETTE_CLIC : 0);
    const int xr = (v[3] & 0x0F) << 4 | v[2] >> 4, yr = v[4];   /* 8 bits de poids fort des deux axes de 12 bits */
    if (cal_n < CALIBRE) {   /* centre relevé : stick tenu pour nul en attendant */
        if (!b) {
            cal_x += xr; cal_y += yr;
            if (++cal_n == CALIBRE) {
                x_c = cal_x / CALIBRE; y_c = cal_y / CALIBRE;
                x_min = x_c - COURSE0; x_max = x_c + COURSE0; y_min = y_c - COURSE0; y_max = y_c + COURSE0;
                ESP_LOGI(TAG, "centre du stick : %d %d", x_c, y_c);
            }
        }
        etat = ranger(b, 0, 0);
        return;
    }
    if (xr < x_min) x_min = xr;
    if (xr > x_max) x_max = xr;
    if (yr < y_min) y_min = yr;
    if (yr > y_max) y_max = yr;
    const int x = axe(xr, x_c, x_min, x_max), y = axe(yr, y_c, y_min, y_max);
    const uint32_t avant = etat, e = ranger(b, x, y);
    etat = e;
    if ((avant & 0xFF) != (e & 0xFF)) ESP_LOGD(TAG, "trame %02x %02x %02x %02x %02x : boutons %d, stick %d %d", v[0], v[1], v[2], v[3], v[4], b, x, y);
    if ((avant & 0xFF) != (e & 0xFF) || (int)((avant >> 8 & 0xFF) - 128) / 25 != x / 25 || (int)((avant >> 16 & 0xFF) - 128) / 25 != y / 25)
        vue = esp_timer_get_time();
}

/* Batterie : service 0x180F, niveau 0x2A19 lu puis suivi (descripteur juste après la valeur ; refus sans conséquence) */
static int batt_lue(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    uint8_t v;
    if (!err->status && attr && !os_mbuf_copydata(attr->om, 0, 1, &v)) { batterie = v; ESP_LOGI(TAG, "batterie : %d %%", v); }
    static const uint8_t NOTIF[2] = { 1, 0 };
    if (h_batt) ble_gattc_write_flat(c, h_batt + 1, NOTIF, sizeof NOTIF, NULL, NULL);
    return 0;
}
static int batt_chr(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_chr *ch, void *arg) {
    if (!err->status && ch) h_batt = ch->val_handle;
    if (err->status == BLE_HS_EDONE && h_batt) ble_gattc_read(c, h_batt, batt_lue, NULL);
    return 0;
}
static int batt_svc(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_svc *s, void *arg) {
    if (!err->status && s) ble_gattc_disc_chrs_by_uuid(c, s->start_handle, s->end_handle, BLE_UUID16_DECLARE(0x2A19), batt_chr, NULL);
    return 0;
}

/* Inventaire du GATT de la balle, en console à chaque connexion : services, caractéristiques et propriétés (0x08
   écriture, 0x04 écriture sans réponse, 0x10 notification), pour chercher la commande de sa lumière et de sa vibration,
   qui n'est pas documentée (pokeball-plus-4-windows ne fait que lire) */
#define INV_MAX 12
static struct { uint16_t debut, fin; } inv[INV_MAX];
static int inv_n, inv_k;
static int inv_chr(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_chr *ch, void *arg) {
    char u[BLE_UUID_STR_LEN];
    if (!err->status && ch) ESP_LOGI(TAG, "  caractéristique %s : valeur %u, propriétés 0x%02x", ble_uuid_to_str(&ch->uuid.u, u), ch->val_handle, ch->properties);
    if (err->status == BLE_HS_EDONE && ++inv_k < inv_n) ble_gattc_disc_all_chrs(c, inv[inv_k].debut, inv[inv_k].fin, inv_chr, NULL);
    return 0;
}
static int inv_svc(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_svc *sv, void *arg) {
    char u[BLE_UUID_STR_LEN];
    if (!err->status && sv && inv_n < INV_MAX) {
        ESP_LOGI(TAG, "service %d %s : %u à %u", inv_n, ble_uuid_to_str(&sv->uuid.u, u), sv->start_handle, sv->end_handle);
        inv[inv_n].debut = sv->start_handle; inv[inv_n].fin = sv->end_handle; inv_n++;
    }
    if (err->status == BLE_HS_EDONE && inv_n) { inv_k = 0; ble_gattc_disc_all_chrs(c, inv[0].debut, inv[0].fin, inv_chr, NULL); }
    return 0;
}

static void jouer(int act) {   /* jouer l'activité (u16) */
    const uint8_t j[5] = { 0x03, 2, 0, act & 0xFF, act >> 8 };
    der_cmd = esp_timer_get_time();
    ble_gattc_write_no_rsp_flat(conn, h_cmd, j, sizeof j);
}
void manette_effet(int effet) {
    const int64_t t = esp_timer_get_time();
    if (!connectee || !h_cmd || effet < 0 || effet >= MANETTE_EFFETS || t - der_cmd < VIBRE_US) return;
    if (!perso && (effet == MANETTE_SOIN || effet == MANETTE_NIVEAU)) return;   /* sans lumière sur mesure : rien */
    jouer(perso ? ACT_EFFETS + effet : ACT_VIBRE);
    fin_effet = t + DUREE_MS[effet] * 1000LL; ambiance_due = true;
}
void manette_ambiance(bool on) { ambiance = on; ambiance_due = on; }
void manette_tick(void) {
    if (!ambiance || !perso || !connectee || !h_cmd) return;
    const int64_t t = esp_timer_get_time();
    if (t < fin_effet || t - der_cmd < VIBRE_US || (!ambiance_due && t - der_ambiance < AMBIANCE_US)) return;
    ambiance_due = false; der_ambiance = t;
    jouer(ACT_AMBIANCE);
}

/* Réponses de la balle (…e8) : celle de la lecture des octets d'état des effets sur mesure (activités 70 à 100 : 42
   octets de réponse, d'où le MTU négocié à la connexion), demandée une fois abonné ; puis l'effet de validation pour
   dire que la balle est là */
static int rep_abonne(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    static const uint8_t LIRE[11] = { 0x0b, 8, 0, ETAT_PERSO & 0xFF, ETAT_PERSO >> 8 & 0xFF, ETAT_PERSO >> 16, 0, ETAT_N, 0, 0, 0 };
    if (err->status || ble_gattc_write_no_rsp_flat(c, h_cmd, LIRE, sizeof LIRE)) manette_effet(MANETTE_VALIDER);
    return 0;
}
static void reponse(const uint8_t *v, int n) {
    if (v[0] != 0x06 || n < 11 || (v[3] | v[4] << 8 | v[5] << 16) != ETAT_PERSO) return;   /* lecture 0b : [pos][taille][données] */
    bool ok = n >= 11 + ETAT_N;   /* réponse coupée (MTU non négocié) : pas d'effets sur mesure */
    for (int k = 0; ok && k < MANETTE_EFFETS; k++) ok = !v[11 + k];
    if (ok) ok = !v[11 + ACT_AMBIANCE - ACT_EFFETS] && !v[11 + ACT_ETEINT - ACT_EFFETS];   /* ambiance, éteint ; entre les deux, celles de la balle */
    perso = ok;
    ESP_LOGI(TAG, "effets sur mesure : %s", perso ? "oui (activités 70 à 79, ambiance 100, éteint 101)" : "non (activité 13)");
    manette_effet(MANETTE_VALIDER);
}

static int ecrit(uint16_t c, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    if (!err->status) {
        connectee = true; vue = esp_timer_get_time();
        ESP_LOGI(TAG, "Poké Ball Plus connectée");
        static const uint8_t NOTIF[2] = { 1, 0 };
        if (!h_cmd || !h_rep_cccd || ble_gattc_write_flat(c, h_rep_cccd, NOTIF, sizeof NOTIF, rep_abonne, NULL)) manette_effet(MANETTE_VALIDER);
        ble_gattc_disc_svc_by_uuid(c, BLE_UUID16_DECLARE(0x180F), batt_svc, NULL);
        inv_n = 0;
        ble_gattc_disc_all_svcs(c, inv_svc, NULL);
        return 0;
    }
    if (err->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) || err->status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC)) {
        ESP_LOGI(TAG, "abonnement refusé sans chiffrement : appairage");
        ble_gap_security_initiate(c);   /* l'abonnement est refait au chiffrement (BLE_GAP_EVENT_ENC_CHANGE) */
        return 0;
    }
    ESP_LOGW(TAG, "abonnement : %d", err->status);
    ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}
static void abonner(void) {
    static const uint8_t NOTIF[2] = { 1, 0 };
    if (ble_gattc_write_flat(conn, h_cccd, NOTIF, sizeof NOTIF, ecrit, NULL)) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}
/* Descripteurs depuis la valeur des entrées jusqu'au bout du service : le premier 0x2902 est le leur (les suivants sont
   ceux des autres caractéristiques du service : prendre le dernier abonnait à …e8, et rien n'arrivait en jeu) */
static int descripteur(uint16_t c, const struct ble_gatt_error *err, uint16_t chr, const struct ble_gatt_dsc *d, void *arg) {
    if (!err->status && d && !ble_uuid_cmp(&d->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16))) {
        if (!h_cccd) h_cccd = d->handle;
        else if (h_rep && d->handle > h_rep && !h_rep_cccd) h_rep_cccd = d->handle;   /* celui des réponses (…e8) */
    }
    if (err->status == BLE_HS_EDONE) {
        if (!h_cccd) h_cccd = h_val + 1;   /* habituellement juste après la valeur */
        abonner();
    }
    return 0;
}
static int caracteristique(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_chr *ch, void *arg) {
    if (!err->status && ch && !ble_uuid_cmp(&ch->uuid.u, &U_ENTREES.u)) h_val = ch->val_handle;
    if (!err->status && ch && !ble_uuid_cmp(&ch->uuid.u, &U_COMMANDES.u)) h_cmd = ch->val_handle;
    if (!err->status && ch && !ble_uuid_cmp(&ch->uuid.u, &U_REPONSES.u)) h_rep = ch->val_handle;
    if (err->status == BLE_HS_EDONE) {
        if (!h_val || ble_gattc_disc_all_dscs(c, h_val, svc_fin, descripteur, NULL)) {
            ESP_LOGW(TAG, "caractéristique des entrées absente");
            ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    return 0;
}
static int service(uint16_t c, const struct ble_gatt_error *err, const struct ble_gatt_svc *s, void *arg) {
    static uint16_t debut;
    if (!err->status && s) { debut = s->start_handle; svc_fin = s->end_handle; }
    if (err->status == BLE_HS_EDONE) {
        if (!debut || ble_gattc_disc_all_chrs(c, debut, svc_fin, caracteristique, NULL)) {   /* entrées et commandes */
            ESP_LOGW(TAG, "service de la Poké Ball Plus absent");
            ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
        }
        debut = 0;
    }
    return 0;
}

/* Une annonce : la balle (son nom), ou un appareil déjà appairé avec la montre ; chaque nom vu est noté en console une
   fois (la recherche ne rapporte qu'une fois chaque appareil) */
/* Adresse de la balle, retenue à la première connexion (NVS « manette ») : elle revient ensuite sans son nom, ou par
   une annonce dirigée vers la montre */
static ble_addr_t balle;
static bool balle_connue;
static void balle_lire(void) {
    nvs_handle_t h;
    size_t n = sizeof balle;
    if (nvs_open("manette", NVS_READONLY, &h) != ESP_OK) return;
    balle_connue = nvs_get_blob(h, "balle", &balle, &n) == ESP_OK && n == sizeof balle;
    nvs_close(h);
}
static void balle_noter(const ble_addr_t *a) {
    if (balle_connue && !ble_addr_cmp(&balle, a)) return;
    balle = *a; balle_connue = true;
    nvs_handle_t h;
    if (nvs_open("manette", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "balle", &balle, sizeof balle);
    nvs_commit(h);
    nvs_close(h);
}

static ble_addr_t vus[16];   /* adresses déjà notées en console (les plus récentes) */
static int nvus;
static bool deja_vu(const ble_addr_t *a) {
    for (int k = 0; k < (nvus < 16 ? nvus : 16); k++) if (!ble_addr_cmp(&vus[k], a)) return true;
    vus[nvus++ % 16] = *a;
    return false;
}
static bool cible(const struct ble_gap_disc_desc *d) {
    struct ble_hs_adv_fields f;
    const bool lu = !ble_hs_adv_parse_fields(&f, d->data, d->length_data);
    if (lu && f.name && f.name_len && !deja_vu(&d->addr)) ESP_LOGI(TAG, "vu : « %.*s » (%d dBm)", f.name_len, (const char *)f.name, d->rssi);
    if (lu && f.name && f.name_len == strlen(NOM) && !memcmp(f.name, NOM, f.name_len)) return true;
    if (d->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) { ESP_LOGI(TAG, "annonce dirigée vers la montre"); return true; }
    if (balle_connue && !ble_addr_cmp(&balle, &d->addr)) return true;
    ble_addr_t pairs[4];
    int n = 0;
    if (ble_store_util_bonded_peers(pairs, &n, 4)) return false;
    for (int k = 0; k < n; k++) if (!ble_addr_cmp(&pairs[k], &d->addr)) return true;
    return false;
}

/* Intervalle de connexion de 7,5 à 15 ms (unités de 1,25 ms ; délai de supervision 2 s) : la balle envoie ses trames en
   continu ; à 50 ms (défaut de NimBLE), Echoes n'en recevait qu'une par intervalle, 20 par seconde (mesure du 31/10) :
   la file de la balle se remplissait, touches vues 2 s après l'appui, et les réponses GATT attendaient derrière elle
   (déconnexion au bout des 30 s d'ATT) */
static const struct ble_gap_conn_params PARAMS = { .scan_itvl = 16, .scan_window = 16, .itvl_min = 6, .itvl_max = 12, .latency = 0, .supervision_timeout = 200 };

static int evenement(struct ble_gap_event *e, void *arg) {
    struct ble_gap_conn_desc d;
    switch (e->type) {
    case BLE_GAP_EVENT_DISC:
        if (!cible(&e->disc)) break;
        ESP_LOGI(TAG, "Poké Ball Plus trouvée (%d dBm) : connexion", e->disc.rssi);
        ble_gap_disc_cancel();
        if (ble_gap_connect(own_addr_type, &e->disc.addr, 30000, &PARAMS, evenement, NULL)) chercher();
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        chercher();
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status) { chercher(); break; }
        conn = e->connect.conn_handle; h_val = h_cmd = h_rep = h_rep_cccd = h_cccd = h_batt = 0; perso = false;
        cal_n = cal_x = cal_y = 0;
        if (ble_gap_conn_find(conn, &d) == 0) {
            balle_noter(&d.peer_id_addr);
            ESP_LOGI(TAG, "connexion : intervalle %d × 1,25 ms, latence %d, délai %d × 10 ms", d.conn_itvl, d.conn_latency, d.supervision_timeout);
        }
        ble_gattc_exchange_mtu(conn, NULL, NULL);   /* réponses de plus de 20 octets (lecture des octets d'état) */
        if (ble_gattc_disc_svc_by_uuid(conn, &U_SVC.u, service, NULL)) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Poké Ball Plus déconnectée (%d)", e->disconnect.reason);
        conn = BLE_HS_CONN_HANDLE_NONE; connectee = false; batterie = -1; etat = ranger(0, 0, 0); vue = esp_timer_get_time();
        chercher();
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "chiffrement : %d", e->enc_change.status);
        if (!e->enc_change.status && h_cccd && !connectee) abonner();
        break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:   /* la balle demande d'autres réglages : notés, acceptés */
        ESP_LOGI(TAG, "la balle demande : intervalle %d à %d × 1,25 ms, latence %d", e->conn_update_req.peer_params->itvl_min, e->conn_update_req.peer_params->itvl_max,
                 e->conn_update_req.peer_params->latency);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (ble_gap_conn_find(e->conn_update.conn_handle, &d) == 0)
            ESP_LOGI(TAG, "connexion changée (%d) : intervalle %d × 1,25 ms, latence %d, délai %d × 10 ms", e->conn_update.status, d.conn_itvl, d.conn_latency, d.supervision_timeout);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:   /* la balle a oublié la montre : ancien appairage effacé, on recommence */
        if (ble_gap_conn_find(e->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (e->notify_rx.attr_handle == h_val) {
            uint8_t v[16];
            const int n = OS_MBUF_PKTLEN(e->notify_rx.om) < (int)sizeof v ? OS_MBUF_PKTLEN(e->notify_rx.om) : (int)sizeof v;
            if (!os_mbuf_copydata(e->notify_rx.om, 0, n, v)) trame(v, n);
        } else if (h_rep && e->notify_rx.attr_handle == h_rep) {
            uint8_t v[48];
            const int n = OS_MBUF_PKTLEN(e->notify_rx.om) < (int)sizeof v ? OS_MBUF_PKTLEN(e->notify_rx.om) : (int)sizeof v;
            if (!os_mbuf_copydata(e->notify_rx.om, 0, n, v)) reponse(v, n);
        } else if (h_batt && e->notify_rx.attr_handle == h_batt) {
            uint8_t v;
            if (!os_mbuf_copydata(e->notify_rx.om, 0, 1, &v)) batterie = v;
        }
        break;
    default:
        break;
    }
    return 0;
}

static void synchro(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    ESP_LOGI(TAG, "Bluetooth prêt : recherche");
    chercher();
}
static void tache_hote(void *arg) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);   /* store/config (pas d'en-tête public) */

bool manette_demarrer(void) {
    if (demarre) return true;
    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "NimBLE impossible (mémoire)"); return false; }
    balle_lire();
    ble_hs_cfg.sync_cb = synchro;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gap_device_name_set("Echoes Montre");
    ble_store_config_init();
    demarre = true;
    nimble_port_freertos_init(tache_hote);
    ESP_LOGI(TAG, "recherche de la Poké Ball Plus (bouton du haut : elle clignote en blanc)");
    return true;
}

void manette_arreter(void) {
    if (!demarre) return;
    demarre = false;
    if (connectee && perso && h_cmd) { jouer(ACT_ETEINT); vTaskDelay(pdMS_TO_TICKS(150)); }   /* LED éteinte avant de partir */
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    ble_gap_disc_cancel();
    nimble_port_stop();
    nimble_port_deinit();
    conn = BLE_HS_CONN_HANDLE_NONE; connectee = false; batterie = -1; etat = ranger(0, 0, 0);
}

bool manette_active(void) { return demarre; }
bool manette_connectee(void) { return connectee; }
int manette_batterie(void) { return connectee ? batterie : -1; }
manette_t manette_etat(void) {
    const uint32_t e = etat;
    if (!e) return (manette_t){ 0, 0, 0 };
    return (manette_t){ (int)(e & 0xFF), (int)(e >> 8 & 0xFF) - 128, (int)(e >> 16 & 0xFF) - 128 };
}
int64_t manette_vue(void) { return vue; }
void manette_mesure(int *trames, int *ecart_ms) { *trames = mes_n; *ecart_ms = mes_ecart / 1000; mes_n = 0; mes_ecart = 0; }

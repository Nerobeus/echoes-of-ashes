/*
 * Liaison Bluetooth montre ↔ téléphone (montre/PROTOCOLE.md), NimBLE en rôle périphérique : la montre diffuse
 * « Echoes Montre », le téléphone se connecte, s'appaire (just works, liaison gardée : ble_store_config_init) et écrit
 * HEURE, NOTIF, EFFACER, APPEL ; la montre notifie ACTION et ETAT. Service batterie 0x180F. Diffusion lente (1 s) en
 * veille, rapide (100 ms) les secondes qui suivent un réveil ; la montre ne cherche jamais.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble.h"

static const char *TAG = "ble";
static const ble_rappels_t *R;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t h_action, h_etat, h_batt;
static bool rapide = true, demarre;
static uint8_t batt = 100;
static char etat_txt[32] = "100\x1f" "0\x1f" "1";
static uint8_t own_addr_type;

/* UUID e5c0a5eX-6d6f-6e74-7265-4563686f6573 (octets dans l'ordre de NimBLE : poids faible d'abord) */
#define UUID_MONTRE(x) BLE_UUID128_INIT(0x73, 0x65, 0x6f, 0x68, 0x63, 0x45, 0x65, 0x72, 0x74, 0x6e, 0x6f, 0x6d, (x), 0xa5, 0xc0, 0xe5)
static const ble_uuid128_t U_SVC = UUID_MONTRE(0xe0), U_HEURE = UUID_MONTRE(0xe1), U_NOTIF = UUID_MONTRE(0xe2),
    U_EFFACER = UUID_MONTRE(0xe3), U_APPEL = UUID_MONTRE(0xe4), U_ACTION = UUID_MONTRE(0xe5), U_ETAT = UUID_MONTRE(0xe6);

/* Champs séparés par 0x1F, copiés dans b (taille n) : pointeurs dans b */
static int champs(char *b, char **f, int nmax) {
    int k = 0;
    f[k++] = b;
    for (char *p = b; *p && k < nmax; p++) if (*p == 0x1F) { *p = 0; f[k++] = p + 1; }
    for (int i = k; i < nmax; i++) f[i] = "";
    return k;
}

static int acces(uint16_t ch, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const ble_uuid_t *u = ctxt->chr ? ctxt->chr->uuid : NULL;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (u && !ble_uuid_cmp(u, &U_ETAT.u)) return os_mbuf_append(ctxt->om, etat_txt, strlen(etat_txt)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        if (u && !ble_uuid_cmp(u, BLE_UUID16_DECLARE(0x2A19))) return os_mbuf_append(ctxt->om, &batt, 1) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
        return 0;   /* ACTION : rien à lire */
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || !u) return BLE_ATT_ERR_UNLIKELY;
    static char b[700];
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, b, sizeof b - 1, &n)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    b[n] = 0;
    if (!ble_uuid_cmp(u, &U_HEURE.u)) {
        if (n < 10) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        int64_t t; int16_t tz;
        memcpy(&t, b, 8); memcpy(&tz, b + 8, 2);
        if (R && R->heure) R->heure(t, tz);
    } else if (!ble_uuid_cmp(u, &U_NOTIF.u)) {
        char *f[5];
        champs(b, f, 5);
        long long h = 0;
        sscanf(f[4], "%lld", &h);
        if (R && R->notif) R->notif(f[0], f[1], f[2], f[3], h);
    } else if (!ble_uuid_cmp(u, &U_EFFACER.u)) {
        if (R && R->effacer) R->effacer(b);
    } else if (!ble_uuid_cmp(u, &U_APPEL.u)) {
        char *f[3];
        champs(b, f, 3);
        if (R && R->appel) R->appel(f[0], f[1], f[2]);
    }
    return 0;
}

static const struct ble_gatt_svc_def SERVICES[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &U_SVC.u, .characteristics = (struct ble_gatt_chr_def[]) {
        { .uuid = &U_HEURE.u, .access_cb = acces, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC },
        { .uuid = &U_NOTIF.u, .access_cb = acces, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC },
        { .uuid = &U_EFFACER.u, .access_cb = acces, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC },
        { .uuid = &U_APPEL.u, .access_cb = acces, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC },
        { .uuid = &U_ACTION.u, .access_cb = acces, .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ, .val_handle = &h_action },
        { .uuid = &U_ETAT.u, .access_cb = acces, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &h_etat },
        { 0 } } },
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180F), .characteristics = (struct ble_gatt_chr_def[]) {
        { .uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = acces, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &h_batt },
        { 0 } } },
    { 0 },
};

static int evenement_gap(struct ble_gap_event *e, void *arg);

static void diffuser(void) {
    if (!demarre || conn != BLE_HS_CONN_HANDLE_NONE) return;
    ble_gap_adv_stop();
    struct ble_hs_adv_fields f = { 0 };
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *nom = ble_svc_gap_device_name();
    f.name = (const uint8_t *)nom; f.name_len = strlen(nom); f.name_is_complete = 1;
    ble_gap_adv_set_fields(&f);
    struct ble_hs_adv_fields r = { 0 };   /* l'UUID du service dans la réponse au balayage (31 octets par paquet) */
    r.uuids128 = &U_SVC; r.num_uuids128 = 1; r.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&r);
    struct ble_gap_adv_params p = { .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN };
    p.itvl_min = p.itvl_max = rapide ? 160 : 1600;   /* × 0,625 ms : 100 ms ou 1 s */
    const int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, evenement_gap, NULL);
    if (rc) ESP_LOGW(TAG, "diffusion : %d", rc);
}

static int evenement_gap(struct ble_gap_event *e, void *arg) {
    struct ble_gap_conn_desc d;
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status == 0) {
            conn = e->connect.conn_handle;
            ESP_LOGI(TAG, "téléphone connecté");
            if (R && R->connexion) R->connexion(true);
        } else diffuser();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "téléphone déconnecté (%d)", e->disconnect.reason);
        conn = BLE_HS_CONN_HANDLE_NONE;
        if (R && R->connexion) R->connexion(false);
        diffuser();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        diffuser();
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:   /* le téléphone a oublié la montre : on efface l'ancien appairage, on recommence */
        if (ble_gap_conn_find(e->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "chiffrement : %d", e->enc_change.status);
        break;
    default:
        break;
    }
    return 0;
}

static void synchro(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    diffuser();
}

static void tache_hote(void *arg) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);   /* store/config (pas d'en-tête public) */

void ble_demarrer(const ble_rappels_t *r) {
    if (demarre) return;
    R = r;
    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "NimBLE impossible"); return; }
    ble_hs_cfg.sync_cb = synchro;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(SERVICES);
    ble_gatts_add_svcs(SERVICES);
    ble_svc_gap_device_name_set("Echoes Montre");
    ble_store_config_init();
    demarre = true;
    nimble_port_freertos_init(tache_hote);
}

void ble_arreter(void) {
    if (!demarre) return;
    demarre = false;
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    ble_gap_adv_stop();
    nimble_port_stop();
    nimble_port_deinit();
    conn = BLE_HS_CONN_HANDLE_NONE;
}

bool ble_connecte(void) { return conn != BLE_HS_CONN_HANDLE_NONE; }

void ble_diffusion_rapide(bool oui) {
    if (oui == rapide) return;
    rapide = oui;
    if (demarre && conn == BLE_HS_CONN_HANDLE_NONE) diffuser();
}

void ble_action(const char *texte) {
    if (!demarre || conn == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(texte, strlen(texte));
    if (om) ble_gatts_notify_custom(conn, h_action, om);
}

void ble_etat(int batterie, bool charge) {
    batt = (uint8_t)(batterie < 0 ? 0 : batterie > 100 ? 100 : batterie);
    snprintf(etat_txt, sizeof etat_txt, "%d\x1f%d\x1f" "1", batt, charge ? 1 : 0);
    if (!demarre || conn == BLE_HS_CONN_HANDLE_NONE) return;
    ble_gatts_chr_updated(h_etat);
    ble_gatts_chr_updated(h_batt);
}

void ble_oublier(void) {
    if (!demarre) return;
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
}

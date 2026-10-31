/*
 * Tactile en I2C : FT3168 (adresse 0x38, carte 2,06" de test, repris du projet tiny386) ou CST9217 (adresse 0x5A, carte
 * 1,75" : protocole de TouchDrvCST92xx de SensorLib, celle de l'exemple Waveshare 10_Touch_CST9217). S'il ne répond
 * pas, aucun doigt n'est lu. Sa broche INT (active basse) sert aussi au réveil de la veille (veille.c).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "board.h"
#include "board_power.h"
#include "touch.h"

static const char *TAG = "tactile";
static bool ok;

static void reset_tp(int attente_ms) {
    gpio_config_t rst = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_TP_RST };
    gpio_config(&rst);
    gpio_set_level(PIN_TP_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1); vTaskDelay(pdMS_TO_TICKS(attente_ms));
    gpio_config_t irq = { .mode = GPIO_MODE_INPUT, .pin_bit_mask = 1ULL << PIN_TP_INT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&irq);
}
static int borne(int v, int n) { return v < 0 ? 0 : v >= n ? n - 1 : v; }

#if TOUCH_FT3168
#define FT_ADDR 0x38

void touch_init(void) {
    reset_tp(200);
    uint8_t reg = 0x02, v;
    ok = i2c_master_write_read_device(board_i2c(), FT_ADDR, &reg, 1, &v, 1, pdMS_TO_TICKS(100)) == ESP_OK;
    ESP_LOGI(TAG, "%s", ok ? "FT3168 prêt" : "FT3168 ne répond pas");
}

/* Registres à partir de 0x02 : nombre de doigts, doigt 1 en 0x03..0x06, doigt 2 en 0x09..0x0C */
int touch_read(int xy[4]) {
    if (!ok) return 0;
    uint8_t reg = 0x02, d[11];
    if (i2c_master_write_read_device(board_i2c(), FT_ADDR, &reg, 1, d, sizeof d, pdMS_TO_TICKS(20)) != ESP_OK) return 0;
    int n = d[0] & 0x0F;
    if (n > 2) n = 2;
    for (int k = 0; k < n; k++) {
        const uint8_t *p = d + 1 + k * 6;
        xy[k * 2] = borne(((p[0] & 0x0F) << 8) | p[1], LCD_W);
        xy[k * 2 + 1] = borne(((p[2] & 0x0F) << 8) | p[3], LCD_H);
    }
    return n;
}
int touch_etat(int xy[4]) { return touch_read(xy); }
#elif TOUCH_CST9217
#define CST_ADDR 0x5A
#define CST_ACK  0xAB      /* accusé de lecture, et octet de contrôle d'une trame valide */
#define CST_DOIGTS 2

/* Registres sur 16 bits, octet de poids fort d'abord */
static esp_err_t cst_lire(uint16_t reg, uint8_t *d, int n) {
    const uint8_t r[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_write_read_device(board_i2c(), CST_ADDR, r, 2, d, n, pdMS_TO_TICKS(20));
}

void touch_init(void) {
    reset_tp(50);
    /* Mode commande (D1 01), puis la somme de contrôle 0xCACA.... en D1 FC et le type de puce en D2 04 ; une remise à
       zéro remet ensuite le contrôleur en mode normal (rapport des doigts en D0 00) */
    const uint8_t cmd[2] = { 0xD1, 0x01 };
    i2c_master_write_to_device(board_i2c(), CST_ADDR, cmd, 2, pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t c[4] = { 0 }, t[4] = { 0 };
    const int lu = cst_lire(0xD1FC, c, 4) == ESP_OK && cst_lire(0xD204, t, 4) == ESP_OK;
    const unsigned puce = (unsigned)(t[3] << 8 | t[2]);
    ok = lu && c[3] == 0xCA && c[2] == 0xCA;
    reset_tp(50);
    if (ok) ESP_LOGI(TAG, "CST%04X prêt", puce);
    else if (lu) { ok = 1; ESP_LOGW(TAG, "CST9217 : somme de contrôle inattendue (%02x%02x), lu quand même", c[3], c[2]); }
    else ESP_LOGW(TAG, "CST9217 ne répond pas");
}

/* Trame de D0 00 : 5 octets par doigt (le premier en 0, le second en 7), nombre de doigts en 5, ACK en 6 ; un doigt :
   identifiant (4 bits hauts) et évènement (4 bits bas, 6 : posé), x et y sur 12 bits (octets 1 et 2, demi-octets de 3) */
int touch_etat(int xy[4]) {
    if (!ok) return -1;
    uint8_t d[CST_DOIGTS * 5 + 5];
    if (cst_lire(0xD000, d, sizeof d) != ESP_OK) return -1;
    const uint8_t ack[3] = { 0xD0, 0x00, CST_ACK };
    i2c_master_write_to_device(board_i2c(), CST_ADDR, ack, 3, pdMS_TO_TICKS(20));
    static int avant;   /* un doigt posé : sa place en console (réglage du tactile des cartes) */
    if (d[0] == CST_ACK || d[6] != CST_ACK) return -1;   /* trame déjà lue (accusée) ou incomplète : rien de nouveau */
    if (d[0] == 0) { avant = 0; return 0; }
    int n = d[5] & 0x7F, k = 0;
    if (n > CST_DOIGTS) n = CST_DOIGTS;
    for (int i = 0; i < n; i++) {
        const uint8_t *p = i ? d + i * 5 + 2 : d;
        if ((p[0] & 0x0F) != 0x06) continue;
        int x = p[1] << 4 | p[3] >> 4, y = p[2] << 4 | (p[3] & 0x0F);
#ifdef TOUCH_MIROIR
        x = LCD_W - 1 - x; y = LCD_H - 1 - y;
#endif
        xy[k * 2] = borne(x, LCD_W);
        xy[k * 2 + 1] = borne(y, LCD_H);
        k++;
    }
    if (k && !avant) ESP_LOGI(TAG, "doigt en %d %d", xy[0], xy[1]);
    avant = k;
    return k;
}
int touch_read(int xy[4]) { const int n = touch_etat(xy); return n < 0 ? 0 : n; }
#else
void touch_init(void) { ESP_LOGW(TAG, "pas de tactile pour cette carte"); }
int touch_read(int xy[4]) { (void)xy; return 0; }
int touch_etat(int xy[4]) { (void)xy; return -1; }
#endif

int touch_int_bas(void) { return gpio_get_level(PIN_TP_INT) == 0; }

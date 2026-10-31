/*
 * Alimentation : bus I2C partagé et PMIC AXP2101 (régulateurs de l'écran et du tactile), comme le projet tiny386.
 * ALDO1-4 et BLDO1-2 à 3,3 V, tous allumés.
 */
#include <stdio.h>
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "board.h"
#include "board_power.h"

#define AXP2101_ADDR 0x34
static const char *TAG = "alim";

static esp_err_t axp_write(uint8_t reg, uint8_t v) {
    uint8_t d[2] = { reg, v };
    return i2c_master_write_to_device(I2C_NUM_0, AXP2101_ADDR, d, 2, pdMS_TO_TICKS(100));
}

void board_power_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE, .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    /* Relevé des adresses I2C présentes (brochage des variantes de carte) */
    char vus[128] = ""; int n = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        i2c_cmd_handle_t c = i2c_cmd_link_create();
        i2c_master_start(c); i2c_master_write_byte(c, a << 1 | I2C_MASTER_WRITE, true); i2c_master_stop(c);
        if (i2c_master_cmd_begin(I2C_NUM_0, c, pdMS_TO_TICKS(20)) == ESP_OK && n < 120) n += snprintf(vus + n, sizeof vus - n, " %02x", a);
        i2c_cmd_link_delete(c);
    }
    ESP_LOGI(TAG, "I2C :%s", vus);

    uint8_t reg = 0x03, id = 0;
    if (i2c_master_write_read_device(I2C_NUM_0, AXP2101_ADDR, &reg, 1, &id, 1, pdMS_TO_TICKS(100)) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 absent : alimentation laissée telle quelle");
        return;
    }
    const uint8_t V3V3 = 28;                       /* V = 0,5 + 0,1 × valeur */
    for (uint8_t r = 0x92; r <= 0x97; r++) axp_write(r, V3V3);   /* ALDO1-4, BLDO1-2 */
    axp_write(0x90, 0x3F);                         /* les six régulateurs allumés */
    axp_write(0x91, 0x00);
#ifdef PIN_PWR
    gpio_config_t pwr = { .mode = GPIO_MODE_INPUT, .pin_bit_mask = 1ULL << PIN_PWR };   /* tirage externe */
    gpio_config(&pwr);
#endif
#if !CARTE_TCA9554
    /* Bouton PWR lu par l'AXP2101 (sa touche PWRON) : appuis court et long gardés dans l'état d'IRQ 2 (0x49, bits 3 et
       2) ; autorisés en 0x41, anciens appuis effacés */
    uint8_t ie = 0x41, en;
    if (i2c_master_write_read_device(I2C_NUM_0, AXP2101_ADDR, &ie, 1, &en, 1, pdMS_TO_TICKS(50)) == ESP_OK) axp_write(0x41, en | 0x0F);
    axp_write(0x49, 0x0F);   /* bits 0 et 1 : relâché, enfoncé ; 2 et 3 : appui long, court */
#endif
    ESP_LOGI(TAG, "AXP2101 (id 0x%02x) : régulateurs à 3,3 V", id);
}

i2c_port_t board_i2c(void) { return I2C_NUM_0; }

static int axp_lire(uint8_t reg, uint8_t *v) { return i2c_master_write_read_device(I2C_NUM_0, AXP2101_ADDR, &reg, 1, v, 1, pdMS_TO_TICKS(50)) == ESP_OK; }

/* Tension de la batterie (registres 0x34-0x35 : 14 bits, 1 mV), comme axp2101_get_battery_percent du projet tiny386 ;
   pourcentage linéaire de 3,3 V (0 %) à 4,15 V (100 % ; 100 dès 4,1 V) */
int alim_batterie_mv(void) {
    uint8_t h, l;
    if (!axp_lire(0x34, &h) || !axp_lire(0x35, &l)) return -1;
    const int mv = (h & 0x3F) << 8 | l;
    return mv > 0 ? mv : -1;
}
int alim_batterie_pct(void) {
    const int mv = alim_batterie_mv();
    if (mv < 0) return -1;
    if (mv >= 4100) return 100;
    const int p = (mv - 3300) * 100 / (4150 - 3300);
    return p < 0 ? 0 : p > 100 ? 100 : p;
}
/* État 1 de l'AXP2101 (0x00) : bit 5, VBUS présent (USB branché) */
int alim_usb(void) { uint8_t s; return axp_lire(0x00, &s) ? (s >> 5) & 1 : 1; }
/* Extinction logicielle (0x10, bit 0) : l'AXP2101 coupe tout ; on rallume par PWR */
void alim_eteindre(void) { axp_write(0x10, 0x01); }

/* Ce que l'AXP2101 a vu de sa touche PWRON depuis la dernière lecture (0x49, bits 0 à 3), effacé ; -1 sans réponse */
int alim_pwr_axp(void) {
    uint8_t v;
    if (!axp_lire(0x49, &v)) return -1;
    if (v & 0x0F) axp_write(0x49, v & 0x0F);
    return v & 0x0F;
}

/* Bouton PWR : son état passe par le TCA9554 (0x20, registre d'entrée 0x00, bit 4 haut quand il est appuyé) de la 1,75" */
int alim_bouton_pwr(void) {
#if CARTE_TCA9554
    uint8_t reg = 0x00, v;
    return i2c_master_write_read_device(I2C_NUM_0, 0x20, &reg, 1, &v, 1, pdMS_TO_TICKS(20)) == ESP_OK && (v >> 4 & 1);
#elif defined(PIN_PWR)
    return gpio_get_level(PIN_PWR);
#else
    /* Sans extenseur ni broche : l'AXP2101 signale un appui fini (court ou long), rendu une fois puis effacé */
    uint8_t v;
    if (!axp_lire(0x49, &v) || !(v & 0x0C)) return 0;
    axp_write(0x49, v & 0x0C);
    return 1;
#endif
}

/*
 * Horloge temps réel PCF85063 (I2C 0x51, sur le bus de l'AXP2101), reprise du projet tiny386 (pcf85063.c) : registres
 * des secondes aux années en BCD à partir de 0x04 ; bit 7 des secondes (OS) levé quand l'oscillateur s'est arrêté (pile
 * vide, première mise sous tension) : l'heure ne vaut plus rien tant qu'on ne l'a pas réglée. L'horloge garde l'heure
 * locale (réglée depuis le PC par engine/tools/envoi_sd.py : SDX HEURE, sdx.c) ; au démarrage, elle donne l'heure au
 * système (settimeofday), que le firmware lit ensuite avec time / localtime_r.
 */
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "board_power.h"
#include "horloge.h"

#define PCF_ADDR 0x51
static const char *TAG = "horloge";
static int valide;

static int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
static uint8_t vers_bcd(int v) { return (uint8_t)((v / 10) << 4 | v % 10); }

int horloge_lire(struct tm *t) {
    uint8_t reg = 0x04, d[7];
    if (i2c_master_write_read_device(board_i2c(), PCF_ADDR, &reg, 1, d, 7, pdMS_TO_TICKS(50)) != ESP_OK) return 0;
    if (d[0] & 0x80) return 0;   /* oscillateur arrêté : heure inconnue */
    memset(t, 0, sizeof *t);
    t->tm_sec = bcd(d[0] & 0x7F); t->tm_min = bcd(d[1] & 0x7F); t->tm_hour = bcd(d[2] & 0x3F);
    t->tm_mday = bcd(d[3] & 0x3F); t->tm_wday = d[4] & 7; t->tm_mon = bcd(d[5] & 0x1F) - 1; t->tm_year = bcd(d[6]) + 100;
    t->tm_isdst = -1;
    return t->tm_sec < 60 && t->tm_min < 60 && t->tm_hour < 24 && t->tm_mday >= 1 && t->tm_mday <= 31 && t->tm_mon >= 0 && t->tm_mon < 12;
}

int horloge_ecrire(const struct tm *t) {
    if (t->tm_year < 100 || t->tm_year > 199 || t->tm_mon < 0 || t->tm_mon > 11 || t->tm_mday < 1 || t->tm_mday > 31) return 0;
    const uint8_t d[8] = { 0x04, vers_bcd(t->tm_sec), vers_bcd(t->tm_min), vers_bcd(t->tm_hour), vers_bcd(t->tm_mday),
                           (uint8_t)(t->tm_wday & 7), vers_bcd(t->tm_mon + 1), vers_bcd(t->tm_year - 100) };   /* OS effacé */
    return i2c_master_write_to_device(board_i2c(), PCF_ADDR, d, sizeof d, pdMS_TO_TICKS(50)) == ESP_OK;
}

/* Heure du système = heure de l'horloge (heure locale, fuseau UTC du système : localtime_r rend l'heure telle quelle) */
static void systeme(const struct tm *t) {
    struct tm c = *t;
    const struct timeval tv = { .tv_sec = mktime(&c), .tv_usec = 0 };
    settimeofday(&tv, NULL);
    valide = 1;
}

/* Sans PCF85063 (1,75" C), l'heure réglée reste dans l'ESP32 : sa minuterie RTC tourne en veille légère et l'heure du
   système tient à un redémarrage logiciel (copie par l'USB), pas à une coupure ; elle vaut si elle est après 2026 */
#define HEURE_MIN 1767225600   /* 2026-01-01 */
RTC_NOINIT_ATTR static int64_t garde_us;
RTC_NOINIT_ATTR static uint32_t garde_magic;
void horloge_garder(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    garde_us = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    garde_magic = valide ? 0x48455552u : 0;
}

int horloge_init(void) {
    setenv("TZ", "UTC0", 1); tzset();
    struct tm t;
    if (!horloge_lire(&t)) {
        if (time(NULL) < HEURE_MIN && garde_magic == 0x48455552u) {   /* reprise de l'heure gardée avant esp_restart */
            const struct timeval tv = { .tv_sec = (time_t)(garde_us / 1000000) + 1, .tv_usec = (suseconds_t)(garde_us % 1000000) };
            settimeofday(&tv, NULL);
        }
        garde_magic = 0;
        if (time(NULL) >= HEURE_MIN) { valide = 1; ESP_LOGI(TAG, "pas de PCF85063 : heure gardée par l'ESP32 depuis le dernier réglage"); return 1; }
        ESP_LOGW(TAG, "PCF85063 absent ou pas réglé : heure inconnue (envoi_sd.py --heure la règle)");
        return 0;
    }
    systeme(&t);
    ESP_LOGI(TAG, "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return 1;
}

int horloge_regler(const struct tm *t) {
    if (!horloge_ecrire(t) && (t->tm_year < 100 || t->tm_year > 199)) return 0;   /* sans PCF85063 : le système seul */
    systeme(t);
    return 1;
}

int horloge_valide(void) { return valide; }

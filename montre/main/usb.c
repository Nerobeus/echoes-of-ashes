/*
 * Stockage USB : la montre redémarre dans ce mode (demande en mémoire RTC), initialise la carte SD sans la monter et la
 * prête au PC par TinyUSB (classe MSC, composant espressif/esp_tinyusb). Le port USB quitte alors la console et la copie
 * par USB (USB Serial/JTAG) : le PC voit une clé. Écran : octets écrits et lus, débit, bouton QUITTER ; PWR éteint ou
 * rallume l'écran ; le PC qui lâche la carte ramène aussi à l'accueil.
 */
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "board_power.h"
#include "horloge.h"
#include "dessin.h"
#include "theme.h"
#include "touch.h"
#include "lcd_co5300.h"
#include <math.h>
#include "usb.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/rtc_cntl_struct.h"

#if __has_include("tusb_msc_storage.h")
#define AVEC_USB 1
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#else
#define AVEC_USB 0
#endif

static const char *TAG = "usb";
#define MAGIC 0x55534221u   /* « USB! » */
RTC_NOINIT_ATTR static uint32_t demande;

bool usb_stockage(void) {
#if AVEC_USB
    demande = MAGIC;
    horloge_garder();
    esp_restart();
#endif
    return false;
}
bool usb_demande(void) { const bool d = demande == MAGIC; demande = 0; return d; }

/* Le PHY USB rendu à la console (USB Serial/JTAG). TinyUSB l'a donné à l'OTG, et ce choix (registre RTC) survit à
   esp_restart : sans ce retour, plus de port série, donc plus de flash ni de copie par l'USB, jusqu'à une coupure
   d'alimentation (la batterie tient la montre allumée câble débranché). Le PHY repris à l'OTG, le PC doit voir un
   débranchement (D+ sans tirage 300 ms), sinon il garde la clé en erreur et ne voit jamais le port série (COM8) :
   avant, il fallait éteindre la montre pour le retrouver. */
void usb_console(void) {
    const bool otg = RTCCNTL.usb_conf.sw_hw_usb_phy_sel && RTCCNTL.usb_conf.sw_usb_phy_sel;
    if (!otg) { usb_serial_jtag_ll_phy_enable_external(false); return; }
    const usb_serial_jtag_pull_override_vals_t debranche = { .dp_pu = false, .dm_pu = false, .dp_pd = true, .dm_pd = true };
    usb_serial_jtag_ll_phy_enable_pull_override(&debranche);
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
    vTaskDelay(pdMS_TO_TICKS(300));
    usb_serial_jtag_ll_phy_disable_pull_override();
}

/* Octets lus et écrits sur la carte par le PC : chaque échange avec la carte passe par compte (do_transaction) */
static esp_err_t (*vraie_transaction)(int slot, sdmmc_command_t *c);
static volatile uint64_t lus, ecrits;
static esp_err_t compte(int slot, sdmmc_command_t *c) {
    const esp_err_t r = vraie_transaction(slot, c);
    if (r == ESP_OK && c->data && c->datalen) { if (c->flags & SCF_CMD_READ) lus += c->datalen; else ecrits += c->datalen; }
    return r;
}

static void ecran(const char *l1, const char *l2, uint16_t c) {
    d_effacer(T_INK);
    th_fond();
    d_anneau(233, 233, 222, 226, c);
    th_titre(150, "STOCKAGE USB", "Carte SD vue par le PC");
    d_paragraphe(&P_RANG, 80, 200, 306, 3, l1, c);
    d_paragraphe(&P_TEXTE, 80, 296, 306, 3, l2, T_DIM);
    d_envoyer();
}

static void taille(uint64_t o, char *b, int n) {
    if (o < 10ull << 20) snprintf(b, n, "%llu Ko", (unsigned long long)(o >> 10));
    else snprintf(b, n, "%llu,%llu Mo", (unsigned long long)(o >> 20), (unsigned long long)((o >> 10) % 1024 * 10 / 1024));
}

/* Écran du mode : état, octets écrits et lus, débit, anneau qui tourne pendant les échanges, bouton QUITTER */
#define QUITTER_Y 352
#define QUITTER_H 44
static void tableau(bool carte, bool branchee, bool actif, int64_t t, uint32_t debit, bool presse, const char *msg) {
    d_effacer(T_INK);
    th_fond();
    /* anneau : tête qui tourne et traînée pendant les échanges, rail au repos */
    const int tete = actif ? (int)(t / 25000 % 60) : -1;
    for (int k = 0; k < 60; k++) {
        const float a0 = (k + 0.15f) * 6.2831853f / 60, a1 = (k + 0.85f) * 6.2831853f / 60;
        const float q[8] = { 233 + sinf(a0) * 223, 233 - cosf(a0) * 223, 233 + sinf(a0) * 231, 233 - cosf(a0) * 231,
                             233 + sinf(a1) * 231, 233 - cosf(a1) * 231, 233 + sinf(a1) * 223, 233 - cosf(a1) * 223 };
        const int age = tete < 0 ? 99 : (tete - k + 60) % 60;
        d_poly(q, 4, age == 0 ? T2 : T1, age == 0 ? 16 : age < 16 ? 14 - age * 10 / 16 : 2);
    }
    th_titre(104, "STOCKAGE USB", !carte ? "Pas de carte SD" : branchee ? "Carte SD prêtée au PC" : "En attente du PC");
    char b[32];
    taille(ecrits, b, sizeof b);
    th_texte(&P_MONO, 233, 164, "ÉCRIT", T_DIM, A_CENTRE);
    th_texte(&P_GRAND, 233, 198, b, T_BLANC, A_CENTRE);
    taille(lus, b, sizeof b);
    th_texte(&P_MONO, 233, 232, "LU", T_DIM, A_CENTRE);
    th_texte(&P_GRAND, 233, 266, b, T_BLANC, A_CENTRE);
    if (actif) snprintf(b, sizeof b, "%u Ko/s", (unsigned)debit); else snprintf(b, sizeof b, "AU REPOS");
    th_texte(&P_MONO, 233, 300, b, actif ? T1 : T_DIM, A_CENTRE);
    th_texte(&P_MONO, 233, 330, msg ? msg : "Éjecter côté PC avant de quitter", msg ? T2 : T_DIM, A_CENTRE);
    th_rangee(QUITTER_Y, QUITTER_H, "QUITTER", NULL, presse, T_BLANC);
    d_envoyer();
}

void usb_mode(void) {
#if AVEC_USB
    ESP_LOGI(TAG, "mode stockage USB");
    ecran("Carte SD en préparation…", "", T1);
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    vraie_transaction = host.do_transaction;
    host.do_transaction = compte;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = CONFIG_ECHOES_SD_CLK; slot.cmd = CONFIG_ECHOES_SD_CMD; slot.d0 = CONFIG_ECHOES_SD_D0;
    slot.d1 = CONFIG_ECHOES_SD_D1; slot.d2 = CONFIG_ECHOES_SD_D2; slot.d3 = CONFIG_ECHOES_SD_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    sdmmc_card_t *card = calloc(1, sizeof *card);
    const bool carte = card && sdmmc_host_init() == ESP_OK && sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot) == ESP_OK &&
                       sdmmc_card_init(&host, card) == ESP_OK;
    if (carte) {
        const tinyusb_msc_sdmmc_config_t msc = { .card = card };
        tinyusb_msc_storage_init_sdmmc(&msc);
        const tinyusb_config_t tusb = { 0 };
        tinyusb_driver_install(&tusb);
    }
    lus = ecrits = 0;
    /* QUITTER au doigt (branchée : un second appui dans les 3 s, pour ne pas couper une copie) ; PWR éteint ou rallume
       l'écran seulement (un appui ne coupe plus la copie) ; retour à l'accueil aussi si le PC lâche la carte 3 s */
    bool vu = false, ecran_on = true, doigt = false, presse = false;
    int64_t calme = esp_timer_get_time(), lache = 0, vu_doigt = 0, quitter_t = 0, dessin_t = 0, debit_t = 0, actif_t = 0;
    uint64_t debit_o = 0, avant = 0;
    uint32_t debit = 0;
    int dy = 0;
    for (;;) {
        const int64_t t = esp_timer_get_time();
        const bool pwr = alim_bouton_pwr();
        if (pwr && t - calme > 300000) {
            ecran_on = !ecran_on;
            lcd_wait_all();
            if (ecran_on) { lcd_reveiller(); lcd_set_brightness(0xD0); dessin_t = 0; } else lcd_dormir();
        }
        if (pwr) calme = t;
        if (tud_mounted()) { vu = true; lache = 0; }
        else if (vu) { if (!lache) lache = t; else if (t - lache > 3000000) break; }
        /* échanges : débit sur une seconde, actif s'il y en a eu dans la demi-seconde */
        const uint64_t tot = lus + ecrits;
        if (tot != avant) { avant = tot; actif_t = t; }
        if (t - debit_t >= 1000000) { debit = (uint32_t)((tot - debit_o) * 1000000 / (uint64_t)(t - debit_t) / 1024); debit_o = tot; debit_t = t; }
        /* doigt : appui sur QUITTER au relâchement */
        if (ecran_on) {
            int xy[4];
            const int n = touch_etat(xy);
            if (n > 0) { vu_doigt = t; if (!doigt) { doigt = true; dy = xy[1]; } presse = dy >= QUITTER_Y && dy < QUITTER_Y + QUITTER_H; }
            else if (doigt && (n == 0 || t - vu_doigt > 70000)) {
                doigt = false;
                if (presse) {
                    presse = false;
                    if (!tud_mounted() || (quitter_t && t - quitter_t < 3000000)) break;
                    quitter_t = t;
                }
                dessin_t = 0;
            }
        }
        if (ecran_on && t - dessin_t > (doigt ? 50000 : 200000)) {
            dessin_t = t;
            const char *msg = quitter_t && t - quitter_t < 3000000 ? "Toucher encore : quitter quand même" : NULL;
            tableau(carte, tud_mounted(), t - actif_t < 500000, t, debit, presse, msg);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!ecran_on) { lcd_reveiller(); lcd_set_brightness(0xD0); }
    ecran("Retour à l'accueil…", "", T1);
    if (carte) { tud_disconnect(); vTaskDelay(pdMS_TO_TICKS(50)); }   /* la clé quitte le PC */
    usb_console();
    vTaskDelay(pdMS_TO_TICKS(100));
    horloge_garder();
    esp_restart();
#endif
}

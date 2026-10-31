/*
 * Son : codec ES8311 (I2C 0x18, bus partagé avec l'AXP2101 et le tactile) et I2S en 24 kHz 16 bits stéréo, comme le
 * projet tiny386 (registres de son pilote, horloge maître 6,144 MHz). Une tâche tire les échantillons mono du moteur
 * (eng_audio_mix) et les envoie sur les deux voies ; l'amplificateur s'allume par sa broche d'activation.
 * Broches I2S : menuconfig > Echoes of Ashes (carte de test 2,06" par défaut ; 1,75" : MCLK 42, BCLK 9, WS 45, DOUT 8).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "board_power.h"
#include "eng.h"
#include "audio_esp.h"

#if CONFIG_ECHOES_AUDIO

#define ES8311_ADDR 0x18
#define RATE 24000
#define BLOC 480   /* 20 ms */

static const char *TAG = "son";
static i2s_chan_handle_t tx;

static esp_err_t es_w(uint8_t reg, uint8_t v) {
    uint8_t d[2] = { reg, v };
    return i2c_master_write_to_device(board_i2c(), ES8311_ADDR, d, 2, pdMS_TO_TICKS(100));
}

/* Registres du pilote tiny386 (Waveshare) : remise à zéro, horloges pour 24 kHz avec MCLK = 256 × fs, I2S 16 bits,
   mise sous tension du convertisseur de sortie, volume */
static int es8311_init(void) {
    if (es_w(0x00, 0x1F) != ESP_OK) return 0;
    vTaskDelay(pdMS_TO_TICKS(20));
    static const uint8_t R[][2] = {
        { 0x00, 0x00 }, { 0x00, 0x80 },
        { 0x01, 0x3F }, { 0x02, 0x00 }, { 0x03, 0x10 }, { 0x04, 0x10 }, { 0x05, 0x00 }, { 0x06, 0x03 }, { 0x07, 0x00 }, { 0x08, 0xFF },
        { 0x09, 0x0C }, { 0x0A, 0x0C },
        { 0x0D, 0x01 }, { 0x0E, 0x02 }, { 0x12, 0x00 }, { 0x13, 0x10 }, { 0x14, 0x1A }, { 0x1C, 0x6A }, { 0x37, 0x08 }, { 0x32, 0xBF },
    };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) es_w(R[i][0], R[i][1]);
    vTaskDelay(pdMS_TO_TICKS(50));
    return 1;
}

static void audio_task(void *arg) {
    static int16_t mono[BLOC], st[BLOC * 2];
    i2s_channel_enable(tx);
    for (;;) {
        eng_audio_mix(mono, BLOC, RATE);
        for (int i = 0; i < BLOC; i++) st[2 * i] = st[2 * i + 1] = mono[i];
        size_t n;
        i2s_channel_write(tx, st, sizeof st, &n, portMAX_DELAY);
    }
}

/* Codec et canal I2S : avant eng_init, qui prend ensuite la SRAM interne dont les tampons DMA ont besoin */
void audio_esp_init(void) {
    if (!es8311_init()) { ESP_LOGW(TAG, "ES8311 absent : jeu muet"); return; }
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    cc.dma_desc_num = 4; cc.dma_frame_num = BLOC;
    if (i2s_new_channel(&cc, &tx, NULL) != ESP_OK) { ESP_LOGW(TAG, "I2S indisponible"); tx = NULL; return; }
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = CONFIG_ECHOES_I2S_MCLK, .bclk = CONFIG_ECHOES_I2S_BCLK, .ws = CONFIG_ECHOES_I2S_WS,
                      .dout = CONFIG_ECHOES_I2S_DOUT, .din = I2S_GPIO_UNUSED },
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK) {
        ESP_LOGW(TAG, "I2S : configuration refusée"); i2s_del_channel(tx); tx = NULL; return;
    }
    gpio_config_t pa = { .pin_bit_mask = 1ULL << CONFIG_ECHOES_PA_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&pa);
    gpio_set_level(CONFIG_ECHOES_PA_GPIO, 1);
    ESP_LOGI(TAG, "son : ES8311 et I2S %d Hz (MCLK %d, BCLK %d, WS %d, DOUT %d)", RATE, CONFIG_ECHOES_I2S_MCLK,
             CONFIG_ECHOES_I2S_BCLK, CONFIG_ECHOES_I2S_WS, CONFIG_ECHOES_I2S_DOUT);
}

/* Tâche du son : après eng_init (elle tire les échantillons du moteur) */
void audio_esp_start(void) {
    if (tx && xTaskCreatePinnedToCore(audio_task, "son", 4096, NULL, 6, NULL, 0) != pdPASS) ESP_LOGE(TAG, "tâche son impossible à créer");
}

#else
void audio_esp_init(void) {}
void audio_esp_start(void) {}
#endif

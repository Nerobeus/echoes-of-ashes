/* Son de la carte : codec ES8311 et I2S, tâche qui tire les échantillons du moteur (audio_esp.c) */
#ifndef AUDIO_ESP_H
#define AUDIO_ESP_H

void audio_esp_init(void);    /* codec et canal I2S, avant eng_init */
void audio_esp_start(void);   /* tâche du son, après eng_init */

#endif

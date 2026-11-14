set(core ../../../core)
idf_component_register(
    SRCS main.c lcd_co5300.c board_power.c touch.c sd.c sdx.c audio_esp.c horloge.c veille.c transfert.c apps.c reglages.c manette.c
         ${core}/world.c ${core}/game.c ${core}/render.c ${core}/ui.c ${core}/hud.c ${core}/battle.c ${core}/script.c ${core}/audio.c ${core}/course.c ${core}/music.c ${core}/interieur.c ${core}/accueil.c ${core}/police.c ${core}/coffres.c ${core}/pilote.c ${core}/carte.c ${core}/carriere.c ${core}/garage.c ${core}/panneaux.c ${core}/repliques.c ${core}/arene.c ${core}/ile.c ${core}/antigrav.c
    INCLUDE_DIRS . ${core}
    REQUIRES driver esp_partition esp_timer fatfs sdmmc vfs hal esp_wifi esp_netif esp_event nvs_flash esp_http_client lwip wifi_provisioning bt app_update)

# 3D interne : 233 × 233 doublée (par défaut), ou 466 × 466 native ; l'interface est toujours en 466 × 466
if(CONFIG_ECHOES_NATIVE_466)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENG_W=466 ENG_H_PX=466 ENG_ROUND=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENG_W=233 ENG_H_PX=233 ENG_ROUND=1)
endif()
# Place des intérieurs au plus 1,6 Mo (PSRAM presque pleine) : un intérieur plus gros n'est pas visitable (core/world.c)
target_compile_definitions(${COMPONENT_LIB} PRIVATE INTR_FEN_MAX=1638400)
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-strict-aliasing -Wno-unused-parameter)
# Édition enfant (idf_enfant.cmd) : le cœur ne lance que les paquets de l'édition enfant (section EDIT)
if(CONFIG_ECHOES_ENFANT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ECHOES_ENFANT=1)
endif()
# Banc d'essai : profilage du rendu des bandes en cycles (eng_profile) ; aussi en jeu avec idf.cmd build -DECHOES_PROFIL=1
# (postes écrits toutes les 2 s dans la console ; -DECHOES_PROFIL=0 pour revenir au firmware normal)
if(CONFIG_ECHOES_BANC OR ECHOES_PROFIL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENG_PROFILE=1)
endif()

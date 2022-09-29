idf_component_register(SRCS "audio_player_wav.c" "audio_player_mp3.c" "audio_player_flac.c" "audio_player_helper.c"
                       INCLUDE_DIRS "."
                       REQUIRES sdmmc fatfs driver vfs minimp3 miniflac)

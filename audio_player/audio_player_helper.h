#ifndef __AUDIO_PLAYER_HELPER_H__
#define __AUDIO_PLAYER_HELPER_H__

#include "audio_player_wav.h"
#include "audio_player_mp3.h"
#include "audio_player_flac.h"

// exit code
#define PLAYER_EXIT_CODE_OK 0x001

#define PLAYER_EXIT_CODE_USER 0x101

#define PLAYER_EXIT_CODE_PHY_INIT_FAIL 0x201
#define PLAYER_EXIT_CODE_ALLOC_FAIL 0x202

#define PLAYER_EXIT_CODE_UNSUPPORTED_FORMAT 0x301

// play mode
#define PLAYER_IDLE 0x0
#define PLAYER_PLAYING_MP3 0x1
#define PLAYER_PLAYING_WAV 0x2
#define PLAYER_PLAYING_FLAC 0x3

typedef struct
{
    uint16_t bitPerSample;           // bps
    uint16_t channel;                // ch
    uint32_t sampleRate;             // sr
    uint32_t byte_rate_max_block_sz; // in mp3 byte_rate,in flac max_block sz
    uint8_t type;
} audio_generic_info_t;

/*
in this struct , phy use wav , other phy are copy from wav
*/
typedef struct
{
    audio_player_wav_handle_t wav;
    audio_player_mp3_handle_t mp3;
    audio_player_flac_handle_t flac;

    //////////////
    uint8_t playing_type; // set in play func , clean in task exit cb

    void (*_playerTaskExit)(int32_t code); // callback at playTask exit

} audio_player_aio_t;

esp_err_t audio_player_aio_init(audio_player_aio_t *handle);

esp_err_t audio_player_aio_play_file(audio_player_aio_t *handle, const char *file_dir);

esp_err_t audio_player_aio_set_i2s_config(audio_player_aio_t *handle, audio_player_physical_handle_t phy);
esp_err_t audio_player_aio_set_loop_mode(audio_player_aio_t *handle, uint8_t loop);

esp_err_t audio_player_aio_set_pause(audio_player_aio_t *handle, uint8_t pause);
esp_err_t audio_player_aio_set_stop(audio_player_aio_t *handle);

esp_err_t audio_player_aio_get_file_info(audio_player_aio_t *handle, audio_generic_info_t *info);

esp_err_t audio_player_aio_get_file_name(audio_player_aio_t *handle, const char **name);
uint8_t audio_player_aio_get_pause(audio_player_aio_t *handle);
esp_err_t audio_player_aio_get_pos(audio_player_aio_t *handle, uint32_t *cur, uint32_t *all);

uint8_t audio_player_aio_get_busy(audio_player_aio_t *handle);

esp_err_t audio_player_aio_wait_task_exit(audio_player_aio_t *handle);

esp_err_t audio_player_aio_set_task_exit_event_cb(audio_player_aio_t *handle, void (*cb)(int32_t code));

#endif
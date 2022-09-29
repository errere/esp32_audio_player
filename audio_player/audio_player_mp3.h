#ifndef __AUDIO_PLAYER_MP3_H__
#define __AUDIO_PLAYER_MP3_H__

#include "minimp3.h"
//#include "minimp3\minimp3_ex.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"

//#include "driver/i2s.h"

#include "driver/i2s_std.h"

#include "driver/gpio.h"

#define _MP3_INPUT_BUFFER_SIZE 8192
#define _MP3_INPUT_BUFFER_RELOAD_LIMIT 2048

// decoder locate
#define MP3_DECODER_MALLOC_CAP MALLOC_CAP_DEFAULT
// file buffer locate
#define MP3_INPUT_BUFFER_MALLOC_CAP MALLOC_CAP_SPIRAM
// pcm buffer locate
#define MP3_PCM_BUFFER_MALLOC_CAP MALLOC_CAP_SPIRAM

#define MP3_PLAYER_CMD_PAUSE 0x101
#define MP3_PLAYER_CMD_STOP 0x102
#define MP3_PLAYER_CMD_CONTINUE_PLAY 0x103

// exit code
#define MP3_PLAYER_EXIT_CODE_OK 0x001

#define MP3_PLAYER_EXIT_CODE_USER 0x101

#define MP3_PLAYER_EXIT_CODE_PHY_INIT_FAIL 0x201
#define MP3_PLAYER_EXIT_CODE_ALLOC_FAIL 0x202

#define MP3_PLAYER_EXIT_CODE_UNSUPPORTED_FORMAT 0x301

typedef enum
{
    MP3_LOOP_FILE = 0,
    MP3_FILE_SINGLE = 1,
} mp3_player_loop_mode_t;

typedef struct
{
    i2s_chan_config_t tx_chan_cfg;
    i2s_std_config_t tx_std_cfg;
    i2s_chan_handle_t tx_chan; // I2S tx channel handler
    uint8_t i2s_inited;
} mp3_player_physical_t;

typedef struct
{
    int channel;
    int layer;
    int bitrate;
    int hz;
    uint8_t info_init;
} mp3_player_info_t;

typedef struct
{
    TaskHandle_t playerTask;
    uint8_t isPause;
    uint8_t isBusy;
    size_t total_length;
    size_t reade_length;

} mp3_player_status_t;

typedef struct
{
    mp3_player_loop_mode_t loop;

    SemaphoreHandle_t xSemStatusFree;
    QueueHandle_t _xQueueCommand; // a queue send command to player task

    void (*_playerTaskExit)(int32_t code, void *data); // callback at playTask exit
} mp3_player_command_t;

typedef struct
{

    mp3_player_physical_t phy;
    mp3_player_info_t info; // need clean in exit
    mp3_player_status_t status;
    mp3_player_command_t cmd;

    mp3dec_t *mp3d;                // alloc in heap //need clean free in exit
    mp3dec_frame_info_t mp3d_info; // alloc in struct //need clean in exit

    FILE *file;     // need clean close in exit
    char *fileName; // alloc in spiram //need clean free in exit

    uint16_t *decoded_pcm; // alloc in heap //need clean free in exit

    // mp3 input stream control
    uint8_t *input_buffer; // alloc in heap //need clean free in exit
    uint8_t *input_buffer_ptr;

    size_t file_length; /* input file total size */
    size_t read_length; /* reade length */

    size_t input_buffer_remain;

    void *user; // user context
} audio_player_mp3_handle_t;

esp_err_t audio_player_mp3_init(audio_player_mp3_handle_t *handle);

esp_err_t audio_player_mp3_play_file(audio_player_mp3_handle_t *handle, const char *file_dir);

esp_err_t audio_player_mp3_set_i2s_config(audio_player_mp3_handle_t *handle, mp3_player_physical_t phy);
esp_err_t audio_player_mp3_set_loop_mode(audio_player_mp3_handle_t *handle, mp3_player_loop_mode_t mode);

esp_err_t audio_player_mp3_set_pause(audio_player_mp3_handle_t *handle, uint8_t pause);
esp_err_t audio_player_mp3_set_stop(audio_player_mp3_handle_t *handle);

esp_err_t audio_player_mp3_get_file_info(audio_player_mp3_handle_t *handle, mp3_player_info_t *info,TickType_t max_delay);

esp_err_t audio_player_mp3_get_file_name(audio_player_mp3_handle_t *handle, const char **name);
esp_err_t audio_player_mp3_get_status(audio_player_mp3_handle_t *handle, mp3_player_status_t *stat);

uint8_t audio_player_mp3_get_busy(audio_player_mp3_handle_t *handle);

esp_err_t audio_player_mp3_wait_task_exit(audio_player_mp3_handle_t *handle);

esp_err_t audio_player_mp3_set_task_exit_event_cb(audio_player_mp3_handle_t *handle, void (*cb)(int32_t code, void *data));

esp_err_t audio_player_mp3_set_user_data(audio_player_mp3_handle_t *handle, void *user);
#endif

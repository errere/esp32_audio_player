#ifndef __AUDIO_PLAYER_FLAC_H__
#define __AUDIO_PLAYER_FLAC_H__

#include "flac.h"

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

#define _FLAC_INPUT_BUFFER_SIZE 8192
#define _FLAC_INPUT_BUFFER_RELOAD_LIMIT 2048

// decoder locate
#define FLAC_DECODER_MALLOC_CAP MALLOC_CAP_DEFAULT
// file buffer locate
#define FLAC_INPUT_BUFFER_MALLOC_CAP MALLOC_CAP_SPIRAM
// pcm buffer locate
#define FLAC_RAW_BUFFER_MALLOC_CAP MALLOC_CAP_SPIRAM
#define FLAC_PACKED_BUFFER_MALLOC_CAP MALLOC_CAP_DMA

#define FLAC_PLAYER_CMD_PAUSE 0x101
#define FLAC_PLAYER_CMD_STOP 0x102

#define FLAC_PLAYER_CMD_CONTINUE_PLAY 0x103

// exit code
#define FLAC_PLAYER_EXIT_CODE_OK 0x001

#define FLAC_PLAYER_EXIT_CODE_USER 0x101

#define FLAC_PLAYER_EXIT_CODE_PHY_INIT_FAIL 0x201
#define FLAC_PLAYER_EXIT_CODE_ALLOC_FAIL 0x202

#define FLAC_PLAYER_EXIT_CODE_UNSUPPORTED_FORMAT 0x301

typedef enum
{
    FLAC_LOOP_FILE = 0,
    FLAC_FILE_SINGLE = 1,
} flac_player_loop_mode_t;

typedef struct
{
    i2s_chan_config_t tx_chan_cfg;
    i2s_std_config_t tx_std_cfg;
    i2s_chan_handle_t tx_chan; // I2S tx channel handler
    uint8_t i2s_inited;
} flac_player_physical_t;

typedef struct
{
    uint8_t channel;
    uint8_t bps;
    uint32_t sample_rate;
    uint16_t max_block_sz;
} flac_player_info_t;

typedef struct
{
    TaskHandle_t playerTask;
    uint8_t isPause;
    uint8_t isBusy;
    size_t total_length;
    size_t reade_length;

} flac_player_status_t;

typedef struct
{
    flac_player_loop_mode_t loop;

    SemaphoreHandle_t xSemStatusFree;
    QueueHandle_t _xQueueCommand; // a queue send command to player task

    void (*_playerTaskExit)(int32_t code, void *data); // callback at playTask exit
} flac_player_command_t;

typedef struct
{

    flac_player_physical_t phy;
    flac_player_info_t info; // need clean in exit
    flac_player_status_t status;
    flac_player_command_t cmd;

    miniflac_t *decoder; // alloc in heap //need clean free in exit

    FILE *file;     // need clean close in exit
    char *fileName; // alloc in spiram //need clean free in exit

    int32_t **raw_sample; // in task alloc in heap(n*8,but only alloc 2 ch in esp32) //need clean free in exit
    uint8_t *pcm_buffer;  // in task alloc in heap //need clean free in exit

    uint8_t *input_buffer; // alloc in heap //need clean free in exit
    uint8_t *input_buffer_ptr;

    size_t file_length; /* input file total size ,only write in play func*/
    size_t read_length; /* reade length */

    size_t input_buffer_remain;

    void *user; // user context

} audio_player_flac_handle_t;

esp_err_t audio_player_flac_init(audio_player_flac_handle_t *handle);

esp_err_t audio_player_flac_play_file(audio_player_flac_handle_t *handle, const char *file_dir);

esp_err_t audio_player_flac_set_i2s_config(audio_player_flac_handle_t *handle, flac_player_physical_t phy);
esp_err_t audio_player_flac_set_loop_mode(audio_player_flac_handle_t *handle, flac_player_loop_mode_t mode);

esp_err_t audio_player_flac_set_pause(audio_player_flac_handle_t *handle, uint8_t pause);
esp_err_t audio_player_flac_set_stop(audio_player_flac_handle_t *handle);

esp_err_t audio_player_flac_get_file_info(audio_player_flac_handle_t *handle, flac_player_info_t *info);

esp_err_t audio_player_flac_get_file_name(audio_player_flac_handle_t *handle, const char **name);
esp_err_t audio_player_flac_get_status(audio_player_flac_handle_t *handle, flac_player_status_t *stat);

uint8_t audio_player_flac_get_busy(audio_player_flac_handle_t *handle);

esp_err_t audio_player_flac_wait_task_exit(audio_player_flac_handle_t *handle);

esp_err_t audio_player_flac_set_task_exit_event_cb(audio_player_flac_handle_t *handle, void (*cb)(int32_t code, void *data));

esp_err_t audio_player_flac_set_user_data(audio_player_flac_handle_t *handle, void *user);
#endif
// eof
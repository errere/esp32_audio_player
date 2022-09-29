#ifndef __AUDIO_PLAYER_H__
#define __AUDIO_PLAYER_H__

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

#define WAV_PCM_BUFFER_MALLOC_CAP MALLOC_CAP_DMA

// cmd
#define PLAYER_CMD_PAUSE 0x101         // play->pause
#define PLAYER_CMD_STOP 0x102          // any->stop
#define PLAYER_CMD_CONTINUE_PLAY 0x103 // pause->play

#define PLAYER_CMD_JUMP 0x201 // jump

// exit code
#define WAV_PLAYER_EXIT_CODE_OK 0x001

#define WAV_PLAYER_EXIT_CODE_USER 0x101

#define WAV_PLAYER_EXIT_CODE_PHY_INIT_FAIL 0x201
#define WAV_PLAYER_EXIT_CODE_ALLOC_FAIL 0x202

#define WAV_PLAYER_EXIT_CODE_UNSUPPORTED_FORMAT 0x301

/*==========wav decode header==========*/
typedef struct
{
    // length = 44
    char ChunkID[4]; //'R','I','F','F' in ascii
    uint32_t ChunkSize;
    char Format[4]; //'W','A','V','E' in ascii
    /*===============================================*/
    char SubChunk1ID[4]; //'f','m','t',' ' in ascii
    uint32_t SubChunk1Size;
    uint16_t AudioFormat;
    uint16_t NumChannels;
    uint32_t SampleRate;
    uint32_t ByteRate;
    uint16_t BlockAlign;
    uint16_t BitsPerSample;
    char SubChunk2ID[4];
    uint32_t SubChunk2Size;
} General_Chunk_struct_t;

typedef union
{
    General_Chunk_struct_t str;
    uint8_t stream[44];
} General_Chunk_t;

typedef struct
{
    char ID[4];
    uint32_t size;
} ChunkHeader_struct_t;

typedef union
{
    ChunkHeader_struct_t str;
    uint8_t data[8];
} ChunkHeader_t;

typedef struct
{
    uint16_t fmt;
    uint16_t bitPerSample;
    uint16_t channel;
    uint32_t sampleRate;
    uint32_t length;
    uint32_t entryPoint;
    uint32_t totalSample;
} Simplified_Wav_struct_t;

// typedef enum
// {
//     FILE_WAV = 0,
//     FILE_FLAC = 1,
//     FILE_MP3 = 2
// } audio_file_encode_t;

typedef enum
{
    LOOP_FILE = 0,
    FILE_SINGLE = 1,
} audio_file_loop_t;

typedef struct
{
    i2s_chan_config_t tx_chan_cfg;
    i2s_std_config_t tx_std_cfg;
    i2s_chan_handle_t tx_chan; // I2S tx channel handler
    uint8_t i2s_inited;
} audio_player_physical_handle_t;

typedef struct
{
    TaskHandle_t playerTask;
    uint32_t playProcess;
    uint8_t isPause;
    uint8_t isBusy;
} player_status_t;

typedef struct
{

    audio_file_loop_t loop;                                   // loop or once play
    uint32_t requestPosition;                                 // jump position
    int32_t (*_playerTaskEntry)(Simplified_Wav_struct_t wav); // callback at playTask entry
    void (*_playerTaskExit)(int32_t code, void *data);        // callback at playTask exit

    QueueHandle_t _xQueueCommand;         // a queue send command to player task
    SemaphoreHandle_t _xMutexProcessFree; // a mutex indicate player_status_t->playProcess free
} player_command_t;

typedef struct
{
    audio_player_physical_handle_t phy; // i2s handle
    Simplified_Wav_struct_t wav;        // wav data
    FILE *audio_file_handle;            // file pointer
    uint8_t *sample_buffer;             // play task sample ram buffer
    // audio_file_encode_t encode;         // wav mp3 flac
    player_status_t status; // player system info
    player_command_t cmd;   // command
    char *fileName;
    void *user; // user context
} audio_player_wav_handle_t;

esp_err_t audio_player_init(audio_player_wav_handle_t *handle);

esp_err_t audio_player_play_wav_file(audio_player_wav_handle_t *handle, const char *file);

esp_err_t audio_player_set_pause(audio_player_wav_handle_t *handle, uint8_t pause);
esp_err_t audio_player_set_stop(audio_player_wav_handle_t *handle);
esp_err_t audio_player_set_pos(audio_player_wav_handle_t *handle, uint32_t pos);
esp_err_t audio_player_set_loop_mode(audio_player_wav_handle_t *handle, audio_file_loop_t loop);

esp_err_t audio_player_get_pos(audio_player_wav_handle_t *handle, uint32_t *pos);
esp_err_t audio_player_get_total_sample(audio_player_wav_handle_t *handle, uint32_t *samp);
esp_err_t audio_player_get_status(audio_player_wav_handle_t *handle, player_status_t *stat);
esp_err_t audio_player_get_file_info(audio_player_wav_handle_t *handle, Simplified_Wav_struct_t *wav);

esp_err_t audio_player_set_task_entry_event_cb(audio_player_wav_handle_t *handle, int32_t (*cb)(Simplified_Wav_struct_t wav));
esp_err_t audio_player_set_task_exit_event_cb(audio_player_wav_handle_t *handle, void (*cb)(int32_t code, void *data));

esp_err_t audio_player_wait_play_task_exit(audio_player_wav_handle_t *handle);

uint8_t audio_player_get_busy(audio_player_wav_handle_t *handle);

esp_err_t audio_player_get_file_name(audio_player_wav_handle_t *handle, const char **dst);

/**
 * @brief set iis phy by user
 *
 * @param handle a pointer to player control handle
 *
 * @param config i2s phy args
 *
 * @note config mast set zero before use like memset(&i2s0_conf,0x00,sizeof(audio_player_physical_handle_t));
 *
 * @return ESP_OK if set ok , other means audio player busy
 */
esp_err_t audio_player_set_iis_config(audio_player_wav_handle_t *handle, audio_player_physical_handle_t config);

esp_err_t audio_player_set_user_data(audio_player_wav_handle_t *handle, void *user);
#endif

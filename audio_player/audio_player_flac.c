#include "audio_player_flac.h"

static const char *TAG = "flac_player";

#define FLAC_DEBUG_MEM(x) ESP_LOGI(TAG, "%s: all : %fk , internal %fk , minimum %fk  all-internal %fk", x, esp_get_free_heap_size() / 1000.0, esp_get_free_internal_heap_size() / 1000.0, esp_get_minimum_free_heap_size() / 1000.0, (esp_get_free_heap_size() - esp_get_free_internal_heap_size()) / 1000.0);

static esp_err_t checkFileExist(const char *fileName)
{
    FILE *fp = 0x00;
    fp = fopen(fileName, "rb");
    if (fp == 0x00)
    {
        return ESP_ERR_NOT_FOUND;
    }
    fclose(fp);
    return ESP_OK;
}

static void pack_uint24le(uint8_t *output, uint32_t n)
{
    output[0] = (uint8_t)(n & 0xFF);
    output[1] = (uint8_t)(n >> 8);
    output[2] = (uint8_t)(n >> 16);
}

static void pack_int24le(uint8_t *output, int32_t n)
{
    pack_uint24le(output, (uint32_t)n);
}

static void pack_uint16le(uint8_t *output, uint16_t n)
{
    output[0] = (uint8_t)(n & 0xFF);
    output[1] = (uint8_t)(n >> 8);
}

static void pack_int16le(uint8_t *output, int16_t n)
{
    pack_uint16le(output, (uint16_t)n);
}

static void uint8_packer(uint8_t *outSamples, int32_t *samples[8], uint32_t channels, uint32_t frame_size, uint8_t shift)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t usample;
    for (i = 0; i < frame_size; i++)
    {
        for (j = 0; j < channels; j++)
        {
            usample = (uint32_t)samples[j][i];
            usample <<= shift;
            outSamples[1 * ((i * channels) + j)] = (uint8_t)usample;
        }
    }
}

static void int16_packer(uint8_t *outSamples, int32_t *samples[8], uint32_t channels, uint32_t frame_size, uint8_t shift)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t usample;
    for (i = 0; i < frame_size; i++)
    {
        for (j = 0; j < channels; j++)
        {
            usample = (uint32_t)samples[j][i];
            usample <<= shift;
            pack_int16le(&outSamples[2 * ((i * channels) + j)], (int16_t)usample);
        }
    }
}

static void int24_packer(uint8_t *outSamples, int32_t *samples[8], uint32_t channels, uint32_t frame_size, uint8_t shift)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t usample;
    for (i = 0; i < frame_size; i++)
    {
        for (j = 0; j < channels; j++)
        {
            usample = (uint32_t)samples[j][i];
            usample <<= shift;
            pack_int24le(&outSamples[3 * ((i * channels) + j)], (int32_t)usample);
        }
    }
}

/**
 *
 * @brief 下一帧文件，删除长度为 buffer_used 的数据，当buffer内剩余数据小于一个值时消耗fp从文件系统读取数据补充到buffer，并记录读取数量到read_length
 *
 * @note 不检查参数合法性
 * @note 依赖一个全局量来控制buffer剩余多少才开始读取文件
 *
 * @param buffer 输入buffer的
 * @param buffer_ptr 供读取函数使用的在buffer内游动的指针的
 * @param buffer_length buffer长度
 * @param remain buffer剩余数据量
 * @param read_length 累计读取数据量
 * @param buffer_used buffer的用量，将会从buffer中删除对应的数据量
 * @param fp 文件指针
 * @param file_length 文件总长度，用来判断最后一次读取
 *
 * @return -1：没有触发读取 ， 0：触发了读取，但是没有读到文件末尾， 1：触发了读取，读到文件末尾
 *
 */
static int8_t file_next(uint8_t **buffer, uint8_t **buffer_ptr,
                        size_t buffer_length, size_t *remain, size_t *read_length,
                        size_t buffer_used,
                        FILE **fp, size_t file_length)
{

    *remain -= buffer_used;
    *buffer_ptr += buffer_used;

    if (file_length == *read_length)
    {
        ESP_LOGW(TAG, "read data from eof");
    }

    if (*remain > _FLAC_INPUT_BUFFER_RELOAD_LIMIT)
    {
        // dont read
        return -1;
    }

    uint8_t eof = 0;

    memmove(*buffer, *buffer_ptr, *remain);

    *buffer_ptr = (*buffer) + (*remain);

    size_t want_read_length = buffer_length - *remain;

    if ((file_length - *read_length) < want_read_length)
    {
        // last read
        want_read_length = (file_length - *read_length);
        eof = 1;
    }

    fread(*buffer_ptr, 1, want_read_length, *fp);

    *read_length += want_read_length;

    *buffer_ptr = *buffer;
    *remain += want_read_length;

    return eof;
}

static inline esp_err_t _i2s_pause(audio_player_flac_handle_t *handle)
{
    return i2s_channel_disable(handle->phy.tx_chan);
}

static inline esp_err_t _i2s_play(audio_player_flac_handle_t *handle)
{
    return i2s_channel_enable(handle->phy.tx_chan);
}

static inline esp_err_t _i2s_stop(audio_player_flac_handle_t *handle)
{
    if (handle->phy.i2s_inited)
    {
        ESP_LOGI(TAG, "i2s delete");
        handle->phy.i2s_inited = 0;
        return i2s_del_channel(handle->phy.tx_chan);
    }
    return ESP_OK;
}

static esp_err_t _i2s_reset(audio_player_flac_handle_t *handle)
{
    // disable chan
    esp_err_t error = _i2s_stop(handle);
    if (error != ESP_OK)
    {
        ESP_LOGW(TAG, "_i2s_stop func error (i2s not init)");
    }

    // init i2s
    error = i2s_new_channel(&handle->phy.tx_chan_cfg, &handle->phy.tx_chan, NULL);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_new_channel func error");
        goto clean;
    }
    handle->phy.i2s_inited = 1; // chan inited

    // clk
    handle->phy.tx_std_cfg.clk_cfg.sample_rate_hz = handle->info.sample_rate;
    handle->phy.tx_std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    handle->phy.tx_std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384; // may be 24 bpp

    // slot
    // this func depend dac config(philip,lj,rj...)
    i2s_std_slot_config_t slot = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(handle->info.bps, handle->info.channel); // handle->wav.channel);
    handle->phy.tx_std_cfg.slot_cfg = slot;

    ESP_LOGI(TAG, "i2s init args: sample_rate:%d  , ch:%d , bps:%d",
             (int)handle->info.sample_rate, (int)handle->info.channel, (int)handle->info.bps);

    error = i2s_channel_init_std_mode(handle->phy.tx_chan, &handle->phy.tx_std_cfg);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode func error");
        goto clean;
    }

    return error;

clean:

    _i2s_stop(handle);

    return error;
}

static esp_err_t get_meta_data(audio_player_flac_handle_t *handle)
{
    MINIFLAC_RESULT flac_ret;
    uint32_t used;

    miniflac_init(handle->decoder, MINIFLAC_CONTAINER_UNKNOWN);
    // load buffer
    fseek(handle->file, 0, SEEK_SET);
    handle->read_length = 0;
    handle->input_buffer_remain = 0;
    handle->input_buffer_ptr = handle->input_buffer;
    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
              _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
              0, // nou read
              &handle->file, handle->file_length);

    // sync
    do
    {
        flac_ret = miniflac_sync(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used);
        // printf("ret0=%d\r\n", (int)flac_ret);
        if (flac_ret >= 0)
        {
            file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                      _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                      used,
                      &handle->file, handle->file_length);
        }
        else
        {
            ESP_LOGE(TAG, "get_meta_data -> miniflac_sync error %d", (int)flac_ret);
            return ESP_FAIL;
        }
    } while (flac_ret != MINIFLAC_OK);

    /* work our way through the metadata frames */
    while (handle->decoder->state == MINIFLAC_METADATA)
    {
        // printf("metadata block: type: %u, is_last: %u, length: %u\n",
        //        decoder->metadata.header.type_raw,
        //        decoder->metadata.header.is_last,
        //        decoder->metadata.header.length);

        if (handle->decoder->metadata.header.type == MINIFLAC_METADATA_STREAMINFO)
        {
            // max block size
            do
            {
                flac_ret = miniflac_streaminfo_max_block_size(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used, &handle->info.max_block_sz);
                // printf("ret1=%d\r\n", (int)flac_ret);
                if (flac_ret >= 0)
                {
                    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                              _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                              used,
                              &handle->file, handle->file_length);
                }
                else
                {
                    ESP_LOGE(TAG, "get_meta_data -> miniflac_streaminfo_sample_rate error %d", (int)flac_ret);
                    return ESP_FAIL;
                }
            } while (flac_ret != MINIFLAC_OK);
            ESP_LOGI(TAG, "max block size : %d", (int)handle->info.max_block_sz);

            // sample_rate
            do
            {
                flac_ret = miniflac_streaminfo_sample_rate(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used, &handle->info.sample_rate);
                // printf("ret1=%d\r\n", (int)flac_ret);
                if (flac_ret >= 0)
                {
                    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                              _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                              used,
                              &handle->file, handle->file_length);
                }
                else
                {
                    ESP_LOGE(TAG, "get_meta_data -> miniflac_streaminfo_sample_rate error %d", (int)flac_ret);
                    return ESP_FAIL;
                }
            } while (flac_ret != MINIFLAC_OK);
            ESP_LOGI(TAG, "sample_rate : %d", (int)handle->info.sample_rate);

            // channel
            do
            {
                flac_ret = miniflac_streaminfo_channels(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used, &handle->info.channel);
                // printf("ret2=%d\r\n", (int)flac_ret);
                if (flac_ret >= 0)
                {
                    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                              _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                              used,
                              &handle->file, handle->file_length);
                }
                else
                {
                    ESP_LOGE(TAG, "get_meta_data -> miniflac_streaminfo_channels error %d", (int)flac_ret);
                    return ESP_FAIL;
                }
            } while (flac_ret != MINIFLAC_OK);
            ESP_LOGI(TAG, "channel : %d", (int)handle->info.channel);

            // bps
            do
            {
                flac_ret = miniflac_streaminfo_bps(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used, &handle->info.bps);
                // printf("ret3=%d\r\n", (int)flac_ret);
                if (flac_ret >= 0)
                {
                    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                              _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                              used,
                              &handle->file, handle->file_length);
                }
                else
                {
                    ESP_LOGE(TAG, "get_meta_data -> miniflac_streaminfo_bps error %d", (int)flac_ret);
                    return ESP_FAIL;
                }
            } while (flac_ret != MINIFLAC_OK);

            ESP_LOGI(TAG, "bps : %d", (int)handle->info.bps);

        } // MINIFLAC_METADATA_STREAMINFO

        do
        {
            flac_ret = miniflac_sync(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used);
            // printf("ret4=%d\r\n", (int)flac_ret);
            if (flac_ret >= 0)
            {
                file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                          _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                          used,
                          &handle->file, handle->file_length);
            }
            else
            {
                ESP_LOGE(TAG, "get_meta_data -> miniflac_sync 2 error %d", (int)flac_ret);
                return ESP_FAIL;
            }
        } while (flac_ret != MINIFLAC_OK);
    } // while
    return ESP_OK;
}

void _vTaskAudioPlayer(void *arg)
{
    FLAC_DEBUG_MEM("task start");
    audio_player_flac_handle_t *handle = (audio_player_flac_handle_t *)arg;
    esp_err_t error = ESP_OK;
    uint32_t used;
    uint32_t cmd = 0;
    uint8_t last_is_pause = 0;
    uint8_t _is_pause = 0;

    int32_t exit_code = 0;

    MINIFLAC_RESULT flac_ret;

    // get meta data
    get_meta_data(handle);

    // limit(for esp32 s3 48k@24bps is ok , 96k@16bps is fail)
    if (handle->info.bps > 24 || handle->info.channel > 2 || handle->info.max_block_sz > 8192 || handle->info.sample_rate > 48000)
    {
        ESP_LOGE(TAG, "the file not support bu esp32 (bps:%d,ch:%d,max block sz:%d,sr:%d)",
                 (int)handle->info.bps, (int)handle->info.channel, (int)handle->info.max_block_sz, (int)handle->info.sample_rate);
        exit_code = FLAC_PLAYER_EXIT_CODE_UNSUPPORTED_FORMAT;
        goto exit;
    }

    // init i2s
    // 设置音频流格式
    ESP_LOGI(TAG, "i2s init.. , use i2s %d", (int)handle->phy.tx_chan_cfg.id);
    error = _i2s_reset(handle);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "_i2s_reset error");
        exit_code = FLAC_PLAYER_EXIT_CODE_PHY_INIT_FAIL;
        goto exit;
    }
    _i2s_play(handle);

    // prepare play
    uint32_t sampSize = 0;
    uint8_t shift = 0;
    if (handle->info.bps <= 8)
    {
        sampSize = 1;
        shift = 8 - handle->info.bps;
    }
    else if (handle->info.bps <= 16)
    {
        sampSize = 2;
        shift = 16 - handle->info.bps;
    }
    else if (handle->info.bps <= 24)
    {
        sampSize = 3;
        shift = 24 - handle->info.bps;
    }
    else
    {
        abort();
    }

    // alloc raw pcm buffer
    handle->raw_sample = (int32_t **)heap_caps_malloc((sizeof(int32_t *) * 8), FLAC_RAW_BUFFER_MALLOC_CAP);
    if (handle->raw_sample == 0x00)
    {
        ESP_LOGE(TAG, "alloc raw_sample error");
        exit_code = FLAC_PLAYER_EXIT_CODE_ALLOC_FAIL;
        goto exit;
    }
    // clean this buffer
    for (uint8_t i = 0; i < 8; i++)
    {
        handle->raw_sample[i] = 0x00;
    }
    // alloc
    ESP_LOGI(TAG, "prepare alloc %d bytes buffer for raw pcm", (int)(sizeof(int32_t) * handle->info.max_block_sz));
    for (uint8_t i = 0; i < 2; i++)
    {
        handle->raw_sample[i] = (int32_t *)heap_caps_malloc((sizeof(int32_t) * handle->info.max_block_sz), FLAC_RAW_BUFFER_MALLOC_CAP); // 8192 max
        if (handle->raw_sample[i] == 0x00)
        {
            ESP_LOGE(TAG, "alloc raw buffer #%d error", (int)i);
            exit_code = FLAC_PLAYER_EXIT_CODE_ALLOC_FAIL;
            goto exit;
        }
    }
    // alloc packed pcm buffer
    ESP_LOGI(TAG, "prepare alloc %d bytes buffer for packed pcm", (int)(handle->info.channel * sampSize * handle->info.max_block_sz));
    handle->pcm_buffer = (uint8_t *)heap_caps_malloc((handle->info.channel * sampSize * handle->info.max_block_sz), FLAC_PACKED_BUFFER_MALLOC_CAP);
    if (handle->pcm_buffer == 0x00)
    {
        ESP_LOGE(TAG, "alloc pcm buffer error");
        exit_code = FLAC_PLAYER_EXIT_CODE_ALLOC_FAIL;
        goto exit;
    }

start_play:
    // reset read system and decoder
    miniflac_init(handle->decoder, MINIFLAC_CONTAINER_UNKNOWN);
    // load buffer
    fseek(handle->file, 0, SEEK_SET);
    handle->read_length = 0;
    handle->input_buffer_remain = 0;
    handle->input_buffer_ptr = handle->input_buffer;
    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
              _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
              0, // nou read
              &handle->file, handle->file_length);

    for (;;)
    {
        if ((handle->file_length - handle->read_length) <= 0 && (handle->input_buffer_remain == 0))
        {
            ESP_LOGI(TAG, "file finish");
            if (handle->cmd.loop == FLAC_LOOP_FILE)
            {
                goto start_play;
            }
            else
            {
                exit_code = FLAC_PLAYER_EXIT_CODE_OK;
                goto exit;
            }

        } // finish

        if (xQueueReceive(handle->cmd._xQueueCommand, &cmd, 0) == pdPASS)
        {
            switch (cmd)
            {
            case FLAC_PLAYER_CMD_PAUSE:
                _is_pause = 1;
                break;
            case FLAC_PLAYER_CMD_STOP:
                exit_code = FLAC_PLAYER_EXIT_CODE_OK;
                goto exit;
                break;
            case FLAC_PLAYER_CMD_CONTINUE_PLAY:
                _is_pause = 0;
                break;

            default:
                break;
            }
        } // xQueueReceive

        if (xSemaphoreTake(handle->cmd.xSemStatusFree, 0) == pdPASS)
        {
            // total length set in play func
            handle->status.reade_length = handle->read_length;
            xSemaphoreGive(handle->cmd.xSemStatusFree);
        } // xSemaphoreTake

        if (_is_pause == 0)
        {

            if (last_is_pause != 0)
            {
                last_is_pause = 0;
                handle->status.isPause = 0;
                ESP_LOGI(TAG, "continue");
                _i2s_play(handle);
            }
            /*====================================FLAC_MAIN=====================*/

            do
            {
                flac_ret = miniflac_decode(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used, handle->raw_sample);
                // printf("ret0=%d\r\n", (int)flac_ret);
                if (flac_ret >= 0)
                {
                    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                              _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                              used,
                              &handle->file, handle->file_length);
                }
                else
                {
                    ESP_LOGE(TAG, "get_meta_data -> miniflac_sync error %d", (int)flac_ret);
                    exit_code = FLAC_PLAYER_EXIT_CODE_UNSUPPORTED_FORMAT;
                    goto exit;
                }
            } while (flac_ret != MINIFLAC_OK);
            // process pcm
            if (handle->info.bps <= 8)
            {
                uint8_packer(handle->pcm_buffer, handle->raw_sample, handle->info.channel, handle->decoder->frame.header.block_size, shift);
            }
            else if (handle->info.bps <= 16)
            {
                int16_packer(handle->pcm_buffer, handle->raw_sample, handle->info.channel, handle->decoder->frame.header.block_size, shift);
            }
            else if (handle->info.bps <= 24)
            {
                int24_packer(handle->pcm_buffer, handle->raw_sample, handle->info.channel, handle->decoder->frame.header.block_size, shift);
            }
            // push pcm
            size_t w_bytes;
            size_t want = handle->decoder->frame.header.block_size * handle->info.channel * sampSize;
            i2s_channel_write(handle->phy.tx_chan, handle->pcm_buffer, want, &w_bytes, portMAX_DELAY);
            if (w_bytes != want)
            {
                ESP_LOGW(TAG, "not equal   %d     %d", w_bytes, want);
            }

            // example has this code but it cause inf loop in file play end... and delete it has not influence to play....
            //  sync
            //  do
            //  {
            //      flac_ret = miniflac_sync(handle->decoder, handle->input_buffer_ptr, handle->input_buffer_remain, &used);
            //      // printf("ret0=%d\r\n", (int)flac_ret);
            //      if (flac_ret >= 0)
            //      {
            //          file_next(&handle->input_buffer, &handle->input_buffer_ptr,
            //                    _FLAC_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
            //                    used,
            //                    &handle->file, handle->file_length);
            //      }
            //      else
            //      {
            //          ESP_LOGE(TAG, "get_meta_data -> miniflac_sync error %d", (int)flac_ret);
            //          goto exit;
            //      }
            //  } while (flac_ret != MINIFLAC_OK);

            /*====================================END===========================*/

        } //_is_pause == 0
        else
        {
            if (last_is_pause == 0)
            {
                last_is_pause = 1;
                handle->status.isPause = 1;
                ESP_LOGI(TAG, "pause");
                _i2s_pause(handle);
            }
        } //_is_pause != 0

        vTaskDelay(1);
    } // for
exit:
    if (handle->phy.tx_chan)
    {
        _i2s_pause(handle);
    }

    //_i2s_stop(handle);

    if (handle->file != 0x00)
    {
        fclose(handle->file);
        handle->file = 0x00;
    }

    if (handle->fileName != 0x00)
    {
        free(handle->fileName);
        handle->fileName = 0x00;
    }

    if (handle->raw_sample != 0x00)
    {
        if (handle->raw_sample[0] != 0x00)
        {
            free(handle->raw_sample[0]);
        }
        if (handle->raw_sample[1] != 0x00)
        {
            free(handle->raw_sample[1]);
        }
        free(handle->raw_sample);
    }

    if (handle->pcm_buffer != 0x00)
    {
        free(handle->pcm_buffer);
        handle->pcm_buffer = 0x00;
    }

    if (handle->input_buffer != 0x00)
    {
        free(handle->input_buffer);
        handle->input_buffer = 0x00;
    }

    if (handle->decoder != 0x00)
    {
        free(handle->decoder);
        handle->decoder = 0x00;
    }

    memset(&handle->info, 0x00, sizeof(flac_player_info_t));

    if (handle->cmd._playerTaskExit != NULL)
    {
        handle->cmd._playerTaskExit(exit_code, handle->user);
    } //_playerTaskExit

    ESP_LOGI(TAG, "task exit");
    FLAC_DEBUG_MEM("task exit");

    handle->status.isBusy = 0;

    vTaskDelete(NULL);
}

esp_err_t audio_player_flac_init(audio_player_flac_handle_t *handle)
{
    memset(handle, 0x00, sizeof(audio_player_flac_handle_t));

    handle->cmd._xQueueCommand = xQueueCreate(2, sizeof(uint32_t));
    handle->cmd.xSemStatusFree = xSemaphoreCreateMutex();
    xSemaphoreGive(handle->cmd.xSemStatusFree);

    return ESP_OK;
}

esp_err_t audio_player_flac_play_file(audio_player_flac_handle_t *handle, const char *file_dir)
{
    FLAC_DEBUG_MEM("play start");
    esp_err_t error = ESP_OK;
    uint8_t already_running = 0;

    // check player free
    if (handle->status.isBusy != 0)
    {
        ESP_LOGE(TAG, "player busy");
        error = ESP_ERR_NOT_FINISHED;
        already_running = 1;
        goto clean;
    }

    error = checkFileExist(file_dir);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "file not found");
        goto clean;
    } // error != ESP_OK

    // open file
    handle->file = fopen(file_dir, "rb");
    if (handle->file == 0x00)
    {
        ESP_LOGE(TAG, "file open fail");
        error = ESP_ERR_NOT_FOUND;
        goto clean;
    } // handle->audio_file_handle == 0x00

    fseek(handle->file, 0, SEEK_END);
    handle->file_length = ftell(handle->file);
    fseek(handle->file, 0, SEEK_SET);

    handle->status.total_length = handle->file_length;

    // alloc filename
    int file_name_len = strlen(file_dir);
    // rear use ,send it to spiram
    handle->fileName = heap_caps_malloc(file_name_len + 1, MALLOC_CAP_SPIRAM);
    if (handle->fileName == 0x00)
    {
        ESP_LOGE(TAG, "alloc filename buffer fail");
        goto clean;
    }
    strncpy(handle->fileName, file_dir, file_name_len);
    handle->fileName[file_name_len] = '\0';

    // alloc input buffer
    handle->input_buffer = (uint8_t *)heap_caps_malloc((_FLAC_INPUT_BUFFER_SIZE * sizeof(uint8_t)), FLAC_INPUT_BUFFER_MALLOC_CAP);
    if (handle->input_buffer == 0)
    {
        ESP_LOGE(TAG, "handle->input_buffer alloc error %d bytes", (int)(_FLAC_INPUT_BUFFER_SIZE * sizeof(uint8_t)));
        error = ESP_ERR_NO_MEM;
        goto clean;
    }
    handle->input_buffer_ptr = handle->input_buffer;

    // alloc flac decoder
    handle->decoder = (miniflac_t *)heap_caps_malloc(miniflac_size(), FLAC_DECODER_MALLOC_CAP);
    if (handle->decoder == 0x00)
    {
        ESP_LOGE(TAG, "alloc decoder error");
        error = ESP_ERR_NOT_FOUND;
        goto clean;
    }

    // set to busy
    handle->status.isBusy = 1;

    // creat task
    ESP_LOGI(TAG, "create player task");

    BaseType_t ret = xTaskCreatePinnedToCore(_vTaskAudioPlayer,          // code
                                             "flac",                     // name
                                             16384,                      // stack
                                             handle,                     // arg
                                             3,                          // priority
                                             &handle->status.playerTask, // handle
                                             1);                         // core
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "task create fail");
        error = ESP_FAIL;
        goto clean;
    }

    return error;

clean:
    if (already_running == 0)
    {
        if (handle->file != 0x00)
        {
            fclose(handle->file);
            handle->file = 0x00;
        }

        if (handle->fileName != 0x00)
        {
            free(handle->fileName);
            handle->fileName = 0x00;
        }

        // pcm buffer alloc in task

        if (handle->input_buffer != 0)
        {
            free(handle->input_buffer);
            handle->input_buffer = 0x00;
        }

        if (handle->decoder != 0)
        {
            free(handle->decoder);
            handle->decoder = 0x00;
        }

        handle->status.isBusy = 0;
    } //! already_running
    else
    {
        ESP_LOGE(TAG, "player busy ,dont touch handle");
    } // already_running

    ESP_LOGE(TAG, "player task not create , error is %s", esp_err_to_name(error));
    return error;
}

esp_err_t audio_player_flac_set_i2s_config(audio_player_flac_handle_t *handle, flac_player_physical_t phy)
{
    // check player free
    esp_err_t error = ESP_OK;
    if (handle->status.isBusy != 0)
    {
        ESP_LOGE(TAG, "player busy");
        error = ESP_ERR_NOT_FINISHED;
        // goto clean;
    }
    else
    {
        // handle->phy = config;
        uint8_t inited = handle->phy.i2s_inited;
        memcpy(&handle->phy, &phy, sizeof(flac_player_physical_t));
        handle->phy.i2s_inited = inited;
    }

    return error;
}
esp_err_t audio_player_flac_set_loop_mode(audio_player_flac_handle_t *handle, flac_player_loop_mode_t mode)
{
    handle->cmd.loop = mode;
    return ESP_OK;
}

esp_err_t audio_player_flac_set_pause(audio_player_flac_handle_t *handle, uint8_t pause)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t cmd;

    if (pause)
    {
        cmd = FLAC_PLAYER_CMD_PAUSE;
    }
    else
    {
        cmd = FLAC_PLAYER_CMD_CONTINUE_PLAY;
    }

    xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);

    return ESP_OK;
}
esp_err_t audio_player_flac_set_stop(audio_player_flac_handle_t *handle)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t cmd = FLAC_PLAYER_CMD_STOP;

    xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t audio_player_flac_get_file_info(audio_player_flac_handle_t *handle, flac_player_info_t *info)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(info, &handle->info, sizeof(flac_player_info_t));

    return ESP_OK;
}

esp_err_t audio_player_flac_get_file_name(audio_player_flac_handle_t *handle, const char **name)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }
    *name = (const char *)handle->fileName;
    return ESP_OK;
}
esp_err_t audio_player_flac_get_status(audio_player_flac_handle_t *handle, flac_player_status_t *stat)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGD(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(handle->cmd.xSemStatusFree, portMAX_DELAY) == pdPASS)
    {

        memcpy(stat, &handle->status, sizeof(flac_player_status_t));
        xSemaphoreGive(handle->cmd.xSemStatusFree);
    }
    return ESP_OK;
}

uint8_t audio_player_flac_get_busy(audio_player_flac_handle_t *handle)
{
    return handle->status.isBusy;
}

esp_err_t audio_player_flac_wait_task_exit(audio_player_flac_handle_t *handle)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }
    for (;;)
    {
        if (handle->status.isBusy == 0)
        {
            return ESP_OK;
        }
        vTaskDelay(1);
    }
    return ESP_OK;
}

esp_err_t audio_player_flac_set_task_exit_event_cb(audio_player_flac_handle_t *handle, void (*cb)(int32_t code, void *data))
{
    handle->cmd._playerTaskExit = cb;
    return ESP_OK;
}
esp_err_t audio_player_flac_set_user_data(audio_player_flac_handle_t *handle, void *user)
{
    handle->user = user;
    return ESP_OK;
}
// eof
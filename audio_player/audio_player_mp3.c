#include "audio_player_mp3.h"

static const char *TAG = "mp3_player";

#define MP3_DEBUG_MEM(x) ESP_LOGI(TAG, "%s: all : %fk , internal %fk , minimum %fk  all-internal %fk", x, esp_get_free_heap_size() / 1000.0, esp_get_free_internal_heap_size() / 1000.0, esp_get_minimum_free_heap_size() / 1000.0, (esp_get_free_heap_size() - esp_get_free_internal_heap_size()) / 1000.0);


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

    if (*remain > _MP3_INPUT_BUFFER_RELOAD_LIMIT)
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

static inline esp_err_t _i2s_pause(audio_player_mp3_handle_t *handle)
{
    return i2s_channel_disable(handle->phy.tx_chan);
}

static inline esp_err_t _i2s_play(audio_player_mp3_handle_t *handle)
{
    return i2s_channel_enable(handle->phy.tx_chan);
}

static inline esp_err_t _i2s_stop(audio_player_mp3_handle_t *handle)
{
    if (handle->phy.i2s_inited)
    {
        ESP_LOGI(TAG, "i2s delete");
        handle->phy.i2s_inited = 0;
        return i2s_del_channel(handle->phy.tx_chan);
    }
    return ESP_OK;
}

static esp_err_t _i2s_reset(audio_player_mp3_handle_t *handle)
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
    handle->phy.tx_std_cfg.clk_cfg.sample_rate_hz = handle->mp3d_info.hz;
    handle->phy.tx_std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    handle->phy.tx_std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256; // mp3 use 16bpp

    // slot
    // this func depend dac config(philip,lj,rj...)
    i2s_std_slot_config_t slot = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(16, handle->mp3d_info.channels); // handle->wav.channel);
    handle->phy.tx_std_cfg.slot_cfg = slot;

    ESP_LOGI(TAG, "i2s init args: sample_rate:%d  , ch:%d , layer:%d , byterate:%d",
             (int)handle->mp3d_info.hz, (int)handle->mp3d_info.channels, (int)handle->mp3d_info.layer, (int)handle->mp3d_info.bitrate_kbps);

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

static void _vTaskAudioPlayer(void *arg)
{
    // ESP_LOGI(TAG, "task start mem");
    // _MP3_DEBUG_MEM();
    MP3_DEBUG_MEM("task start");

    audio_player_mp3_handle_t *handle = (audio_player_mp3_handle_t *)arg;
    esp_err_t error = ESP_OK;
    int32_t exit_code = 0;

    mp3dec_init(handle->mp3d);

    // file length set by startup func

    // fread(handle->input_buffer, 1, _MP3_INPUT_BUFFER_SIZE, handle->file);
    // handle->read_length = _MP3_INPUT_BUFFER_SIZE;
    // handle->input_buffer_remain = _MP3_INPUT_BUFFER_SIZE;
    // handle->input_buffer_ptr = handle->input_buffer;

    handle->read_length = 0;
    handle->input_buffer_remain = 0;
    handle->input_buffer_ptr = handle->input_buffer;
    file_next(&handle->input_buffer, &handle->input_buffer_ptr,
              _MP3_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
              0, // nou read
              &handle->file, handle->file_length);

    // ESP_LOGI(TAG, "after malloc mem");
    // _MP3_DEBUG_MEM();

    uint32_t cmd = 0;

    uint8_t last_is_pause = 0;
    uint8_t _is_pause = 0;

    for (;;)
    {
        if ((handle->file_length - handle->read_length) <= 0 && (handle->input_buffer_remain == 0))
        {
            ESP_LOGI(TAG, "file finish");
            if (handle->cmd.loop == MP3_FILE_SINGLE)
            {
                exit_code = MP3_PLAYER_EXIT_CODE_OK;
                goto exit;
            }
            else
            {
                mp3dec_init(handle->mp3d);
                fseek(handle->file, 0, SEEK_SET);
                fread(handle->input_buffer, 1, _MP3_INPUT_BUFFER_SIZE, handle->file);
                handle->read_length = _MP3_INPUT_BUFFER_SIZE;
                handle->input_buffer_remain = _MP3_INPUT_BUFFER_SIZE;
                handle->input_buffer_ptr = handle->input_buffer;
            }

        } // finish

        if (xQueueReceive(handle->cmd._xQueueCommand, &cmd, 0) == pdPASS)
        {
            switch (cmd)
            {
            case MP3_PLAYER_CMD_PAUSE:
                _is_pause = 1;
                break;
            case MP3_PLAYER_CMD_STOP:
                exit_code = MP3_PLAYER_EXIT_CODE_OK;
                goto exit;
                break;
            case MP3_PLAYER_CMD_CONTINUE_PLAY:
                _is_pause = 0;
                break;

            default:
                break;
            }
        } // xQueueReceive

        if (_is_pause == 0)
        {

            if (last_is_pause != 0)
            {
                last_is_pause = 0;
                handle->status.isPause = 0;
                ESP_LOGI(TAG, "continue");
                _i2s_play(handle);
            }

            size_t samples = mp3dec_decode_frame(handle->mp3d, handle->input_buffer_ptr, handle->input_buffer_remain, (mp3d_sample_t *)handle->decoded_pcm, &handle->mp3d_info);

            if (handle->mp3d_info.frame_bytes > 0)
            {

                // handle->input_buffer_remain -= handle->mp3d_info.frame_bytes; // decrease remain
                // handle->input_buffer_ptr += handle->mp3d_info.frame_bytes;    // pinter add

                int8_t next_ret = file_next(&handle->input_buffer, &handle->input_buffer_ptr,
                                            _MP3_INPUT_BUFFER_SIZE, &handle->input_buffer_remain, &handle->read_length,
                                            handle->mp3d_info.frame_bytes,
                                            &handle->file, handle->file_length);

                if (next_ret >= 0)
                {
                    if (xSemaphoreTake(handle->cmd.xSemStatusFree, 0) == pdPASS)
                    {
                        // update status
                        handle->status.reade_length = handle->read_length;
                        xSemaphoreGive(handle->cmd.xSemStatusFree);
                    }
                    if (next_ret == 1)
                    {
                        ESP_LOGI(TAG, "last read");
                    }
                }
                // if (handle->input_buffer_remain < _MP3_INPUT_BUFFER_RELOAD_LIMIT)
                // {
                //     // data less than INPUT_BUFFER_RELOAD_LIMIT
                //     memmove(handle->input_buffer, handle->input_buffer_ptr, handle->input_buffer_remain);
                //     handle->input_buffer_ptr = handle->input_buffer + handle->input_buffer_remain;
                //     size_t next_read_length = (_MP3_INPUT_BUFFER_SIZE - handle->input_buffer_remain); // calc read max buffer need how many data
                //     if ((handle->file_length - handle->read_length) < next_read_length)
                //     {
                //         // last chunk re calc file remain data length and read all this data to buffer
                //         next_read_length = handle->file_length - handle->read_length;
                //     }
                //     handle->read_length += next_read_length;                            // increase read length
                //     fread(handle->input_buffer_ptr, 1, next_read_length, handle->file); // read file
                //     handle->input_buffer_ptr = handle->input_buffer;                    // set part_buffer_ptr to head
                //     handle->input_buffer_remain += next_read_length;                    // update part_buffer_len to mach new part_buffer
                // } // part_buffer_len < INPUT_BUFFER_RELOAD_LIMIT

                if (handle->mp3d_info.hz > 0)
                {
                    if (handle->info.info_init == 0)
                    {
                        handle->info.info_init = 1;
                        ESP_LOGI(TAG, "sr = %d", (int)handle->mp3d_info.hz);
                        // 设置音频流格式
                        ESP_LOGI(TAG, "i2s init.. , use i2s %d", (int)handle->phy.tx_chan_cfg.id);
                        error = _i2s_reset(handle);
                        if (error != ESP_OK)
                        {
                            ESP_LOGE(TAG, "_i2s_reset error");
                            exit_code = MP3_PLAYER_EXIT_CODE_PHY_INIT_FAIL;
                            ;
                            goto exit;
                        }
                        _i2s_play(handle);
                        handle->info.bitrate = handle->mp3d_info.bitrate_kbps;
                        handle->info.channel = handle->mp3d_info.channels;
                        handle->info.layer = handle->mp3d_info.layer;
                        handle->info.hz = handle->mp3d_info.hz;
                    }
                } // hz > 0

                if (samples > 0)
                {
                    // 播放录音
                    if (handle->info.info_init == 1)
                    {
                        size_t w_bytes;
                        size_t want = samples * 2 * sizeof(uint16_t);
                        i2s_channel_write(handle->phy.tx_chan, handle->decoded_pcm, want, &w_bytes, portMAX_DELAY);
                        if (w_bytes != want)
                        {
                            ESP_LOGW(TAG, "not equal   %d     %d", w_bytes, want);
                        }
                    }

                } // samples > 0
            }     // frame_bytes > 0
        }         //_is_pause == 0
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

        // vTaskDelay(1);
    } // for

// clean
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

    if (handle->decoded_pcm != 0)
    {
        free(handle->decoded_pcm);
        handle->decoded_pcm = 0x00;
    }

    if (handle->input_buffer != 0)
    {
        free(handle->input_buffer);
        handle->input_buffer = 0x00;
    }

    if (handle->mp3d != 0)
    {
        free(handle->mp3d);
        handle->mp3d = 0x00;
    }

    memset(&handle->mp3d_info, 0x00, sizeof(mp3dec_frame_info_t));
    memset(&handle->info, 0x00, sizeof(mp3_player_info_t));

    if (handle->cmd._playerTaskExit != NULL)
    {
        handle->cmd._playerTaskExit(exit_code, handle->user);
    } //_playerTaskExit

    ESP_LOGI(TAG, "task exit");
    // _MP3_DEBUG_MEM();
    MP3_DEBUG_MEM("task exit");

    handle->status.isBusy = 0;

    vTaskDelete(NULL);
}

esp_err_t audio_player_mp3_init(audio_player_mp3_handle_t *handle)
{
    memset(handle, 0x00, sizeof(audio_player_mp3_handle_t));

    handle->cmd._xQueueCommand = xQueueCreate(2, sizeof(uint32_t));
    handle->cmd.xSemStatusFree = xSemaphoreCreateMutex();
    xSemaphoreGive(handle->cmd.xSemStatusFree);

    return ESP_OK;
}

esp_err_t audio_player_mp3_play_file(audio_player_mp3_handle_t *handle, const char *file_dir)
{
    // ESP_LOGI(TAG, "before start mem");
    // _MP3_DEBUG_MEM();
    MP3_DEBUG_MEM("play start");

    esp_err_t error;
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

    // alloc data
    handle->decoded_pcm = (uint16_t *)heap_caps_malloc((MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(uint16_t)), MP3_PCM_BUFFER_MALLOC_CAP);
    if (handle->decoded_pcm == 0)
    {
        ESP_LOGE(TAG, "handle->decoded_pcm alloc error %d bytes", (int)(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(uint16_t)));
        error = ESP_ERR_NO_MEM;
        goto clean;
    }

    handle->input_buffer = (uint8_t *)heap_caps_malloc((_MP3_INPUT_BUFFER_SIZE * sizeof(uint8_t)), MP3_INPUT_BUFFER_MALLOC_CAP);
    if (handle->input_buffer == 0)
    {
        ESP_LOGE(TAG, "handle->input_buffer alloc error %d bytes", (int)(_MP3_INPUT_BUFFER_SIZE * sizeof(uint8_t)));
        error = ESP_ERR_NO_MEM;
        goto clean;
    }
    handle->input_buffer_ptr = handle->input_buffer;

    handle->mp3d = (mp3dec_t *)heap_caps_malloc(sizeof(mp3dec_t), MP3_DECODER_MALLOC_CAP);
    if (handle->mp3d == 0)
    {
        ESP_LOGE(TAG, "handle->mp3d alloc error %d bytes", (int)sizeof(mp3dec_t));
        error = ESP_ERR_NO_MEM;
        goto clean;
    }

    // set to busy
    handle->status.isBusy = 1;

    // start task
    ESP_LOGI(TAG, "create player task");
    BaseType_t ret = xTaskCreatePinnedToCore(_vTaskAudioPlayer,          // code
                                             "mp3",                      // name
                                             32768,                      // stack
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

    return ESP_OK;

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

        if (handle->decoded_pcm != 0)
        {
            free(handle->decoded_pcm);
            handle->decoded_pcm = 0x00;
        }

        if (handle->input_buffer != 0)
        {
            free(handle->input_buffer);
            handle->input_buffer = 0x00;
        }

        if (handle->mp3d != 0)
        {
            free(handle->mp3d);
            handle->mp3d = 0x00;
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

esp_err_t audio_player_mp3_set_i2s_config(audio_player_mp3_handle_t *handle, mp3_player_physical_t phy)
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
        memcpy(&handle->phy, &phy, sizeof(mp3_player_physical_t));
        handle->phy.i2s_inited = inited;
    }

    return error;
}
esp_err_t audio_player_mp3_set_loop_mode(audio_player_mp3_handle_t *handle, mp3_player_loop_mode_t mode)
{
    handle->cmd.loop = mode;
    return ESP_OK;
}

esp_err_t audio_player_mp3_set_pause(audio_player_mp3_handle_t *handle, uint8_t pause)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t cmd;

    if (pause)
    {
        cmd = MP3_PLAYER_CMD_PAUSE;
    }
    else
    {
        cmd = MP3_PLAYER_CMD_CONTINUE_PLAY;
    }

    xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);

    return ESP_OK;
}
esp_err_t audio_player_mp3_set_stop(audio_player_mp3_handle_t *handle)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t cmd = MP3_PLAYER_CMD_STOP;

    xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t audio_player_mp3_get_file_info(audio_player_mp3_handle_t *handle, mp3_player_info_t *info,TickType_t max_delay)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    // if (handle->info.info_init == 0)
    // {
    //     ESP_LOGE(TAG, "info not init");
    //     return ESP_ERR_NOT_FOUND;
    // }
    TickType_t i = 0;
    while (handle->info.info_init == 0)
    {
        vTaskDelay(1);
        i++;
        if (i > max_delay)
        {
            return ESP_ERR_TIMEOUT;
        }
    }

    memcpy(info, &handle->info, sizeof(mp3_player_info_t));

    return ESP_OK;
}

esp_err_t audio_player_mp3_get_file_name(audio_player_mp3_handle_t *handle, const char **name)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }
    *name = (const char *)handle->fileName;
    return ESP_OK;
}
esp_err_t audio_player_mp3_get_status(audio_player_mp3_handle_t *handle, mp3_player_status_t *stat)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGD(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(handle->cmd.xSemStatusFree, portMAX_DELAY) == pdPASS)
    {

        memcpy(stat, &handle->status, sizeof(mp3_player_status_t));
        xSemaphoreGive(handle->cmd.xSemStatusFree);
    }

    return ESP_OK;
}

uint8_t audio_player_mp3_get_busy(audio_player_mp3_handle_t *handle)
{
    return handle->status.isBusy;
}

esp_err_t audio_player_mp3_wait_task_exit(audio_player_mp3_handle_t *handle)
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

esp_err_t audio_player_mp3_set_task_exit_event_cb(audio_player_mp3_handle_t *handle, void (*cb)(int32_t code, void *data))
{
    handle->cmd._playerTaskExit = cb;
    return ESP_OK;
}

esp_err_t audio_player_mp3_set_user_data(audio_player_mp3_handle_t *handle, void *user)
{
    handle->user = user;
    return ESP_OK;
}
// eof
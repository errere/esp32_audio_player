#include "audio_player_wav.h"

static const char *TAG = "wav_player";

#define WAV_DEBUG_MEM(x) ESP_LOGI(TAG, "%s: all : %fk , internal %fk , minimum %fk  all-internal %fk", x, esp_get_free_heap_size() / 1000.0, esp_get_free_internal_heap_size() / 1000.0, esp_get_minimum_free_heap_size() / 1000.0, (esp_get_free_heap_size() - esp_get_free_internal_heap_size()) / 1000.0);


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

static esp_err_t wavDecode(FILE **wav_file_handle, Simplified_Wav_struct_t *dst)
{
    // https://blog.csdn.net/imxiangzi/article/details/80265978

    General_Chunk_t audioData;
    fseek(*wav_file_handle, 0L, SEEK_SET);
    fread(audioData.stream, 1, 44, *wav_file_handle);
    ESP_LOGI("wav_decode", "AudioFormat:%d,sampleRate:%d,BitsPerSample:%d", (int)audioData.str.AudioFormat, (int)audioData.str.SampleRate, (int)audioData.str.BitsPerSample);
    dst->fmt = audioData.str.AudioFormat;
    dst->bitPerSample = audioData.str.BitsPerSample;
    dst->channel = audioData.str.NumChannels;
    if (audioData.str.AudioFormat != 1 ||
        strncmp(audioData.str.ChunkID, "RIFF", 4) != 0 ||
        strncmp(audioData.str.Format, "WAVE", 4) != 0 ||
        strncmp(audioData.str.SubChunk1ID, "fmt ", 4) != 0)
    {
        // format not support
        ESP_LOGE("wav_decode", "wave file error,ChunkID:%.4s,FormatID:%.4s,SubChunk1ID:%.4s,AudioFormat:%x", audioData.str.ChunkID, audioData.str.Format, audioData.str.SubChunk1ID, (int)audioData.str.AudioFormat);
        fseek(*wav_file_handle, 0L, SEEK_SET);
        return ESP_ERR_NOT_SUPPORTED;
    }

    dst->sampleRate = audioData.str.SampleRate;
    dst->length = audioData.str.SubChunk2Size;
    dst->entryPoint = ftell(*wav_file_handle);

    if (strncmp(audioData.str.SubChunk2ID, "data", 4) != 0)
    {
        ChunkHeader_t hd;
        fseek(*wav_file_handle, audioData.str.SubChunk2Size, SEEK_CUR);
        while (1)
        {
            fread(hd.data, 1, 8, *wav_file_handle);
            ESP_LOGW("wav_decode", "extern chunk:%c%c%c%c,Size:%x", hd.str.ID[0], hd.str.ID[1], hd.str.ID[2], hd.str.ID[3], (int)hd.str.size);
            if (strncmp(hd.str.ID, "data", 4) == 0)
            {
                dst->length = hd.str.size;
                dst->entryPoint = ftell(*wav_file_handle);
                break;
            }
            fseek(*wav_file_handle, hd.str.size, SEEK_CUR);
        }
    }

    ESP_LOGI("wav_decode", "sampleSize:%x,entryPoint:%x", (int)dst->length, (int)dst->entryPoint);
    dst->totalSample = (dst->length / ((dst->bitPerSample / 8) * dst->channel));

    fseek(*wav_file_handle, 0L, SEEK_SET);

    return ESP_OK;
}

static inline esp_err_t _i2s_pause(audio_player_physical_handle_t *phy)
{
    return i2s_channel_disable(phy->tx_chan);
}

static inline esp_err_t _i2s_play(audio_player_physical_handle_t *phy)
{
    return i2s_channel_enable(phy->tx_chan);
}

static esp_err_t _i2s_stop(audio_player_physical_handle_t *phy)
{
    if (phy->i2s_inited)
    {
        phy->i2s_inited = 0;
        return i2s_del_channel(phy->tx_chan);
    }
    return ESP_OK;
}

static esp_err_t _i2s_reset(audio_player_wav_handle_t *handle)
{
    esp_err_t error = _i2s_stop(&handle->phy);
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
    handle->phy.tx_std_cfg.clk_cfg.sample_rate_hz = handle->wav.sampleRate;
    handle->phy.tx_std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    handle->phy.tx_std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384; // for 24 bit
    // slot
    // this func depend dac config(philip,lj,rj...)
    i2s_std_slot_config_t slot = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(handle->wav.bitPerSample, handle->wav.channel); // handle->wav.channel);
    handle->phy.tx_std_cfg.slot_cfg = slot;

    ESP_LOGI(TAG, "i2s init args: sample_rate:%ld , bits_per_sample:%d , ch:%d",
             handle->wav.sampleRate, (int)handle->wav.bitPerSample, (int)handle->wav.channel);

    error = i2s_channel_init_std_mode(handle->phy.tx_chan, &handle->phy.tx_std_cfg);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode func error");
        goto clean;
    }

    return error;

clean:
    _i2s_stop(&handle->phy);

    return error;
}

static void _vTaskAudioPlayer(void *arg)
{

    WAV_DEBUG_MEM("task start");

    int32_t exit_code = 0;
    esp_err_t error;
    uint32_t sample_buffer_length;
    uint32_t cmd;

    audio_player_wav_handle_t *handle = (audio_player_wav_handle_t *)arg;
    ESP_LOGI(TAG, "in task handle address in %p", handle);

    if (handle->cmd._playerTaskEntry != NULL)
    {
        exit_code = handle->cmd._playerTaskEntry(handle->wav);
        if (exit_code < 0)
        {
            ESP_LOGE(TAG, "exit by user");
            exit_code = WAV_PLAYER_EXIT_CODE_USER;
            goto exit;
        }
    } //_playerTaskEntry

    // i2s init
    ESP_LOGI(TAG, "i2s init.. , use i2s %d", (int)handle->phy.tx_chan_cfg.id);

    // idf 5 i2s init

    error = _i2s_reset(handle);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_driver_install fail");
        exit_code = WAV_PLAYER_EXIT_CODE_PHY_INIT_FAIL;
        goto exit;
    } // error != ESP_OK

    // alloc buffer for file read
    sample_buffer_length = (64 * handle->wav.channel * handle->wav.bitPerSample);
    ESP_LOGI(TAG, "alloc buffer size is %ld", sample_buffer_length);
    handle->sample_buffer = (uint8_t *)heap_caps_malloc(sample_buffer_length, WAV_PCM_BUFFER_MALLOC_CAP);
    if (handle->sample_buffer == 0x00)
    {
        ESP_LOGE(TAG, "alloc audio buffer fail");
        exit_code = WAV_PLAYER_EXIT_CODE_ALLOC_FAIL;
        goto exit;
    }

    // prepare file read
    uint32_t sampleLength = handle->wav.length;
    uint32_t entryPoint = handle->wav.entryPoint;
    uint8_t _isPause = 0;
    uint8_t _isLastPause = 1;
    size_t written;

    fseek(handle->audio_file_handle, entryPoint, SEEK_SET);

    /*========================================main loop========================================*/
    for (;;)
    {
        if (xQueueReceive(handle->cmd._xQueueCommand, &cmd, 0) == pdPASS)
        {
            switch (cmd)
            {
            case PLAYER_CMD_PAUSE:
                _isPause = 1;
                break;
            case PLAYER_CMD_STOP:
                exit_code = WAV_PLAYER_EXIT_CODE_OK;
                goto exit;
                break;
            case PLAYER_CMD_CONTINUE_PLAY:
                _isPause = 0;
                break;
            case PLAYER_CMD_JUMP:
                fseek(handle->audio_file_handle, handle->cmd.requestPosition, SEEK_SET);
                break;

            default:
                ESP_LOGE(TAG, "unknow cmd %ld", cmd);
                break;
            } // switch
        }     // process command

        handle->status.isPause = _isPause;

        if (_isPause == 0)
        {
            if (_isLastPause == 1)
            {
                _isLastPause = 0;
                ESP_LOGI(TAG, "enable i2s");
                _i2s_play(&handle->phy);
            } //_isLastPause
            uint32_t current_pos = ftell(handle->audio_file_handle);
            if (xSemaphoreTake(handle->cmd._xMutexProcessFree, 0) == pdPASS)
            {
                handle->status.playProcess = ((current_pos - handle->wav.entryPoint) / (handle->wav.channel * (handle->wav.bitPerSample / 8)));
                xSemaphoreGive(handle->cmd._xMutexProcessFree);
            }
            if (current_pos + sample_buffer_length > sampleLength)
            {
                // last chunk
                uint32_t lastC = sampleLength - current_pos;
                fread(handle->sample_buffer, 1, lastC, handle->audio_file_handle);
                error = i2s_channel_write(handle->phy.tx_chan, (const char *)handle->sample_buffer, lastC, &written, 1000);
                if (error != ESP_OK)
                {
                    ESP_LOGE(TAG, "i2s_channel_write error at %s", esp_err_to_name(error));
                }
                // i2s_write(handle->phy.i2s_dev_num, (const char *)handle->sample_buffer, lastC, &written, portMAX_DELAY);
                // i2s_write_expand(handle->phy.i2s_dev_num, (const char *)sample_buffer, lastC,16,24, &written, portMAX_DELAY);//for adau debug use
                if (written != lastC)
                {
                    ESP_LOGW(TAG, "written not equal req , w = %d , r = %ld", written, lastC);
                }
                // check loop setting
                if (handle->cmd.loop == LOOP_FILE)
                {
                    // loop
                    fseek(handle->audio_file_handle, entryPoint, SEEK_SET);
                    _i2s_pause(&handle->phy);
                    ESP_LOGI(TAG, "file end , but loop,goto start..");
                    _i2s_play(&handle->phy);
                }
                else
                {
                    // not loop
                    goto exit;
                } // not loop
                // set read pointer to sample head

            } // last chunk
            else
            {
                fread(handle->sample_buffer, 1, sample_buffer_length, handle->audio_file_handle);
                i2s_channel_write(handle->phy.tx_chan, (const char *)handle->sample_buffer, sample_buffer_length, &written, 1000);
                if (error != ESP_OK)
                {
                    ESP_LOGE(TAG, "i2s_channel_write error at %s", esp_err_to_name(error));
                }
                // i2s_write(handle->phy.i2s_dev_num, (const char *)handle->sample_buffer, sample_buffer_length, &written, portMAX_DELAY);
                // i2s_write_expand(handle->phy.i2s_dev_num,(const char *)sample_buffer,sample_buffer_length,16,24,&written, portMAX_DELAY);//for adau debug use
                if (written != sample_buffer_length)
                {
                    ESP_LOGW(TAG, "written not equal req , w = %d , r = %ld", written, sample_buffer_length);
                }
            } // nomore chunk
        }     // play
        else
        {
            // i2s_zero_dma_buffer(handle->phy.i2s_dev_num);

            if (_isLastPause == 0)
            {
                _isLastPause = 1;
                ESP_LOGI(TAG, "disable i2s");
                _i2s_pause(&handle->phy);
            }

            vTaskDelay(1);
        } // pause

    } // for
    /*========================================main loop end========================================*/

exit:
    // i2s_zero_dma_buffer(handle->phy.i2s_dev_num);
    _i2s_pause(&handle->phy);

    if (handle->sample_buffer != 0x00)
    {
        free(handle->sample_buffer);
        handle->sample_buffer = 0x00;
    } // ample_buffer != 0x00

    if (handle->fileName != 0x00)
    {
        free(handle->fileName);
        handle->fileName = 0x00;
    }

    error = _i2s_stop(&handle->phy);
    if (error != ESP_OK)
    {
        ESP_LOGW(TAG, "i2s uninstall on not init");
    } // error != ESP_OK

    if (handle->audio_file_handle != NULL)
    {
        fclose(handle->audio_file_handle);
    }

    if (handle->cmd._playerTaskExit != NULL)
    {
        handle->cmd._playerTaskExit(exit_code, handle->user);
    } //_playerTaskExit

    WAV_DEBUG_MEM("task exit");

    handle->status.isBusy = 0;

    vTaskDelete(NULL);
}

// api
esp_err_t audio_player_init(audio_player_wav_handle_t *handle)
{
    memset(handle, 0x00, sizeof(audio_player_wav_handle_t));

    handle->cmd._xQueueCommand = xQueueCreate(2, sizeof(uint32_t));
    handle->cmd._xMutexProcessFree = xSemaphoreCreateMutex();

    return ESP_OK;
}

esp_err_t audio_player_play_wav_file(audio_player_wav_handle_t *handle, const char *file)
{
    WAV_DEBUG_MEM("play start");

    //ESP_LOGI(TAG, "out task handle address in %p", handle);
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

    // check file type(wav only)
    // if (handle->encode != FILE_WAV)
    // {
    //     ESP_LOGE(TAG, "file encode error");
    //     error = ESP_ERR_INVALID_ARG;
    //     goto clean;
    // }

    // check file exist
    error = checkFileExist(file);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "file not found");
        goto clean;
    } // error != ESP_OK

    // open file
    handle->audio_file_handle = fopen(file, "rb");
    if (handle->audio_file_handle == 0x00)
    {
        ESP_LOGE(TAG, "file open fail");
        error = ESP_ERR_NOT_FOUND;
        goto clean;
    } // handle->audio_file_handle == 0x00

    // decode wav header
    error = wavDecode(&handle->audio_file_handle, &handle->wav);
    if (error != ESP_OK)
    {
        ESP_LOGE(TAG, "wav header decode error");
        goto clean;
    } // error != ESP_OK

    // handle->encode = FILE_WAV;

    int file_name_len = strlen(file);
    // rear use ,send it to spiram
    handle->fileName = heap_caps_malloc(file_name_len + 1, MALLOC_CAP_SPIRAM);
    if (handle->fileName == 0x00)
    {
        ESP_LOGE(TAG, "alloc filename buffer fail");
        goto clean;
    }
    strncpy(handle->fileName, file, file_name_len);
    handle->fileName[file_name_len] = '\0';

    // set to busy
    handle->status.isBusy = 1;

    // phy init in task
    // file encode set in other func
    // cmd set in other func

    // start task
    ESP_LOGI(TAG, "create player task");
    BaseType_t ret = xTaskCreatePinnedToCore(_vTaskAudioPlayer,          // code
                                             "player",                   // name
                                             8192,                       // stack
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
        if (handle->audio_file_handle != 0x00)
        {
            fclose(handle->audio_file_handle);
        }

        if (handle->fileName != 0x00)
        {
            free(handle->fileName);
            handle->fileName = 0x00;
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

esp_err_t audio_player_set_pause(audio_player_wav_handle_t *handle, uint8_t pause)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t cmd;
    if (pause)
    {
        cmd = PLAYER_CMD_PAUSE;
        xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);
    }
    else
    {
        cmd = PLAYER_CMD_CONTINUE_PLAY;
        xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);
    }

    return ESP_OK;
}

esp_err_t audio_player_set_stop(audio_player_wav_handle_t *handle)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t cmd;
    cmd = PLAYER_CMD_STOP;
    xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t audio_player_set_pos(audio_player_wav_handle_t *handle, uint32_t pos)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t cmd;
    if (pos > handle->wav.totalSample)
    {
        ESP_LOGE(TAG, "pos out of range");
        return ESP_ERR_INVALID_ARG;
    }
    handle->cmd.requestPosition = handle->wav.entryPoint + (pos * handle->wav.channel * (handle->wav.bitPerSample / 8));
    ESP_LOGI("player", "set pos %x", (int)handle->cmd.requestPosition);
    cmd = PLAYER_CMD_JUMP;
    xQueueSend(handle->cmd._xQueueCommand, &cmd, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t audio_player_set_loop_mode(audio_player_wav_handle_t *handle, audio_file_loop_t loop)
{
    handle->cmd.loop = loop;
    return ESP_OK;
}

esp_err_t audio_player_get_pos(audio_player_wav_handle_t *handle, uint32_t *pos)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(handle->cmd._xMutexProcessFree, portMAX_DELAY) == pdPASS)
    {
        *pos = handle->status.playProcess;
        xSemaphoreGive(handle->cmd._xMutexProcessFree);
    }

    return ESP_OK;
}

esp_err_t audio_player_get_total_sample(audio_player_wav_handle_t *handle, uint32_t *samp)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    *samp = handle->wav.totalSample;

    return ESP_OK;
}

esp_err_t audio_player_get_status(audio_player_wav_handle_t *handle, player_status_t *stat)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGD(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(stat, &handle->status, sizeof(player_status_t));
    return ESP_OK;
}

esp_err_t audio_player_get_file_info(audio_player_wav_handle_t *handle, Simplified_Wav_struct_t *wav)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(wav, &handle->wav, sizeof(Simplified_Wav_struct_t));
    return ESP_OK;
}

esp_err_t audio_player_set_task_entry_event_cb(audio_player_wav_handle_t *handle, int32_t (*cb)(Simplified_Wav_struct_t wav))
{
    handle->cmd._playerTaskEntry = cb;
    return ESP_OK;
}

esp_err_t audio_player_set_task_exit_event_cb(audio_player_wav_handle_t *handle, void (*cb)(int32_t code, void *data))
{
    handle->cmd._playerTaskExit = cb;
    return ESP_OK;
}

esp_err_t audio_player_wait_play_task_exit(audio_player_wav_handle_t *handle)
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

uint8_t audio_player_get_busy(audio_player_wav_handle_t *handle)
{
    return handle->status.isBusy;
}

esp_err_t audio_player_get_file_name(audio_player_wav_handle_t *handle, const char **dst)
{
    if (handle->status.isBusy == 0)
    {
        ESP_LOGE(TAG, "player not run");
        return ESP_ERR_NOT_FOUND;
    }
    *dst = (const char *)handle->fileName;
    return ESP_OK;
}

esp_err_t audio_player_set_iis_config(audio_player_wav_handle_t *handle, audio_player_physical_handle_t config)
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
        memcpy(&handle->phy, &config, sizeof(audio_player_physical_handle_t));
        handle->phy.i2s_inited = inited;
    }

    return error;

    // clean:
    //     return error;
}

esp_err_t audio_player_set_user_data(audio_player_wav_handle_t *handle, void *user)
{
    handle->user = user;
    return ESP_OK;
}
// eof
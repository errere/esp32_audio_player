#include "audio_player_helper.h"
#include <ctype.h>

static const char *TAG = "aio_player";

static void _aio_task_exit_cb(int32_t code, void *user)
{
    audio_player_aio_t *handle = (audio_player_aio_t *)user;

    ESP_LOGI(TAG, "aio stop");

    if (handle->_playerTaskExit != NULL)
    {
        handle->_playerTaskExit(code);
    } //_playerTaskExit

    handle->playing_type = PLAYER_IDLE;
}

static void _aio_meta_data_decode_down_cb(uint32_t bps, uint32_t ch, uint32_t sr, void *user)
{
    audio_player_aio_t *handle = (audio_player_aio_t *)user;
    if (handle->_met_data_decode_down != NULL)
    {
        handle->_met_data_decode_down(bps, ch, sr);
    }
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0)
        return 0;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        int diff = tolower(c1) - tolower(c2);
        if (diff != 0)
        {
            return diff;
        }
        if (c1 == '\0')
        { // c2 must also be '\0' because diff==0
            break;
        }
    }
    return 0;
}

esp_err_t audio_player_aio_init(audio_player_aio_t *handle)
{
    memset(handle, 0x00, sizeof(audio_player_aio_t));

    audio_player_init(&handle->wav);
    audio_player_mp3_init(&handle->mp3);
    audio_player_flac_init(&handle->flac);

    audio_player_set_task_exit_event_cb(&handle->wav, _aio_task_exit_cb);
    audio_player_flac_set_task_exit_event_cb(&handle->flac, _aio_task_exit_cb);
    audio_player_mp3_set_task_exit_event_cb(&handle->mp3, _aio_task_exit_cb);

    audio_player_set_meta_down_cb(&handle->wav, _aio_meta_data_decode_down_cb);
    audio_player_flac_set_meta_down_cb(&handle->flac, _aio_meta_data_decode_down_cb);
    audio_player_mp3_set_meta_down_cb(&handle->mp3, _aio_meta_data_decode_down_cb);

    audio_player_set_user_data(&handle->wav, handle);
    audio_player_mp3_set_user_data(&handle->mp3, handle);
    audio_player_flac_set_user_data(&handle->flac, handle);

    handle->playing_type = PLAYER_IDLE;
    return ESP_OK;
}

esp_err_t audio_player_aio_play_file(audio_player_aio_t *handle, const char *file_dir)
{
    size_t len = strlen(file_dir);
    int i = 0;
    esp_err_t ret = ESP_OK;

    const char *tmp = file_dir;

    for (i = (len - 1); i >= 0; i--)
    {
        if (file_dir[i] == '.')
        {
            tmp = file_dir + i;
            break;
        }
    }
    if (i <= 0)
    {
        ESP_LOGE(TAG, "can not find \'.\',exit..");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "file_ext_name : %s", (const char *)tmp);

    if (handle->wav.phy.i2s_inited == 1)
    {
        ESP_LOGI(TAG, "del wav channel");
        i2s_del_channel(handle->wav.phy.tx_chan);
        handle->wav.phy.i2s_inited = 0;
    }
    if (handle->flac.phy.i2s_inited == 1)
    {
        ESP_LOGI(TAG, "del flac channel");
        i2s_del_channel(handle->flac.phy.tx_chan);
        handle->flac.phy.i2s_inited = 0;
    }
    if (handle->mp3.phy.i2s_inited == 1)
    {
        ESP_LOGI(TAG, "del mp3 channel");
        i2s_del_channel(handle->mp3.phy.tx_chan);
        handle->mp3.phy.i2s_inited = 0;
    }

    if (strncasecmp(tmp, ".wav", 4) == 0)
    {
        if (audio_player_play_wav_file(&handle->wav, file_dir) == ESP_OK)
        {
            handle->playing_type = PLAYER_PLAYING_WAV;
            ESP_LOGI(TAG, "play wav");
        }
    }
    else if (strncasecmp(tmp, ".flac", 5) == 0)
    {
        if (audio_player_flac_play_file(&handle->flac, file_dir) == ESP_OK)
        {
            handle->playing_type = PLAYER_PLAYING_FLAC;
            ESP_LOGI(TAG, "play flac");
        }
    }
    else if (strncasecmp(tmp, ".mp3", 4) == 0)
    {
        if (audio_player_mp3_play_file(&handle->mp3, file_dir) == ESP_OK)
        {
            handle->playing_type = PLAYER_PLAYING_MP3;
            ESP_LOGI(TAG, "play mp3");
        }
    }
    else
    {
        ESP_LOGE(TAG, "%s not support", (const char *)tmp);
        ret = ESP_ERR_NOT_SUPPORTED;
    }

    return ret;
}

esp_err_t audio_player_aio_set_i2s_config(audio_player_aio_t *handle, audio_player_physical_handle_t phy)
{
    if (handle->playing_type != PLAYER_IDLE)
    {
        ESP_LOGE(TAG, "aio busy %d", (int)handle->playing_type);
        return ESP_ERR_NOT_FINISHED;
    }

    union
    {
        mp3_player_physical_t mp3;
        audio_player_physical_handle_t wav;
        flac_player_physical_t flac;
    } phy_conv;
    phy_conv.wav = phy;

    audio_player_set_iis_config(&handle->wav, phy_conv.wav);
    audio_player_flac_set_i2s_config(&handle->flac, phy_conv.flac);
    audio_player_mp3_set_i2s_config(&handle->mp3, phy_conv.mp3);

    return ESP_OK;
}
esp_err_t audio_player_aio_set_loop_mode(audio_player_aio_t *handle, uint8_t loop)
{
    if (loop)
    {
        audio_player_set_loop_mode(&handle->wav, LOOP_FILE);
        audio_player_flac_set_loop_mode(&handle->flac, FLAC_LOOP_FILE);
        audio_player_mp3_set_loop_mode(&handle->mp3, MP3_LOOP_FILE);
    }
    else
    {
        audio_player_set_loop_mode(&handle->wav, FILE_SINGLE);
        audio_player_flac_set_loop_mode(&handle->flac, FLAC_FILE_SINGLE);
        audio_player_mp3_set_loop_mode(&handle->mp3, MP3_FILE_SINGLE);
    }
    return ESP_OK;
}

esp_err_t audio_player_aio_set_pause(audio_player_aio_t *handle, uint8_t pause)
{
    if (handle->playing_type == PLAYER_IDLE)
    {
        ESP_LOGE(TAG, "player idle");
        return ESP_ERR_NOT_FOUND;
    }
    switch (handle->playing_type)
    {
    case PLAYER_PLAYING_WAV:
        return audio_player_set_pause(&handle->wav, pause);
        break;

    case PLAYER_PLAYING_FLAC:
        return audio_player_flac_set_pause(&handle->flac, pause);
        break;

    case PLAYER_PLAYING_MP3:
        return audio_player_mp3_set_pause(&handle->mp3, pause);
        break;

    default:
        ESP_LOGE(TAG, "WTF");
        return ESP_FAIL;
        break;
    }
    return ESP_OK;
}
esp_err_t audio_player_aio_set_stop(audio_player_aio_t *handle)
{
    if (handle->playing_type == PLAYER_IDLE)
    {
        ESP_LOGE(TAG, "player idle");
        return ESP_ERR_NOT_FOUND;
    }
    switch (handle->playing_type)
    {
    case PLAYER_PLAYING_WAV:
        return audio_player_set_stop(&handle->wav);
        break;

    case PLAYER_PLAYING_FLAC:
        return audio_player_flac_set_stop(&handle->flac);
        break;

    case PLAYER_PLAYING_MP3:
        return audio_player_mp3_set_stop(&handle->mp3);
        break;

    default:
        ESP_LOGE(TAG, "WTF");
        return ESP_FAIL;
        break;
    }
    return ESP_OK;
}

esp_err_t audio_player_aio_get_file_info(audio_player_aio_t *handle, audio_generic_info_t *info)
{
    if (handle->playing_type == PLAYER_IDLE)
    {
        ESP_LOGE(TAG, "player idle");
        return ESP_ERR_NOT_FOUND;
    }

    info->type = handle->playing_type;

    switch (handle->playing_type)
    {
    case PLAYER_PLAYING_WAV:
        Simplified_Wav_struct_t r_info;
        ESP_ERROR_CHECK_WITHOUT_ABORT(audio_player_get_file_info(&handle->wav, &r_info));
        info->bitPerSample = r_info.bitPerSample;
        info->byte_rate_max_block_sz = 0;
        info->channel = r_info.channel;
        info->sampleRate = r_info.sampleRate;
        break;

    case PLAYER_PLAYING_FLAC:
        flac_player_info_t flac_info;
        ESP_ERROR_CHECK_WITHOUT_ABORT(audio_player_flac_get_file_info(&handle->flac, &flac_info));
        info->bitPerSample = flac_info.bps;
        info->byte_rate_max_block_sz = flac_info.max_block_sz;
        info->channel = flac_info.channel;
        info->sampleRate = flac_info.sample_rate;
        break;

    case PLAYER_PLAYING_MP3:
        mp3_player_info_t mp3_info;
        ESP_ERROR_CHECK_WITHOUT_ABORT(audio_player_mp3_get_file_info(&handle->mp3, &mp3_info, 100)); // 1s timeout
        info->bitPerSample = 16;
        info->byte_rate_max_block_sz = mp3_info.bitrate;
        info->channel = mp3_info.channel;
        info->sampleRate = mp3_info.hz;
        break;

    default:
        ESP_LOGE(TAG, "WTF");
        return ESP_FAIL;
        break;
    }
    return ESP_OK;
}

esp_err_t audio_player_aio_get_file_name(audio_player_aio_t *handle, const char **name)
{
    if (handle->playing_type == PLAYER_IDLE)
    {
        ESP_LOGE(TAG, "player idle");
        return ESP_ERR_NOT_FOUND;
    }
    switch (handle->playing_type)
    {
    case PLAYER_PLAYING_WAV:
        return audio_player_get_file_name(&handle->wav, name);
        break;

    case PLAYER_PLAYING_FLAC:
        return audio_player_flac_get_file_name(&handle->flac, name);
        break;

    case PLAYER_PLAYING_MP3:
        return audio_player_mp3_get_file_name(&handle->mp3, name);
        break;

    default:
        ESP_LOGE(TAG, "WTF");
        return ESP_FAIL;
        break;
    }
    return ESP_OK;
}
uint8_t audio_player_aio_get_pause(audio_player_aio_t *handle)
{
    if (handle->playing_type == PLAYER_IDLE)
    {
        ESP_LOGE(TAG, "player idle");
        return 0;
    }
    switch (handle->playing_type)
    {
    case PLAYER_PLAYING_WAV:
        player_status_t wav_stat;
        if (audio_player_get_status(&handle->wav, &wav_stat) != ESP_OK)
        {
            return 0;
        }
        return wav_stat.isPause;
        break;

    case PLAYER_PLAYING_FLAC:
        flac_player_status_t flac_stat;
        if (audio_player_flac_get_status(&handle->flac, &flac_stat) != ESP_OK)
        {
            return 0;
        }
        return flac_stat.isPause;
        break;

    case PLAYER_PLAYING_MP3:
        mp3_player_status_t mp3_stat;
        if (audio_player_mp3_get_status(&handle->mp3, &mp3_stat) != ESP_OK)
        {
            return 0;
        }
        return mp3_stat.isPause;
        break;

    default:
        ESP_LOGE(TAG, "WTF");
        return 0;
        break;
    }
    return 0;
}
esp_err_t audio_player_aio_get_pos(audio_player_aio_t *handle, uint32_t *cur, uint32_t *all)
{
    if (handle->playing_type == PLAYER_IDLE)
    {
        ESP_LOGE(TAG, "player idle");
        return 0;
    }
    switch (handle->playing_type)
    {
    case PLAYER_PLAYING_WAV:
        player_status_t wav_stat;
        if (audio_player_get_status(&handle->wav, &wav_stat) != ESP_OK)
        {
            return ESP_FAIL;
        }
        *cur = wav_stat.playProcess;
        return audio_player_get_total_sample(&handle->wav, all);
        break;

    case PLAYER_PLAYING_FLAC:
        flac_player_status_t flac_stat;
        if (audio_player_flac_get_status(&handle->flac, &flac_stat) != ESP_OK)
        {
            return ESP_FAIL;
        }
        *cur = flac_stat.reade_length;
        *all = flac_stat.total_length;
        return ESP_OK;
        break;

    case PLAYER_PLAYING_MP3:
        mp3_player_status_t mp3_stat;
        if (audio_player_mp3_get_status(&handle->mp3, &mp3_stat) != ESP_OK)
        {
            return ESP_FAIL;
        }
        *cur = mp3_stat.reade_length;
        *all = mp3_stat.total_length;
        return ESP_OK;
        break;

    default:
        ESP_LOGE(TAG, "WTF");
        return ESP_FAIL;
        break;
    }
    return ESP_OK;
}

uint8_t audio_player_aio_get_busy(audio_player_aio_t *handle)
{
    return (handle->playing_type != PLAYER_IDLE);
}
uint8_t audio_player_aio_get_type(audio_player_aio_t *handle)
{
    return (handle->playing_type);
}

esp_err_t audio_player_aio_wait_task_exit(audio_player_aio_t *handle)
{
    if (handle->playing_type != 0)
    {
        for (;;)
        {
            if (handle->playing_type == 0)
            {
                break;
            }
            vTaskDelay(1);
        }
    }
    return ESP_OK;
}

esp_err_t audio_player_aio_set_task_exit_event_cb(audio_player_aio_t *handle, void (*cb)(int32_t code))
{
    handle->_playerTaskExit = cb;
    return ESP_OK;
}
esp_err_t audio_player_aio_set_meta_data_decode_down_cb(audio_player_aio_t *handle, void (*cb)(uint32_t bps, uint32_t ch, uint32_t sr))
{
    handle->_met_data_decode_down = cb;
    return ESP_OK;
}
// eof

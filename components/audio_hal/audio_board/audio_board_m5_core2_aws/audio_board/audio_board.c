/*
*
* Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*/

#include <string.h>
#include <esp_log.h>
#include <audio_board.h>
#include "esp_intr_alloc.h"
#include <va_dsp_hal.h>
#include <axp192.h>

#define PLAT_TAG "AUDIO_BOARD"

#define PLAT_ASSERT(a, format, b, ...) \
    if ((a) == NULL) { \
        ESP_LOGE(PLAT_TAG, format, ##__VA_ARGS__); \
        return b;\
    }

int i2s_mode = -1;

esp_err_t audio_board_i2s_pin_config(int port_num, i2s_pin_config_t *pf_i2s_pin)
{   
    PLAT_ASSERT(pf_i2s_pin, "Error assigning i2s pins", -1);
    switch(port_num) {
        case 0:
            pf_i2s_pin->bck_io_num = GPIO_NUM_12;
            pf_i2s_pin->ws_io_num =  GPIO_NUM_0;
            pf_i2s_pin->data_out_num = GPIO_NUM_2;
            pf_i2s_pin->data_in_num = GPIO_NUM_34;
            break;
        case 1:
            pf_i2s_pin->bck_io_num = GPIO_NUM_12;
            pf_i2s_pin->ws_io_num =  GPIO_NUM_0;
            pf_i2s_pin->data_out_num = GPIO_NUM_2;
            pf_i2s_pin->data_in_num = GPIO_NUM_34;
            break;
        default:
            ESP_LOGE(PLAT_TAG, "Entered i2s port number is wrong");
            return ESP_FAIL;
    }

    return ESP_OK; 
}

esp_err_t audio_board_i2s_init_default(i2s_config_t *i2s_cfg_dft)
{
    i2s_cfg_dft->mode = I2S_MODE_MASTER | I2S_MODE_TX; /*| I2S_MODE_RX | I2S_MODE_PDM;*/
    i2s_cfg_dft->sample_rate = 48000;
    i2s_cfg_dft->bits_per_sample = 16;
    i2s_cfg_dft->channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
    i2s_cfg_dft->communication_format = I2S_COMM_FORMAT_I2S;
    i2s_cfg_dft->dma_buf_count = 3;                   /*!< number of dma buffer */
    i2s_cfg_dft->dma_buf_len = 300;                   /*!< size of each dma buffer (Byte) */
    i2s_cfg_dft->intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_cfg_dft->use_apll = false;
    i2s_cfg_dft->tx_desc_auto_clear = true;

    return ESP_OK;
}

esp_err_t audio_board_i2s_set_spk_mic_mode(int mode)
{
    esp_err_t err = ESP_OK;
    i2s_config_t i2s_cfg = {};

    /*Dont want to reinstall I2S driver if already installed*/
    if(i2s_mode != mode)
    {
        if(mode == MODE_SPK)
        {
            va_dsp_hal_stream_pause();
        }
        ESP_LOGW(PLAT_TAG, "Installing I2S driver mode %d", mode);
        i2s_mode = mode;
        i2s_driver_uninstall(I2S_NUM_0);

        audio_board_i2s_init_default(&i2s_cfg);

        if (mode == MODE_MIC)
        {
            i2s_cfg.mode = (I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
        }
        else
        {
            i2s_cfg.mode = (I2S_MODE_MASTER | I2S_MODE_TX);
            i2s_cfg.use_apll = false;
            i2s_cfg.tx_desc_auto_clear = true;
        }
        err = i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(PLAT_TAG, "Error installing i2s driver");
            return err;
        } 
        i2s_pin_config_t ab_i2s_pin;
        // Write I2S0 pin config
        audio_board_i2s_pin_config(I2S_NUM_0, &ab_i2s_pin);
        err = i2s_set_pin(I2S_NUM_0, &ab_i2s_pin);
        if (err != ESP_OK) {
            ESP_LOGE(PLAT_TAG, "Error setting i2s pin config");
            return err;
        } 
        err = i2s_set_clk(I2S_NUM_0, 48000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
        if (err != ESP_OK) {
            ESP_LOGE(PLAT_TAG, "Error setting i2s clk");
            return err;
        } 
        if(mode == MODE_MIC)
        {
            va_dsp_hal_stream_resume();
        }
    }
    else
    {
        ESP_LOGD(PLAT_TAG, "Driver already installed for mode %d", mode);
    }

    if(mode == MODE_SPK)
    {
        Axp192_SetGPIO2Level(1);
    }
    else
    {
        Axp192_SetGPIO2Level(0);
    }

    return err;
}

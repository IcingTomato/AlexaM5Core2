/*
*
* Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
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

/*  ---------------------------------------------------------------------------------------
*   |                                                                                       |
*   |   The file includes functions and variables to configure ES8323.                      |
*   |                                                                                       |
*   ----------------------------------------------------------------------------------------
*/
#include <string.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include <esp_codec.h>
#include <audio_board.h>

#define TAG "esp_codec_es8323"

#define ES8323_DISABLE_MUTE 0x00   //disable mute
#define ES8323_ENABLE_MUTE  0x01   //enable  mute

#define ES8323_DEFAULT_VOL 45

#define ES8323_I2C_MASTER_SPEED 100000  //set master clk speed to 100k

#define ES_ASSERT(a, format, b, ...) \
    if ((a) != 0) { \
        ESP_LOGE(TAG, format, ##__VA_ARGS__); \
        return b;\
    }

#define LOG_8323(fmt, ...)   ESP_LOGW(TAG, fmt, ##__VA_ARGS__)

uint8_t curr_vol = 0;

/**
 * @brief Initialization function for i2c
 */
static esp_err_t audio_codec_i2c_init(int i2c_master_port)
{
    int res;
    i2c_config_t pf_i2c_pin = {0};

    res = audio_board_i2c_pin_config(i2c_master_port, &pf_i2c_pin);

    pf_i2c_pin.mode = I2C_MODE_MASTER;
    pf_i2c_pin.master.clk_speed = ES8323_I2C_MASTER_SPEED;

    res |= i2c_param_config(i2c_master_port, &pf_i2c_pin);
    res |= i2c_driver_install(i2c_master_port, pf_i2c_pin.mode, 0, 0, 0);
    return res;
}

/**
 * @brief Write ES8323 register
 *
 * @param slave_add : slave address
 * @param reg_add    : register address
 * @param data      : data to write
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
static esp_err_t es8323_write_reg(uint8_t slave_add, uint8_t reg_add, uint8_t data)
{
    int res = 0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    res |= i2c_master_start(cmd);
    res |= i2c_master_write_byte(cmd, slave_add, 1 /*ACK_CHECK_EN*/);
    res |= i2c_master_write_byte(cmd, reg_add, 1 /*ACK_CHECK_EN*/);
    res |= i2c_master_write_byte(cmd, data, 1 /*ACK_CHECK_EN*/);
    res |= i2c_master_stop(cmd);
    res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    ES_ASSERT(res, "es8323_write_reg error", -1);
    return res;
}

/**
 * @brief Read ES8323 register
 *
 * @param reg_add    : register address
 *
 * @return
 *     - (-1)     Error
 *     - (0)      Success
 */
static esp_err_t es8323_read_reg(uint8_t reg_add, uint8_t *p_data)
{
    uint8_t data;
    esp_err_t res;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    res  = i2c_master_start(cmd);
    res |= i2c_master_write_byte(cmd, ES8323_ADDR, 1 /*ACK_CHECK_EN*/);
    res |= i2c_master_write_byte(cmd, reg_add, 1 /*ACK_CHECK_EN*/);
    res |= i2c_master_stop(cmd);
    res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    cmd = i2c_cmd_link_create();
    res |= i2c_master_start(cmd);
    res |= i2c_master_write_byte(cmd, ES8323_ADDR | 0x01, 1 /*ACK_CHECK_EN*/);
    res |= i2c_master_read_byte(cmd, &data, 0x01/*NACK_VAL*/);
    res |= i2c_master_stop(cmd);
    res |= i2c_master_cmd_begin(0, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    ES_ASSERT(res, "es8323_read_reg error", -1);
    *p_data = data;
    return res;
}

/**
 * @brief Configure ES8323 ADC and DAC volume. Basicly you can consider this as ADC and DAC gain
 *
 * @param mode:             set ADC or DAC or all
 * @param volume:           -96 ~ 0              for example es8323_set_adc_dac_volume(ES8323_MODULE_ADC, 30, 6); means set ADC volume -30.5db
 * @param dot:              whether include 0.5. for example es8323_set_adc_dac_volume(ES8323_MODULE_ADC, 30, 4); means set ADC volume -30db
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
static esp_err_t es8323_set_adc_dac_volume(media_hal_codec_mode_t mode, float volume)
{
    esp_err_t res = 0;
    uint8_t vol;
    if ( volume < -96 || volume > 0 ) {
        LOG_8323("Warning: volume < -96! or > 0!\n");
        if (volume < -96)
            volume = -96;
        else
            volume = 0;
    }
    vol = (uint8_t) ((-1) * (volume * 2));

    if (mode == MEDIA_HAL_CODEC_MODE_ENCODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
        res  = es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL8, vol);
        res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL9, vol);  //ADC Right Volume=0db
    }
    if (mode == MEDIA_HAL_CODEC_MODE_DECODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
        res  = es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL5, 10);
        res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL4, 10);
    }
    return res;
}

esp_err_t es8323_set_state(media_hal_codec_mode_t mode, media_hal_sel_state_t media_hal_state)
{
    esp_err_t res = 0;
    uint8_t reg= 0;
    if(media_hal_state == MEDIA_HAL_START_STATE) {
        uint8_t prev_data = 0, data = 0;
        es8323_read_reg(ES8323_DACCONTROL21, &prev_data);
        if (mode == MEDIA_HAL_CODEC_MODE_LINE_IN) {
            res  = es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL16, 0x09); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2 by pass enable
            res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL17, 0x50); // left DAC to left mixer enable  and  LIN signal to left mixer enable 0db  : bupass enable
            res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL20, 0x50); // right DAC to right mixer enable  and  LIN signal to right mixer enable 0db : bupass enable
            res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL21, 0xC0); //enable dac
        } else {
            res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL21, 0x80);   //enable dac
        }
        es8323_read_reg(ES8323_DACCONTROL21, &data);
        if (prev_data != data) {
            res  = es8323_write_reg(ES8323_ADDR, ES8323_CHIPPOWER, 0xF0);   //start state machine
            res |= es8323_write_reg(ES8323_ADDR, ES8323_CHIPPOWER, 0x00);   //start state machine
        }
        if (mode == MEDIA_HAL_CODEC_MODE_ENCODE || mode == MEDIA_HAL_CODEC_MODE_BOTH || mode == MEDIA_HAL_CODEC_MODE_LINE_IN)
            res  = es8323_write_reg(ES8323_ADDR, ES8323_ADCPOWER, 0x00);   //power up adc and line in
        if (mode == MEDIA_HAL_CODEC_MODE_DECODE || mode == MEDIA_HAL_CODEC_MODE_BOTH || mode == MEDIA_HAL_CODEC_MODE_LINE_IN) {
            res  = es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0x3c);   //power up dac and line out
            /*
            Here we mute the dac when the State for decoding is in STOP mode
            Hence now we read the previous volume which has 33 steps in total
            also es8323_control_volume parameter is in %, so multiply the register value by 3
            */
            res |= es8323_read_reg(ES8323_DACCONTROL24, &reg);
            reg  = reg * 3;
            res |= es8323_control_volume(reg);
        }
        return res;
    }
    if(media_hal_state == MEDIA_HAL_STOP_STATE) {
        if (mode == MEDIA_HAL_CODEC_MODE_LINE_IN) {
        res  = es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL21, 0x80); //enable dac
        res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL16, 0x00); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
        res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL17, 0x90); // only left DAC to left mixer enable 0db
        res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL20, 0x90); // only right DAC to right mixer enable 0db
        return res;
        }
        if (mode == MEDIA_HAL_CODEC_MODE_DECODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
            res  = es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0x2c);
            res |= es8323_control_volume(0);    //Mute
        }
        if (mode == MEDIA_HAL_CODEC_MODE_ENCODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
            res  = es8323_write_reg(ES8323_ADDR, ES8323_ADCPOWER, 0xFF);  //power down adc and line in
        }
        if (mode == MEDIA_HAL_CODEC_MODE_BOTH) {
            res  = es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL21, 0x9C);  //disable mclk
        }
        return res;
    }
    return res;
}

esp_err_t es8323_deinit(int port_num)
{
    esp_err_t ret = 0;
    //XXX: Simply returning since deinit causes noise
    /*
    //gpio_set_level(GPIO_CODEC_EN, 0);
    ret = es8323_write_reg(ES8323_ADDR, ES8323_CHIPPOWER, 0xFF);  //reset and stop es8323
    ret = es8323_write_reg(ES8323_ADDR, ES8323_ADCPOWER, 0xFF);  //Power down adc
    ret = es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0xC0);  //Power down dac
    i2c_driver_delete(port_num);
    */
    return ret;
}
esp_err_t es8323_powerup()
{
    esp_err_t ret = 0;
    gpio_set_level(GPIO_CODEC_EN, 1);
    ret = es8323_write_reg(ES8323_ADDR, ES8323_CHIPPOWER, 0x00);  //Power up codec
    ret = es8323_write_reg(ES8323_ADDR, ES8323_ADCPOWER, 0x00);  //Power up adc
    es8323_control_volume(curr_vol);
    //ret = es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0x2C);  //Power up dac
    //PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
    //SET_PERI_REG_BITS(PIN_CTRL, CLK_OUT1, 0, CLK_OUT1_S);

    return ret;
}

esp_err_t es8323_powerdown()
{
    esp_err_t ret =0;
    //gpio_set_level(GPIO_CODEC_EN, 0);
    ret = es8323_write_reg(ES8323_ADDR, ES8323_CHIPPOWER, 0xFF);  //Power down codec
    ret = es8323_write_reg(ES8323_ADDR, ES8323_ADCPOWER, 0xFF);  //Power down adc
    //ret = es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0xC0);  //Power down dac
    return ret;
}

esp_err_t es8323_init(media_hal_config_t *media_hal_conf)
{
    ESP_LOGI(TAG, "Initialising esp_codec");
    media_hal_op_mode_t es8323_mode = media_hal_conf->op_mode;
    media_hal_adc_input_t es8323_adc_input = media_hal_conf->adc_input;
    media_hal_dac_output_t es8323_dac_output = media_hal_conf->dac_output;
    int port_num = media_hal_conf->port_num;

    esp_err_t res;
#if 1
    gpio_config_t  io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_SEL_CODEC_EN;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_CODEC_EN, 0);
#endif
    //PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
    //SET_PERI_REG_BITS(PIN_CTRL, CLK_OUT1, 0, CLK_OUT1_S);
    audio_codec_i2c_init(port_num);   //set i2c pin and i2c clock frequency for esp32
    res = es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL3, 0x00);  // 0x04 mute/0x00 unmute&ramp;DAC unmute and  disabled digital volume control soft ramp
    /* Chip Control and Power Management */
    res |= es8323_write_reg(ES8323_ADDR, ES8323_CONTROL2, 0x50);
    res |= es8323_write_reg(ES8323_ADDR, ES8323_CHIPPOWER, 0x00);         //normal all and power up all
    res |= es8323_write_reg(ES8323_ADDR, ES8323_MASTERMODE, es8323_mode); //CODEC IN I2S SLAVE MODE

    /* dac */
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0x2C);       //disable DAC and disable Lout/Rout/1/2
    res |= es8323_write_reg(ES8323_ADDR, ES8323_CONTROL1, 0x12);       //Enfr=0,Play&Record Mode,(0x17-both of mic&paly)
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL1, 0x18);    //1a 0x18:16bit iis , 0x00:24
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL2, 0x02);    //DACFsMode,SINGLE SPEED; DACFsRatio,256
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL16, 0x00);   // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL17, 0x90);   // only left DAC to left mixer enable 0db
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL20, 0x90);   // only right DAC to right mixer enable 0db
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL21, 0x80);   //set internal ADC and DAC use the same LRCK clock, ADC LRCK as internal LRCK
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL23, 0x00);   //vroi=0
    res |= es8323_set_adc_dac_volume(MEDIA_HAL_CODEC_MODE_DECODE, 0);  // 0db

    /* adc */
    res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCPOWER, 0xFF);    //power down adc
    res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL1, 0x88); //0x88 MIC PGA =24DB

    res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL3, 0x02);
    res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL4, 0x0); //0d 0x0c I2S-16BIT, LEFT ADC DATA = LIN1 , RIGHT ADC DATA =RIN1
    res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL5, 0x02);  //ADCFsMode,singel SPEED,RATIO=256
    //ALC for Microphone
    res |= es8323_set_adc_dac_volume(MEDIA_HAL_CODEC_MODE_ENCODE, 0);      // 0db
    res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCPOWER, 0x09); //Power up ADC, Enable LIN&RIN, Power down MICBIAS, set int1lp to low power mode

    if(es8323_dac_output == MEDIA_HAL_DAC_OUTPUT_LINE2) {
        //res |= es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0x2c);  //Enable Lout/Rout 2
    } else if(es8323_dac_output == MEDIA_HAL_DAC_OUTPUT_ALL) {
        //res |= es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0x2c);  //Enable Lout/Rout 1 and 2 both
    } else {
        //res |= es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, 0x2c);  //Default: Enable Lout/Rout 1
    }
    if(es8323_adc_input == MEDIA_HAL_ADC_INPUT_LINE2) {
        res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL2, 0x50);  // Enable LIN2/RIN2 as ADC input
    } else if(es8323_adc_input == MEDIA_HAL_ADC_INPUT_DIFFERENCE) {
        res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL2, 0xf0);  // Enable LIN1/RIN1 as well as LIN2/RIN2 for ADC input
    } else {
        res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL2, 0x00);  //Default: Enable LIN1/RIN1 as ADC input
    }
    //     es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL8, 0xC0);
    //res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL9,0xC0);
    return res;
}

esp_err_t es8323_config_format(media_hal_codec_mode_t mode, media_hal_format_t fmt)
{
    esp_err_t res = 0;
    uint8_t reg = 0;
    if (mode == MEDIA_HAL_CODEC_MODE_ENCODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
        res = es8323_read_reg(ES8323_ADCCONTROL4, &reg);
        reg = reg & 0xfc;
        res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL4, 0x0C);
    }
    if (mode == MEDIA_HAL_CODEC_MODE_DECODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
        res = es8323_read_reg(ES8323_DACCONTROL1, &reg);
        reg = reg & 0xf9;
        res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL1, 0x20);
    }
    return res;
}

esp_err_t es8323_control_volume(uint8_t volume)
{
    esp_err_t res = 0;
    curr_vol = volume;
    uint8_t reg = 0;

    if (volume > 100) {
        volume = 100;
    }
    res = es8323_read_reg(ES8323_DACCONTROL3, &reg);
    reg = reg & 0xFB;
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL3, reg | (ES8323_DISABLE_MUTE << 2));
    volume = (volume / 5) + 2;
    //res  = es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL24, volume);
    //res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL25, volume);
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL26, volume);
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL27, volume);
    return res;
}

esp_err_t es8323_get_volume(uint8_t *volume)
{
    esp_err_t res = 0;
    uint8_t reg = 0;
    res = es8323_read_reg(ES8323_DACCONTROL26, &reg);
    if (res == ESP_FAIL) {
        *volume = 0;
    } else {
        *volume = reg;
        *volume *= 3;
        if (*volume == 99) {
            *volume = 100;
        }
    }
    *volume = curr_vol;
    return res;
}

esp_err_t es8323_set_mute(bool bmute)
{

    esp_err_t res = 0;
    uint8_t reg = 0;

    if (bmute) {
        res = es8323_read_reg(ES8323_DACCONTROL3, &reg);
        reg = reg & 0xFB;
        res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL3, reg | (ES8323_ENABLE_MUTE << 2));
    } else {
        res = es8323_read_reg(ES8323_DACCONTROL3, &reg);
        reg = reg & 0xFB;
        res |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL3, reg | (ES8323_DISABLE_MUTE << 2));
    }
    return res;
}

esp_err_t es8323_set_bits_per_sample(media_hal_codec_mode_t mode, media_hal_bit_length_t bits_per_sample)
{
    esp_err_t res = 0;
    uint8_t reg = 0;
    uint8_t bits = 0;
    bits_per_sample = MEDIA_HAL_BIT_LENGTH_16BITS;
    if(mode == MEDIA_HAL_CODEC_MODE_ENCODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
        res = es8323_read_reg(ES8323_ADCCONTROL4, &reg);
        reg = reg & 0xe3;
        switch(bits_per_sample) {
            case MEDIA_HAL_BIT_LENGTH_8BITS:
                break;
            case MEDIA_HAL_BIT_LENGTH_16BITS:
                bits = 0x03;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL4, 0x0C);
                break;
            case MEDIA_HAL_BIT_LENGTH_18BITS:
                bits = 0x02;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL4, reg | (bits << 2));
                break;
            case MEDIA_HAL_BIT_LENGTH_20BITS:
                bits = 0x01;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL4, reg | (bits << 2));
                break;
            case MEDIA_HAL_BIT_LENGTH_24BITS:
                bits = 0x00;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL4, reg | (bits << 2));
                break;
            case MEDIA_HAL_BIT_LENGTH_32BITS:
                bits = 0x04;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL4, reg | (bits << 2));
                break;
            default:
                break;
        }

    }
    if(mode == MEDIA_HAL_CODEC_MODE_DECODE || mode == MEDIA_HAL_CODEC_MODE_BOTH) {
        res = es8323_read_reg(ES8323_DACCONTROL1, &reg);
        reg = reg & 0xc7;
        switch(bits_per_sample) {
            case MEDIA_HAL_BIT_LENGTH_8BITS:
                break;
            case MEDIA_HAL_BIT_LENGTH_16BITS:
                bits = 0x03;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL1, 0x18);
                break;
            case MEDIA_HAL_BIT_LENGTH_18BITS:
                bits = 0x02;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL1, reg | (bits << 3));
                break;
            case MEDIA_HAL_BIT_LENGTH_20BITS:
                bits = 0x01;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL1, reg | (bits << 3));
                break;
            case MEDIA_HAL_BIT_LENGTH_24BITS:
                bits = 0x00;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL1, reg | (bits << 3));
                break;
            case MEDIA_HAL_BIT_LENGTH_32BITS:
                bits = 0x04;
                res |=  es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL1, reg | (bits << 3));
                break;
            default:
                break;
        }
    }
    return res;
}

#if 0
/**
 * @param gain: Config DAC Output
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
int es8323_config_dac_output(int output)
{
    int res;
    uint8_t reg = 0;
    res = es8323_read_reg(ES8323_DACPOWER, &reg);
    reg = reg & 0xc3;
    res |= es8323_write_reg(ES8323_ADDR, ES8323_DACPOWER, reg | output);
    return res;
}

/**
 * @param gain: Config ADC input
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
int es8323_config_adc_input(es8323_adc_input_t input)
{
    int res;
    uint8_t reg = 0;
    res = es8323_read_reg(ES8323_ADCCONTROL2, &reg);
    reg = reg & 0x0f;
    res |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL2, reg | input);
    return res;
}
#endif

esp_err_t es8323_set_mic_gain(es8323_mic_gain_t gain)
{
    esp_err_t ret;
    int gain_n;
    gain_n = (int)gain / 3;
    ret = es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL1, gain_n); //MIC PGA
    return ret;
}

void es8323_read_all_registers()
{
    for (int i = 0; i < 50; i++) {
        uint8_t reg = 0;
        es8323_read_reg(i, &reg);
        ets_printf("%x: %x\n", i, reg);
    }
}

esp_err_t es8323_write_register(uint8_t reg_add, uint8_t data)
{
    return es8323_write_reg(ES8323_ADDR, reg_add, data);
}

esp_err_t es8323_set_i2s_clk(media_hal_codec_mode_t media_hal_codec_mode, media_hal_bit_length_t media_hal_bit_length)
{
    int clk_div = 2;
    esp_err_t ret;
    if(media_hal_bit_length == MEDIA_HAL_BIT_LENGTH_16BITS || media_hal_bit_length == MEDIA_HAL_BIT_LENGTH_32BITS) {
        clk_div = 3;
    }
    ret = es8323_set_bits_per_sample(media_hal_codec_mode, media_hal_bit_length);
    ret |= es8323_write_reg(ES8323_ADDR, ES8323_ADCCONTROL5, clk_div);  //ADCFsMode,singel SPEED,RATIO=256
    ret |= es8323_write_reg(ES8323_ADDR, ES8323_DACCONTROL2, clk_div);  //ADCFsMode,singel SPEED,RATIO=256
    return ret;
}

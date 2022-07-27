/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/**
* \file
*   Ring Buffer library
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <basic_rb.h>
#include "esp_log.h"
#include "esp_err.h"
#include <esp_audio_mem.h>

static const char *TAG = "[basic_rb]";

typedef struct ringbuf {
    /* Keep rb_type_t first */
    rb_type_t type;
    char *name;
    uint8_t *base;        /**< Original pointer */
    /* XXX: these need to be volatile? */
    uint8_t *volatile readptr;   /**< Read pointer */
    uint8_t *volatile writeptr;   /**< Write pointer */
    volatile ssize_t fill_cnt;  /**< Number of filled slots */
    ssize_t size;       /**< Buffer size */
    xSemaphoreHandle can_read;
    xSemaphoreHandle can_write;
    xSemaphoreHandle lock;
    int abort_read;
    int abort_write;
    int writer_finished;  //to prevent infinite blocking for buffer read
    int reader_unblock;
} ringbuf_t;

rb_handle_t rb_init(const char *name, uint32_t size)
{
    ringbuf_t *r;
    unsigned char *buf;

    if (size < 2 || !name) {
        return NULL;
    }

    r = malloc(sizeof(ringbuf_t));
    assert(r);
    buf = esp_audio_mem_calloc(1, size);
    assert(buf);

    r->type = RB_TYPE_BASIC;
    r->name = (char *) name;
    r->base = r->readptr = r->writeptr = buf;
    r->fill_cnt = 0;
    r->size = size;

    vSemaphoreCreateBinary(r->can_read);
    assert(r->can_read);
    vSemaphoreCreateBinary(r->can_write);
    assert(r->can_write);
    r->lock = xSemaphoreCreateMutex();
    assert(r->lock);

    r->abort_read = 0;
    r->abort_write = 0;
    r->writer_finished = 0;
    r->reader_unblock = 0;

    return (rb_handle_t)r;
}

void rb_cleanup(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    free(rb->base);
    rb->base = NULL;
    vSemaphoreDelete(rb->can_read);
    rb->can_read = NULL;
    vSemaphoreDelete(rb->can_write);
    rb->can_write = NULL;
    vSemaphoreDelete(rb->lock);
    rb->lock = NULL;
    free(rb);
}

/*
 * @brief: get the number of filled bytes in the buffer
 */
ssize_t rb_filled(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return -1;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return -1;
    }

    return rb->fill_cnt;
}

/*
 * @brief: get the number of empty bytes available in the buffer
 */
ssize_t rb_available(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return -1;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return -1;
    }

    ESP_LOGD(TAG, "rb leftover %d bytes", rb->size - rb->fill_cnt);
    return (rb->size - rb->fill_cnt);
}

int rb_read(rb_handle_t handle, uint8_t *buf, int buf_len, uint32_t ticks_to_wait)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return 0;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return 0;
    }

    int read_size;
    int total_read_size = 0;

    /**
     * In case where we are able to read buf_len in one go,
     * we are not able to check for abort and keep returning buf_len as bytes read.
     * Check for argument validity check and abort case before entering memcpy loop.
     */

    if (rb == NULL || rb->abort_read == 1) {
        return ESP_FAIL;
    }

    xSemaphoreTake(rb->lock, portMAX_DELAY);

    while (buf_len) {
        if (rb->fill_cnt < buf_len) {
            read_size = rb->fill_cnt;
        } else {
            read_size = buf_len;
        }
        if ((rb->readptr + read_size) > (rb->base + rb->size)) {
            int rlen1 = rb->base + rb->size - rb->readptr;
            int rlen2 = read_size - rlen1;
            if (buf) {
                memcpy(buf, rb->readptr, rlen1);
                memcpy(buf + rlen1, rb->base, rlen2);
            }
            rb->readptr = rb->base + rlen2;
        } else {
            if (buf) {
                memcpy(buf, rb->readptr, read_size);
            }
            rb->readptr = rb->readptr + read_size;
        }

        buf_len -= read_size;
        rb->fill_cnt -= read_size;
        total_read_size += read_size;
        if (buf) {
            buf += read_size;
        }

        xSemaphoreGive(rb->can_write);

        if (buf_len == 0) {
            break;
        }

        xSemaphoreGive(rb->lock);
        if (!rb->writer_finished && !rb->abort_read && !rb->reader_unblock) {
            if (xSemaphoreTake(rb->can_read, ticks_to_wait) != pdTRUE) {
                /* Small delay to avoid WDT triggering when the ticks_to_wait is set to 0 */
                vTaskDelay(1);
                goto out;
            }
        }
        if (rb->abort_read == 1) {
            total_read_size = RB_ABORT;
            goto out;
        }
        if (rb->writer_finished == 1) {
            goto out;
        }
        if (rb->reader_unblock == 1) {
            if (total_read_size == 0) {
                total_read_size = RB_READER_UNBLOCK;
            }
            goto out;
        }

        xSemaphoreTake(rb->lock, portMAX_DELAY);
    }

    xSemaphoreGive(rb->lock);
out:
    if (rb->writer_finished == 1 && total_read_size == 0) {
        total_read_size = RB_WRITER_FINISHED;
    }
    rb->reader_unblock = 0; /* We are anyway unblocking reader */
    return total_read_size;
}

int rb_write(rb_handle_t handle, uint8_t *buf, int buf_len, uint32_t ticks_to_wait)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return 0;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return 0;
    }

    int write_size;
    int total_write_size = 0;

    /**
     * In case where we are able to write buf_len in one go,
     * we are not able to check for abort and keep returning buf_len as bytes written.
     * Check for arguments' validity and abort case before entering memcpy loop.
     */

    if (rb == NULL || buf == NULL || rb->abort_write == 1) {
        return RB_FAIL;
    }

    xSemaphoreTake(rb->lock, portMAX_DELAY);

    while (buf_len) {
        if ((rb->size - rb->fill_cnt) < buf_len) {
            write_size = rb->size - rb->fill_cnt;
        } else {
            write_size = buf_len;
        }
        if ((rb->writeptr + write_size) > (rb->base + rb->size)) {
            int wlen1 = rb->base + rb->size - rb->writeptr;
            int wlen2 = write_size - wlen1;
            memcpy(rb->writeptr, buf, wlen1);
            memcpy(rb->base, buf + wlen1, wlen2);
            rb->writeptr = rb->base + wlen2;
        } else {
            memcpy(rb->writeptr, buf, write_size);
            rb->writeptr = rb->writeptr + write_size;
        }

        buf_len -= write_size;
        rb->fill_cnt += write_size;
        total_write_size += write_size;
        buf += write_size;

        xSemaphoreGive(rb->can_read);

        if (buf_len == 0) {
            break;
        }

        xSemaphoreGive(rb->lock);
        if (rb->writer_finished) {
            return write_size > 0 ? write_size : RB_WRITER_FINISHED;
        }
        if (xSemaphoreTake(rb->can_write, ticks_to_wait) != pdTRUE) {
            goto out;
        }
        if (rb->abort_write == 1) {
            goto out;
        }
        xSemaphoreTake(rb->lock, portMAX_DELAY);
    }

    xSemaphoreGive(rb->lock);
out:
    return total_write_size;
}

/**
 * abort and set abort_read and abort_write to asked values.
 */
static void _rb_reset(rb_handle_t handle, int abort_read, int abort_write)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    xSemaphoreTake(rb->lock, portMAX_DELAY);
    rb->readptr = rb->writeptr = rb->base;
    rb->fill_cnt = 0;
    rb->writer_finished = 0;
    rb->reader_unblock = 0;
    rb->abort_read = abort_read;
    rb->abort_write = abort_write;
    xSemaphoreGive(rb->lock);
}

void rb_reset(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    _rb_reset(rb, 0, 0);
}

void rb_abort_read(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    rb->abort_read = 1;
    xSemaphoreGive(rb->can_read);
    xSemaphoreGive(rb->lock);
}

void rb_abort_write(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    rb->abort_write = 1;
    xSemaphoreGive(rb->can_write);
    xSemaphoreGive(rb->lock);
}

void rb_abort(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    rb->abort_read = 1;
    rb->abort_write = 1;
    xSemaphoreGive(rb->can_read);
    xSemaphoreGive(rb->can_write);
    xSemaphoreGive(rb->lock);
}

/**
 * Reset the ringbuffer and keep keep rb_write aborted.
 * Note that we are taking lock before even toggling `abort_write` variable.
 * This serves a special purpose to not allow this abort to be mixed with rb_write.
 */
void rb_reset_and_abort_write(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    _rb_reset(rb, 0, 1);
    xSemaphoreGive(rb->can_write);
}

void rb_signal_writer_finished(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    rb->writer_finished = 1;
    xSemaphoreGive(rb->can_read);
}

int rb_is_writer_finished(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return -1;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return -1;
    }

    return (rb->writer_finished);
}

void rb_wakeup_reader(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    rb->reader_unblock = 1;
    xSemaphoreGive(rb->can_read);
}

void rb_stat(rb_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "handle is NULL");
        return;
    }
    ringbuf_t *rb = (ringbuf_t *)handle;
    if (rb->type != RB_TYPE_BASIC) {
        ESP_LOGE(TAG, "Incorrect rb_type: %d", rb->type);
        return;
    }

    xSemaphoreTake(rb->lock, portMAX_DELAY);
    ESP_LOGI(TAG, "filled: %d, base: %p, read_ptr: %p, write_ptr: %p, size: %d\n",
                rb->fill_cnt, rb->base, rb->readptr, rb->writeptr, rb->size);
    xSemaphoreGive(rb->lock);
}

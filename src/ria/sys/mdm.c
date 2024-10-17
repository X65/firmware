/*
 * Copyright (c) 2024 Tomasz Sterna
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mdm.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "main.h"
#include "pico/util/queue.h"
#include <pico/platform/compiler.h>
#include <pico/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int32_t esp_err_t;

#define DMA_CHAN            SPI_DMA_CH_AUTO
#define ESP_SPI_DMA_MAX_LEN 4092
#define CMD_HD_WRBUF_REG    0x01
#define CMD_HD_RDBUF_REG    0x02
#define CMD_HD_WRDMA_REG    0x03
#define CMD_HD_RDDMA_REG    0x04
#define CMD_HD_WR_END_REG   0x07
#define CMD_HD_INT0_REG     0x08
#define WRBUF_START_ADDR    0x0
#define RDBUF_START_ADDR    0x4
#define STREAM_BUFFER_SIZE  1024 * 8

static const char *MSG_READY = "\r\nready\r\n";

typedef enum
{
    SPI_NULL = 0,
    SPI_READ,  // slave -> master
    SPI_WRITE, // master -> slave
} spi_mode_t;

typedef struct
{
    uint32_t magic : 8; // 0xFE
    uint32_t send_seq : 8;
    uint32_t send_len : 16;
} spi_send_opt_t;

typedef struct
{
    uint32_t direct : 8;
    uint32_t seq_num : 8;
    uint32_t transmit_len : 16;
} spi_recv_opt_t;

typedef struct
{
    spi_mode_t direct;
} spi_msg_t;

/**
 * This structure describes one SPI transaction. The descriptor should not be modified until the transaction finishes.
 */
typedef struct
{
    uint32_t flags;  ///< Bitwise OR of SPI_TRANS_* flags
    uint16_t cmd;    /**< Command data, of which the length is set in the ``command_bits`` of spi_device_interface_config_t.
                      *
                      *  <b>NOTE: this field, used to be "command" in ESP-IDF 2.1 and before, is re-written to be used in a new way in ESP-IDF 3.0.</b>
                      *
                      *  Example: write 0x0123 and command_bits=12 to send command 0x12, 0x3_ (in previous version, you may have to write 0x3_12).
                      */
    uint64_t addr;   /**< Address data, of which the length is set in the ``address_bits`` of spi_device_interface_config_t.
                      *
                      *  <b>NOTE: this field, used to be "address" in ESP-IDF 2.1 and before, is re-written to be used in a new way in ESP-IDF3.0.</b>
                      *
                      *  Example: write 0x123400 and address_bits=24 to send address of 0x12, 0x34, 0x00 (in previous version, you may have to write 0x12340000).
                      */
    size_t length;   ///< Total data length, in bits
    size_t rxlength; ///< Total data length received, should be not greater than ``length`` in full-duplex mode (0 defaults this to the value of ``length``).
    void *user;      ///< User-defined variable. Can be used to store eg transaction ID.
    union
    {
        const void *tx_buffer; ///< Pointer to transmit buffer, or NULL for no MOSI phase
        uint8_t tx_data[4];    ///< If SPI_TRANS_USE_TXDATA is set, data set here is sent directly from this variable.
    };
    union
    {
        void *rx_buffer;    ///< Pointer to receive buffer, or NULL for no MISO phase. Written by 4 bytes-unit if DMA is used.
        uint8_t rx_data[4]; ///< If SPI_TRANS_USE_RXDATA is set, data is received directly to this variable
    };
} spi_transaction_t; // the rx data should start from a 32-bit aligned address to get around dma issue.

static queue_t msg_queue; // message queue used for communicating read/write start
static queue_t esp_at_uart_queue;
// static spi_device_handle_t handle;
// static StreamBufferHandle_t spi_master_tx_ring_buf = NULL;
static const char *TAG = "SPI AT Master";
// static SemaphoreHandle_t pxMutex;
static bool slave_notify_flag = false;
static bool is_ready = false;
static uint8_t initiative_send_flag = 0;  // it means master has data to send to slave
static uint32_t plan_send_len = 0;        // master plan to send data len
static const uint8_t *plan_send_data = 0; // master plan to send data len
static uint8_t trans_data[ESP_SPI_DMA_MAX_LEN];

static uint8_t current_send_seq = 0;
static uint8_t current_recv_seq = 0;

void gpio_handshake_isr_handler(void)
{
    if (gpio_get_irq_event_mask(ESP_AT_HS_PIN) & GPIO_IRQ_EDGE_RISE)
    {
        gpio_acknowledge_irq(ESP_AT_HS_PIN, GPIO_IRQ_EDGE_RISE);
        slave_notify_flag = true;
    }
}

#define spi_mutex_lock()
#define spi_mutex_unlock()
#define ESP_LOGI(TAG, FORMAT, ...) printf("I: " FORMAT "\n", ##__VA_ARGS__)
#define ESP_LOGD(TAG, FORMAT, ...) printf("D: " FORMAT "\n", ##__VA_ARGS__)
#define ESP_LOGE(TAG, FORMAT, ...) printf("E: " FORMAT "\n", ##__VA_ARGS__)
#define handle                     ESP_SPI
static void spi_device_polling_transmit(spi_inst_t *spi_hw, spi_transaction_t *trans)
{

    uint8_t cmd[3] = {(uint8_t)trans->cmd, (uint8_t)trans->addr, 0x00};
    // printf(">>> %02x %02x %02x %d/%d\n", cmd[0], cmd[1], cmd[2], trans->length, trans->rxlength);

    gpio_put(ESP_SPI_CS_PIN, 0);

    spi_write_blocking(spi_hw, cmd, 3);

    if (trans->length && trans->tx_buffer)
    {
        spi_write_blocking(spi_hw, trans->tx_buffer, trans->length / 8);
        // printf(">%d: %02x %02x %02x %02x\n", trans->length, ((const uint8_t *)trans->tx_buffer)[0], ((const uint8_t *)trans->tx_buffer)[1], ((const uint8_t *)trans->tx_buffer)[2], ((const uint8_t *)trans->tx_buffer)[3]);
    }
    else if (trans->rxlength && trans->rx_buffer)
    {
        spi_read_blocking(spi_hw, 0x00, trans->rx_buffer, trans->rxlength / 8);
        // printf("<%d: %02x %02x %02x %02x\n", trans->rxlength, ((uint8_t *)trans->rx_buffer)[0], ((uint8_t *)trans->rx_buffer)[1], ((uint8_t *)trans->rx_buffer)[2], ((uint8_t *)trans->rx_buffer)[3]);
    }

    gpio_put(ESP_SPI_CS_PIN, 1);
}

static void at_spi_master_send_data(uint8_t *data, uint32_t len)
{
    spi_transaction_t trans = {
        .cmd = CMD_HD_WRDMA_REG, // master -> slave command, do not change
        .length = len * 8,
        .tx_buffer = (void *)data};
    spi_device_polling_transmit(handle, &trans);
}

static void at_spi_master_recv_data(uint8_t *data, uint32_t len)
{
    spi_transaction_t trans = {
        .cmd = CMD_HD_RDDMA_REG, // master -> slave command, do not change
        .rxlength = len * 8,
        .rx_buffer = (void *)data};
    spi_device_polling_transmit(handle, &trans);
}

// send a single to slave to tell slave that master has read DMA done
static void at_spi_rddma_done(void)
{
    spi_transaction_t end_t = {
        .cmd = CMD_HD_INT0_REG,
    };
    spi_device_polling_transmit(handle, &end_t);
}

// send a single to slave to tell slave that master has write DMA done
static void at_spi_wrdma_done(void)
{
    spi_transaction_t end_t = {
        .cmd = CMD_HD_WR_END_REG,
    };
    spi_device_polling_transmit(handle, &end_t);
}

// when spi slave ready to send/recv data from the spi master, the spi slave will a trigger GPIO interrupt,
// then spi master should query whether the slave will perform read or write operation.
static spi_recv_opt_t query_slave_data_trans_info(void)
{
    spi_recv_opt_t recv_opt;
    spi_transaction_t trans = {
        .cmd = CMD_HD_RDBUF_REG,
        .addr = RDBUF_START_ADDR,
        .rxlength = 4 * 8,
        .rx_buffer = &recv_opt,
    };
    spi_device_polling_transmit(handle, (spi_transaction_t *)&trans);
    return recv_opt;
}

// before spi master write to slave, the master should write WRBUF_REG register to notify slave,
// and then wait for handshake line trigger gpio interrupt to start the data transmission.
static void spi_master_request_to_write(uint8_t send_seq, uint16_t send_len)
{
    spi_send_opt_t send_opt;
    send_opt.magic = 0xFE;
    send_opt.send_seq = send_seq;
    send_opt.send_len = send_len;

    spi_transaction_t trans = {
        .cmd = CMD_HD_WRBUF_REG,
        .addr = WRBUF_START_ADDR,
        .length = 4 * 8,
        .tx_buffer = &send_opt,
    };
    spi_device_polling_transmit(handle, (spi_transaction_t *)&trans);
    // increment
    current_send_seq = send_seq;
}

// spi master write data to slave
static int8_t spi_write_data(const uint8_t *buf, int32_t len)
{
    if (len > ESP_SPI_DMA_MAX_LEN)
    {
        ESP_LOGE(TAG, "Send length error, len:%ld", len);
        return -1;
    }
    at_spi_master_send_data(buf, len);
    at_spi_wrdma_done();
    return 0;
}

// write data to spi slave, this is just for test
int32_t mdm_write_data_to_slave(const uint8_t *data, size_t size)
{
    if (!is_ready)
    {
        return -1;
    }

    int16_t length = size;

    if (!data || length > STREAM_BUFFER_SIZE)
    {
        ESP_LOGE(TAG, "Write data error, len:%ld", length);
        return -1;
    }

    if (size > 0)
    {
        plan_send_len = size > ESP_SPI_DMA_MAX_LEN ? ESP_SPI_DMA_MAX_LEN : size;
        plan_send_data = data;
        spi_master_request_to_write(current_send_seq + 1, plan_send_len); // to tell slave that the master want to write data
    }

    return length;
}

void mdm_init(void)
{
    // GPIO config for the handshake line.
    gpio_init(ESP_AT_HS_PIN);
    gpio_pull_up(ESP_AT_HS_PIN);

    // Set up handshake line interrupt.
    gpio_add_raw_irq_handler(ESP_AT_HS_PIN, &gpio_handshake_isr_handler);
    gpio_set_irq_enabled(ESP_AT_HS_PIN, GPIO_IRQ_EDGE_RISE, true);
    slave_notify_flag = gpio_get(ESP_AT_HS_PIN);
    irq_set_enabled(IO_IRQ_BANK0, true);

    // init bus
    spi_init(ESP_SPI, ESP_BAUDRATE_HZ);
    gpio_set_function(ESP_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ESP_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ESP_SPI_TX_PIN, GPIO_FUNC_SPI);
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(ESP_SPI_CS_PIN);
    gpio_set_dir(ESP_SPI_CS_PIN, GPIO_OUT);
    gpio_put(ESP_SPI_CS_PIN, 1);

    // spi_recv_opt_t recv_opt = query_slave_data_trans_info();
    // ESP_LOGI(TAG, "now direct:%u", recv_opt.direct);

    // if (recv_opt.direct == SPI_READ)
    // { // if slave in waiting response status, master need to give a read done single.
    //     if (recv_opt.seq_num != ((current_recv_seq + 1) & 0xFF))
    //     {
    //         ESP_LOGE(TAG, "SPI recv seq error, %x, %x", recv_opt.seq_num, (current_recv_seq + 1));
    //         if (recv_opt.seq_num == 1)
    //         {
    //             ESP_LOGE(TAG, "Maybe SLAVE restart, ignore");
    //         }
    //     }

    //     current_recv_seq = recv_opt.seq_num;

    //     at_spi_rddma_done();
    // }
}

void mdm_reclock(void)
{
    spi_set_baudrate(ESP_SPI, ESP_BAUDRATE_HZ);
}

void mdm_task(void)
{
    esp_err_t ret;
    uint32_t send_len = 0;

    if (slave_notify_flag)
    {
        slave_notify_flag = false;

        // printf("send_seq %d / recv_seq %d\n", current_send_seq, current_recv_seq);
        spi_recv_opt_t recv_opt = query_slave_data_trans_info();

        if (recv_opt.direct == SPI_NULL)
        {
            // this happens during reset
            is_ready = false;
            current_send_seq = 0;
            current_recv_seq = 0;
        }
        else if (recv_opt.direct == SPI_WRITE)
        {
            // printf("<<< WRITE_RQ [seq: %x]\n", recv_opt.seq_num);
            if (plan_send_len == 0)
            {
                ESP_LOGE(TAG, "slave is waiting for send data but length is 0 [seq: %x, len: %d]", recv_opt.seq_num, recv_opt.transmit_len);

                current_send_seq = recv_opt.seq_num;
                at_spi_wrdma_done();
                return;
            }

            if (recv_opt.seq_num != current_send_seq)
            {
                ESP_LOGE(TAG, "SPI send seq error, got: %x, was: %x", recv_opt.seq_num, current_send_seq);
                if (recv_opt.seq_num == 1)
                {
                    if (is_ready)
                        ESP_LOGE(TAG, "Maybe SLAVE restart, ignore");
                    current_send_seq = recv_opt.seq_num;
                }
                else
                {
                    return;
                }
            }

            // // initiative_send_flag = 0;
            // send_len = xStreamBufferReceive(spi_master_tx_ring_buf, (void *)trans_data, plan_send_len, 0);

            // if (send_len != plan_send_len)
            // {
            //     ESP_LOGE(TAG, "Read len expect %lu, but actual read %lu\n", plan_send_len, send_len);
            //     break;
            // }

            ret = spi_write_data(plan_send_data, plan_send_len);
            if (ret < 0)
            {
                ESP_LOGE(TAG, "Load data error");
                return;
            }

            // // maybe streambuffer filled some data when SPI transmit, just consider it after send done, because send flag has already in SLAVE queue
            // uint32_t tmp_send_len = xStreamBufferBytesAvailable(spi_master_tx_ring_buf);
            // if (tmp_send_len > 0)
            // {
            //     plan_send_len = tmp_send_len > ESP_SPI_DMA_MAX_LEN ? ESP_SPI_DMA_MAX_LEN : tmp_send_len;
            //     spi_master_request_to_write(current_send_seq + 1, plan_send_len);
            // }
            // else
            // {
            //     initiative_send_flag = 0;
            // }
        }
        else if (recv_opt.direct == SPI_READ)
        {
            // printf("<<< READ_RQ [seq: %x, len: %d]\n", recv_opt.seq_num, recv_opt.transmit_len);
            if (recv_opt.seq_num != ((current_recv_seq + 1) & 0xFF))
            {
                ESP_LOGE(TAG, "SPI recv seq error, got: %x, was: %x", recv_opt.seq_num, (current_recv_seq + 1));
                if (recv_opt.seq_num == 1)
                {
                    if (is_ready)
                        ESP_LOGE(TAG, "Maybe SLAVE restart, ignore");
                }
                else
                {
                    return;
                }
            }

            if (recv_opt.transmit_len > STREAM_BUFFER_SIZE || recv_opt.transmit_len == 0)
            {
                ESP_LOGE(TAG, "SPI read len error, %x", recv_opt.transmit_len);
                return;
            }

            current_recv_seq = recv_opt.seq_num;
            memset(trans_data, 0x0, recv_opt.transmit_len);
            at_spi_master_recv_data(trans_data, recv_opt.transmit_len);
            at_spi_rddma_done();
            trans_data[recv_opt.transmit_len] = '\0';
            if (!strncmp(trans_data, MSG_READY, recv_opt.transmit_len))
            {
                is_ready = true;
            }
            else
            {
                bool has_data = false;
                for (size_t i = 0; i < recv_opt.transmit_len; ++i)
                {
                    if (trans_data[i] != '\r' && trans_data[i] != '\n')
                    {
                        has_data = true;
                    }
                }
                if (has_data)
                {
                    printf("%s", trans_data);
                    // for (size_t i = 0; i < recv_opt.transmit_len; ++i)
                    // {
                    //     printf("%x ", trans_data[i]);
                    // }
                    // printf("\n");
                }
            }
        }
        else
        {
            ESP_LOGD(TAG, "Unknow direct: %d", recv_opt.direct);
            spi_mutex_unlock();
            return;
        }
    }
}

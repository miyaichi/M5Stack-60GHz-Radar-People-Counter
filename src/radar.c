#include "radar.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "radar";

#define RADAR_UART    UART_NUM_2
#define RADAR_RX_PIN  16
#define RADAR_TX_PIN  17
#define RADAR_BAUD    115200
#define RX_BUF_SIZE   512

// Frame format (14 bytes):
//  [0]    0xAA  header1
//  [1]    0x55  header2
//  [2]    0x06  function
//  [3]    0xA2  sub-function
//  [4]    0x06  data length
//  [5]    0x00  reserved
//  [6-7]  uint16 LE: distance (cm)
//  [8-9]  int16  LE: azimuth  (0.01 degrees)
//  [10-11] int16 LE: elevation (0.01 degrees)
//  [12]   uint8: checksum = sum(byte[0..11]) & 0xFF
//  [13]   0x00  tail
#define FRAME_LEN  14
#define HDR1       0xAA
#define HDR2       0x55

static uint8_t  g_rxbuf[RX_BUF_SIZE];
static uint8_t  g_frame[FRAME_LEN];
static int      g_frame_pos = 0;

static bool verify_and_parse(const uint8_t *f, radar_data_t *out)
{
    // Verify checksum
    uint8_t sum = 0;
    for (int i = 0; i < 12; i++) sum += f[i];
    if (sum != f[12]) {
        ESP_LOGW(TAG, "Checksum mismatch: calc=%02X got=%02X", sum, f[12]);
        return false;
    }

    uint16_t dist_cm = (uint16_t)f[6] | ((uint16_t)f[7] << 8);
    int16_t  az_cdeg = (int16_t)((uint16_t)f[8] | ((uint16_t)f[9] << 8));
    int16_t  el_cdeg = (int16_t)((uint16_t)f[10] | ((uint16_t)f[11] << 8));

    out->distance  = dist_cm / 100.0f;
    out->azimuth   = az_cdeg / 100.0f;
    out->elevation = el_cdeg / 100.0f;
    out->target_detected = (dist_cm > 0);
    out->parse_ok  = true;

    memcpy(out->raw, f, FRAME_LEN);
    out->raw_len = FRAME_LEN;
    out->frame_count++;

    ESP_LOGI(TAG, "dist=%.2fm az=%.1fdeg el=%.1fdeg",
             out->distance, out->azimuth, out->elevation);
    return true;
}

static bool feed_byte(uint8_t b, radar_data_t *out)
{
    if (g_frame_pos == 0 && b != HDR1) return false;
    if (g_frame_pos == 1 && b != HDR2) { g_frame_pos = 0; return false; }

    g_frame[g_frame_pos++] = b;

    if (g_frame_pos == FRAME_LEN) {
        g_frame_pos = 0;
        return verify_and_parse(g_frame, out);
    }
    return false;
}

void radar_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = RADAR_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(RADAR_UART, &cfg);
    uart_set_pin(RADAR_UART, RADAR_TX_PIN, RADAR_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(RADAR_UART, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    ESP_LOGI(TAG, "Radar UART2: GPIO%d(RX) GPIO%d(TX) %dbps",
             RADAR_RX_PIN, RADAR_TX_PIN, RADAR_BAUD);
}

bool radar_poll(radar_data_t *data)
{
    int n = uart_read_bytes(RADAR_UART, g_rxbuf, sizeof(g_rxbuf), 0);
    if (n <= 0) return false;

    for (int i = 0; i < n; i++) {
        if (feed_byte(g_rxbuf[i], data)) return true;
    }
    return false;
}

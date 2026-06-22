#pragma once
#include <stdint.h>
#include <stdbool.h>

// Wiring: M5Stack PORT C (blue Grove)
//   GPIO16 (RX) <- Radar TX
//   GPIO17 (TX) -> Radar RX  (not required for read-only)
//   5V           -> Radar VCC
//   GND          -> Radar GND

typedef struct {
    bool    target_detected;
    float   distance;    // meters
    float   azimuth;     // degrees
    float   elevation;   // degrees
    bool    parse_ok;    // true if data was successfully parsed
    uint8_t raw[64];     // raw payload bytes for debugging
    int     raw_len;
    int     frame_count;
} radar_data_t;

void radar_init(void);
bool radar_poll(radar_data_t *data);  // returns true if new frame received

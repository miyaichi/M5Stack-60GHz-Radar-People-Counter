#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "ili9342c.h"
#include "radar.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "main";

// Colors
#define BG      RGB565( 10,  10,  30)
#define CNT_COL RGB565(  0, 255, 128)
#define DIR_COL RGB565(100, 200, 255)
#define ACTIVE  RGB565(  0, 220,   0)
#define COUNTED RGB565(255,  60,  60)
#define LABEL   RGB565(255, 220,   0)
#define DIM     RGB565( 80,  80,  80)

// Layout (320×240)
#define TITLE_Y   5    // "PEOPLE COUNTER" scale=2 (16px)
#define COUNT_Y  30    // big count number scale=6 (48px) → ends at 78
#define DIR_Y   100    // "← N   N →"  scale=2 (16px)
#define RADAR_Y 132    // distance + azimuth scale=2 (16px)
#define STAT_Y  162    // status bar scale=2 (16px)
#define RANGE_Y 196    // range setting scale=2 (16px)
#define BTN_Y   220    // button hint scale=1 (8px)

// Detection zone
#define MIN_DIST        0.15f   // meters — sensor blind zone limit
#define MAX_DIST_DEFAULT 3.0f
#define MAX_DIST_MIN    0.5f
#define MAX_DIST_MAX    3.0f
#define MAX_DIST_STEP   0.5f

// State machine timings (×50ms per tick)
#define ENTRY_CONFIRM   3       // frames in zone to confirm entry
#define EXIT_CONFIRM    5       // frames out of zone to confirm exit + count
#define COOLDOWN_FRAMES 10      // frames before accepting next person

// Direction detection
#define DIR_AZ_THRESH   15.0f   // degrees azimuth change to classify direction

// Buttons
#define BTN_A_PIN  39
#define BTN_B_PIN  38
#define BTN_C_PIN  37

typedef enum { CNT_IDLE, CNT_ENTERING, CNT_TRACKING, CNT_EXITING } cnt_state_t;

typedef struct {
    int total;
    int ltr;            // azimuth increasing (left → right)
    int rtl;            // azimuth decreasing (right → left)
    cnt_state_t state;
    int timer;
    float start_az;
    float last_az;
} counter_t;

static counter_t g_ctr;
static int g_last_total = -1;
static float g_max_dist = MAX_DIST_DEFAULT;

// ── Counter ──────────────────────────────────────────────────────

static bool in_zone(const radar_data_t *d)
{
    return d->parse_ok && d->target_detected
           && d->distance >= MIN_DIST
           && d->distance <= g_max_dist;
}

static void counter_update(const radar_data_t *d)
{
    counter_t *c = &g_ctr;
    bool det = in_zone(d);

    switch (c->state) {
    case CNT_IDLE:
        if (det) {
            c->state    = CNT_ENTERING;
            c->timer    = 1;
            c->start_az = d->azimuth;
            c->last_az  = d->azimuth;
        }
        break;

    case CNT_ENTERING:
        if (det) {
            c->last_az = d->azimuth;
            if (++c->timer >= ENTRY_CONFIRM)
                c->state = CNT_TRACKING;
        } else {
            c->state = CNT_IDLE;
            c->timer = 0;
        }
        break;

    case CNT_TRACKING:
        if (det) {
            c->last_az = d->azimuth;
            c->timer   = 0;
        } else {
            if (++c->timer >= EXIT_CONFIRM) {
                c->total++;
                float delta = c->last_az - c->start_az;
                if      (delta >  DIR_AZ_THRESH) c->ltr++;
                else if (delta < -DIR_AZ_THRESH) c->rtl++;
                ESP_LOGI(TAG, "COUNT %d  ltr=%d rtl=%d  az_delta=%.1f",
                         c->total, c->ltr, c->rtl, delta);
                c->state = CNT_EXITING;
                c->timer = 0;
            }
        }
        break;

    case CNT_EXITING:
        if (++c->timer >= COOLDOWN_FRAMES) {
            c->state = CNT_IDLE;
            c->timer = 0;
        }
        break;
    }
}

// ── Display ──────────────────────────────────────────────────────

static void draw_static_ui(void)
{
    lcd_fill(BG);
    // "PEOPLE COUNTER" = 14 chars × 8 × 2 = 224px → x = (320-224)/2 = 48
    lcd_draw_string_scaled(48, TITLE_Y, "PEOPLE COUNTER", WHITE, BG, 2);
}

static void update_count(int n)
{
    if (n == g_last_total) return;
    g_last_total = n;

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", n);
    int chars = (int)strlen(buf);
    int scale = 6;
    int x = (320 - chars * 8 * scale) / 2;
    if (x < 0) x = 0;

    lcd_fill_rect(0, COUNT_Y, 320, 8 * scale, BG);
    lcd_draw_string_scaled(x, COUNT_Y, buf, CNT_COL, BG, scale);
}

static void update_dir(int ltr, int rtl)
{
    char buf[16];
    lcd_fill_rect(0, DIR_Y, 320, 16, BG);

    snprintf(buf, sizeof(buf), "<- %d", rtl);
    lcd_draw_string_scaled(8, DIR_Y, buf, DIR_COL, BG, 2);

    snprintf(buf, sizeof(buf), "%d ->", ltr);
    int chars = (int)strlen(buf);
    lcd_draw_string_scaled(320 - chars * 16 - 8, DIR_Y, buf, DIR_COL, BG, 2);
}

static void update_radar(const radar_data_t *d)
{
    char buf[32];
    lcd_fill_rect(0, RADAR_Y, 320, 16, BG);
    if (d->parse_ok)
        snprintf(buf, sizeof(buf), "%.2fm  az:%.1fdeg", d->distance, d->azimuth);
    else
        snprintf(buf, sizeof(buf), "No data");
    lcd_draw_string_scaled(8, RADAR_Y, buf, DIM, BG, 2);
}

static void update_status(cnt_state_t state)
{
    const char *msg;
    uint16_t col;
    switch (state) {
    case CNT_IDLE:     msg = "Ready           "; col = DIM;    break;
    case CNT_ENTERING: msg = "Entering...     "; col = LABEL;  break;
    case CNT_TRACKING: msg = ">>> Detecting>>>"; col = ACTIVE; break;
    case CNT_EXITING:  msg = "<<< Counted! >>>"; col = COUNTED;break;
    default:           msg = "                "; col = DIM;    break;
    }
    lcd_fill_rect(0, STAT_Y, 320, 16, BG);
    lcd_draw_string_scaled(8, STAT_Y, msg, col, BG, 2);
}

static void update_range(void)
{
    char buf[32];
    // Line 1: range value in scale=2 (prominent)
    snprintf(buf, sizeof(buf), "Max:%.1fm        ", g_max_dist);
    lcd_fill_rect(0, RANGE_Y, 320, 16, BG);
    lcd_draw_string_scaled(8, RANGE_Y, buf, LABEL, BG, 2);

    // Line 2: button hints in scale=1
    lcd_fill_rect(0, BTN_Y, 320, 8, BG);
    lcd_draw_string(0, BTN_Y, "[A]Reset [B]-0.5m [C]+0.5m", DIM, BG);
}

// ── Main ─────────────────────────────────────────────────────────

static bool btn_pressed(int pin, bool *prev)
{
    bool cur = (gpio_get_level(pin) == 0);
    bool pressed = cur && !(*prev);
    *prev = cur;
    return pressed;
}

void app_main(void)
{
    ESP_LOGI(TAG, "People Counter starting");

    gpio_set_direction(BTN_A_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(BTN_B_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(BTN_C_PIN, GPIO_MODE_INPUT);

    lcd_init();
    radar_init();
    draw_static_ui();
    update_count(0);
    update_dir(0, 0);
    update_range();

    radar_data_t data = {0};
    memset(&g_ctr, 0, sizeof(g_ctr));

    bool prev_a = false, prev_b = false, prev_c = false;

    while (1) {
        // Button A: reset counter
        if (btn_pressed(BTN_A_PIN, &prev_a)) {
            memset(&g_ctr, 0, sizeof(g_ctr));
            g_last_total = -1;
            update_count(0);
            update_dir(0, 0);
            ESP_LOGI(TAG, "Counter reset");
        }

        // Button B: decrease max distance
        if (btn_pressed(BTN_B_PIN, &prev_b)) {
            if (g_max_dist - MAX_DIST_STEP >= MAX_DIST_MIN)
                g_max_dist -= MAX_DIST_STEP;
            update_range();
            ESP_LOGI(TAG, "Max dist: %.1fm", g_max_dist);
        }

        // Button C: increase max distance
        if (btn_pressed(BTN_C_PIN, &prev_c)) {
            if (g_max_dist + MAX_DIST_STEP <= MAX_DIST_MAX)
                g_max_dist += MAX_DIST_STEP;
            update_range();
            ESP_LOGI(TAG, "Max dist: %.1fm", g_max_dist);
        }

        if (radar_poll(&data)) {
            counter_update(&data);
            update_count(g_ctr.total);
            update_dir(g_ctr.ltr, g_ctr.rtl);
            update_radar(&data);
            update_status(g_ctr.state);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

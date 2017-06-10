#include "pattern.h"

#include <tc.h>
#include <tc_interrupt.h>
#include <stdio.h>

struct tc_module tc_instance;

uint16_t pattern_num = 0;
uint16_t frame_num = 0;
uint8_t global_brightness_scale = 0;
uint8_t pattern_running = 1;
uint8_t pattern_tick = 1;

const pattern_def defined_patterns[] = {
  {"White Chase", pattern_chase_white},
  {"Red Chase", pattern_chase_red},
  {"Blue Chase", pattern_chase_blue},
  {"Max Bright", pattern_bright},
  {"50%", pattern_bright_half},
  {"50% PWM", pattern_bright_pwm},
  {"Ripple", pattern_ripple},
  {NULL, NULL},
};

static void frame_next_tick();

void pattern_setup() {
  // Start periodic interrupts to call frame_next.
  // An 8MHz GCLK with a 1024 prescaler into an 8-bit counter = 32.768ms
  struct tc_config config_tc;
  tc_get_config_defaults(&config_tc);

  config_tc.counter_size = TC_COUNTER_SIZE_8BIT;
  config_tc.clock_source = GCLK_GENERATOR_0;
  config_tc.clock_prescaler = TC_CLOCK_PRESCALER_DIV1024;
  config_tc.run_in_standby = true;
  config_tc.counter_8_bit.period = 255;

  // Start things up
  tc_init(&tc_instance, PATTERN_TC, &config_tc);
  tc_register_callback(&tc_instance, frame_next_tick, TC_CALLBACK_OVERFLOW);
  tc_enable_callback(&tc_instance, TC_CALLBACK_OVERFLOW);
  tc_enable(&tc_instance);
}

void pattern_start() {
  while(1) {
    if (pattern_tick > 1) {
      printf("Pattern overflow!\r\n");
    }
    frame_next(&tc_instance);
    pattern_tick--;
    // TODO: standby mode
    while(!pattern_tick){}
  }
}

static void frame_next_tick() {
  pattern_tick++;
}

void pattern_next() {
  if (!pattern_running) {
    pattern_running = 1;
    frame_num = 0;
    return;
  }
  if (defined_patterns[pattern_num+1].pixel_update != NULL) {
    pattern_num++;
  } else {
    pattern_num = 0;
  }
  frame_num = 0;
  printf("Pattern changed to %s\r\n", defined_patterns[pattern_num].name);
}

void pattern_off() {
  // Turn off all the pixels
  int i;
  pattern_running = 0;
  pixel empty = {0,0,0,0};
  apa102c_frame_begin();
  for (i=0;i<NUM_PIXELS;i++)
    apa102c_send_pixel(&empty);
  apa102c_frame_end();
  frame_num = 0;
}

void frame_next(struct tc_module *module) {
  pixel px;
  uint8_t pos;

#ifdef PATTERN_DEBUG
  uint8_t slices = (uint8_t)tc_get_count_value(module);
#endif

  if (!pattern_running)
    return;

  apa102c_frame_begin();
  for (pos=0;pos<NUM_PIXELS;pos++) {
    CLEAR_PIXEL(px);
    defined_patterns[pattern_num].pixel_update(frame_num, pos, &px);
#ifndef DISABLE_GAMMA_CORRECT
    px.red = gamma_table[px.red];
    px.green = gamma_table[px.green];
    px.blue = gamma_table[px.blue];
#endif
    px.brightness = (px.brightness & BRIGHT_MASK) >> global_brightness_scale;
    apa102c_send_pixel(&px);
  }
  apa102c_frame_end();
  frame_num++;
  if (frame_num >= FRAME_NUM_MAX)
    frame_num -= FRAME_NUM_MAX;

#ifdef PATTERN_DEBUG
  slices = (uint8_t)tc_get_count_value(module) - slices;
  printf("Spent %d slices on frame.\r\n", slices);
#endif
}

/* Position information for XXV */
uint8_t pixel_get_row(uint8_t num) {
  num &= 0xF;
  if (num < 8)
    return num;
  return (7 - (num & 7));
}

uint8_t pixel_get_col_coarse(uint8_t num) {
  return (num >> 3);
}

uint8_t pixel_get_col_fine(uint8_t num) {
  uint8_t block;

  // The V
  if (num > 31)
    return (num - 0x10);

  // First or 2nd X?
  block = (num & 0x10) >> 1;
  num &= 0xF;
  // Flip top and bottom
  if (num & 0x4)
    num ^= 0x7;
  if (num < 8)
    return num + block;
  return (7 - (num & 7)) + block;
}

#ifndef MAIN_H
#define MAIN_H

/*********************
 *      INCLUDES
 *********************/

#include <vector>

#ifdef ESP32
  #define ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED 1
  #define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 0
  #include <Arduino.h>
  #include <lvgl.h>
#else // HAL
  #include <cstdlib>
  #include <cstdio>
  #include <iostream>
  #include <string>
  #include "lvgl/lvgl.h"
#endif // ESP32 / HAL

/*********************
 *      DEFINES
 *********************/

#define SCREEN_WIDTH 135
#define SCREEN_HEIGHT 240

#ifdef ESP32
  // create widgets for HAL (or other) without touch, not needed to ESP32 T-Display
  #define LV_OBJ_CREATE_NO_CLICK(function, parent) function(parent)
  #define PRINTF Serial.printf
  #define FLUSH Serial.flush()
  #define STRING String
  #define TO_STRING String
  #define FLOAT_TO_STRING(number, decimals) String(number, decimals)
  #define SUBSTRING substring
#else // HAL
  // create widgets for HAL (or other) without touch, not needed to ESP32 T-Display
  #define LV_OBJ_CREATE_NO_CLICK(function, parent) (__no_click = function(parent)); lv_obj_set_flag(__no_click, LV_OBJ_FLAG_CLICKABLE, false)
  #define PRINTF printf
  #define FLUSH fflush(stdout)
  #define STRING std::string
  #define TO_STRING std::to_string
  #define FLOAT_TO_STRING(number, decimals) std::to_string(number).substr(0, std::to_string(number).find('.') + decimals + 1)
  #define SUBSTRING substr
#endif // ESP32 / HAL

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
  BUTTON_A,
  BUTTON_B,
  BUTTON_C
} buttons_t;

#ifndef ESP32
  typedef enum {
    LOW,
    HIGH
  } button_state_t;
#endif // !ESP32 / HAL

/**********************
 *      VARIABLES
 **********************/

extern lv_obj_t *screen, *__no_click;
extern void (*hop)();
extern void (*initial_hop)();

/**********************
 *  STATIC PROTOTYPES
 **********************/

#ifndef ESP32
int64_t millis();
void delay(int64_t milliseconds);
#endif // !ESP32 / HAL

void log_print(lv_log_level_t level, const char * buf);

// Status
uint8_t get_button_state(uint8_t button);
float get_battery_voltage();
float get_battery_percent();
bool wifi_is_connected();
bool ble_is_connected();

void update_interface();
void clear_screen();

// Read/send data from/to antenna
STRING antenna_send_command(STRING command);
STRING antenna_read_command();
void antenna_clear();
void antenna_prepare_read();
std::vector<STRING> antenna_read(bool allow_replication = false, STRING filter = STRING());

// Send data via BLE
bool send_data_via_ble(STRING data, int key_interval_ms = 30, bool send_endl = true);


#endif // MAIN_H

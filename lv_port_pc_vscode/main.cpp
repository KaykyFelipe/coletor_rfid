
/**
 * @file main
 *
 */

#include "main.h"
#include "src/ble/ble.h"
#include "src/buttons/buttons.h"
#include "src/display/display.h"
#include "src/power/power.h"
#include "src/state/app.h"

/*********************
 *      ESP32
 *********************/

#ifdef ESP32

/* For use NimBLE, do this changes: (https://github.com/T-vK/ESP32-BLE-Keyboard/issues/340)
 * Needed to include "NimBLEConnInfo.h", "NimBLEDevice.h" and "NimBLEServer.h" on BleKeyboard.h at line 11.
 * Needed also to include this function about line 180 with NimBLE compiler "#if" directive and respective fix on "BleKeyboard.cpp":
  virtual void onConnect(BLEServer* pServer, NimBLEConnInfo & connInfo) override;
  virtual void onDisconnect(BLEServer* pServer, NimBLEConnInfo & connInfo, int reason) override;
  virtual void onWrite(BLECharacteristic* me, NimBLEConnInfo & connInfo) override;
 * On "BleKeyboard.cpp", some functions was changed to add "get" or "set".
 */
//#define USE_NIMBLE
#include <BleKeyboard.h>

#include <TFT_eSPI.h>

#include <pins_arduino.h>
#include <driver/rtc_io.h>
#include <esp_adc_cal.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>

#define BUTTON_A_PIN 27  // green  | 27 |  0 for board button
#define BUTTON_B_PIN 32 // yellow | 32 | 35 for board button
#define BUTTON_C_PIN 33  // orange
#define RX2           2  // black  => TX at antenna
#define TX2          15  // yellow => RX at antenna
#define ANTENNA_EN   17  // green  => EN at antenna
#define BATTERY      25  // blue

// https://github.com/Xinyuan-LilyGO/TTGO-T-Display
#define TFT_Driver   ST7789
#define TFT_MISO     N/A
#define TFT_MOSI     19
#define TFT_SCLK     18
#define TFT_CS        5
#define TFT_DC       16
#define TFT_RST      23
#define TFT_BL        4
#define I2C_SDA      21
#define I2C_SCL      22
#define ADC_IN 	     34  // V_BAT
#define BUTTON1      35
#define BUTTON2       0
#define ADC_Power    14

#else // HAL

/*********************
 *      INCLUDES HAL
 *********************/

#ifdef _MSC_VER
  #include <Windows.h>
#else
  #include <unistd.h>
  #include <pthread.h>
#endif

#include <chrono>
#include <SDL.h>
#include <fstream>
#include <algorithm>

/*********************
 *      DEFINES HAL
 *********************/

#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE /* needed for usleep() */
#endif

/**********************
 *      TYPEDEFS HAL
 **********************/

class BleKeyboard {
  private:
    bool connected = false;
  public:
    bool isConnected() {return connected;}
    void setConnected(bool status) {connected = status;}
};

/**********************
 *  STATIC PROTOTYPES HAL
 **********************/

static lv_display_t * hal_init(int32_t w, int32_t h);

/**********************
 *  STATIC VARIABLES HAL
 **********************/

static button_state_t button_state[] = {HIGH, HIGH, HIGH};
static BleKeyboard ble_keyboard;

/**********************
 *      MACROS HAL
 **********************/

/**********************
 *   GLOBAL FUNCTIONS HAL
 **********************/

extern void freertos_main(void);

#endif // ESP32

#include <regex>

#define INACTIVE_POWEROFF_MILLISECONDS 600000UL
static uint64_t millis_before, millis_now;
static uint64_t heartbeat_millis = 0, heartbeat_duration = 1000;
static uint64_t poweroff_millis = 0;
static uint64_t battery_millis = 0;
static uint64_t antenna_millis = 0;
static uint64_t wifi_millis = 0;
static uint64_t bluetooth_millis = 0;
static bool wifi_connected = false, ble_connected = false;
static std::vector<STRING> assets;

lv_obj_t *screen, *__no_click;

#ifdef ESP32

static int vref = 1100;
static float battery_voltage = 3.7;  // to do movel average
static float battery_factor = 2. * 3.75 / 3.8;  // resistors diff, ideal 2.
static adc_oneshot_unit_handle_t adc2_handle;
static adc_cali_handle_t adc_cali;

// Teclado HID + servico GATT proprio, criados em src/ble/ble.cpp
static BleKeyboard &ble_keyboard = ble_keyboard_instance();

static char buffer[1024] = {'\0'};
static size_t len_buffer = 0;
static char hex[3] = {'\0'};
static STRING sbuffer;

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
static uint32_t draw_buf[DRAW_BUF_SIZE];  // / 4];

static void init_pins() {
  rtc_gpio_deinit((gpio_num_t)BUTTON_C_PIN);  // devolve o pino ao uso normal depois do deep sleep
  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);
  pinMode(ANTENNA_EN, OUTPUT);
  pinMode(BATTERY, INPUT);
  digitalWrite(ANTENNA_EN, HIGH);
}

static void print_wakeup_reason() {
    esp_sleep_wakeup_cause_t wakeup_reason;

    wakeup_reason = esp_sleep_get_wakeup_cause();

    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0 :
            Serial.println("Wakeup caused by external signal using RTC_IO");
            rtc_gpio_deinit(GPIO_NUM_17);
            rtc_gpio_deinit(GPIO_NUM_15);
            break;
        case ESP_SLEEP_WAKEUP_EXT1 :
            Serial.println("Wakeup caused by external signal using RTC_CNTL");
            break;
        case ESP_SLEEP_WAKEUP_TIMER :
            Serial.println("Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD :
            Serial.println("Wakeup caused by touchpad"); break;
            rtc_gpio_deinit(GPIO_NUM_17);
            rtc_gpio_deinit(GPIO_NUM_15);
            break;
        case ESP_SLEEP_WAKEUP_ULP :
            Serial.println("Wakeup caused by ULP program");
            break;
        default :
            Serial.printf("Wakeup was not caused by deep sleep: %d\r\n",wakeup_reason);
            break;
    }
    int touchPin = esp_sleep_get_touchpad_wakeup_status();
    Serial.printf("Touch detected on touch pin %d\r\n", touchPin);
}

static void init_serial() {
    Serial.begin(115200);    // Initialize serial communications with the PC
    while (!Serial);         // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)

    Serial.println("\r\n");
    Serial.println("**********************************************");
    Serial.println("*        Collector by Junior Polegato        *");
    Serial.println("**********************************************");
    Serial.println("\r\n");
    print_wakeup_reason();
}

static void init_antenna() {
    Serial2.begin(38400, SERIAL_8N1, RX2, TX2);    // Initialize serial communications with the antenna RFID UHF
    while (!Serial2);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
    Serial2.setTimeout(250);

    Serial2.write("\nN0,00\r");
    len_buffer = Serial2.readBytes(buffer, 1023);
    PRINTF("Power: |%d| %s\r\n", len_buffer, buffer);

    Serial2.write("\nN4,00\r");  // 01: US 902~928 | 02: TW 922~928 | 03: CN 920~925 | 04: CN2 840~845 | 05: EU 865~868 | 06: JP 916~921 | 07: KR 917~921 | 08: VN 918~923
    len_buffer = Serial2.readBytes(buffer, 1023);
    PRINTF("Frequency: |%d| %s\r\n", len_buffer, buffer);

    Serial2.setTimeout(50);
}

static void cal_vref() {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_2,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc2_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_11, // Adjust attenuation as needed
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Adjust bitwidth as needed
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, ADC_CHANNEL_8, &config));

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(init_config.unit_id, config.atten, (adc_bits_width_t)config.bitwidth, 1100, &adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.printf("eFuse Vref:%u mV\r\n", adc_chars.vref);
        vref = adc_chars.vref;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.printf("eFuse Two Point --> coeff_a:%umV coeff_b:%umV\r\n", adc_chars.coeff_a, adc_chars.coeff_b);
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP_FIT) {
        Serial.printf("eFuse Two Point --> coeff_a:%umV coeff_b:%umV\r\n", adc_chars.coeff_a, adc_chars.coeff_b);
    } else {
        Serial.printf("Default Vref: 1100mV\r\n");
    }

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = init_config.unit_id,
        .atten = config.atten,
        .bitwidth = config.bitwidth,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali));

    int raw_value;
    ESP_ERROR_CHECK(adc_oneshot_read(adc2_handle, ADC_CHANNEL_8, &raw_value));
    Serial.printf("Raw value: %d\n", raw_value);

    int calibrated_voltage;
    ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(adc2_handle, adc_cali, ADC_CHANNEL_8, &calibrated_voltage));
    Serial.printf("Calibrated Voltage: %d mV\n", calibrated_voltage);

    // uint16_t v = analogRead(BATTERY);
    // battery_voltage = ((float)v / 4095.) * 3.3 * (vref / 1000.) * battery_factor;
    battery_voltage = calibrated_voltage / 1000. * battery_factor;
}

static void init_ble_keyboard() {
    Serial.println("Starting BLE work!");
    ble_keyboard.setName("Collector keyboard");
    ble_keyboard.begin();
}

void setup() {
  // Start LVGL
  lv_init();

  // Create a display object
  lv_display_t * disp;

  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));

  // Rotation
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

  // Set the active screen
  screen = lv_screen_active();

#else

static lv_obj_t *btn, *antenna, *battery, *battery_label;
void loop();

int main(int argc, char **argv){
  (void)argc; /*Unused*/
  (void)argv; /*Unused*/

  /*Initialize LVGL*/
  lv_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
  hal_init(SCREEN_HEIGHT * 1.5, SCREEN_WIDTH * 5.5);

#endif // ESP32

  // Build the fixed top bar and the central area
  display_init();
  buttons_init();
  app_init();

  // Register print function for debugging
  lv_log_register_print_cb(log_print);

  #if LV_USE_OS == LV_OS_NONE

    lv_timer_handler();

    millis_before = millis();
    poweroff_millis = millis_before;
    antenna_millis = 100 * millis_before;
    heartbeat_millis = millis_before;
    battery_millis = millis_before;
    wifi_millis = millis_before;
    bluetooth_millis = millis_before;

    #ifdef ESP32
      // Initialiaze the hardware
      init_serial();
      init_pins();
      cal_vref();
      init_antenna();
      init_ble_keyboard();
    #else // ESP32
      while (true)
        loop();
      return 0;
    #endif // ESP32

  #elif LV_USE_OS == LV_OS_FREERTOS
    /* Run FreeRTOS and create lvgl task */
    freertos_main();
    return 0;
  #endif // LV_USE_OS
}

static void verify_inactivity() {
  if (millis_now - poweroff_millis > INACTIVE_POWEROFF_MILLISECONDS) {
    #ifdef ESP32
      // Desliga a tela e dorme ate o botao de leitura (C) ser pressionado.
      // Antes o aparelho acordava sozinho a cada 35 s e gastava bateria.
      display_set_backlight(false);
      rtc_gpio_pullup_en((gpio_num_t)BUTTON_C_PIN);
      rtc_gpio_pulldown_dis((gpio_num_t)BUTTON_C_PIN);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_C_PIN, 0);  // 0 = pressionado
      esp_deep_sleep_start();
    #else
      PRINTF("exit: %d | %d | %d | %d\n", millis_now, poweroff_millis, millis_now - poweroff_millis, INACTIVE_POWEROFF_MILLISECONDS);
      exit(0);
    #endif // ESP32
  }
  // Restart timeout for poweroff if any buttons are pressed
  if (get_button_state(BUTTON_A) == LOW || get_button_state(BUTTON_B) == LOW || get_button_state(BUTTON_C) == LOW) {
    poweroff_millis = millis();
  }
}

static void update_battery_indicator() {
  float percent = get_battery_percent();
  PRINTF("Battery: %.2f%%\n", percent);
  const char *symbol = percent > 85. ? "\uF240" : percent > 60. ? "\uF241" : percent > 40. ? "\uF242" : percent > 10. ? "\uF243" : "\uF244";
  display_set_battery(percent, symbol);
}

static void heatbeat() {
  if (heartbeat_duration == 1000 || heartbeat_duration == 332) {
    heartbeat_duration = heartbeat_duration == 1000 ? 333 : 335;
  }
  else {
    heartbeat_duration = heartbeat_duration == 333 ? 332 : 1000;
  }
}

void update_interface() {
  lv_timer_handler();
  millis_now = millis();
  lv_tick_inc(millis_now - millis_before);  // tell LVGL how much time has passed
  millis_before = millis_now;
}

void loop() {
  PRINTF(".");
  FLUSH;

  /* Periodically call the lv_task handler.
   * It could be done in a timer interrupt or an OS task too.*/
  update_interface();

  // Heartbeat
  if (millis_now - heartbeat_millis > heartbeat_duration) {
    PRINTF("heartbeat_duration: %llu\n", heartbeat_duration);
    heartbeat_millis = millis_now;

    // Update status each about 2 seconds
    if (heartbeat_duration == 1000) {
      update_battery_indicator();
      verify_inactivity();
      ble_connected = ble_keyboard.isConnected();
      display_set_connected(ble_connected);
    }

    heatbeat();
  }

  // Buttons (with debounce) and screens
  buttons_update(millis_now);
  if (buttons_any_down()) poweroff_millis = millis_now;  // qualquer aperto adia o desligamento
  power_loop(millis_now);  // apaga/acende a tela
  app_loop(millis_now);

  // Wait 5 ms
  delay(5);
}

 // If logging is enabled, it will inform the user about what is happening in the library
void log_print(lv_log_level_t level, const char * buf) {
    LV_UNUSED(level);
    PRINTF("===> %s", buf);
    FLUSH;
}

uint8_t get_button_state(uint8_t button) {
  #ifdef ESP32
    return digitalRead(button == BUTTON_A ? BUTTON_A_PIN : button == BUTTON_B ? BUTTON_B_PIN : BUTTON_C_PIN);
  #else
    return button_state[button];
  #endif
}

float get_battery_voltage() {
  #ifdef ESP32
    // uint16_t v = analogRead(BATTERY);
    // auto voltage = ((float)v / 4095.) * 3.3 * (vref / 1000.) * battery_factor;
    // battery_voltage = (battery_voltage * 4. + current) / 5.;  // do movel average

    int calibrated_voltage;
    ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(adc2_handle, adc_cali, ADC_CHANNEL_8, &calibrated_voltage));
    Serial.printf("Calibrated Voltage: %d mV\n", calibrated_voltage);

    // uint16_t v = analogRead(BATTERY);
    // battery_voltage = ((float)v / 4095.) * 3.3 * (vref / 1000.) * battery_factor;
    battery_voltage = (battery_voltage * 4. + calibrated_voltage / 1000. * battery_factor) / 5.;  // do movel average
    PRINTF("~~~~~~~~~~~~~ voltage: %.5f (%u) ~~~~~~~~~~~~~\r\n", battery_voltage, BATTERY);
    return battery_voltage;
  #else
    return lv_slider_get_value(battery) / 100.;
  #endif // ESP32
}

float get_battery_percent() {
  float voltage = get_battery_voltage();
  if (voltage > 4.1) {  // 100%
    return 100.;
  }
  if (voltage > 3.3) {  // 100% - 0%
    return 100. - ((4.1 - voltage) / .008);
  }
  return 0.;
}

bool wifi_is_connected() {
  return wifi_connected;
}

bool ble_is_connected() {
  return ble_connected;
}

void antenna_clear() {
  assets.clear();
}

void antenna_prepare_read() {
  #ifdef ESP32
    Serial2.write("\nU\r");
  #endif // ESP32
}

std::vector<STRING> antenna_read(bool allow_replication, STRING filter) {
  std::vector<STRING> _assets;
  std::regex pattern(filter.c_str());

  #ifdef ESP32

    len_buffer = 0;
    sbuffer.clear();
    do {
        len_buffer = Serial2.readBytes(&buffer[len_buffer], 1023);  // \nU340053522E30322E303030303737DFBB\r\nU340053522E30322E303030303737DFBB\r\nU\r\n
        STRING data(buffer, len_buffer);
        sbuffer += data;
        data.replace('\n', '.');
        data.replace('\r', '_');
        // Serial.printf("buffer: |%d| %s\r\n", len_buffer, data.c_str());
    } while (len_buffer > 0 && !sbuffer.endsWith("\nU\r\n") && !sbuffer.endsWith("\nX\r\n"));
    // Serial.printf("Lidos: __________|%d|__________\r\n%s\r\n______________________\r\n", len_buffer, sbuffer.c_str());

    len_buffer = sbuffer.length();
    if (len_buffer < 12) {
      return _assets;
    }

    STRING asset;
    int count = 0;
    for(int i = 6; i < len_buffer; i += 2) {
        strncpy(hex, &sbuffer[i], 2);
        char chr = (char)strtol(hex, NULL, 16);
        // Serial.printf("i: %3d | hex: %s | chr: %c\r\n", i, hex, chr);
        if (chr < ' ' || chr > '~')
            asset += '.';
        else
            asset += chr;
        count++;
        // if (count == 12) {
        if (sbuffer[i + 1] == '\n') {
            asset.remove(asset.length() - 3, 3);
            Serial.printf("ASC-II: »%s«\r\n", asset.c_str());

            if (allow_replication || std::find(assets.begin(), assets.end(), asset) == assets.end()) {
              if (filter.length() == 0 || std::regex_match(asset.c_str(), pattern)) {
                assets.push_back(asset);
                _assets.push_back(asset);
              }
            }

            do {
                while (i < len_buffer && sbuffer[i++] != 'U');
            } while (sbuffer[i] == '\r' && i < len_buffer);
            i += 2;

            // Serial.printf("**** %d | %02X ****\r\n", i, sbuffer[i]);
            count = 0;
            asset.clear();
        }
    }

  #else // HAL

    size_t i = 0, j = 0;
    STRING text(lv_textarea_get_text(antenna)), asset;
    std::cout << "------------" << std::endl << text << "------------" << std::endl;
    while ((j = text.find('\n', i)) != std::string::npos) {
      asset = text.substr(i, j - i);
      if (allow_replication || std::find(assets.begin(), assets.end(), asset) == assets.end()) {
        if (filter.size() && std::regex_match(asset, pattern)) {
          assets.push_back(asset);
          _assets.push_back(asset);
        }
      }
      i = j + 1;
    }

  #endif // ESP32 / HAL

  return _assets;
}

bool send_data_via_ble(STRING data, int key_interval_ms, bool send_endl) {
  if (ble_keyboard.isConnected()) {
    PRINTF("Sending via BLE: %s\r\n", data.c_str());

    #ifdef ESP32

      ble_keyboard.setDelay(key_interval_ms);
      if (send_endl) {
        ble_keyboard.println(data);
      }
      else {
        ble_keyboard.print(data);
      }
      ble_keyboard.flush();

    #else // HAL

      STRING file_name = "/tmp/collector_ble.txt";
      std::ofstream outFile(file_name, std::ios::app);
      if (outFile.is_open()) {
          outFile << data << std::endl; // Writes the string and a newline
          outFile.close();
          std::cout << "Data written successfully." << std::endl;
      } else {
          std::cerr << "Unable to open file." << std::endl;
      }
      PRINTF("Data added to file: %s", file_name.c_str());

    #endif // ESP32 / HAL

    return true;
  }

  PRINTF("BLE not connected to send: %s\r\n", data.c_str());
  return false;
}

/*****************   Just for HAL   *****************/

#ifndef ESP32

// Sleep about milliseconds
void delay(int64_t milliseconds) {
  #if defined _MSC_VER
    Sleep(milliseconds);
  #else
    usleep(milliseconds * 1000);
  #endif // _MSC_VER
}

// Get the system milliseconds
int64_t millis() {
    // Get the current time point from the system clock
    auto now = std::chrono::system_clock::now();

    // Convert the time point to duration since epoch
    auto duration_since_epoch = now.time_since_epoch();

    // Cast the duration to milliseconds and get the count
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration_since_epoch).count();
}

/**********************
 *   STATIC FUNCTIONS HAL
 **********************/

static void antenna_event_cb(lv_event_t *e) {
  const char *action = (const char *)lv_event_get_user_data(e);
  if (action == "📋") {
    char *text = SDL_GetClipboardText();
    lv_textarea_add_text(antenna, text);
    SDL_free(text);
  }
  else if (action == "🧹") {
    lv_textarea_set_text(antenna, "");
  }
  else if (action == "🖮") {
    ble_keyboard.setConnected(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
  }
  else if (action == "🛜") {
    ble_keyboard.setConnected(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
  }
}

static void button_event_cb(lv_event_t *e) {
  const char *buttons = (const char *)lv_event_get_user_data(e);
  lv_event_code_t event = lv_event_get_code(e);
  for (size_t b=0; b < strlen(buttons); b++) {
    button_state[buttons[b] - 'A'] = (button_state_t)(event == LV_EVENT_RELEASED);
  }
  printf("------- %s ===> %s ---------\n", buttons, event == LV_EVENT_RELEASED ? "RELEASED" : "PRESSED");
  printf("Button A: %s\n", button_state[BUTTON_A] ? "HIGH" : "LOW");
  printf("Button B: %s\n", button_state[BUTTON_B] ? "HIGH" : "LOW");
  printf("Button C: %s\n", button_state[BUTTON_C] ? "HIGH" : "LOW");
}

// Callback that prints the current slider value on the HAL
static void battery_cb(lv_event_t *e) {
    STRING voltage;
    voltage = "Battery: " + FLOAT_TO_STRING(lv_slider_get_value(battery) / 100., 2) + " V";
    lv_label_set_text(battery_label, voltage.c_str());
    LV_LOG_USER("Battery changed to %s%", voltage.c_str());
}

/**
 * Initialize the Hardware Abstraction Layer (HAL) for the LVGL graphics
 * library
 */
static lv_display_t * hal_init(int32_t w, int32_t h) {
  lv_obj_t *btn, *lbl;

  lv_group_set_default(lv_group_create());

  lv_display_t * disp = lv_sdl_window_create(w, h);
  lv_sdl_window_set_title(disp, "Collector by Junior Polegato");

  lv_indev_t * mouse = lv_sdl_mouse_create();
  lv_indev_set_group(mouse, lv_group_get_default());
  lv_indev_set_display(mouse, disp);
  lv_display_set_default(disp);

  LV_IMAGE_DECLARE(mouse_cursor_icon); /*Declare the image file.*/
  lv_obj_t * cursor_obj;
  cursor_obj = lv_image_create(lv_screen_active()); /*Create an image object for the cursor */
  lv_image_set_src(cursor_obj, &mouse_cursor_icon);           /*Set the image source*/
  lv_indev_set_cursor(mouse, cursor_obj);             /*Connect the image  object to the driver*/

  lv_indev_t * mousewheel = lv_sdl_mousewheel_create();
  lv_indev_set_display(mousewheel, disp);
  lv_indev_set_group(mousewheel, lv_group_get_default());

  lv_indev_t * kb = lv_sdl_keyboard_create();
  lv_indev_set_display(kb, disp);
  lv_indev_set_group(kb, lv_group_get_default());

  // Body
  lv_obj_t *body = lv_obj_create(lv_screen_active());
  lv_obj_set_size(body, w - 10, h - 10);
  lv_obj_set_pos(body, 5, 5);
  lv_obj_set_style_radius(body, 40, LV_PART_MAIN);

  static lv_style_t style_body;
  lv_style_init(&style_body);
  lv_style_set_bg_color(&style_body, lv_palette_darken(LV_PALETTE_GREY, 4));
  lv_obj_add_style(body, &style_body, 0);

  // Antenna
  antenna = lv_textarea_create(body);
  lv_obj_set_size(antenna, w - 50, h / 3);
  lv_obj_align(antenna, LV_ALIGN_TOP_MID, 0, 10);
  lv_textarea_set_placeholder_text(antenna, "Antena: digite aqui as TAGs que serão lidas");
  // lv_textarea_add_text(antenna, "F001: \uF001 F008: \uF008 F00B: \uF00B F00C: \uF00C F00D: \uF00D F011: \uF011 F013: \uF013 F015: \uF015 F019: \uF019 F01C: \uF01C F021: \uF021 F026: \uF026 F027: \uF027 F028: \uF028 F03E: \uF03E F048: \uF048 F04B: \uF04B F04C: \uF04C F04D: \uF04D F051: \uF051 F052: \uF052 F053: \uF053 F054: \uF054 F067: \uF067 F068: \uF068 F06E: \uF06E F070: \uF070 F071: \uF071 F074: \uF074 F077: \uF077 F078: \uF078 F079: \uF079 F07B: \uF07B F093: \uF093 F095: \uF095 F0C4: \uF0C4 F0C5: \uF0C5 F0C7: \uF0C7 F0E7: \uF0E7 F0EA: \uF0EA F0F3: \uF0F3 F11C: \uF11C F124: \uF124 F15B: \uF15B F1EB: \uF1EB F240: \uF240 F241: \uF241 F242: \uF242 F243: \uF243 F244: \uF244 F287: \uF287 F293: \uF293 F2ED: \uF2ED F304: \uF304 F55A: \uF55A F7C2: \uF7C2 F8A2: \uF8A2");
  lv_textarea_add_text(antenna, "TP.01.123456\nTP.01.004321\nTP.02.000015\nTP.02.000014\n");

  btn = lv_button_create(body);
  lv_label_set_text(lv_label_create(btn), "\uF0EA Colar");
  lv_obj_add_event_cb(btn, antenna_event_cb, LV_EVENT_CLICKED, (void *)"📋");
  lv_obj_align(btn, LV_ALIGN_CENTER, 115, -68);

  btn = lv_button_create(body);
  lv_label_set_text(lv_label_create(btn), "\uF021 Apagar");
  lv_obj_add_event_cb(btn, antenna_event_cb, LV_EVENT_CLICKED, (void *)"🧹");
  lv_obj_align(btn, LV_ALIGN_CENTER, 15, -68);

  // BLE switch
  btn = lv_switch_create(body);
  lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "\uF293 BLE");
  lv_obj_set_width(btn, 80);
  lv_obj_add_event_cb(btn, antenna_event_cb, LV_EVENT_CLICKED, (void *)"🖮");
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_obj_align(btn, LV_ALIGN_CENTER, -120, -68);

  // Wi-Fi switch
  btn = lv_switch_create(body);
  lbl = lv_label_create(btn);
  lv_obj_set_width(btn, 80);
  lv_label_set_text(lbl, "\uF1EB Wi-Fi");
  lv_obj_add_event_cb(btn, antenna_event_cb, LV_EVENT_CLICKED, (void *)"🛜");
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
  lv_obj_align(btn, LV_ALIGN_CENTER, -120, -30);

  // Work screen
  screen = LV_OBJ_CREATE_NO_CLICK(lv_obj_create, body);
  lv_obj_set_size(screen, SCREEN_HEIGHT, SCREEN_WIDTH);
  lv_obj_align(screen, LV_ALIGN_CENTER, 0, 70);

  static lv_style_t style_screen_padding_0;
  lv_style_init(&style_screen_padding_0);
  lv_style_set_pad_all(&style_screen_padding_0, -3);
  lv_obj_add_style(screen, &style_screen_padding_0, LV_PART_MAIN);

  // Buttons
  static lv_style_t style_button;
  lv_style_init(&style_button);
  lv_style_set_text_font(&style_button, &lv_font_montserrat_24);
  lv_style_set_bg_color(&style_button, lv_palette_lighten(LV_PALETTE_BLUE_GREY, 2));

  const char *labels[] = {"A", ".", "B", ".",  ".", "C", "."};
  const char *buttons[] = {"A", "AB", "B", "ABC",  "AC", "C", "BC"};
  int32_t x_ofss[] = {10, 75, 110, 75, 30, 60, 120};
  int32_t y_ofss[] = {-85, -110, -85, -70, -55, -15, -55};
  int32_t ws[] = {LV_SIZE_CONTENT, 20, LV_SIZE_CONTENT, 20, 20, LV_SIZE_CONTENT, 20};
  int32_t hs[] = {LV_SIZE_CONTENT, 20, LV_SIZE_CONTENT, 20, 20, LV_SIZE_CONTENT, 20};
  for (size_t button = 0; button < sizeof(labels) / sizeof(labels[0]); button++){
    btn = lv_button_create(body);
    lv_obj_add_style(btn, &style_button, 0);
    lv_label_set_text(lv_label_create(btn), labels[button]);
    lv_obj_set_size(btn, ws[button], hs[button]);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, x_ofss[button], y_ofss[button]);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_PRESSED, (void *)buttons[button]);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_RELEASED, (void *)buttons[button]);
  }

  // Battery voltage via slider
  battery = lv_slider_create(body);
  lv_obj_set_width(battery, 120);
  lv_obj_align(battery, LV_ALIGN_BOTTOM_RIGHT, -10, -20);
  lv_obj_add_event_cb(battery, battery_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_slider_set_range(battery, 280, 420);
  lv_slider_set_value(battery, 382, LV_ANIM_OFF);

  // Create a label over the slider to display the current slider value
  battery_label = lv_label_create(body);
  lv_label_set_text(battery_label, "Battery: 3.82 V");
  lv_obj_align_to(battery_label, battery, LV_ALIGN_CENTER, 0, 0);

  return disp;
}

#endif // !ESP32 (HAL)

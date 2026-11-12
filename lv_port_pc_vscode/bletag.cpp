#include "main.h"
#include "bletag.h"

static uint16_t current_pos = 0;
static uint16_t next_pos = 1;
static std::vector<STRING> operations = {"Coletar", "Aplicativo", "Planilha", "Limpar", "Reiniciar"};
static std::vector<STRING> tags;
static std::vector<lv_obj_t*> buttons_operations;
static std::vector<lv_obj_t*> buttons_tags;
static std::vector<lv_obj_t*> buttons;
static lv_obj_t *button_ok, *slider, *slider_label, *border;
static bool button_A_released = true;
static bool button_B_released = true;
static bool button_C_released = true;
static uint64_t bletag_millis = 0;
static bool cleanning = false, reset = false;
static lv_style_t style_button_green;

static bool allow_replication = false;
static STRING filter = "^([A-Z]{2}\\.\\d{2}\\.\\d{6}|AtivaLocacao|0123456789AB)$";

void (*initial_hop)() = bletag_init;

static void bletag_loop();

void bletag_init() {
    lv_obj_t *button;

    PRINTF("Creating BLE TAG screen...\n");

    // Clean the screen
    clear_screen();

    // Style green for pressed or checked buttons states
    lv_style_init(&style_button_green);
    lv_style_set_bg_color(&style_button_green, lv_palette_main(LV_PALETTE_GREEN));
    lv_obj_add_style(screen, &style_button_green, LV_PART_SCROLLBAR);

    // Style padding 5 for buttons
    static lv_style_t style_button_padding_5;
    lv_style_init(&style_button_padding_5);
    lv_style_set_pad_all(&style_button_padding_5, 5);

    // Create a button with text label aligned center on top
    button = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(button, &style_button_padding_5, LV_STATE_DEFAULT);
    lv_label_set_text(lv_label_create(button), "     Ativa Locação     ");
    lv_obj_set_flag(button, LV_OBJ_FLAG_FLOATING, true);

    // Create a Button (button_ok)
    button_ok = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
    // lv_obj_add_event_cb(button_ok, event_handler_button_ok, LV_EVENT_CLICKED, NULL);
    lv_obj_align(button_ok, LV_ALIGN_BOTTOM_RIGHT, -5, -12);
    lv_obj_remove_flag(button_ok, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_style(button_ok, &style_button_green, LV_STATE_PRESSED);
    lv_obj_add_style(button_ok, &style_button_padding_5, LV_STATE_DEFAULT);
    lv_label_set_text(lv_label_create(button_ok), "Ok");
    lv_obj_set_flag(button_ok, LV_OBJ_FLAG_FLOATING, true);

    // Create all toggles buttons
    next_pos = 0;
    for (const STRING& op : operations) {
        lv_obj_t *button = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
        lv_obj_align(button, LV_ALIGN_CENTER, SCREEN_HEIGHT * next_pos++, 0);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_style(button, &style_button_green, LV_STATE_CHECKED);
        lv_obj_set_style_text_font(button, &lv_font_montserrat_24, LV_STATE_DEFAULT);
        lv_label_set_text(lv_label_create(button), op.c_str());
        buttons_operations.push_back(button);
        buttons.push_back(button);
    }
    lv_obj_set_state(buttons[0], LV_STATE_CHECKED, true);
    current_pos = 0;
    next_pos = 1;

    // Create a slider aligned in the center bottom of the TFT display
    slider = LV_OBJ_CREATE_NO_CLICK(lv_slider_create, screen);
    lv_obj_set_width(slider, 175);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_LEFT, 15, -17);
    // lv_obj_add_event_cb(slider, slider_event_callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_slider_set_range(slider, 0, 0);
    lv_obj_set_style_anim_duration(slider, 1000, 0);
    lv_obj_set_flag(slider, LV_OBJ_FLAG_FLOATING, true);

    // Create a label below the slider to display the current slider value
    slider_label = lv_label_create(screen);
    lv_label_set_text(slider_label, "0/0");
    lv_obj_align_to(slider_label, slider, LV_ALIGN_CENTER, 0, 1);
    lv_obj_set_flag(slider_label, LV_OBJ_FLAG_FLOATING, true);

    // Boder at the right of scroll
    border = lv_label_create(screen);
    lv_label_set_text(border, "");
    lv_obj_align_to(border, buttons.back(), LV_ALIGN_CENTER, SCREEN_HEIGHT / 2, 0);

    // Wait user release the button(s)
    PRINTF("LOW: %d    |    HIGH: %d\n", LOW, HIGH);
    PRINTF("BUTTON_A: %d | BUTTON_B: %d | BUTTON_C: %d\n", get_button_state(BUTTON_A), get_button_state(BUTTON_B), get_button_state(BUTTON_C));
    while (get_button_state(BUTTON_A) == LOW || get_button_state(BUTTON_B) == LOW || get_button_state(BUTTON_C) == LOW) {
        PRINTF("BUTTON_A: %d | BUTTON_B: %d | BUTTON_C: %d\n", get_button_state(BUTTON_A), get_button_state(BUTTON_B), get_button_state(BUTTON_C));
        delay(100);
        lv_timer_handler();
    }

    // Start the loop
    bletag_millis = millis();
    button_A_released = button_B_released = button_C_released = true;
    current_pos = next_pos = 0;
    hop = bletag_loop;
}

// Callback that prints the current slider value on the TFT display and Serial Monitor for debugging purposes
static void bletag_slider_cb(lv_event_t *e) {
    STRING position;
    position = TO_STRING(lv_slider_get_value(slider)) + " / " + TO_STRING(lv_slider_get_max_value(slider));
    lv_label_set_text(slider_label, position.c_str());
    LV_LOG_USER("Slider changed to ", position.c_str());
}

// Animation to screen scroll to zero position
static void anim_screen_cb(void * var, int32_t v) {
    lv_obj_scroll_to_x((lv_obj_t *) var, v, LV_ANIM_OFF);
}

static void bletag_loop() {
    // If all buttons are pressed, then reset
    if (get_button_state(BUTTON_A) == LOW && get_button_state(BUTTON_B) == LOW && get_button_state(BUTTON_C) == LOW  && !cleanning) {
        lv_slider_set_value(slider, tags.size(), LV_ANIM_ON);
        antenna_clear();
        reset = true;
    }

    // Runs the code below this line every 100 ms
    if (millis() - bletag_millis > 100 && !cleanning) {
        bletag_millis = millis();

        // Change the slider value if a button down
        // if (get_button_state(BUTTON_A) == LOW) {
        //     lv_slider_set_value(slider, lv_slider_get_value(slider) - 1, LV_ANIM_ON);
        //     bletag_slider_cb(NULL);
        // }
        // if (get_button_state(BUTTON_B) == LOW) {
        //     lv_slider_set_value(slider, lv_slider_get_value(slider) + 1, LV_ANIM_ON);
        //     bletag_slider_cb(NULL);
        // }
    }

    if (reset || cleanning) {
        reset = false;
        if (!cleanning) {
            lv_slider_set_value(slider, 0, LV_ANIM_ON);

            // lv_obj_scroll_to_x(screen, 0, LV_ANIM_ON);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, screen);
            lv_anim_set_values(&a, SCREEN_HEIGHT * next_pos, 0);
            lv_anim_set_duration(&a, 500 * next_pos + 1000);
            lv_anim_set_exec_cb(&a, anim_screen_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);

            cleanning = true;
        }

        // Destroy widgets after a short time
        PRINTF("scroll x: %d\n", lv_obj_get_scroll_x(screen));
        if (lv_obj_get_scroll_x(screen) == 0) {
            PRINTF("Clean the screen...\n");
            // Stop bletag loop
            hop = NULL;
            buttons_operations.clear();
            buttons_tags.clear();
            buttons.clear();
            tags.clear();
            clear_screen();
            cleanning = false;
        }

        return;
    }

    if (get_button_state(BUTTON_A) == LOW && button_A_released || get_button_state(BUTTON_B) == LOW && button_B_released) {
        lv_obj_scroll_to_x(screen, SCREEN_HEIGHT * current_pos, LV_ANIM_OFF);  // to stop the animation if there is one
        if (get_button_state(BUTTON_A) == LOW  && button_A_released) {
            PRINTF("BUTTON_A PRESSED\n");
            button_A_released = false;

            if (!button_B_released) {
                PRINTF("***** B AND A PRESSED\n");
                return;
            }

            if (current_pos > 0) {
                next_pos = current_pos - 1;
                lv_obj_scroll_by(screen, SCREEN_HEIGHT, 0, LV_ANIM_ON);
            }
        }
        else {
            PRINTF("BUTTON_B PRESSED\n");
            button_B_released = false;

            if (!button_A_released) {
                PRINTF("***** A AND B PRESSED\n");
                return;
            }

            if (current_pos < buttons.size() - 1) {
                next_pos = current_pos + 1;
                lv_obj_scroll_by(screen, -SCREEN_HEIGHT, 0, LV_ANIM_ON);
            }
        }
        lv_obj_send_event(buttons[current_pos], LV_EVENT_PRESSED, NULL);
        lv_obj_send_event(buttons[next_pos], LV_EVENT_PRESSED, NULL);
    }
    else if (get_button_state(BUTTON_A) == HIGH && !button_A_released || get_button_state(BUTTON_B) == HIGH && !button_B_released) {
        if (get_button_state(BUTTON_A) == HIGH && !button_A_released) {
            PRINTF("BUTTON_A RELEASED\n");
            button_A_released = true;

            if (!button_B_released) {
                PRINTF("***** FILTER ON\n");
                filter = "^([A-Z]{2}\\.\\d{2}\\.\\d{6}|AtivaLocacao|0123456789AB)$";;
                return;
            }
        }
        else {
            PRINTF("BUTTON_B RELEASED\n");
            button_B_released = true;

            if (!button_A_released) {
                PRINTF("***** REPLICATION ON\n");
                allow_replication = true;
                return;
            }
        }
        lv_obj_send_event(buttons[current_pos], LV_EVENT_RELEASED, NULL);
        lv_obj_send_event(buttons[next_pos], LV_EVENT_RELEASED, NULL);
        current_pos = next_pos;

        lv_slider_set_value(slider, current_pos - operations.size() + 1, LV_ANIM_ON);
        bletag_slider_cb(NULL);
    }

    if (get_button_state(BUTTON_C) == LOW) {
        PRINTF("BUTTON_C PRESSED\n");

        if (!button_A_released || !button_B_released) {
            PRINTF("***** A OR B PRESSED\n");
            button_C_released = false;
            return;
        }

        if (button_C_released) {
            button_C_released = false;
            lv_obj_send_event(button_ok, LV_EVENT_PRESSED, NULL);
            lv_obj_send_event(button_ok, LV_EVENT_CLICKED, NULL);
        }

        if (current_pos == 0) {
            antenna_prepare_read();
            delay(50);
            std::vector<STRING> _tags = antenna_read(allow_replication, filter);
            PRINTF("+TAGs: %d\n", _tags.size());
            if (_tags.size() > 0) {
                next_pos = buttons.size();
                for (const STRING& tag : _tags) {
                    // PRINTF("TAG %d: %s\n", next_pos + 1, tag.c_str());
                    lv_obj_t *button = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
                    lv_obj_align(button, LV_ALIGN_CENTER, SCREEN_HEIGHT * next_pos++, 0);
                    lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
                    lv_obj_add_style(button, &style_button_green, LV_STATE_CHECKED);
                    lv_obj_set_style_text_font(button, &lv_font_montserrat_24, LV_STATE_DEFAULT);
                    lv_label_set_text(lv_label_create(button), tag.c_str());
                    buttons_tags.push_back(button);
                    buttons.push_back(button);
                    tags.push_back(tag);
                }
                lv_obj_align_to(border, buttons.back(), LV_ALIGN_CENTER, SCREEN_HEIGHT / 2, 0);
                
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, screen);
                lv_anim_set_values(&a, SCREEN_HEIGHT * (next_pos - _tags.size()), SCREEN_HEIGHT * next_pos);
                lv_anim_set_duration(&a, 100 * tags.size() + 2000);
                lv_anim_set_exec_cb(&a, anim_screen_cb);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_start(&a);

                lv_slider_set_range(slider, 0, tags.size());
                lv_slider_set_value(slider, tags.size(), LV_ANIM_ON);
                bletag_slider_cb(NULL);

            }
        }
    }
    else if (get_button_state(BUTTON_C) == HIGH && !button_C_released) {
        PRINTF("BUTTON_C RELEASED\n");
        button_C_released = true;

        if (!button_A_released) {
            PRINTF("***** REPLICATION OFF\n");
            allow_replication = false;
            return;
        }
        else if (!button_B_released) {
            PRINTF("***** FILTER OFF\n");
            filter.clear();
            return;
        }

        lv_obj_send_event(button_ok, LV_EVENT_RELEASED, NULL);

        if (current_pos == 0) {  // Coletar
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, screen);
            lv_anim_set_values(&a, SCREEN_HEIGHT * buttons.size(), SCREEN_HEIGHT * current_pos);
            lv_anim_set_duration(&a, 100 * tags.size() + 2000);
            lv_anim_set_exec_cb(&a, anim_screen_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
            next_pos = 1;
            lv_slider_set_value(slider, 0, LV_ANIM_ON);
            bletag_slider_cb(NULL);
        }

        else if (current_pos == 1) {  // Aplicativo
            int i = 0;
            for (const STRING& tag : tags) {
                PRINTF("Sending: %s\n", tag.c_str());

                lv_slider_set_value(slider, ++i, LV_ANIM_OFF);
                bletag_slider_cb(NULL);
                update_interface();

                send_data_via_ble(tag, 15);
                delay(250);
            }
            lv_slider_set_value(slider, 0, LV_ANIM_ON);
            bletag_slider_cb(NULL);
        }

        else if (current_pos == 2) {  // Planilha
            int i = 0;
            for (const STRING& tag : tags) {
                PRINTF("Sending: %s\n", tag.c_str());

                lv_slider_set_value(slider, ++i, LV_ANIM_OFF);
                bletag_slider_cb(NULL);
                update_interface();

                send_data_via_ble(" ", 15, false);
                delay(250);
                send_data_via_ble("\b", 15, false);
                send_data_via_ble(tag, 15);
                delay(250);
            }
            lv_slider_set_value(slider, 0, LV_ANIM_ON);
            bletag_slider_cb(NULL);
        }

        else if (current_pos == 3) {  // Limpar
            for (lv_obj_t*const &button : buttons_tags) {
                lv_obj_del(button);
            }
            buttons.resize(buttons_operations.size());
            lv_obj_align_to(border, buttons.back(), LV_ALIGN_CENTER, SCREEN_HEIGHT / 2, 0);
            buttons_tags.clear();
            tags.clear();
            lv_slider_set_range(slider, 0, 0);
            bletag_slider_cb(NULL);
        }

        else if (current_pos == 4) {  // Reiniciar
            lv_slider_set_value(slider, tags.size(), LV_ANIM_ON);
            antenna_clear();
            reset = true;
        }

        else {
            if (current_pos < buttons.size() - 1) {
                next_pos = current_pos + 1;
                lv_obj_scroll_by(screen, -SCREEN_HEIGHT, 0, LV_ANIM_ON);
            }

            lv_obj_send_event(buttons[current_pos], LV_EVENT_PRESSED, NULL);
            lv_obj_send_event(buttons[next_pos], LV_EVENT_PRESSED, NULL);

            send_data_via_ble(tags[current_pos - operations.size()]);

            lv_obj_send_event(buttons[current_pos], LV_EVENT_RELEASED, NULL);
            lv_obj_send_event(buttons[next_pos], LV_EVENT_RELEASED, NULL);

            current_pos = next_pos;

            lv_slider_set_value(slider, current_pos - operations.size() + 1, LV_ANIM_ON);
            bletag_slider_cb(NULL);

        }

        // next_pos = current_pos + 1;
        // hop = tela2_init;
    }
}

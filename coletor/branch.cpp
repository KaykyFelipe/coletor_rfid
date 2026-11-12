#include "main.h"
#include "branch.h"

static uint16_t current_unit = 0;
static uint16_t next_unit = 1;
static std::vector<STRING> units = {"Matriz", "Cuiabá", "Uberlândia", "Limeira", "Campo Grande", "Aracatuba", "Londrina", "Ribeirão Preto", "Eficaz"};
static std::vector<lv_obj_t*> btns_units;
static lv_obj_t *btn_ok, *slider, *slider_label;
static bool button_A_released = true;
static bool button_B_released = true;
static bool button_C_released = true;
static uint64_t branch_millis = 0;
static bool cleanning = false;
static lv_style_t style_btn_green;

// void (*initial_hop)() = branch_init;

static void branch_loop();

void branch_init() {
    lv_obj_t *btn;

    PRINTF("Creating...\n");

    // Clean the screen
    clear_screen();

    // Style green for pressed or checked buttons states
    lv_style_init(&style_btn_green);
    lv_style_set_bg_color(&style_btn_green, lv_palette_main(LV_PALETTE_GREEN));
    lv_obj_add_style(screen, &style_btn_green, LV_PART_SCROLLBAR);

    // Style padding 5 for buttons
    static lv_style_t style_btn_padding_5;
    lv_style_init(&style_btn_padding_5);
    lv_style_set_pad_all(&style_btn_padding_5, 5);

    // Create a button with text label aligned center on top
    btn = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_style(btn, &style_btn_padding_5, LV_STATE_DEFAULT);
    lv_label_set_text(lv_label_create(btn), "     Ativa Locação     ");
    lv_obj_set_flag(btn, LV_OBJ_FLAG_FLOATING, true);

    // Create a Button (btn_ok)
    btn_ok = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
    // lv_obj_add_event_cb(btn_ok, event_handler_btn_ok, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_remove_flag(btn_ok, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_style(btn_ok, &style_btn_green, LV_STATE_PRESSED);
    lv_obj_add_style(btn_ok, &style_btn_padding_5, LV_STATE_DEFAULT);
    lv_label_set_text(lv_label_create(btn_ok), "Ok");
    lv_obj_set_flag(btn_ok, LV_OBJ_FLAG_FLOATING, true);

    // Create a Toggle button (btn_unit)
    current_unit = 0;
    next_unit = 0;
    for (const STRING& unit : units) {
        lv_obj_t *btn = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
        lv_obj_align(btn, LV_ALIGN_CENTER, SCREEN_HEIGHT * next_unit, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_style(btn, &style_btn_green, LV_STATE_CHECKED);
        lv_label_set_text(lv_label_create(btn), unit.c_str());

        if (current_unit == next_unit++)
            lv_obj_set_state(btn, LV_STATE_CHECKED, true);

        btns_units.push_back(btn);
    }
    next_unit = 1;

    // Create a slider aligned in the center bottom of the TFT display
    slider = LV_OBJ_CREATE_NO_CLICK(lv_slider_create, screen);
    lv_obj_set_width(slider, 200);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 40);
    // lv_obj_add_event_cb(slider, slider_event_callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_style_anim_duration(slider, 1000, 0);
    lv_obj_set_flag(slider, LV_OBJ_FLAG_FLOATING, true);

    // Create a label below the slider to display the current slider value
    slider_label = lv_label_create(screen);
    lv_label_set_text(slider_label, "0%");
    lv_obj_align_to(slider_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, -15);
    lv_obj_set_flag(slider_label, LV_OBJ_FLAG_FLOATING, true);

    // Wait user release the button(s)
    PRINTF("LOW: %d    |    HIGH: %d\n", LOW, HIGH);
    PRINTF("BUTTON_A: %d | BUTTON_B: %d | BUTTON_C: %d\n", get_button_state(BUTTON_A), get_button_state(BUTTON_B), get_button_state(BUTTON_C));
    while (get_button_state(BUTTON_A) == LOW || get_button_state(BUTTON_B) == LOW || get_button_state(BUTTON_C) == LOW) {
        PRINTF("BUTTON_A: %d | BUTTON_B: %d | BUTTON_C: %d\n", get_button_state(BUTTON_A), get_button_state(BUTTON_B), get_button_state(BUTTON_C));
        delay(100);
        lv_timer_handler();
    }

    // Start the loop
    branch_millis = millis();
    button_A_released = button_B_released = button_C_released = true;
    current_unit = next_unit = 0;
    hop = branch_loop;
}

// Callback that prints the current slider value on the TFT display and Serial Monitor for debugging purposes
static void branch_slider_cb(lv_event_t *e) {
    STRING percent;
    // lv_obj_t * slider = (lv_obj_t*) lv_event_get_target(e);
    percent = TO_STRING(10000 * lv_slider_get_value(slider) / lv_slider_get_max_value(slider) / 100) + "%";
    lv_label_set_text(slider_label, percent.c_str());
    LV_LOG_USER("Slider changed to %s%", percent.c_str());
}

// Animation to screen scroll to zero position
static void anim_screen_cb(void * var, int32_t v) {
    lv_obj_scroll_to_x((lv_obj_t *) var, v, LV_ANIM_OFF);
}

static void branch_loop() {
    // If all buttons are pressed, then reset
    if (get_button_state(BUTTON_A) == LOW && get_button_state(BUTTON_B) == LOW && get_button_state(BUTTON_C) == LOW  && !cleanning)
        lv_slider_set_value(slider, lv_slider_get_max_value(slider), LV_ANIM_ON);

    // Runs the code below this line every 100 ms
    if (millis() - branch_millis > 100 && !cleanning) {
        branch_millis = millis();

        // Change the slider value if a button down
        if (get_button_state(BUTTON_A) == LOW) {
            lv_slider_set_value(slider, lv_slider_get_value(slider) - 1, LV_ANIM_ON);
            branch_slider_cb(NULL);
        }
        if (get_button_state(BUTTON_B) == LOW) {
            lv_slider_set_value(slider, lv_slider_get_value(slider) + 1, LV_ANIM_ON);
            branch_slider_cb(NULL);
        }
    }

    if (lv_slider_get_value(slider) == lv_slider_get_max_value(slider) || cleanning) {
        if (!cleanning) {
            lv_slider_set_value(slider, 0, LV_ANIM_ON);

            // lv_obj_scroll_to_x(screen, 0, LV_ANIM_ON);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, screen);
            lv_anim_set_values(&a, SCREEN_HEIGHT * (next_unit + 1), 0);
            lv_anim_set_duration(&a, 500 * (next_unit + 2));
            lv_anim_set_exec_cb(&a, anim_screen_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);

            cleanning = true;
        }

        // Destroy widgets after a short time
        PRINTF("scroll x: %d\n", lv_obj_get_scroll_x(screen));
        if (lv_obj_get_scroll_x(screen) == 0) {
            PRINTF("Clean the screen...\n");
            // Stop branch loop
            hop = NULL;
            clear_screen();
            cleanning = false;
        }

        return;
    }

    if (get_button_state(BUTTON_C) == LOW) {
        PRINTF("BUTTON_C PRESSED\n");
        if (button_C_released) {
            button_C_released = false;
            lv_obj_send_event(btn_ok, LV_EVENT_PRESSED, NULL);
            lv_obj_send_event(btn_ok, LV_EVENT_CLICKED, NULL);
            for (lv_obj_t* &btn : btns_units) {
                lv_obj_del(btn);
            }
            btns_units.clear();
            current_unit = 0;
            next_unit = 0;
        }
        antenna_prepare_read();
        delay(50);
        std::vector<STRING> tags = antenna_read();
        PRINTF("TAGs: %d\n", tags.size());
        if (tags.size() > 0) {
            for (const STRING& tag : tags) {
                PRINTF("TAG %d: %s\n", next_unit + 1, tag.c_str());
                lv_obj_t *btn = LV_OBJ_CREATE_NO_CLICK(lv_button_create, screen);
                lv_obj_align(btn, LV_ALIGN_CENTER, SCREEN_HEIGHT * next_unit, 0);
                lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
                lv_obj_add_style(btn, &style_btn_green, LV_STATE_CHECKED);
                lv_label_set_text(lv_label_create(btn), tag.c_str());

                if (current_unit == next_unit++)
                    lv_obj_set_state(btn, LV_STATE_CHECKED, true);

                btns_units.push_back(btn);
            }

            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, screen);
            lv_anim_set_values(&a, SCREEN_HEIGHT * (next_unit - tags.size()), SCREEN_HEIGHT * next_unit);
            lv_anim_set_duration(&a, 500 * tags.size());
            lv_anim_set_exec_cb(&a, anim_screen_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
        }
    }
    else if (get_button_state(BUTTON_C) == HIGH && !button_C_released) {
        PRINTF("BUTTON_C RELEASED\n");
        button_C_released = true;
        lv_obj_send_event(btn_ok, LV_EVENT_RELEASED, NULL);
        next_unit = current_unit + 1;
        // hop = tela2_init;
    }

    if (get_button_state(BUTTON_A) == LOW && button_A_released || get_button_state(BUTTON_B) == LOW && button_B_released) {
        lv_obj_scroll_to_x(screen, SCREEN_HEIGHT * current_unit, LV_ANIM_OFF);  // to stop the animation if there is one
        if (get_button_state(BUTTON_A) == LOW) {
            PRINTF("BUTTON_A PRESSED\n");
            button_A_released = false;
            if (current_unit > 0) {
                next_unit = current_unit - 1;
                lv_obj_scroll_by(screen, SCREEN_HEIGHT, 0, LV_ANIM_ON);
            }
        }
        else {
            PRINTF("BUTTON_B PRESSED\n");
            button_B_released = false;
            if (current_unit < btns_units.size() - 1) {
                next_unit = current_unit + 1;
                lv_obj_scroll_by(screen, -SCREEN_HEIGHT, 0, LV_ANIM_ON);
            }
        }
        lv_obj_send_event(btns_units[current_unit], LV_EVENT_PRESSED, NULL);
        lv_obj_send_event(btns_units[next_unit], LV_EVENT_PRESSED, NULL);
    }
    else if (get_button_state(BUTTON_A) == HIGH && !button_A_released || get_button_state(BUTTON_B) == HIGH && !button_B_released) {
        if (get_button_state(BUTTON_A) == HIGH && !button_A_released) {
            PRINTF("BUTTON_A RELEASED\n");
            button_A_released = true;
        }
        else {
            PRINTF("BUTTON_B RELEASED\n");
            button_B_released = true;
        }
        lv_obj_send_event(btns_units[current_unit], LV_EVENT_RELEASED, NULL);
        lv_obj_send_event(btns_units[next_unit], LV_EVENT_RELEASED, NULL);
        current_unit = next_unit;
    }
}

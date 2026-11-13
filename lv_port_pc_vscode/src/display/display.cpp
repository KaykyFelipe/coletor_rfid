#include "display.h"
#include "../config.h"

#include <cstring>

// Tela deitada: 240 x 135 (SCREEN_HEIGHT x SCREEN_WIDTH apos a rotacao).
#define VIEW_W SCREEN_HEIGHT
#define VIEW_H SCREEN_WIDTH
#define TOPBAR_H 22

#define COLOR_BG      lv_color_hex(0x0B1114)
#define COLOR_LINE    lv_color_hex(0x22303A)
#define COLOR_TEXT    lv_color_hex(0xFFFFFF)
#define COLOR_MUTED   lv_color_hex(0x8A979F)
#define COLOR_GREEN   lv_color_hex(0x2E9E5B)
#define COLOR_AMBER   lv_color_hex(0xE0A010)

static lv_obj_t *status_dot, *status_label, *battery_percent_label, *battery_symbol_label;
static lv_obj_t *content, *title_label, *subtitle_label;
static display_screen_t current = DISPLAY_READY;

static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_text(label, "");
  return label;
}

// Maior fonte em que o nome do equipamento cabe em uma linha.
static const lv_font_t *font_for_name(const char *text) {
  const lv_font_t *fonts[] = {&lv_font_montserrat_32, &lv_font_montserrat_28, &lv_font_montserrat_24, &lv_font_montserrat_20, &lv_font_montserrat_16};
  for (const lv_font_t *font : fonts) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= VIEW_W - 12) return font;
  }
  return &lv_font_montserrat_16;
}

void display_init() {
  lv_obj_clean(screen);

  // Fundo escuro, sem borda, sem rolagem
  lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_radius(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  // Barra superior: indicador de conexao
  status_dot = lv_obj_create(screen);
  lv_obj_remove_style_all(status_dot);
  lv_obj_set_size(status_dot, 8, 8);
  lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
  lv_obj_set_pos(status_dot, 7, (TOPBAR_H - 8) / 2);

  status_label = create_label(screen, &lv_font_montserrat_12, COLOR_TEXT);
  lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 20, (TOPBAR_H - 14) / 2);

  // Barra superior: bateria (icone a direita, percentual a esquerda dele)
  battery_symbol_label = create_label(screen, &lv_font_montserrat_14, COLOR_TEXT);
  lv_label_set_text(battery_symbol_label, "-");
  lv_obj_align(battery_symbol_label, LV_ALIGN_TOP_RIGHT, -6, (TOPBAR_H - 16) / 2);

  battery_percent_label = create_label(screen, &lv_font_montserrat_12, COLOR_TEXT);
  lv_obj_align(battery_percent_label, LV_ALIGN_TOP_RIGHT, -30, (TOPBAR_H - 14) / 2);

  // Linha divisoria
  lv_obj_t *line = lv_obj_create(screen);
  lv_obj_remove_style_all(line);
  lv_obj_set_size(line, VIEW_W, 1);
  lv_obj_set_pos(line, 0, TOPBAR_H);
  lv_obj_set_style_bg_color(line, COLOR_LINE, 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

  // Area central: titulo e subtitulo empilhados e centralizados
  content = lv_obj_create(screen);
  lv_obj_remove_style_all(content);
  lv_obj_set_size(content, VIEW_W, VIEW_H - TOPBAR_H - 1);
  lv_obj_set_pos(content, 0, TOPBAR_H + 1);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(content, 6, 0);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  title_label = create_label(content, &lv_font_montserrat_24, COLOR_TEXT);
  lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);

  subtitle_label = create_label(content, &lv_font_montserrat_12, COLOR_MUTED);
  lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_CENTER, 0);

  display_set_connected(false);
  display_show(DISPLAY_READY);
}

void display_set_connected(bool connected) {
  lv_obj_set_style_bg_color(status_dot, connected ? COLOR_GREEN : COLOR_AMBER, 0);
  lv_obj_set_style_text_color(status_label, connected ? COLOR_TEXT : COLOR_AMBER, 0);
  lv_label_set_text(status_label, connected ? "Celular ligado" : "Sem celular");
}

void display_set_battery(float percent, const char *symbol) {
  lv_label_set_text_fmt(battery_percent_label, "%d%%", (int)(percent + .5));
  lv_label_set_text(battery_symbol_label, symbol);
}

void display_show(display_screen_t which, const char *text, int count) {
  current = which;

  const lv_font_t *title_font = &lv_font_montserrat_24;
  lv_color_t subtitle_color = COLOR_MUTED;
  const lv_font_t *subtitle_font = &lv_font_montserrat_12;
  bool has_subtitle = false;

  switch (which) {
    case DISPLAY_READY:
      lv_label_set_text(title_label, "Segure o botão\npara ler");
      if (text) {
        // Aviso das leituras guardadas
        lv_label_set_text(subtitle_label, text);
        subtitle_color = COLOR_AMBER;
        has_subtitle = true;
      }
      break;

    case DISPLAY_READING:
      title_font = &lv_font_montserrat_32;
      lv_label_set_text(title_label, "Lendo...");
      break;

    case DISPLAY_READ_OK:
      title_font = font_for_name(text ? text : "");
      lv_label_set_text(title_label, text ? text : "");
      if (count > 0) lv_label_set_text_fmt(subtitle_label, LV_SYMBOL_OK " Leitura feita  #%d", count);
      else lv_label_set_text(subtitle_label, LV_SYMBOL_OK " Leitura feita");
      subtitle_color = COLOR_GREEN;
      subtitle_font = &lv_font_montserrat_16;
      has_subtitle = true;
      break;

    case DISPLAY_NO_TAG:
      lv_label_set_text(title_label, "Nenhuma tag\nencontrada");
      break;

    case DISPLAY_OFFLINE:
      title_font = &lv_font_montserrat_32;
      lv_label_set_text_fmt(title_label, count == 1 ? "%d guardada" : "%d guardadas", count);
      lv_label_set_text(subtitle_label, "Pode continuar lendo.\nEnvia quando o celular voltar.");
      has_subtitle = true;
      break;

    case DISPLAY_SENDING:
      title_font = &lv_font_montserrat_32;
      if (count > 0) lv_label_set_text_fmt(title_label, "Enviando %d...", count);
      else lv_label_set_text(title_label, "Enviando...");
      break;

    case DISPLAY_MESSAGE:
      title_font = font_for_name(text ? text : "");
      lv_label_set_text(title_label, text ? text : "");
      break;
  }

  lv_obj_set_style_text_font(title_label, title_font, 0);
  lv_obj_set_style_text_font(subtitle_label, subtitle_font, 0);
  lv_obj_set_style_text_color(subtitle_label, subtitle_color, 0);
  lv_obj_set_flag(subtitle_label, LV_OBJ_FLAG_HIDDEN, !has_subtitle);
}

display_screen_t display_current() {
  return current;
}

void display_set_backlight(bool on) {
  #ifdef ESP32
    pinMode(DISPLAY_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(DISPLAY_BACKLIGHT_PIN, on ? HIGH : LOW);
  #else // HAL
    // Simulador de PC: um retangulo preto por cima faz o papel da tela apagada.
    static lv_obj_t *blackout = NULL;
    if (blackout == NULL) {
      blackout = lv_obj_create(lv_obj_get_parent(content));
      lv_obj_remove_style_all(blackout);
      lv_obj_add_flag(blackout, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_size(blackout, VIEW_W, VIEW_H);
      lv_obj_set_pos(blackout, 0, 0);
      lv_obj_set_style_bg_color(blackout, lv_color_hex(0x000000), 0);
      lv_obj_set_style_bg_opa(blackout, LV_OPA_COVER, 0);
    }
    lv_obj_set_flag(blackout, LV_OBJ_FLAG_HIDDEN, on);
  #endif // ESP32 / HAL
}

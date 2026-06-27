#ifndef OLED_MENU_H
#define OLED_MENU_H

#include <stdint.h>

typedef struct menu_item {
  const char *text;
  void (*action_cb)(void);
  const struct menu_list *child_menu;
} menu_item_t;

typedef struct menu_list {
  const char *title;
  const menu_item_t *items;
  uint8_t item_count;
  const struct menu_list *parent_menu;
} menu_list_t;

// Standard Navigation APIs
void menu_init(const menu_list_t *root);
void menu_next(void);
void menu_prev(void);
void menu_select(void);
void menu_back(void);

// Smooth Animation Render Loop Update
void menu_render_smooth(void);

#endif

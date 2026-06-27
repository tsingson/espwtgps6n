#include "oled_menu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled_ssd1306.h"
#include <math.h>
#include <stdio.h>

static const menu_list_t *current_menu = NULL;
static uint8_t selected_index = 0;

// 🌟 Smooth Viewport Animation Control Parameters
static float current_camera_y =
    0.0f; // Actual smooth animated scrolling position
static float target_camera_y = 0.0f; // Calculated destination scroll target
#define ROW_HEIGHT 10 // Row vertical spacing (8px char + 2px padding gap)
#define VIEWPORT_HEIGHT                                                        \
  48 // Height allocated for list contents (64 total - 16 title/footer padding)
#define ANIMS_SPEED 0.25f // LERP Step weight (0.1 = slow, 0.4 = snapping fast)

void menu_init(const menu_list_t *root) {
  current_menu = root;
  selected_index = 0;
  current_camera_y = 0.0f;
  target_camera_y = 0.0f;
}

void menu_next(void) {
  if (current_menu && selected_index < current_menu->item_count - 1) {
    selected_index++;
  }
}

void menu_prev(void) {
  if (selected_index > 0) {
    selected_index--;
  }
}

void menu_select(void) {
  if (!current_menu)
    return;
  const menu_item_t *active_item = &current_menu->items[selected_index];
  if (active_item->child_menu) {
    current_menu = active_item->child_menu;
    selected_index = 0;
    current_camera_y = 0.0f;
    target_camera_y = 0.0f;
  } else if (active_item->action_cb) {
    active_item->action_cb();
  }
}

void menu_back(void) {
  if (current_menu && current_menu->parent_menu) {
    current_menu = current_menu->parent_menu;
    selected_index = 0;
    current_camera_y = 0.0f;
    target_camera_y = 0.0f;
  }
}

// 🌟 Viewport calculation engine with built-in LERP loop
void menu_render_smooth(void) {
  if (!current_menu)
    return;

  // A. Dynamic Window Management: Calculate upper/lower physical viewport
  // boundaries
  int selected_row_top = selected_index * ROW_HEIGHT;
  int selected_row_bottom = selected_row_top + ROW_HEIGHT;

  // Check if the current item is moving out of the bottom window view threshold
  if (selected_row_bottom - target_camera_y > VIEWPORT_HEIGHT) {
    target_camera_y = selected_row_bottom - VIEWPORT_HEIGHT;
  }
  // Check if the current item is moving past the top window view threshold
  if (selected_row_top < target_camera_y) {
    target_camera_y = selected_row_top;
  }

  // B. Main Animation Frame Update Loop
  // Continues calculating intermediate positions until current_camera close
  // enough to target
  while (fabs(target_camera_y - current_camera_y) > 0.1f) {
    // Linear Interpolation: current = current + (target - current) * factor
    current_camera_y += (target_camera_y - current_camera_y) * ANIMS_SPEED;

    oled_clear();

    // 1. Draw static floating Title Window Banner (Header)
    oled_fill_rectangle(0, 0, 128, 10);
    oled_show_string_ex(2, 1, current_menu->title,
                        1); // Reverse background title

    // 2. Render scrolling list items relative to the animated camera frame
    for (uint8_t i = 0; i < current_menu->item_count; i++) {
      // Compute current absolute dynamic rendering offset on screen canvas
      int virtual_y_pos = 14 + (i * ROW_HEIGHT) - (int)current_camera_y;

      // Viewport clipping bounds checks: Skip calculation if text falls
      // completely outside visible frame
      if (virtual_y_pos < 12 || virtual_y_pos > 56) {
        continue;
      }

      if (i == selected_index) {
        // Render cursor active row selection background blocks
        oled_fill_rectangle(0, virtual_y_pos - 1, 128, ROW_HEIGHT);
        oled_show_string_ex(8, virtual_y_pos, current_menu->items[i].text,
                            1); // Inverse dark font
        oled_show_string_ex(1, virtual_y_pos, ">", 1);
      } else {
        // Render standard unselected row text stream blocks
        oled_show_string_ex(8, virtual_y_pos, current_menu->items[i].text,
                            0); // Passive font
      }
    }

    // 3. Optional: Subtle visual element scrollbar on the right screen border
    // edge
    if (current_menu->item_count > 5) {
      int total_content_h = current_menu->item_count * ROW_HEIGHT;
      int bar_h = (VIEWPORT_HEIGHT * VIEWPORT_HEIGHT) / total_content_h;
      int bar_y = 14 + ((int)current_camera_y * (VIEWPORT_HEIGHT - bar_h)) /
                           (total_content_h - VIEWPORT_HEIGHT);
      oled_fill_rectangle(126, bar_y, 2, bar_h);
    }

    oled_refresh();
    vTaskDelay(pdMS_TO_TICKS(30)); // Lock frame update rate cycle to ~33 FPS
  }
}

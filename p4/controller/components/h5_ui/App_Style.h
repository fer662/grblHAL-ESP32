#ifndef APP_STYLE_H
#define APP_STYLE_H

#include <lvgl.h>

// ============================================================================
// SCREEN DIMENSIONS
// ============================================================================

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 800

// ============================================================================
// GLOBAL COLOR PALETTE
// ============================================================================

// Primary Colors
#define APP_COLOR_PRIMARY lv_color_hex(0x007ACC)
#define APP_COLOR_PRIMARY_DARK lv_color_hex(0x005A9E)
#define APP_COLOR_PRIMARY_LIGHT lv_color_hex(0x4FC3F7)

// Secondary Colors
#define APP_COLOR_SECONDARY lv_color_hex(0x666666)
#define APP_COLOR_SECONDARY_DARK lv_color_hex(0x424242)
#define APP_COLOR_SECONDARY_LIGHT lv_color_hex(0x888888)

// Background Colors
#define APP_COLOR_BG_PRIMARY lv_color_hex(0x1E1E1E)
#define APP_COLOR_BG_SECONDARY lv_color_hex(0x2C2C2C)
#define APP_COLOR_BG_TERTIARY lv_color_hex(0x4A4A4A)

// Text Colors
#define APP_COLOR_TEXT_PRIMARY lv_color_hex(0xFFFFFF)
#define APP_COLOR_TEXT_SECONDARY lv_color_hex(0xCCCCCC)
#define APP_COLOR_TEXT_DISABLED lv_color_hex(0x888888)

// Status Colors
#define APP_COLOR_SUCCESS lv_color_hex(0x4CAF50)
#define APP_COLOR_WARNING lv_color_hex(0xFF9800)
#define APP_COLOR_ERROR lv_color_hex(0xF44336)
#define APP_COLOR_INFO lv_color_hex(0x2196F3)

#define APP_COLOR_MM APP_COLOR_INFO
#define APP_COLOR_IMPERIAL APP_COLOR_WARNING

#define COLOR_PRESSED(color) lv_color_darken(color, 20)

// Action Colors
#define APP_COLOR_ACTION lv_color_hex(0x007ACC)
#define APP_COLOR_ACTION_PRESSED COLOR_PRESSED(APP_COLOR_ACTION)
#define APP_COLOR_CANCEL lv_color_hex(0xB00020)
#define APP_COLOR_CANCEL_PRESSED COLOR_PRESSED(APP_COLOR_CANCEL)

// Overlay Colors
#define APP_COLOR_OVERLAY lv_color_hex(0x000000)
#define APP_COLOR_OVERLAY_OPACITY LV_OPA_80

// Border Colors
#define APP_COLOR_BORDER lv_color_hex(0x888888)

// ============================================================================
// COMMON STYLE CONSTANTS
// ============================================================================

// Border Radius
#define APP_RADIUS_SMALL 5
#define APP_RADIUS_MEDIUM 8
#define APP_RADIUS_LARGE 10

// Padding
#define APP_PADDING_SMALL 5
#define APP_PADDING_MEDIUM 10
#define APP_PADDING_LARGE 20

// Margins
#define APP_MARGIN_SMALL 5
#define APP_MARGIN_MEDIUM 10
#define APP_MARGIN_LARGE 20

// ============================================================================
// COMPONENT-SPECIFIC STYLES
// ============================================================================

// Button Styles
#define APP_BTN_STYLE_NORMAL APP_COLOR_BG_TERTIARY
#define APP_BTN_STYLE_PRESSED APP_COLOR_SECONDARY
#define APP_BTN_STYLE_SELECTED APP_COLOR_PRIMARY
#define APP_BTN_STYLE_SELECTED_PRESSED APP_COLOR_PRIMARY_DARK
#define APP_BTN_STYLE_CANCEL APP_COLOR_CANCEL
#define APP_BTN_STYLE_CANCEL_PRESSED APP_COLOR_CANCEL_PRESSED

// Container Styles
#define APP_CONTAINER_BG APP_COLOR_BG_SECONDARY
#define APP_CONTAINER_BORDER APP_COLOR_BORDER

// Text Styles
#define APP_TEXT_COLOR APP_COLOR_TEXT_PRIMARY
#define APP_TEXT_COLOR_SECONDARY APP_COLOR_TEXT_SECONDARY

#endif // APP_STYLE_H

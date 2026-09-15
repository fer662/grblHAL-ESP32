#include "display.h"
#include "App_Style.h"
void Display::begin() {
    lv_disp_t *d = lv_disp_get_default();
    if (d) lv_disp_set_theme(d, lv_theme_default_init(d, APP_COLOR_PRIMARY, APP_COLOR_ERROR, true, LV_FONT_DEFAULT));
}

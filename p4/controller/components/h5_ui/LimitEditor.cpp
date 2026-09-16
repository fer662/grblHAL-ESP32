#include "LimitEditor.h"
#include "main.h"
#include "App_Style.h"
#include "LVCallbackWrapper.h"

namespace {
const char *names[] = {"X-", "X+", "Z-", "Z+"};
Axis *axisFor(unsigned i) { return i < 2 ? &x : &z; }
long unset(unsigned i) { return i % 2 ? LONG_MAX : LONG_MIN; }
lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int width) {
  auto label = lv_label_create(parent);
  lv_label_set_text(label, value);
  lv_obj_set_width(label, width);
  lv_obj_set_pos(label, x, y);
  return label;
}
lv_obj_t *button(lv_obj_t *parent, const char *value, int x, int y, int w, int h) {
  auto b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_pad_all(b, 6, 0);
  auto label = lv_label_create(b);
  lv_label_set_text(label, value);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  return b;
}
}

LimitEditor::LimitEditor(lv_obj_t *parent) {
  panel = lv_obj_create(parent);
  lv_obj_set_size(panel, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_bg_color(panel, APP_COLOR_BG_PRIMARY, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
  auto title = text(panel, "Machining limits", 40, 32, 1100);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  units = text(panel, "", 40, 96, 1200);
  lv_obj_set_style_text_font(units, &lv_font_montserrat_18, 0);
  for (unsigned i = 0; i < 4; ++i) {
    auto row = lv_obj_create(panel);
    lv_obj_set_pos(row, 40, 160 + i * 108);
    lv_obj_set_size(row, 852, 88);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    text(row, names[i], 0, 28, 64);
    auto entry = button(row, "", 88, 0, 264, 88);
    values[i] = lv_obj_get_child(entry, 0);
    lv_obj_set_style_text_font(values[i], &lv_font_montserrat_18, 0);
    LVCallbackWrapper::add(entry, LV_EVENT_CLICKED, [this, i](lv_event_t *) { enterValue(i); });
    auto current = button(row, "Use current", 376, 0, 280, 88);
    LVCallbackWrapper::add(current, LV_EVENT_CLICKED, [this, i](lv_event_t *) { useCurrent(i); });
    auto clear = button(row, "Clear", 680, 0, 160, 88);
    lv_obj_set_style_bg_color(clear, APP_COLOR_BG_TERTIARY, 0);
    LVCallbackWrapper::add(clear, LV_EVENT_CLICKED, [this, i](lv_event_t *) {
      draft[i] = unset(i); refresh();
    });
  }
  spans[0] = text(panel, "", 944, 192, 288);
  spans[1] = text(panel, "", 944, 408, 288);
  message = text(panel, "", 40, 608, 1200);
  lv_obj_set_style_text_font(message, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(message, APP_COLOR_WARNING, 0);
  auto cancel = button(panel, "Cancel", 40, 696, 280, 72);
  lv_obj_set_style_bg_color(cancel, APP_COLOR_BG_TERTIARY, 0);
  LVCallbackWrapper::add(cancel, LV_EVENT_CLICKED, [this](lv_event_t *) { lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); });
  auto save = button(panel, "Apply", 960, 696, 280, 72);
  lv_obj_set_style_bg_color(save, APP_COLOR_SUCCESS, 0);
  LVCallbackWrapper::add(save, LV_EVENT_CLICKED, [this](lv_event_t *) { apply(); });
  keypad = std::make_unique<Numpad>(panel, [this](float value, Numpad::Action action) {
    unsigned i = (unsigned)action - Numpad::LIMIT_X_MIN;
    if (i >= draft.size()) return;
    long converted;
    if (!coordinatesUnchanged() || !h5_ui_limit_steps(axisFor(i), value, &converted)) {
      lv_label_set_text(message, "Coordinate out of range or work zero/units changed. Cancel and reopen the editor.");
      return;
    }
    draft[i] = converted;
    refresh();
  });
}
LimitEditor::~LimitEditor() { keypad.reset(); lv_obj_del(panel); }
void LimitEditor::show() {
  draft = {x.rightStop, x.leftStop, z.rightStop, z.leftStop};
  h5_ui_limits_editable(); // Refresh the authoritative core coordinate snapshot.
  editMeasure = measure;
  editOffset = {h5_ui_work_offset(&x), h5_ui_work_offset(&z)};
  editSystem = h5_ui_work_system();
  keypad->hide();
  refresh();
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(panel);
}
void LimitEditor::refresh() {
  const char *unit = editMeasure == MEASURE_METRIC ? "mm" : "in";
  String caption = String("Coordinates: ") + editSystem + " work zero | X: slide travel | Units: " + unit;
  lv_label_set_text(units, caption.c_str());
  for (unsigned i = 0; i < draft.size(); ++i) {
    String value = "Enter value\n";
    value += draft[i] == unset(i) ? "Unset" : format_decimal(h5_ui_limit_coordinate(axisFor(i), draft[i]), 3) + " " + unit;
    lv_label_set_text(values[i], value.c_str());
  }
  for (unsigned i = 0; i < 2; ++i) {
    unsigned low = i * 2, high = low + 1;
    String value = String(i ? "Z span\n" : "X span\n");
    if (draft[low] == unset(low) || draft[high] == unset(high)) value += "Set both limits";
    else if (draft[low] >= draft[high]) value += "Invalid order";
    else value += format_decimal(h5_ui_limit_coordinate(axisFor(low), draft[high]) -
                                 h5_ui_limit_coordinate(axisFor(low), draft[low]), 3) + " " + unit;
    lv_label_set_text(spans[i], value.c_str());
  }
  lv_label_set_text(message, "Edits are saved together when you tap Apply. Cancel discards edits.");
}
void LimitEditor::enterValue(unsigned i) {
  String prompt = String(names[i]) + " ENDPOINT (" + (editMeasure == MEASURE_METRIC ? "mm" : "in") + ", " + editSystem + " WORK ZERO)";
  keypad->show(static_cast<Numpad::Action>(Numpad::LIMIT_X_MIN + i), prompt.c_str());
}
void LimitEditor::useCurrent(unsigned i) {
  if (!h5_ui_limits_editable()) {
    lv_label_set_text(message, "Stop motion and assisted operations before capturing a position.");
    return;
  }
  if (!coordinatesUnchanged()) {
    lv_label_set_text(message, "Work zero or units changed. Cancel and reopen the editor."); return;
  }
  draft[i] = axisFor(i)->pos;
  refresh();
}
bool LimitEditor::coordinatesUnchanged() {
  h5_ui_limits_editable();
  return measure == editMeasure && editSystem == h5_ui_work_system() &&
         editOffset[0] == h5_ui_work_offset(&x) && editOffset[1] == h5_ui_work_offset(&z);
}
void LimitEditor::apply() {
  if (!coordinatesUnchanged()) {
    lv_label_set_text(message, "Work zero or units changed. Cancel and reopen the editor."); return;
  }
  if (const char *error = h5_ui_apply_limits(draft.data())) { lv_label_set_text(message, error); return; }
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

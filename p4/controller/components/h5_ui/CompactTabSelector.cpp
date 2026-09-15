#include "CompactTabSelector.h"
#include "App_Style.h"
#include "Buzzer.h"
#include "LVCallbackWrapper.h"
#include "ui_support.h"

CompactTabSelector::CompactTabSelector(lv_obj_t *parent, lv_coord_t width,
                                       lv_coord_t height)
    : selectedTabId(-1), isExpanded(false) {
  // Create main container
  container = lv_obj_create(parent);
  lv_obj_set_size(container, width, height);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_STATE_FOCUSED);
  LVCallbackWrapper::add(container, LV_EVENT_DELETE,
                         [this](lv_event_t *e) { this->container = nullptr; });

  // Create navigation bar (full width, 40px height)
  navigationBar = lv_obj_create(container);
  lv_obj_set_size(navigationBar, SCREEN_WIDTH, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_left(navigationBar, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_right(navigationBar, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(navigationBar, 5, LV_PART_MAIN);
  lv_obj_set_style_radius(navigationBar, 0, LV_PART_MAIN);

  lv_obj_align(navigationBar, LV_ALIGN_TOP_MID, 0, 0);
  // lv_obj_set_style_bg_color(navigationBar, lv_color_hex(0xFF0000), 0);
  lv_obj_set_style_border_width(navigationBar, 0, 0);
  lv_obj_set_flex_flow(navigationBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(navigationBar, 10, LV_PART_MAIN);
  lv_obj_set_style_flex_cross_place(navigationBar, LV_FLEX_ALIGN_CENTER,
                                    LV_PART_MAIN);

  // Disable horizontal scrolling
  lv_obj_clear_flag(navigationBar,
                    LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_SCROLLABLE |
                        LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_set_scroll_dir(navigationBar, LV_DIR_NONE);

  fullScreenMenu = lv_obj_create(parent);
  lv_obj_set_size(fullScreenMenu, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(fullScreenMenu, 0, 0);
  lv_obj_set_style_bg_color(fullScreenMenu, APP_COLOR_OVERLAY, 0);
  lv_obj_set_style_bg_opa(fullScreenMenu, APP_COLOR_OVERLAY_OPACITY, 0);
  lv_obj_set_style_border_width(fullScreenMenu, 0, 0);
  lv_obj_set_style_pad_all(fullScreenMenu, 0, 0);
  lv_obj_add_flag(fullScreenMenu, LV_OBJ_FLAG_HIDDEN);
  LVCallbackWrapper::add(
      fullScreenMenu, LV_EVENT_DELETE,
      [this](lv_event_t *e) { this->fullScreenMenu = nullptr; });

  menuContainer = lv_obj_create(fullScreenMenu);
  lv_obj_set_size(menuContainer, LV_PCT(90), LV_SIZE_CONTENT);
  lv_obj_align(menuContainer, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(menuContainer, APP_CONTAINER_BG, 0);
  lv_obj_set_style_border_width(menuContainer, 0, 0);
  lv_obj_set_style_pad_all(menuContainer, 20, 0);
  lv_obj_set_style_radius(menuContainer, 10, 0);
  lv_obj_set_flex_flow(menuContainer, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(menuContainer, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(menuContainer, LV_DIR_NONE);

  // Create compact button
  createCompactButton();
}

CompactTabSelector::~CompactTabSelector() {
  if (fullScreenMenu != nullptr) {
    lv_obj_del(fullScreenMenu);
    fullScreenMenu = nullptr;
  }

  if (container != nullptr) {
    lv_obj_del(container);
    container = nullptr;
  }
}

void CompactTabSelector::createCompactButton() {
  compactButton = lv_btn_create(navigationBar);
  lv_obj_set_size(compactButton, 140, 45);
  lv_obj_set_style_pad_ver(compactButton, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(compactButton, 0, LV_PART_MAIN);

  lv_obj_align(compactButton, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(compactButton, 5, 0);
  lv_obj_set_style_border_width(compactButton, 1, 0);
  lv_obj_set_style_border_color(compactButton, APP_COLOR_BORDER, 0);

  compactLabel = lv_label_create(compactButton);
  lv_obj_center(compactLabel);

  LVCallbackWrapper::add(compactButton, LV_EVENT_CLICKED,
                         [this](lv_event_t *e) {
                           this->onCompactButtonClick(e);
                           Buzzer::getInstance().beepSuccess();
                         });

  updateCompactDisplay();
}

void CompactTabSelector::addTab(const std::string &name,
                                const std::string &icon, bool isIcon, int id) {
  TabInfo tab;
  tab.name = name;
  tab.icon = icon;
  tab.isIcon = isIcon;
  tab.id = (id >= 0) ? id : tabs.size();

  tabs.push_back(tab);

  // If this is the first tab, select it
  if (tabs.size() == 1) {
    selectedTabId = tab.id;
    updateCompactDisplay();
  }
}

void CompactTabSelector::setSelectedTab(int tabId) {
  selectedTabId = tabId;
  updateCompactDisplay();
}

int CompactTabSelector::getSelectedTab() const { return selectedTabId; }

void CompactTabSelector::setTabSelectedCallback(TabSelectedCallback callback) {
  tabSelectedCallback = callback;
}

void CompactTabSelector::setPosition(lv_coord_t x, lv_coord_t y) {
  lv_obj_set_pos(container, x, y);
}

void CompactTabSelector::setSize(lv_coord_t width, lv_coord_t height) {
  lv_obj_set_size(container, width, height);
}

void CompactTabSelector::updateCompactDisplay() {
  if (tabs.empty()) {
    lv_label_set_text(compactLabel, "?");
    return;
  }

  // Find the selected tab
  for (const auto &tab : tabs) {
    if (tab.id == selectedTabId) {
      if (tab.isIcon && !tab.icon.empty()) {
        // For now, we'll use text representation of icons
        // In a real implementation, you'd load the actual icon
        lv_label_set_text(compactLabel, tab.name.c_str());
      } else {
        lv_label_set_text(compactLabel, tab.name.c_str());
      }
      break;
    }
  }
}

void CompactTabSelector::showFullScreenMenu() {
  if (isExpanded)
    return;

  isExpanded = true;

  lv_obj_clean(menuContainer);

  for (size_t i = 0; i < tabs.size(); i++) {
    createMenuItem(tabs[i], i);
  }
  createMenuItem({"Cancel", "", false, -999}, tabs.size());

  lv_obj_clear_flag(fullScreenMenu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(fullScreenMenu);
}

void CompactTabSelector::hideFullScreenMenu() {
  if (!isExpanded)
    return;

  isExpanded = false;
  lv_obj_add_flag(fullScreenMenu, LV_OBJ_FLAG_HIDDEN);
}

void CompactTabSelector::createMenuItem(const TabInfo &tab, int index) {
  lv_obj_t *itemBtn = lv_btn_create(menuContainer);
  lv_obj_set_size(itemBtn, LV_PCT(30), 60);
  if (tab.id == -999) {
    lv_obj_set_style_bg_color(itemBtn, APP_BTN_STYLE_CANCEL, 0);
    lv_obj_set_style_bg_color(itemBtn, APP_BTN_STYLE_CANCEL_PRESSED,
                              LV_STATE_PRESSED);
  } else if (tab.id == selectedTabId) {
    lv_obj_set_style_bg_color(itemBtn, APP_BTN_STYLE_SELECTED, 0);
    lv_obj_set_style_bg_color(itemBtn, APP_BTN_STYLE_SELECTED_PRESSED,
                              LV_STATE_PRESSED);
  }
  lv_obj_set_style_radius(itemBtn, 8, 0);
  lv_obj_set_style_pad_bottom(itemBtn, 10, 0);

  lv_obj_t *itemLabel = lv_label_create(itemBtn);
  lv_label_set_text(itemLabel, tab.name.c_str());
  lv_obj_center(itemLabel);
  lv_obj_set_style_text_font(itemLabel, LV_FONT_DEFAULT, 0);

  LVCallbackWrapper::add(itemBtn, LV_EVENT_CLICKED,
                         [this, tabId = tab.id](lv_event_t *e) {
                           this->onMenuItemClick(tabId);
                           Buzzer::getInstance().beepSuccess();
                         });
}

void CompactTabSelector::onCompactButtonClick(lv_event_t *e) {
  (void)e; // Unused parameter
  showFullScreenMenu();
}

void CompactTabSelector::onMenuItemClick(int tabId) {
  if (tabId == -999) {
    hideFullScreenMenu();
    return;
  }
  selectedTabId = tabId;
  updateCompactDisplay();
  hideFullScreenMenu();

  if (tabSelectedCallback) {
    tabSelectedCallback(tabId);
  }
}

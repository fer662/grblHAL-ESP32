#ifndef COMPACT_TAB_SELECTOR_H
#define COMPACT_TAB_SELECTOR_H

#include <functional>
#include <lvgl.h>
#include <string>
#include <vector>

/**
 * @brief A compact tab selector component that shows an icon/text in normal
 * state and expands to a full-screen menu when tapped
 */
class CompactTabSelector {
public:
  /**
   * @brief Structure to hold tab information
   */
  struct TabInfo {
    std::string name; // Display name
    std::string icon; // Icon path (optional)
    bool isIcon;      // Whether to use icon or text
    int id;           // Unique identifier for the tab
  };

  /**
   * @brief Callback function type for tab selection
   */
  using TabSelectedCallback = std::function<void(int tabId)>;

  /**
   * @brief Constructor
   * @param parent Parent LVGL object
   * @param width Width of the compact selector
   * @param height Height of the compact selector
   */
  CompactTabSelector(lv_obj_t *parent, lv_coord_t width, lv_coord_t height);

  /**
   * @brief Destructor
   */
  ~CompactTabSelector();

  /**
   * @brief Add a tab to the selector
   * @param name Display name
   * @param icon Icon path (optional)
   * @param isIcon Whether to use icon or text
   * @param id Unique identifier
   */
  void addTab(const std::string &name, const std::string &icon = "",
              bool isIcon = false, int id = -1);

  /**
   * @brief Set the currently selected tab
   * @param tabId ID of the tab to select
   */
  void setSelectedTab(int tabId);

  /**
   * @brief Get the currently selected tab ID
   * @return Selected tab ID
   */
  int getSelectedTab() const;

  /**
   * @brief Set the callback for when a tab is selected
   * @param callback Function to call when a tab is selected
   */
  void setTabSelectedCallback(TabSelectedCallback callback);

  /**
   * @brief Get the main container object
   * @return LVGL object pointer
   */
  lv_obj_t *getContainer() const { return container; }

  /**
   * @brief Get the navigation bar object
   * @return LVGL object pointer
   */
  lv_obj_t *getNavigationBar() const { return navigationBar; }

  /**
   * @brief Set the position of the component
   * @param x X coordinate
   * @param y Y coordinate
   */
  void setPosition(lv_coord_t x, lv_coord_t y);

  /**
   * @brief Set the size of the component
   * @param width Width
   * @param height Height
   */
  void setSize(lv_coord_t width, lv_coord_t height);

private:
  lv_obj_t *container;      // Main container
  lv_obj_t *navigationBar;  // Navigation bar (full width)
  lv_obj_t *compactButton;  // Compact button (normal state)
  lv_obj_t *compactLabel;   // Label for compact button
  lv_obj_t *fullScreenMenu; // Full-screen menu (expanded state)
  lv_obj_t *menuContainer;  // Container for menu items
  lv_obj_t *cancelButton;   // Cancel button
  lv_obj_t *cancelLabel;    // Label for cancel button

  std::vector<TabInfo> tabs;               // List of tabs
  int selectedTabId;                       // Currently selected tab ID
  TabSelectedCallback tabSelectedCallback; // Callback for tab selection
  bool isExpanded;                         // Whether menu is expanded

  // Private methods
  void createCompactButton();
  void createFullScreenMenu();
  void updateCompactDisplay();
  void showFullScreenMenu();
  void hideFullScreenMenu();
  void onCompactButtonClick(lv_event_t *e);
  void onMenuItemClick(int tabId);
  void onCancelButtonClick(lv_event_t *e);
  void createMenuItem(const TabInfo &tab, int index);
};

#endif // COMPACT_TAB_SELECTOR_H

#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <functional>
#include <lvgl.h>
#include <memory>

/**
 * @brief Modal settings screen for configuring application settings
 */
class SettingsScreen {
public:
  /**
   * @brief Constructor
   */
  SettingsScreen();

  /**
   * @brief Destructor
   */
  ~SettingsScreen();

  /**
   * @brief Create the settings screen
   * @param parent Parent LVGL object
   */
  void createSettingsScreen(lv_obj_t *parent);

  /**
   * @brief Show the settings screen
   */
  void showSettingsScreen();

  /**
   * @brief Hide the settings screen
   */
  void hideSettingsScreen();

  /**
   * @brief Check if settings screen is currently visible
   * @return true if visible, false otherwise
   */
  bool isVisible() const;

  /**
   * @brief Callback function type for when settings are closed
   */
  using SettingsClosedCallback = std::function<void()>;

  /**
   * @brief Set callback for when settings screen is closed
   * @param callback Function to call when settings are closed
   */
  void setSettingsClosedCallback(SettingsClosedCallback callback);

private:
  lv_obj_t *parent;            // Parent object
  lv_obj_t *settingsScreen;    // Main settings screen container
  lv_obj_t *settingsContainer; // Container for settings items
  lv_obj_t *titleLabel;        // Title label
  lv_obj_t *closeButton;       // Close button
  lv_obj_t *closeLabel;        // Close button label

  // Settings items
  lv_obj_t *buzzerToggle; // Buzzer toggle switch
  lv_obj_t *buzzerLabel;  // Buzzer setting label

  bool visible; // Whether screen is visible
  SettingsClosedCallback
      settingsClosedCallback; // Callback for when settings are closed

  // Private methods
  void createSettingsContainer();
  void createBuzzerSetting();
  void onCloseButtonClick(lv_event_t *e);
  void onBuzzerToggle(lv_event_t *e);
  void updateBuzzerToggle();
};

#endif // SETTINGS_SCREEN_H

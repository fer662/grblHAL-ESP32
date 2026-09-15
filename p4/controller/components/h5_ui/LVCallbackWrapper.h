#include <functional>
#include <lvgl.h>
#include <memory>
#include <unordered_map>
#include <vector>

// Wrapper to allow C++ lambdas as LVGL event callbacks
class LVCallbackWrapper {
public:
  using LambdaType = std::function<void(lv_event_t *)>;

  static void callback(lv_event_t *e) {
    auto fn = static_cast<LambdaType *>(lv_event_get_user_data(e));
    if (fn) {
      (*fn)(e);
    }
  }

  static void add(lv_obj_t *obj, lv_event_code_t code, LambdaType fn) {
    // Allocate on heap
    auto *stored = new LambdaType(std::move(fn));

    // Store in our registry with event code tracking
    registry[obj].push_back({code, stored});

    // Add the main event callback
    lv_obj_add_event_cb(obj, callback, code, stored);

    // Add a delete event callback to clean up all stored lambdas for this
    // object
    lv_obj_add_event_cb(obj, cleanup_callback, LV_EVENT_DELETE, nullptr);
  }

  // Remove a specific event callback by event code
  static bool remove(lv_obj_t *obj, lv_event_code_t code) {
    auto it = registry.find(obj);
    if (it != registry.end()) {
      auto &callbacks = it->second;
      for (auto callback_it = callbacks.begin(); callback_it != callbacks.end();
           ++callback_it) {
        if (callback_it->first == code) {
          auto *stored = callback_it->second;
          callbacks.erase(callback_it);
          delete stored;

          // Remove the event callback from LVGL
          lv_obj_remove_event_cb(obj, callback);

          return true;
        }
      }
    }
    return false;
  }

  // Remove all event callbacks for a specific object
  static void remove_all(lv_obj_t *obj) {
    auto it = registry.find(obj);
    if (it != registry.end()) {
      for (auto &callback_pair : it->second) {
        delete callback_pair.second;
      }
      registry.erase(it);

      // Remove all event callbacks from LVGL
      lv_obj_remove_event_cb(obj, callback);
      lv_obj_remove_event_cb(obj, cleanup_callback);
    }
  }

  // Clean up all stored lambdas for a specific object
  static void cleanup_object(lv_obj_t *obj) { remove_all(obj); }

  // Clean up all stored lambdas (call this on shutdown)
  static void cleanup_all() {
    for (auto &pair : registry) {
      for (auto &callback_pair : pair.second) {
        delete callback_pair.second;
      }
    }
    registry.clear();
  }

private:
  // Store event code and callback pointer pairs
  static std::unordered_map<
      lv_obj_t *, std::vector<std::pair<lv_event_code_t, LambdaType *>>>
      registry;

  static void cleanup_callback(lv_event_t *e) {
    auto *obj = static_cast<lv_obj_t *>(e->target);
    cleanup_object(obj);
  }
};

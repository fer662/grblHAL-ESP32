#include "LVCallbackWrapper.h"

// Define the static member variable
std::unordered_map<
    lv_obj_t *,
    std::vector<std::pair<lv_event_code_t, LVCallbackWrapper::LambdaType *>>>
    LVCallbackWrapper::registry;

cmake_minimum_required(VERSION 3.16)
project(h5_ui_preview LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(CONTROLLER "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(UI "${CONTROLLER}/components/h5_ui")
set(LVGL "${CONTROLLER}/managed_components/lvgl__lvgl")
file(READ "${LVGL}/lv_conf_template.h" CONF)
string(REPLACE "#if 0 /*Set it to \"1\" to enable content*/" "#if 1" CONF "${CONF}")
string(REGEX REPLACE "#define LV_MEM_SIZE[^\n]*" "#define LV_MEM_SIZE (16U * 1024U * 1024U)" CONF "${CONF}")
foreach(SIZE 10 14 16 18 24 28)
  string(REGEX REPLACE "#define LV_FONT_MONTSERRAT_${SIZE} [01]" "#define LV_FONT_MONTSERRAT_${SIZE} 1" CONF "${CONF}")
endforeach()
string(REPLACE "#define LV_USE_SNAPSHOT 0" "#define LV_USE_SNAPSHOT 1" CONF "${CONF}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_lv_conf.h" "${CONF}")
set(LV_CONF_PATH "${CMAKE_CURRENT_BINARY_DIR}/host_lv_conf.h" CACHE STRING "" FORCE)
add_subdirectory("${LVGL}" lvgl EXCLUDE_FROM_ALL)
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=${LV_CONF_PATH})
add_executable(ui_preview preview.cpp
    ${UI}/AxisControls.cpp ${UI}/CompactTabSelector.cpp ${UI}/DPad.cpp
    ${UI}/NormalOperationMode.cpp ${UI}/Numpad.cpp ${UI}/PitchPicker.cpp
    ${UI}/Pitch.cpp ${UI}/SettingsScreen.cpp ${UI}/StateMachine.cpp
    ${UI}/LVCallbackWrapper.cpp ${UI}/display.cpp)
target_include_directories(ui_preview PRIVATE stubs "${UI}")
target_link_libraries(ui_preview PRIVATE lvgl)

# Keep input/layout assertions active even in a Release host build.
target_compile_options(ui_preview PRIVATE -UNDEBUG)

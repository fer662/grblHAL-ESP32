set(CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../main")
set(CORE_SOURCES
    "${CORE_ROOT}/grbl/grbllib.c"
    "${CORE_ROOT}/grbl/canbus.c"
    "${CORE_ROOT}/grbl/coolant_control.c"
    "${CORE_ROOT}/grbl/crossbar.c"
    "${CORE_ROOT}/grbl/crc.c"
    "${CORE_ROOT}/grbl/nvs_buffer.c"
    "${CORE_ROOT}/grbl/gcode.c"
    "${CORE_ROOT}/grbl/machine_limits.c"
    "${CORE_ROOT}/grbl/messages.c"
    "${CORE_ROOT}/grbl/modbus.c"
    "${CORE_ROOT}/grbl/modbus_rtu.c"
    "${CORE_ROOT}/grbl/motion_control.c"
    "${CORE_ROOT}/grbl/nuts_bolts.c"
    "${CORE_ROOT}/grbl/override.c"
    "${CORE_ROOT}/grbl/probe.c"
    "${CORE_ROOT}/grbl/planner.c"
    "${CORE_ROOT}/grbl/protocol.c"
    "${CORE_ROOT}/grbl/report.c"
    "${CORE_ROOT}/grbl/settings.c"
    "${CORE_ROOT}/grbl/sleep.c"
    "${CORE_ROOT}/grbl/spindle_control.c"
    "${CORE_ROOT}/grbl/state_machine.c"
    "${CORE_ROOT}/grbl/stream.c"
    "${CORE_ROOT}/grbl/stream_file.c"
    "${CORE_ROOT}/grbl/stream_json.c"
    "${CORE_ROOT}/grbl/stepper.c"
    "${CORE_ROOT}/grbl/stepper2.c"
    "${CORE_ROOT}/grbl/strutils.c"
    "${CORE_ROOT}/grbl/system.c"
    "${CORE_ROOT}/grbl/tool_change.c"
    "${CORE_ROOT}/grbl/alarms.c"
    "${CORE_ROOT}/grbl/errors.c"
    "${CORE_ROOT}/grbl/ngc_expr.c"
    "${CORE_ROOT}/grbl/ngc_params.c"
    "${CORE_ROOT}/grbl/ngc_flowctrl.c"
    "${CORE_ROOT}/grbl/regex.c"
    "${CORE_ROOT}/grbl/ioports.c"
    "${CORE_ROOT}/grbl/encoders.c"
    "${CORE_ROOT}/grbl/utf8.c"
    "${CORE_ROOT}/grbl/vfs.c"
    "${CORE_ROOT}/grbl/fs_device.c"
    "${CORE_ROOT}/grbl/fs_ram.c"
    "${CORE_ROOT}/grbl/kinematics/asymmetric_ganging.c"
    "${CORE_ROOT}/grbl/kinematics/corexy.c"
    "${CORE_ROOT}/grbl/kinematics/wall_plotter.c"
    "${CORE_ROOT}/grbl/kinematics/delta.c"
    "${CORE_ROOT}/grbl/kinematics/polar.c"
    "${CORE_ROOT}/grbl/my_plugin.c"
    "${CORE_ROOT}/grbl/pid.c"
)
if(NOT IDF_TARGET STREQUAL "esp32p4")
    message(FATAL_ERROR "grblhal_p4 requires IDF_TARGET=esp32p4")
endif()
idf_component_register(
    SRCS driver.c serial.c storage.c feedback.c spindle.c fpu_isr.c fpu_isr.S ${CORE_SOURCES}
    LDFRAGMENTS "motion.lf"
    INCLUDE_DIRS "." "${CORE_ROOT}" "../../boards"
    REQUIRES nvs_flash esp_driver_gpio esp_driver_uart esp_driver_gptimer esp_driver_pcnt esp_timer esp_hw_support)
if(NOT GRBLHAL_P4_CONFIG_HEADER OR NOT EXISTS "${GRBLHAL_P4_CONFIG_HEADER}")
    message(FATAL_ERROR "Set GRBLHAL_P4_CONFIG_HEADER to your application machine.h")
endif()
target_compile_definitions(${COMPONENT_LIB} PUBLIC GRBL_ESP32 P4_BENCH_ONLY=$<BOOL:${P4_BENCH_ONLY}>)
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-include${GRBLHAL_P4_CONFIG_HEADER}>"
    -Wno-missing-field-initializers -Wno-implicit-fallthrough)
# Pinned upstream Modbus diagnostics use %u for uint32_t. On RV32 both are
# 32-bit, but GCC distinguishes unsigned int from unsigned long. Keep the warning
# visible without patching upstream core or weakening checks in the P4 driver.
set_source_files_properties("${CORE_ROOT}/grbl/modbus_rtu.c" PROPERTIES COMPILE_OPTIONS "-Wno-error=format")
# GCC 14 also diagnoses the pinned core's dynamically sized macro-name sprintf.
# Retain that upstream warning while allowing the unchanged core to compile.
set_source_files_properties("${CORE_ROOT}/grbl/ngc_flowctrl.c" PROPERTIES COMPILE_OPTIONS "-Wno-error=format-overflow")

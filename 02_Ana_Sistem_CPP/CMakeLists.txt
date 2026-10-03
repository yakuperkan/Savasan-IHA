cmake_minimum_required(VERSION 3.10)
project(Savasan_IHA_System LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
option(SAVASAN_DEBUG "Enable debug probes and file logging" OFF)
option(SAVASAN_BUILD_APP "Build DeepStream runtime binary" ON)
option(SAVASAN_BUILD_TESTS "Build unit tests" ON)

add_library(savasan_control STATIC
  src/control/world_target_estimator.cpp
  src/control/vehicle_state_estimator.cpp
  src/control/pid_controller.cpp
  # Telemetri takip zinciri (MSO portu): seyir_guidance.hpp bunlara baglidir.
  src/control/target_estimator.cpp
  src/control/pursuit_guidance.cpp
)
target_include_directories(savasan_control PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(savasan_control PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(SAVASAN_BUILD_APP OR SAVASAN_BUILD_TESTS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GST REQUIRED gstreamer-1.0)

  set(DS_INCLUDES /opt/nvidia/deepstream/deepstream/sources/includes)
  set(DS_LIB_DIR  /opt/nvidia/deepstream/deepstream/lib)

  set(SAVASAN_HAS_DEEPSTREAM_TEST_DEPS OFF)
  if(EXISTS "${DS_INCLUDES}/nvbufsurface.h"
     AND EXISTS "${DS_LIB_DIR}/libnvbufsurface.so"
     AND EXISTS "${DS_LIB_DIR}/libnvds_meta.so"
     AND EXISTS "${DS_LIB_DIR}/libnvdsgst_meta.so")
    set(SAVASAN_HAS_DEEPSTREAM_TEST_DEPS ON)
  endif()

  if(SAVASAN_HAS_DEEPSTREAM_TEST_DEPS)
    set(SAVASAN_DS_INCLUDE_DIRS ${DS_INCLUDES})
    set(SAVASAN_DS_LINK_LIBS
      ${DS_LIB_DIR}/libnvbufsurface.so
      ${DS_LIB_DIR}/libnvds_meta.so
      ${DS_LIB_DIR}/libnvdsgst_meta.so
    )
  else()
    set(SAVASAN_DS_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs/deepstream)
    set(SAVASAN_DS_LINK_LIBS "")
  endif()

  function(savasan_apply_gstreamer_runner_deps target)
    target_include_directories(${target} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${GST_INCLUDE_DIRS}
      ${SAVASAN_DS_INCLUDE_DIRS}
    )
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wno-unused-parameter
      ${GST_CFLAGS_OTHER}
    )
    target_link_libraries(${target} PRIVATE
      savasan_control
      pthread
      ${GST_LIBRARIES}
    )
    if(SAVASAN_DS_LINK_LIBS)
      target_link_libraries(${target} PRIVATE ${SAVASAN_DS_LINK_LIBS})
    endif()
  endfunction()

  set(SAVASAN_PHASE5_CALLBACKS_SUPPORT_SOURCES
    src/runners/phase5_guidance.cpp
    src/runners/phase5_runtime.cpp
    src/runners/mission_mode_utils.cpp
    src/runners/server_clock.cpp
    src/autopilot/alc_link_bridge.cpp
    # phase5_callbacks.cpp + phase5_guidance.cpp bu sembollere referans verir;
    # compile-check link'i icin sart (hepsi DeepStream bagimsiz).
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
    src/tracking/track_selector.cpp
  )

  # savasan_runners_deepstream (phase5_callbacks.cpp) + phase5_guidance.cpp link
  # bagimliliklari. ds_app.cpp CI'a giremedigi (DeepStream/nvinfer) icin OSD stil
  # sembolleri no-op stub'tan saglanir. phase5_callbacks/guidance'a baglanan tum
  # test hedefleri bu listeyi kullanir.
  set(SAVASAN_PHASE5_RUNNERS_LINK_DEPS
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
    src/tracking/track_selector.cpp
    src/autopilot/alc_link_bridge.cpp
    tests/compile_check/ds_osd_style_stub.cpp
  )

  add_library(savasan_runners_deepstream STATIC
    src/runners/phase5_callbacks.cpp
  )
  if(NOT SAVASAN_HAS_DEEPSTREAM_TEST_DEPS)
    target_sources(savasan_runners_deepstream PRIVATE
      tests/stubs/deepstream/gstnvdsmeta_stub.c
    )
  endif()
  savasan_apply_gstreamer_runner_deps(savasan_runners_deepstream)
endif()

if(SAVASAN_BUILD_APP)
  add_executable(savasan_iha
    src/main.cpp
    src/common/config_path.cpp
    src/app/startup_config.cpp
    src/app/phase_dispatch.cpp
    src/runners/gst_app_helpers.cpp
    src/runners/phase1_runner.cpp
    src/runners/phase2_runner.cpp
    src/runners/phase3_runner.cpp
    src/runners/phase5_runner.cpp
    src/runners/phase5_runner_config.cpp
    src/runners/phase5_runtime_options.cpp
    src/runners/phase5_runtime.cpp
    src/runners/mission_mode_utils.cpp
    src/runners/server_clock.cpp
    src/runners/phase5_guidance.cpp
    src/camera/usb_source.cpp
    src/deepstream/ds_app.cpp
    src/tracking/track_selector.cpp
    src/autopilot/alc_link_bridge.cpp
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
  )

  target_include_directories(savasan_iha PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GST_INCLUDE_DIRS}
    ${DS_INCLUDES}
  )

  target_link_libraries(savasan_iha PRIVATE
    savasan_runners_deepstream
    savasan_control
    ${GST_LIBRARIES}
    ${DS_LIB_DIR}/libnvds_meta.so
    ${DS_LIB_DIR}/libnvdsgst_meta.so
    ${DS_LIB_DIR}/libnvbufsurface.so
    pthread
  )
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(SAVASAN_BUILD_APP)
  target_compile_definitions(savasan_iha PRIVATE
    SAVASAN_DEBUG=$<BOOL:${SAVASAN_DEBUG}>
    SAVASAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_compile_options(savasan_iha PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -O3
    -march=armv8.2-a
    -mtune=cortex-a78ae
    -ffast-math
    $<$<CONFIG:Release>:-flto -DNDEBUG>
    ${GST_CFLAGS_OTHER}
  )
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(savasan_iha PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()

  # USB kamera saha kalibrasyonu (trackbar + profil); CUDA gerektirmez.
  find_package(OpenCV 4 QUIET COMPONENTS core imgproc highgui videoio)
  if(NOT OpenCV_FOUND)
    find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio)
  endif()
  add_executable(camera_tune src/tools/camera_tune.cpp)
  target_compile_definitions(camera_tune PRIVATE
    SAVASAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_include_directories(camera_tune PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(camera_tune PRIVATE ${OpenCV_LIBS})
  target_compile_options(camera_tune PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(SAVASAN_BUILD_TESTS)
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  set(gtest_force_shared_cmake ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(test_phase5_callbacks_compile
    tests/compile_check/test_phase5_callbacks_compile.cpp
    ${SAVASAN_PHASE5_CALLBACKS_SUPPORT_SOURCES}
    # track_selector.cpp -> ds_app OSD stil sembolleri; ds_app.cpp CI'a giremez
    # (DeepStream/nvinfer). Link smoke test'i icin no-op stub yeterli.
    # (compile_check dizininde — stubs/deepstream'deki nvdsmeta.h stub'ini ezmesin.)
    tests/compile_check/ds_osd_style_stub.cpp
  )
  savasan_apply_gstreamer_runner_deps(test_phase5_callbacks_compile)
  target_link_libraries(test_phase5_callbacks_compile PRIVATE
    savasan_runners_deepstream
    util
  )
  add_test(NAME test_phase5_callbacks_compile COMMAND test_phase5_callbacks_compile)
  set_tests_properties(test_phase5_callbacks_compile PROPERTIES LABELS "unit;compile-check")

  add_executable(test_pid_controller tests/test_pid_controller.cpp)
  target_link_libraries(test_pid_controller PRIVATE savasan_control)
  add_test(NAME test_pid_controller COMMAND test_pid_controller)
  set_tests_properties(test_pid_controller PROPERTIES LABELS "unit")

  add_executable(test_log_thread_safety tests/test_log_thread_safety.cpp)
  target_include_directories(test_log_thread_safety PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_log_thread_safety PRIVATE pthread)
  add_test(NAME test_log_thread_safety COMMAND test_log_thread_safety)
  set_tests_properties(test_log_thread_safety PROPERTIES LABELS "unit")

  add_executable(test_scope_profiler tests/test_scope_profiler.cpp)
  target_include_directories(test_scope_profiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_scope_profiler PRIVATE pthread)
  add_test(NAME test_scope_profiler COMMAND test_scope_profiler)
  set_tests_properties(test_scope_profiler PROPERTIES LABELS "unit")

  add_executable(test_world_target_estimator tests/test_world_target_estimator.cpp)
  target_link_libraries(test_world_target_estimator PRIVATE savasan_control)
  add_test(NAME test_world_target_estimator COMMAND test_world_target_estimator)
  set_tests_properties(test_world_target_estimator PROPERTIES LABELS "unit")

  add_executable(test_vehicle_state_estimator tests/test_vehicle_state_estimator.cpp)
  target_link_libraries(test_vehicle_state_estimator PRIVATE savasan_control)
  add_test(NAME test_vehicle_state_estimator COMMAND test_vehicle_state_estimator)
  set_tests_properties(test_vehicle_state_estimator PROPERTIES LABELS "unit")

  add_executable(test_potential_field_evasion
    tests/test_potential_field_evasion.cpp
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
  )
  target_include_directories(test_potential_field_evasion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_potential_field_evasion PRIVATE pthread)
  add_test(NAME test_potential_field_evasion COMMAND test_potential_field_evasion)
  set_tests_properties(test_potential_field_evasion PROPERTIES LABELS "unit")

  add_executable(test_phase_dispatch_integration
    tests/integration/test_phase_dispatch_integration.cpp
    src/app/phase_dispatch.cpp
  )
  target_include_directories(test_phase_dispatch_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_phase_dispatch_integration PRIVATE savasan_control)
  add_test(NAME test_phase_dispatch_integration COMMAND test_phase_dispatch_integration)
  set_tests_properties(test_phase_dispatch_integration PROPERTIES LABELS "integration")

  add_executable(test_config_path
    tests/test_config_path.cpp
    src/common/config_path.cpp
  )
  target_include_directories(test_config_path PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(test_config_path PRIVATE
    SAVASAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  add_test(NAME test_config_path COMMAND test_config_path)
  set_tests_properties(test_config_path PROPERTIES LABELS "unit")

  add_executable(test_startup_config_integration
    tests/integration/test_startup_config_integration.cpp
    src/common/config_path.cpp
    src/app/startup_config.cpp
  )
  target_include_directories(test_startup_config_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(test_startup_config_integration PRIVATE
    SAVASAN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(test_startup_config_integration PRIVATE savasan_control)
  add_test(NAME test_startup_config_integration COMMAND test_startup_config_integration)
  set_tests_properties(test_startup_config_integration PROPERTIES LABELS "integration")

  add_executable(test_alc_link_bridge_integration
    tests/integration/test_alc_link_bridge_integration.cpp
    src/autopilot/alc_link_bridge.cpp
  )
  target_include_directories(test_alc_link_bridge_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_alc_link_bridge_integration PRIVATE pthread util)
  add_test(NAME test_alc_link_bridge_integration COMMAND test_alc_link_bridge_integration)
  set_tests_properties(test_alc_link_bridge_integration PROPERTIES LABELS "integration;autopilot-integration")

  add_executable(test_alc_link_bridge_serial_fault_integration
    tests/integration/test_alc_link_bridge_serial_fault_integration.cpp
    src/autopilot/alc_link_bridge.cpp
  )
  target_include_directories(test_alc_link_bridge_serial_fault_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_alc_link_bridge_serial_fault_integration PRIVATE pthread util)
  add_test(NAME test_alc_link_bridge_serial_fault_integration COMMAND test_alc_link_bridge_serial_fault_integration)
  set_tests_properties(test_alc_link_bridge_serial_fault_integration PROPERTIES LABELS "integration;autopilot-integration")

  if(SAVASAN_HAS_DEEPSTREAM_TEST_DEPS)
    add_executable(test_phase5_guidance_control_loop_integration
      tests/integration/test_phase5_guidance_control_loop_integration.cpp
      src/runners/phase5_guidance.cpp
      src/runners/phase5_runtime.cpp
      src/runners/mission_mode_utils.cpp
      src/runners/server_clock.cpp
        ${SAVASAN_PHASE5_RUNNERS_LINK_DEPS}
    )
    target_include_directories(test_phase5_guidance_control_loop_integration PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${GST_INCLUDE_DIRS}
      ${DS_INCLUDES}
    )
    target_link_libraries(test_phase5_guidance_control_loop_integration PRIVATE
      savasan_runners_deepstream
      savasan_control pthread util
      ${GST_LIBRARIES}
      ${DS_LIB_DIR}/libnvbufsurface.so
      ${DS_LIB_DIR}/libnvds_meta.so
      ${DS_LIB_DIR}/libnvdsgst_meta.so
    )
    add_test(NAME test_phase5_guidance_control_loop_integration COMMAND test_phase5_guidance_control_loop_integration)
    set_tests_properties(test_phase5_guidance_control_loop_integration PROPERTIES LABELS "integration;autopilot-integration")
  else()
    message(STATUS "Skipping DeepStream-dependent integration tests (headers/libs not found).")
  endif()

  add_executable(test_potential_field_evasion_integration
    tests/integration/test_potential_field_evasion_integration.cpp
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
  )
  target_include_directories(test_potential_field_evasion_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_potential_field_evasion_integration PRIVATE pthread)
  add_test(NAME test_potential_field_evasion_integration COMMAND test_potential_field_evasion_integration)
  set_tests_properties(test_potential_field_evasion_integration PROPERTIES LABELS "integration;autopilot-integration")

  add_executable(test_world_target_estimator_integration
    tests/integration/test_world_target_estimator_integration.cpp
  )
  target_link_libraries(test_world_target_estimator_integration PRIVATE savasan_control)
  add_test(NAME test_world_target_estimator_integration COMMAND test_world_target_estimator_integration)
  set_tests_properties(test_world_target_estimator_integration PROPERTIES LABELS "integration")

  add_executable(test_vehicle_state_estimator_integration
    tests/integration/test_vehicle_state_estimator_integration.cpp
  )
  target_link_libraries(test_vehicle_state_estimator_integration PRIVATE savasan_control)
  add_test(NAME test_vehicle_state_estimator_integration COMMAND test_vehicle_state_estimator_integration)
  set_tests_properties(test_vehicle_state_estimator_integration PROPERTIES LABELS "integration")

  if(SAVASAN_HAS_DEEPSTREAM_TEST_DEPS)
    add_executable(test_phase5_multithread_stress_integration
      tests/integration/test_phase5_multithread_stress_integration.cpp
      ${SAVASAN_PHASE5_RUNNERS_LINK_DEPS}
      src/runners/phase5_guidance.cpp
      src/runners/phase5_runtime.cpp
      src/runners/mission_mode_utils.cpp
      src/runners/server_clock.cpp
      )
    target_include_directories(test_phase5_multithread_stress_integration PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${GST_INCLUDE_DIRS}
      ${DS_INCLUDES}
    )
    target_link_libraries(test_phase5_multithread_stress_integration PRIVATE
      savasan_runners_deepstream
      savasan_control pthread util
      ${GST_LIBRARIES}
      ${DS_LIB_DIR}/libnvbufsurface.so
      ${DS_LIB_DIR}/libnvds_meta.so
      ${DS_LIB_DIR}/libnvdsgst_meta.so
    )
    add_test(NAME test_phase5_multithread_stress_integration COMMAND test_phase5_multithread_stress_integration)
    set_tests_properties(test_phase5_multithread_stress_integration PROPERTIES LABELS "integration;autopilot-integration")
  endif()

  add_executable(test_mock_framework tests/test_mock_framework.cpp)
  target_include_directories(test_mock_framework PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_mock_framework COMMAND test_mock_framework)
  set_tests_properties(test_mock_framework PROPERTIES LABELS "unit")

  add_executable(test_lock_selection_policy tests/test_lock_selection_policy.cpp)
  target_include_directories(test_lock_selection_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_lock_selection_policy COMMAND test_lock_selection_policy)
  set_tests_properties(test_lock_selection_policy PROPERTIES LABELS "unit")

  add_executable(test_lock_metrics tests/test_lock_metrics.cpp)
  target_include_directories(test_lock_metrics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_lock_metrics COMMAND test_lock_metrics)
  set_tests_properties(test_lock_metrics PROPERTIES LABELS "unit")

  add_executable(test_lock_state_file tests/test_lock_state_file.cpp)
  target_include_directories(test_lock_state_file PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_lock_state_file COMMAND test_lock_state_file)
  set_tests_properties(test_lock_state_file PROPERTIES LABELS "unit")

  add_executable(test_alc_hub_file tests/test_alc_hub_file.cpp)
  target_include_directories(test_alc_hub_file PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_alc_hub_file COMMAND test_alc_hub_file)
  set_tests_properties(test_alc_hub_file PROPERTIES LABELS "unit")

  add_executable(test_seyir_mode_commands tests/test_seyir_mode_commands.cpp)
  target_include_directories(test_seyir_mode_commands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_seyir_mode_commands COMMAND test_seyir_mode_commands)
  set_tests_properties(test_seyir_mode_commands PROPERTIES LABELS "unit")

  add_executable(test_target_estimator_golden tests/test_target_estimator_golden.cpp)
  target_link_libraries(test_target_estimator_golden PRIVATE savasan_control)
  add_test(NAME test_target_estimator_golden
    COMMAND test_target_estimator_golden
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mso_hedef_kestirici_ref.csv)
  set_tests_properties(test_target_estimator_golden PROPERTIES LABELS "unit")

  add_executable(test_geofence tests/test_geofence.cpp
    src/evasion/geofence.cpp
  )
  target_include_directories(test_geofence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_geofence COMMAND test_geofence)
  set_tests_properties(test_geofence PROPERTIES LABELS "unit")

  add_executable(test_seyir_guidance_safety tests/test_seyir_guidance_safety.cpp
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
    src/runners/server_clock.cpp
  )
  target_include_directories(test_seyir_guidance_safety PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_seyir_guidance_safety PRIVATE savasan_control)
  add_test(NAME test_seyir_guidance_safety COMMAND test_seyir_guidance_safety)
  set_tests_properties(test_seyir_guidance_safety PROPERTIES LABELS "unit")

  add_executable(test_seyir_goto tests/test_seyir_goto.cpp
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
    src/runners/server_clock.cpp
  )
  target_include_directories(test_seyir_goto PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_seyir_goto PRIVATE savasan_control)
  add_test(NAME test_seyir_goto COMMAND test_seyir_goto)
  set_tests_properties(test_seyir_goto PROPERTIES LABELS "unit")

  add_executable(test_pursuit_guidance_golden tests/test_pursuit_guidance_golden.cpp)
  target_link_libraries(test_pursuit_guidance_golden PRIVATE savasan_control)
  add_test(NAME test_pursuit_guidance_golden
    COMMAND test_pursuit_guidance_golden
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mso_takip_gudum_ref.csv)
  set_tests_properties(test_pursuit_guidance_golden PROPERTIES LABELS "unit")

  add_executable(test_seyir_command_scheduler tests/test_seyir_command_scheduler.cpp
    src/evasion/geo_kinematics.cpp
    src/evasion/potential_field_evasion.cpp
    src/evasion/geofence.cpp
  )
  target_include_directories(test_seyir_command_scheduler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_seyir_command_scheduler COMMAND test_seyir_command_scheduler)
  set_tests_properties(test_seyir_command_scheduler PROPERTIES LABELS "unit")

  add_executable(test_mission_mode_utils tests/test_mission_mode_utils.cpp
    src/runners/mission_mode_utils.cpp
  )
  target_include_directories(test_mission_mode_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_mission_mode_utils COMMAND test_mission_mode_utils)
  set_tests_properties(test_mission_mode_utils PROPERTIES LABELS "unit")

  add_executable(test_server_clock tests/test_server_clock.cpp
    src/runners/server_clock.cpp
  )
  target_include_directories(test_server_clock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_server_clock COMMAND test_server_clock)
  set_tests_properties(test_server_clock PROPERTIES LABELS "unit")

  add_executable(test_phase5_mission_policy tests/test_phase5_mission_policy.cpp)
  target_include_directories(test_phase5_mission_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME test_phase5_mission_policy COMMAND test_phase5_mission_policy)
  set_tests_properties(test_phase5_mission_policy PROPERTIES LABELS "unit")

  add_executable(test_phase5_runner_config tests/test_phase5_runner_config.cpp
    src/runners/phase5_runner_config.cpp
  )
  target_include_directories(test_phase5_runner_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_phase5_runner_config PRIVATE pthread)
  add_test(NAME test_phase5_runner_config COMMAND test_phase5_runner_config)
  set_tests_properties(test_phase5_runner_config PROPERTIES LABELS "unit")

endif()

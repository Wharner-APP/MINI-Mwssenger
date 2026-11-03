set(MINI_UI_TEST_TARGETS
    test_profile_panel
    test_settings_dialog
    test_about_dialog
    test_diagnostics
    test_main_window_services
    test_auth_dialog_login)

foreach(test_target IN LISTS MINI_UI_TEST_TARGETS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE
        mini_client_lib
        Qt6::Test)
    target_include_directories(${test_target} PRIVATE
        "${CMAKE_SOURCE_DIR}/tests/services"
        "${CMAKE_SOURCE_DIR}/tests/storage")
    target_compile_definitions(${test_target} PRIVATE
        MINI_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src"
        MINI_TS_FILE="${CMAKE_SOURCE_DIR}/translations/mini_ru.ts")
    set_target_properties(${test_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_test(NAME ${test_target} COMMAND ${test_target})
    set_tests_properties(${test_target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MINI_QT_BIN_DIR};PATH=path_list_prepend:${MINI_VCPKG_BIN_DIR}")
endforeach()

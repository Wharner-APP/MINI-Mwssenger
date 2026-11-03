set(MINI_TEST_TARGETS
    test_chatlist
    test_theme
    test_settings
    test_log
    test_i18n
    test_composer
    test_singleinstance
    test_mainwindow)

foreach(test_target IN LISTS MINI_TEST_TARGETS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE mini_client_lib Qt6::Test)
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

set_source_files_properties("${MINI_QM_FILE}" PROPERTIES QT_RESOURCE_ALIAS "mini.qm")
qt_add_resources(test_i18n "test_i18n_translations"
    PREFIX "/i18n"
    FILES "${MINI_QM_FILE}")
add_dependencies(test_i18n mini_translations)

add_subdirectory(services)
add_subdirectory(ui)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/e2e/CMakeLists.txt")
    add_subdirectory(e2e)
endif()

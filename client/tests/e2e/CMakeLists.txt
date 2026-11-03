add_executable(test_e2e test_e2e.cpp)
target_link_libraries(test_e2e PRIVATE mini_client_lib Qt6::Test)
target_compile_definitions(test_e2e PRIVATE
    MINI_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")
set_target_properties(test_e2e PROPERTIES
    WIN32_EXECUTABLE OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME test_e2e COMMAND test_e2e)
set_tests_properties(test_e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MINI_QT_BIN_DIR};PATH=path_list_prepend:${MINI_VCPKG_BIN_DIR}"
    TIMEOUT 240)

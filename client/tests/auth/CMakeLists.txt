set(MINI_AUTH_TEST_TARGETS
    test_signin_key
    test_kdf
    test_signin_service
    test_captcha_service
    test_auth_service)

foreach(test_target IN LISTS MINI_AUTH_TEST_TARGETS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE mini_client_lib Qt6::Test)
    target_include_directories(${test_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${test_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_test(NAME ${test_target} COMMAND ${test_target})
    set_tests_properties(${test_target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MINI_QT_BIN_DIR};PATH=path_list_prepend:${MINI_VCPKG_BIN_DIR}")
endforeach()

set(MINI_CRYPTO_TEST_TARGETS
    test_sodium
    test_identity_keys
    test_x3dh
    test_double_ratchet
    test_safety_number)

foreach(test_target IN LISTS MINI_CRYPTO_TEST_TARGETS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE mini_client_lib Qt6::Test)
    set_target_properties(${test_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_test(NAME ${test_target} COMMAND ${test_target})
    set_tests_properties(${test_target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MINI_QT_BIN_DIR};PATH=path_list_prepend:${MINI_VCPKG_BIN_DIR}")
endforeach()

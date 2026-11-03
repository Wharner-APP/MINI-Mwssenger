add_library(mini_net_test_support STATIC
    StubHttpServer.cpp
    StubHttpServer.h
    StubWsServer.cpp
    StubWsServer.h)
target_link_libraries(mini_net_test_support PUBLIC mini_client_lib)
target_include_directories(mini_net_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set(MINI_NET_TEST_TARGETS
    test_tunnel_resolver
    test_tls_pin
    test_reconnect_policy
    test_apiclient
    test_wsclient
    test_captcha_dialog_lifetime
    test_tls_backend)

foreach(test_target IN LISTS MINI_NET_TEST_TARGETS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE
        mini_client_lib
        mini_net_test_support
        Qt6::Test)
    target_compile_definitions(${test_target} PRIVATE
        MINI_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")
    set_target_properties(${test_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_test(NAME ${test_target} COMMAND ${test_target})
    set_tests_properties(${test_target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MINI_QT_BIN_DIR};PATH=path_list_prepend:${MINI_VCPKG_BIN_DIR}")
endforeach()

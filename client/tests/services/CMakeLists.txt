set(MINI_SERVICES_TEST_TARGETS
    test_message_service
    test_chat_service
    test_contact_service
    test_search_service
    test_session_crypto
    test_message_ui)

add_library(mini_services_test_support INTERFACE)
target_include_directories(mini_services_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(test_target IN LISTS MINI_SERVICES_TEST_TARGETS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE
        mini_client_lib
        mini_services_test_support
        Qt6::Test)
    set_target_properties(${test_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_test(NAME ${test_target} COMMAND ${test_target})
    set_tests_properties(${test_target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${MINI_QT_BIN_DIR};PATH=path_list_prepend:${MINI_VCPKG_BIN_DIR}")
endforeach()

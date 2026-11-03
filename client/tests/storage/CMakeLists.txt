add_library(mini_storage_test_support INTERFACE)
target_include_directories(mini_storage_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

set(MINI_STORAGE_TEST_TARGETS
    test_storage_worker
    test_database
    test_repositories
    test_outbox
    test_search
    test_backup)

foreach(test_target IN LISTS MINI_STORAGE_TEST_TARGETS)
    add_executable(${test_target} ${test_target}.cpp)
    target_link_libraries(${test_target} PRIVATE
        mini_client_lib
        mini_storage_test_support
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

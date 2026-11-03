cmake_minimum_required(VERSION 3.24)

project(MiniMessenger VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test Concurrent)
find_package(Qt6 6.8 QUIET COMPONENTS WebEngineWidgets WebChannel Multimedia MultimediaWidgets)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus)
endif()

file(GLOB_RECURSE MINI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/crypto/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/services/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/domain/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp")
file(GLOB_RECURSE MINI_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/net/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/crypto/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/services/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/storage/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/domain/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h")
file(GLOB_RECURSE MINI_RESOURCE_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "resources/icons/*.svg"
    "resources/fonts/*.ttf"
    "resources/fonts/*.txt"
    "resources/stickers/*.json"
    "resources/stickers/*.ministicker"
    "resources/stickers/*/*.png"
    "resources/stickers/*/*.json"
    "resources/gifs/*.gif"
    "resources/gifs/*.json"
    "resources/sounds/*.wav"
    "resources/emoji/*.json")

add_library(mini_client_lib STATIC ${MINI_SOURCES} ${MINI_HEADERS})
target_include_directories(mini_client_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mini_client_lib PUBLIC
    Qt6::Widgets
    Qt6::Svg
    Qt6::Network
    Qt6::Concurrent
    unofficial-sodium::sodium
    unofficial::sqlite3::sqlite3)
if(TARGET Qt6::CorePrivate)
    target_link_libraries(mini_client_lib PUBLIC Qt6::CorePrivate)
    target_compile_definitions(mini_client_lib PUBLIC MINI_HAVE_ZIP=1)
endif()
if(TARGET Qt6::WebEngineWidgets AND TARGET Qt6::WebChannel)
    target_link_libraries(mini_client_lib PUBLIC Qt6::WebEngineWidgets Qt6::WebChannel)
    target_compile_definitions(mini_client_lib PUBLIC MINI_HAVE_WEBENGINE=1)
else()
    message(WARNING "Qt WebEngine is not installed; hCaptcha will fall back to the built-in challenge")
endif()
if(TARGET Qt6::Multimedia AND TARGET Qt6::MultimediaWidgets)
    # §46.3: notification WAVs play through QSoundEffect.
    # §40.4: the built-in video player uses QMediaPlayer + QVideoWidget.
    target_link_libraries(mini_client_lib PUBLIC Qt6::Multimedia Qt6::MultimediaWidgets)
    target_compile_definitions(mini_client_lib PUBLIC MINI_HAVE_MULTIMEDIA=1)
else()
    message(WARNING "Qt Multimedia is not installed; notification sounds fall back to the system beep and video uses the external player")
endif()
target_compile_definitions(mini_client_lib PUBLIC
    MINI_APP_VERSION="${PROJECT_VERSION}"
    MINI_BUILD_TYPE="$<CONFIG>")

qt_add_resources(mini_client_lib "app_resources"
    PREFIX "/"
    BASE "resources"
    FILES ${MINI_RESOURCE_FILES})

qt_add_executable(mini_client src/main.cpp)
target_link_libraries(mini_client PRIVATE mini_client_lib)
set_target_properties(mini_client PROPERTIES
    OUTPUT_NAME "MINI Messenger"
    WIN32_EXECUTABLE ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(MINI_TRANSLATION_LANGUAGES ru en)
set(MINI_TS_FILES)
foreach(language IN LISTS MINI_TRANSLATION_LANGUAGES)
    list(APPEND MINI_TS_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/translations/mini_${language}.ts")
endforeach()

get_target_property(MINI_QMAKE Qt6::qmake IMPORTED_LOCATION)
if(MINI_QMAKE)
    get_filename_component(MINI_QT_BIN_DIR "${MINI_QMAKE}" DIRECTORY)
else()
    get_filename_component(MINI_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
endif()

if(VCPKG_INSTALLED_DIR)
    set(MINI_VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
else()
    set(MINI_VCPKG_BIN_DIR "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
endif()

set(MINI_QM_DIR "${CMAKE_BINARY_DIR}/translations")
set(MINI_QM_FILES)
foreach(language IN LISTS MINI_TRANSLATION_LANGUAGES)
    list(APPEND MINI_QM_FILES "${MINI_QM_DIR}/mini_${language}.qm")
endforeach()
# Kept for tests/CMakeLists.txt, which embeds the Russian catalog under the
# legacy "mini.qm" resource name.
list(GET MINI_QM_FILES 0 MINI_QM_FILE)
find_program(MINI_LRELEASE_EXECUTABLE
    NAMES lrelease lrelease.exe
    HINTS "${MINI_QT_BIN_DIR}"
    NO_CMAKE_FIND_ROOT_PATH)
if(MINI_LRELEASE_EXECUTABLE)
    foreach(language IN LISTS MINI_TRANSLATION_LANGUAGES)
        set(MINI_TS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/translations/mini_${language}.ts")
        set(MINI_QM_FILE_FOR_LANGUAGE "${MINI_QM_DIR}/mini_${language}.qm")
        add_custom_command(OUTPUT "${MINI_QM_FILE_FOR_LANGUAGE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${MINI_QM_DIR}"
            COMMAND "${MINI_LRELEASE_EXECUTABLE}" "${MINI_TS_FILE}"
                    -qm "${MINI_QM_FILE_FOR_LANGUAGE}"
            DEPENDS "${MINI_TS_FILE}"
            VERBATIM)
    endforeach()
    add_custom_target(mini_translations ALL DEPENDS ${MINI_QM_FILES})
    add_dependencies(mini_client mini_translations)

    # Embed the compiled catalogs so the UI language never depends on the
    # working directory or on file-system quirks of the install location.
    # Generated files live outside the source tree, so each one needs an
    # explicit relative alias inside the resource.
    foreach(language IN LISTS MINI_TRANSLATION_LANGUAGES)
        set_source_files_properties("${MINI_QM_DIR}/mini_${language}.qm"
            PROPERTIES QT_RESOURCE_ALIAS "mini_${language}.qm")
    endforeach()
    qt_add_resources(mini_client "i18n_resources"
        PREFIX "/i18n"
        FILES ${MINI_QM_FILES})
else()
    message(WARNING "lrelease was not found; interface translations will not be built")
endif()

enable_testing()
add_subdirectory(tests)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/net/CMakeLists.txt")
    # Net tests live in their own source folder but reuse the parent variables
    # (Qt/vcpkg bin dirs) and the main build directory.
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/tests/net" net_tests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/storage/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/tests/storage" storage_tests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/auth/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/tests/auth" auth_tests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/crypto/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/tests/crypto" crypto_tests)
endif()

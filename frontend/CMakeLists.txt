cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "ビルドは別のディレクトリで行う（例: cmake -B build）")
endif()

project(fcitx5-pazkey VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(PAZKEY_BUILD_ADDON "fcitx5 アドオンをビルドする" ON)
option(PAZKEY_BUILD_TESTS "単体テストをビルドする" ON)

# ---- data/romaji.tsv をビルド時に組み込む ----
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/romaji.tsv" PAZKEY_ROMAJI_TSV)
configure_file(src/core/romaji_table.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/romaji_table.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/romaji.tsv")

# ---- fcitx5 に依存しない部分 ----
add_library(pazkey-core STATIC
    src/core/utf8.cpp
    src/core/romaji.cpp
    src/core/mozc_defaults.cpp
    src/core/json.cpp
    src/core/config.cpp
    src/core/zenzai_status.cpp
    src/core/protocol.cpp
    src/core/state.cpp
)
target_include_directories(pazkey-core PUBLIC src/core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(pazkey-core PRIVATE -Wall -Wextra)
set_target_properties(pazkey-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 設定の判定を tests/config/check.py から呼ぶための小さな道具（インストールはしない）
add_executable(pazkey-check-config src/tools/check_config.cpp)
target_link_libraries(pazkey-check-config PRIVATE pazkey-core)

# ---- fcitx5 アドオン ----
if(PAZKEY_BUILD_ADDON)
    find_package(Fcitx5Core 5.1 REQUIRED)
    find_package(Fcitx5Module REQUIRED COMPONENTS Notifications)
    include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

    add_library(pazkey MODULE
        src/addon/engine.cpp
        src/addon/client.cpp
        src/addon/fcitx_config.cpp
    )
    target_compile_definitions(pazkey PRIVATE PAZKEY_VERSION="${PROJECT_VERSION}")
    target_link_libraries(pazkey PRIVATE pazkey-core Fcitx5::Core Fcitx5::Module::Notifications)
    target_compile_options(pazkey PRIVATE -Wall -Wextra)
    set_target_properties(pazkey PROPERTIES PREFIX "")
    install(TARGETS pazkey DESTINATION "${FCITX_INSTALL_LIBDIR}/fcitx5")

    configure_file(src/addon/pazkey-addon.conf.in "${CMAKE_CURRENT_BINARY_DIR}/pazkey-addon.conf" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pazkey-addon.conf" RENAME pazkey.conf
            DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
    install(FILES src/addon/pazkey-im.conf RENAME pazkey.conf
            DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")
    # 入力メソッドのアイコン（docs/frontend.md「アイコン」）
    install(FILES data/icons/fcitx_pazkey.svg
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()

# ---- 単体テスト ----
if(PAZKEY_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
    add_executable(pazkey-tests
        tests/utf8_test.cpp
        tests/romaji_test.cpp
        tests/json_test.cpp
        tests/config_test.cpp
        tests/zenzai_notice_test.cpp
        tests/zenzai_status_test.cpp
        tests/state_test.cpp
    )
    target_link_libraries(pazkey-tests PRIVATE pazkey-core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(pazkey-tests)

    if(PAZKEY_BUILD_ADDON)
        # 接続の部分は fcitx5 のイベントループを使うので、アドオンをビルドするときだけ試す。
        add_executable(pazkey-client-tests tests/client_test.cpp src/addon/client.cpp)
        target_include_directories(pazkey-client-tests PRIVATE src/addon)
        target_compile_definitions(pazkey-client-tests PRIVATE PAZKEY_VERSION="${PROJECT_VERSION}")
        target_link_libraries(pazkey-client-tests PRIVATE pazkey-core Fcitx5::Utils GTest::gtest_main)
        gtest_discover_tests(pazkey-client-tests)

        add_executable(pazkey-fcitx-config-tests tests/fcitx_config_test.cpp src/addon/fcitx_config.cpp)
        target_include_directories(pazkey-fcitx-config-tests PRIVATE src/addon)
        target_link_libraries(pazkey-fcitx-config-tests PRIVATE pazkey-core Fcitx5::Config GTest::gtest_main)
        gtest_discover_tests(pazkey-fcitx-config-tests)
    endif()
endif()

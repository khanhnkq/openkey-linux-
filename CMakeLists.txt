cmake_minimum_required(VERSION 3.16)
project(openkey-linux VERSION 0.2.0 LANGUAGES C CXX)

# OpenKey cho Linux — mot engine, hai frontend:
#   fcitx5/   addon chay trong fcitx5 (duong dang dung)
#   wayland/  daemon doc lap openkeyd, khong can fcitx5
# Ca hai dung chung src/openkey_core.* va engine OpenKey trong src/engine/.

include(GNUInstallDirs)
include(CTest)

option(WITH_FCITX5  "Build addon fcitx5"                  ON)
option(WITH_WAYLAND "Build daemon Wayland (openkeyd)"     ON)

# ---------------------------------------------------------------- lop loi
add_library(openkey_core STATIC
    src/openkey_core.cpp
    src/engine/Engine.cpp
    src/engine/Vietnamese.cpp
    src/engine/Macro.cpp
    src/engine/SmartSwitchKey.cpp
    src/engine/ConvertTool.cpp)
target_compile_definitions(openkey_core PUBLIC LINUX=1)
target_include_directories(openkey_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine)
set_target_properties(openkey_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

if(BUILD_TESTING)
    add_executable(openkey_core_test tests/test_core.cpp)
    target_link_libraries(openkey_core_test PRIVATE openkey_core)
    add_test(NAME openkey_core_test COMMAND openkey_core_test)
endif()

# ---------------------------------------------------------------- daemon
if(WITH_WAYLAND)
    add_subdirectory(wayland)
endif()

# ---------------------------------------------------------------- fcitx5
if(WITH_FCITX5)
    find_package(Fcitx5Core 5.0.0 QUIET)
    find_package(Fcitx5Config QUIET)
    find_package(ECM 1.0.0 QUIET)
    if(Fcitx5Core_FOUND AND Fcitx5Config_FOUND AND ECM_FOUND)
        set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
        include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")
        add_subdirectory(fcitx5)
    else()
        message(WARNING
            "Khong tim thay fcitx5 dev — bo qua addon fcitx5. "
            "Cai fcitx5 + extra-cmake-modules, hoac dung -DWITH_FCITX5=OFF.")
    endif()
endif()

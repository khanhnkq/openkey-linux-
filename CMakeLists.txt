cmake_minimum_required(VERSION 3.16)
project(openkey VERSION 0.1.0 LANGUAGES C CXX)

# OpenKey cho Linux — daemon doc lap, KHONG can fcitx5.
#
# Noi truc tiep voi compositor Wayland:
#   zwp_input_method_manager_v2      — nhan phim khi o nhap lieu duoc focus
#   zwp_virtual_keyboard_manager_v1  — bom phim lai ra app
#
# Phan quyet dinh (ghep dau, bang ma, macro, cau hinh) nam o src/openkey_core,
# dung lai nguyen engine cua OpenKey trong src/engine.

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocols)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${GEN_DIR})

function(openkey_protocol stem xml)
    set(hdr ${GEN_DIR}/${stem}-client-protocol.h)
    set(src ${GEN_DIR}/${stem}-protocol.c)
    add_custom_command(
        OUTPUT ${hdr} ${src}
        COMMAND ${WAYLAND_SCANNER} client-header ${PROTO_DIR}/${xml} ${hdr}
        COMMAND ${WAYLAND_SCANNER} private-code ${PROTO_DIR}/${xml} ${src}
        DEPENDS ${PROTO_DIR}/${xml}
        VERBATIM)
    set(OPENKEY_PROTO_HEADERS ${OPENKEY_PROTO_HEADERS} ${hdr} PARENT_SCOPE)
    set(OPENKEY_PROTO_SOURCES ${OPENKEY_PROTO_SOURCES} ${src} PARENT_SCOPE)
endfunction()

openkey_protocol(input-method-unstable-v2 input-method-unstable-v2.xml)
openkey_protocol(virtual-keyboard-unstable-v1 virtual-keyboard-unstable-v1.xml)

set(OPENKEY_CORE_SOURCES
    src/openkey_core.cpp
    src/engine/Engine.cpp
    src/engine/Vietnamese.cpp
    src/engine/Macro.cpp
    src/engine/SmartSwitchKey.cpp
    src/engine/ConvertTool.cpp)

add_executable(openkeyd
    src/main.cpp
    ${OPENKEY_CORE_SOURCES}
    ${OPENKEY_PROTO_SOURCES})
target_compile_definitions(openkeyd PRIVATE LINUX=1)
target_include_directories(openkeyd PRIVATE
    ${GEN_DIR} src src/engine
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS})
target_link_libraries(openkeyd
    ${WAYLAND_CLIENT_LIBRARIES}
    ${XKBCOMMON_LIBRARIES})
set_target_properties(openkeyd PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
install(TARGETS openkeyd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    add_executable(openkey_core_test tests/test_core.cpp ${OPENKEY_CORE_SOURCES})
    target_compile_definitions(openkey_core_test PRIVATE LINUX=1)
    target_include_directories(openkey_core_test PRIVATE src src/engine)
    set_target_properties(openkey_core_test PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    add_test(NAME openkey_core_test COMMAND openkey_core_test)
endif()

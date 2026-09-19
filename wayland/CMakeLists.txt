# Daemon doc lap: noi truc tiep voi compositor qua zwp_input_method_manager_v2
# (nhan phim) va zwp_virtual_keyboard_manager_v1 (bom phim ra app).
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
    set(OPENKEY_PROTO_SOURCES ${OPENKEY_PROTO_SOURCES} ${src} PARENT_SCOPE)
endfunction()

openkey_protocol(input-method-unstable-v2 input-method-unstable-v2.xml)
openkey_protocol(virtual-keyboard-unstable-v1 virtual-keyboard-unstable-v1.xml)

add_executable(openkeyd main.cpp ${OPENKEY_PROTO_SOURCES})
target_include_directories(openkeyd PRIVATE
    ${GEN_DIR} ${WAYLAND_CLIENT_INCLUDE_DIRS} ${XKBCOMMON_INCLUDE_DIRS})
target_link_libraries(openkeyd PRIVATE
    openkey_core ${WAYLAND_CLIENT_LIBRARIES} ${XKBCOMMON_LIBRARIES})
set_target_properties(openkeyd PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

install(TARGETS openkeyd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

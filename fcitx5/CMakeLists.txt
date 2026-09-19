# Addon fcitx5: nhan phim qua InputMethodEngine, xoa bang deleteSurroundingText
# (client co bao surrounding text) hoac forwardKey(BackSpace), roi commitString.
add_fcitx5_addon(openkey openkey.cpp openkey.h)
target_compile_definitions(openkey PRIVATE FCITX_GETTEXT_DOMAIN="fcitx5-openkey")
target_link_libraries(openkey PRIVATE Fcitx5::Core Fcitx5::Config openkey_core)

install(TARGETS openkey DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")

# Khong dung translate (can po/gettext), copy truc tiep .in -> .conf
configure_file(openkey.conf.in openkey.conf COPYONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/openkey.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")

configure_file(openkey-addon.conf.in.in openkey-addon.conf.in)
configure_file("${CMAKE_CURRENT_BINARY_DIR}/openkey-addon.conf.in"
               openkey-addon.conf COPYONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/openkey-addon.conf"
        RENAME openkey.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")

configure_file(openkey.conf.example.in openkey.conf.example COPYONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/openkey.conf.example"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/openkey")

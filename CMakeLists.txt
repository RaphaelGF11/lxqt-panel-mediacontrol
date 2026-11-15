cmake_minimum_required(VERSION 3.16)
project(lxqt-media-widget VERSION 1.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
set(LXQT_MEDIA_QT_MAJOR "5" CACHE STRING "Qt major version used by the target LXQt Panel")
set_property(CACHE LXQT_MEDIA_QT_MAJOR PROPERTY STRINGS "5" "6")
find_package(Qt${LXQT_MEDIA_QT_MAJOR} REQUIRED COMPONENTS Widgets DBus LinguistTools)
find_path(LXQT_PANEL_INCLUDE_DIR ilxqtpanelplugin.h PATH_SUFFIXES lxqt REQUIRED)

file(GLOB catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/translations/mediacontrol_*.ts")
set(resource_xml "<RCC><qresource prefix=\"/lxqt-mediacontrol/i18n\">")
foreach(catalog IN LISTS catalogs)
    get_filename_component(name "${catalog}" NAME_WE)
    set(qm "${CMAKE_CURRENT_BINARY_DIR}/${name}.qm")
    add_custom_command(OUTPUT "${qm}"
        COMMAND Qt${LXQT_MEDIA_QT_MAJOR}::lrelease -nounfinished "${catalog}" -qm "${qm}"
        DEPENDS "${catalog}"
        VERBATIM)
    list(APPEND translations "${qm}")
    string(APPEND resource_xml "<file>${name}.qm</file>")
endforeach()
string(APPEND resource_xml
    "<file alias=\"mediacontrol_zh_Hans.qm\">mediacontrol_zh_CN.qm</file>"
    "<file alias=\"mediacontrol_zh_Hant.qm\">mediacontrol_zh_TW.qm</file></qresource></RCC>")
set(resources "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc")
file(GENERATE OUTPUT "${resources}" CONTENT "${resource_xml}")
set(resource_source "${CMAKE_CURRENT_BINARY_DIR}/mediacontrol_i18n.cpp")
add_custom_command(OUTPUT "${resource_source}"
    COMMAND Qt${LXQT_MEDIA_QT_MAJOR}::rcc -name mediacontrol_i18n -o "${resource_source}" "${resources}"
    DEPENDS "${resources}" ${translations}
    VERBATIM)

add_library(mediacontrol MODULE mediacontrol.cpp "${resource_source}")
set_target_properties(mediacontrol PROPERTIES AUTOMOC ON)
target_compile_features(mediacontrol PRIVATE cxx_std_17)
target_include_directories(mediacontrol PRIVATE "${LXQT_PANEL_INCLUDE_DIR}")
target_link_libraries(mediacontrol PRIVATE Qt${LXQT_MEDIA_QT_MAJOR}::Widgets Qt${LXQT_MEDIA_QT_MAJOR}::DBus)

install(TARGETS mediacontrol LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lxqt-panel")
install(FILES mediacontrol.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/lxqt/lxqt-panel")

cmake_minimum_required(VERSION 3.16)
project(lxqt-media-widget VERSION 1.1.1 LANGUAGES CXX)

include(GNUInstallDirs)
set(LXQT_MEDIA_QT_MAJOR "5" CACHE STRING "Qt major version used by the target LXQt Panel")
set_property(CACHE LXQT_MEDIA_QT_MAJOR PROPERTY STRINGS "5" "6")
find_package(Qt${LXQT_MEDIA_QT_MAJOR} REQUIRED COMPONENTS Widgets DBus)
find_path(LXQT_PANEL_INCLUDE_DIR ilxqtpanelplugin.h PATH_SUFFIXES lxqt REQUIRED)

add_library(mediacontrol MODULE mediacontrol.cpp)
set_target_properties(mediacontrol PROPERTIES AUTOMOC ON)
target_compile_features(mediacontrol PRIVATE cxx_std_17)
target_include_directories(mediacontrol PRIVATE "${LXQT_PANEL_INCLUDE_DIR}")
target_link_libraries(mediacontrol PRIVATE Qt${LXQT_MEDIA_QT_MAJOR}::Widgets Qt${LXQT_MEDIA_QT_MAJOR}::DBus)

install(TARGETS mediacontrol LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/lxqt-panel")
install(FILES mediacontrol.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/lxqt/lxqt-panel")

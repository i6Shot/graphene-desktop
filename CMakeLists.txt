# graphene-desktop
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the Apache License Version 2.0.

cmake_minimum_required(VERSION 3.4)
project(graphene-desktop)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "default install path" FORCE)
endif()

# Config
set(GRAPHENE_DATA_DIR ${CMAKE_INSTALL_PREFIX}/share/graphene) # Must end without a slash
configure_file(${PROJECT_SOURCE_DIR}/graphene-panel.desktop.in ${PROJECT_SOURCE_DIR}/data/autostart/graphene-panel.desktop)
configure_file(${PROJECT_SOURCE_DIR}/graphene-wm.desktop.in ${PROJECT_SOURCE_DIR}/data/autostart/graphene-wm.desktop)
configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_SOURCE_DIR}/config.h)
include_directories(${PROJECT_SOURCE_DIR})

# Acquire libraries needed for all targets
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0>=2.10)
link_libraries(${GLIB2_LIBRARIES})
include_directories(${GLIB2_INCLUDE_DIRS})

# Setup target for the window manager
file(GLOB wm_SRC "wm/*.c" "wm/*.h")
add_executable(graphene-wm ${wm_SRC})
pkg_check_modules(LIBMUTTER REQUIRED libmutter>=3.00)
target_link_libraries(graphene-wm ${LIBMUTTER_LIBRARIES})
target_include_directories(graphene-wm PRIVATE ${LIBMUTTER_INCLUDE_DIRS})

# Setup target for the session manager
file(GLOB session_SRC "session/*.c" "session/*.h")
add_executable(graphene-session ${session_SRC})
pkg_check_modules(GIOUNIX2 REQUIRED gio-unix-2.0>=2.10)
target_link_libraries(graphene-session ${GIOUNIX2_LIBRARIES})
target_include_directories(graphene-session PRIVATE ${GIOUNIX2_INCLUDE_DIRS})

# Setup target for libvos
file(GLOB libvos_SRC "lib/*.c" "lib/*.h")
add_library(vos SHARED ${libvos_SRC})
# set_target_properties(vos PROPERTIES SOVERSION 1.0)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0>=3.10)
pkg_check_modules(LIBPEAS REQUIRED libpeas-1.0>=1.16)
pkg_check_modules(LIBPULSE REQUIRED libpulse>=8.0)
pkg_check_modules(LIBPULSEGLIB REQUIRED libpulse-mainloop-glib>=8.0)
target_include_directories(vos PRIVATE ${GTK3_INCLUDE_DIRS} ${LIBPEAS_INCLUDE_DIRS} ${LIBPULSE_INCLUDE_DIRS} ${LIBPULSEGLIB_INCLUDE_DIRS})

find_package(GObjectIntrospection REQUIRED)
add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/Vos-1.0.gir
    DEPENDS vos
    COMMAND ${INTROSPECTION_SCANNER} --include GObject-2.0 --include Gtk-3.0 --pkg gtk+-3.0 --pkg libpulse --pkg libpulse-mainloop-glib --pkg libpeas-1.0 --pkg-export libvos --library-path ${PROJECT_SOURCE_DIR}/lib --no-libtool --identifier-prefix=Vos --symbol-prefix=vos --namespace Vos --nsversion=1.0 --library vos --warn-all -o ${PROJECT_BINARY_DIR}/Vos-1.0.gir ${libvos_SRC}
)
add_custom_command(
    OUTPUT ${PROJECT_BINARY_DIR}/Vos-1.0.typelib
    DEPENDS ${PROJECT_BINARY_DIR}/Vos-1.0.gir
    COMMAND ${INTROSPECTION_COMPILER} ${PROJECT_BINARY_DIR}/Vos-1.0.gir -o ${PROJECT_BINARY_DIR}/Vos-1.0.typelib
)
add_custom_target(Vos-1.0-gir ALL DEPENDS ${PROJECT_BINARY_DIR}/Vos-1.0.typelib)

# Setup target for the panel executable
add_executable(graphene-panel panel/panel-main.c)
target_include_directories(graphene-panel PRIVATE ${GTK3_INCLUDE_DIRS})
target_link_libraries(graphene-panel vos ${GTK3_LIBRARIES} ${LIBPEAS_LIBRARIES} ${LIBPULSE_LIBRARIES} ${LIBPULSEGLIB_LIBRARIES})

# Install
install(TARGETS graphene-wm graphene-panel DESTINATION ${GRAPHENE_DATA_DIR})
install(TARGETS graphene-session DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(TARGETS vos DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(DIRECTORY data/ DESTINATION ${GRAPHENE_DATA_DIR}) # Slash after 'data' makes it only install contents of directory
install(FILES ${PROJECT_BINARY_DIR}/Vos-1.0.gir DESTINATION ${CMAKE_INSTALL_PREFIX}/share/gir-1.0)
install(FILES ${PROJECT_BINARY_DIR}/Vos-1.0.typelib DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/girepository-1.0)
install(FILES graphene.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/xsessions)

# Uninstall target (only works after installing with install_manifest.txt)
# Removes all files listed in install_manifest.txt, and then removes install_manifest.txt
# If install_manifest.txt is not available, an error message is printed.
add_custom_target(uninstall COMMAND { [ -f install_manifest.txt ] && xargs --verbose -d '\\n' rm < install_manifest.txt && rm install_manifest.txt\; } || echo ' *** ERROR: Missing install_manifest.txt: you must install first!' )
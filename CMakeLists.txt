# This file is part of graphene-desktop, the desktop environment of VeltOS.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the Apache License Version 2.0.

cmake_minimum_required(VERSION 3.4)
project(graphene-desktop)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(GRAPHENE_VERSION 0.2.5)
set(GRAPHENE_DEBUG TRUE)

# Assume the user wants to install Graphene to /usr unless otherwise specified
# with the -DCMAKE_INSTALL_PREFIX=<path> flag to cmake. http://stackoverflow.com/a/16076855
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

# status-notifier-dbus-ifaces is used by multiple targets, so put the generator here
add_custom_command(
  OUTPUT ${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c ${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.h
  COMMAND gdbus-codegen --interface-prefix org --c-namespace DBus --generate-c-code status-notifier-dbus-ifaces ${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.xml
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS ${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.xml
)
add_custom_target(status-notifier-codegen DEPENDS ${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c ${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.h)

# Setup target for the window manager
file(GLOB wm_SRC "wm/*.c")
add_executable(graphene-wm ${wm_SRC})
pkg_check_modules(LIBMUTTER REQUIRED libmutter>=3.00)
pkg_check_modules(LIBPULSEGLIB REQUIRED libpulse-mainloop-glib>=8.0)
pkg_check_modules(LIBSETTINGS REQUIRED libsettings)
target_link_libraries(graphene-wm ${LIBMUTTER_LIBRARIES} ${LIBPULSEGLIB_LIBRARIES} ${LIBSETTINGS_LIBRARIES})
target_include_directories(graphene-wm PRIVATE ${LIBMUTTER_INCLUDE_DIRS} ${LIBPULSEGLIB_INCLUDE_DIRS} ${LIBSETTINGS_INCLUDE_DIRS})

# Setup target for the session manager
add_subdirectory(session)

# Setup target for the panel executable
add_subdirectory(panel)

# Install
install(TARGETS graphene-wm DESTINATION ${GRAPHENE_DATA_DIR})
# install(TARGETS graphene DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(DIRECTORY data/ DESTINATION ${GRAPHENE_DATA_DIR}) # Slash after 'data' makes it only install contents of directory
# install(FILES ${PROJECT_BINARY_DIR}/Graphene-${GRAPHENE_VERSION}.gir DESTINATION ${CMAKE_INSTALL_PREFIX}/share/gir-1.0)
# install(FILES ${PROJECT_BINARY_DIR}/Graphene-${GRAPHENE_VERSION}.typelib DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/girepository-1.0)
install(FILES graphene.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/xsessions)

install(DIRECTORY schemas/ DESTINATION ${CMAKE_INSTALL_PREFIX}/share/glib-2.0/schemas)

if(NOT DONT_COMPILE_SCHEMAS)
  install(CODE "execute_process(COMMAND make compile-schemas)") # There is probably a better way to do this...
endif()

# Uninstall target (only works after installing with install_manifest.txt)
# Removes all files listed in install_manifest.txt, and then removes install_manifest.txt
# If install_manifest.txt is not available, an error message is printed.
add_custom_target(uninstall COMMAND { [ -f install_manifest.txt ] && xargs --verbose -d '\\n' rm < install_manifest.txt && rm install_manifest.txt\; } || echo ' *** ERROR: Missing install_manifest.txt: you must install first!' )

# Convenience target to compile glib schemas (usually post-install and post-uninstall)
add_custom_target(compile-schemas COMMAND glib-compile-schemas \"${CMAKE_INSTALL_PREFIX}/share/glib-2.0/schemas\" )

# Force install - kills running process, runs install
add_custom_target(finstall COMMAND killall graphene-wm graphene-panel graphene-session nautilus gnome-settings-daemon\; make install )
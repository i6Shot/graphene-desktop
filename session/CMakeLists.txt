# This file is part of graphene-desktop, the desktop environment of VeltOS.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the WTFPL.

add_custom_command(
  OUTPUT session-dbus-iface.c session-dbus-iface.h
  COMMAND gdbus-codegen --interface-prefix org.gnome --c-namespace DBus --generate-c-code session-dbus-iface ${CMAKE_CURRENT_SOURCE_DIR}/session-dbus-iface.xml
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_command(OUTPUT "${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c" DEPENDS status-notifier-codegen)

pkg_check_modules(GIOUNIX2 REQUIRED gio-unix-2.0>=2.10)
pkg_check_modules(LIBMUTTER REQUIRED libmutter>=3.22)
pkg_check_modules(LIBPULSEGLIB REQUIRED libpulse-mainloop-glib>=8.0)
pkg_check_modules(POLKITAGENT REQUIRED polkit-agent-1)
link_directories(${LIBMUTTER_LIBRARY_DIRS})

add_executable(graphene-session
	main.c
	session-dbus-iface.c
	session2.c
	client.c
	# status-notifier-watcher.c
	# ${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c
	wmwidgets/percent-floater.c
	util.c
	wm.c
	wmwidgets/background.c
	# ${PROJECT_SOURCE_DIR}/common/dbusmonitor.c
	${PROJECT_SOURCE_DIR}/common/sound.c
)
target_link_libraries(graphene-session
	${GIOUNIX2_LIBRARIES}
	${LIBMUTTER_LIBRARIES}
	${LIBPULSEGLIB_LIBRARIES}
	${POLKITAGENT_LIBRARIES}
)
target_include_directories(graphene-session PRIVATE
	${CMAKE_CURRENT_BINARY_DIR} 
	${GIOUNIX2_INCLUDE_DIRS}
	${LIBMUTTER_INCLUDE_DIRS}
	${LIBPULSEGLIB_INCLUDE_DIRS}
	${POLKITAGENT_INCLUDE_DIRS}
)

# Normally, the CMake install functionality removes the executable's rpath,
# which here includes /usr/lib/mutter. This keeps the rpath.
set_target_properties(graphene-session PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS graphene-session DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

add_custom_command(
  OUTPUT notifications-dbus-iface.c notifications-dbus-iface.h
  COMMAND gdbus-codegen --interface-prefix org.freedesktop --c-namespace DBus --generate-c-code notifications-dbus-iface ${CMAKE_CURRENT_SOURCE_DIR}/notifications-dbus-iface.xml
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_command(OUTPUT "${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c" DEPENDS status-notifier-codegen)

file(GLOB panel_SRC "*.c" "*/*.c")
add_executable(graphene-panel ${panel_SRC} "notifications-dbus-iface.c" "${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c")
pkg_check_modules(GTK3 REQUIRED gtk+-3.0>=3.10)
pkg_check_modules(LIBPULSE REQUIRED libpulse>=8.0)
pkg_check_modules(LIBPULSEGLIB REQUIRED libpulse-mainloop-glib>=8.0)
pkg_check_modules(LIBGNOMEMENU REQUIRED libgnome-menu-3.0>=3.13)
pkg_check_modules(LIBWNCK REQUIRED libwnck-3.0>=3.14)
pkg_check_modules(LIBACT REQUIRED accountsservice>=0.6)
target_include_directories(graphene-panel PRIVATE . ${CMAKE_CURRENT_BINARY_DIR} ${GTK3_INCLUDE_DIRS} ${LIBPULSE_INCLUDE_DIRS} ${LIBPULSEGLIB_INCLUDE_DIRS} ${LIBGNOMEMENU_INCLUDE_DIRS} ${LIBWNCK_INCLUDE_DIRS} ${LIBACT_INCLUDE_DIRS})
target_link_libraries(graphene-panel ${GTK3_LIBRARIES} ${LIBPULSE_LIBRARIES} ${LIBPULSEGLIB_LIBRARIES} ${LIBGNOMEMENU_LIBRARIES} ${LIBWNCK_LIBRARIES} ${LIBACT_LIBRARIES})

install(TARGETS graphene-panel DESTINATION ${GRAPHENE_DATA_DIR})
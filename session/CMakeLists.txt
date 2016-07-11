add_custom_command(OUTPUT "${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c" DEPENDS status-notifier-codegen)

file(GLOB session_SRC "*.c")
add_executable(graphene-session ${session_SRC} "${PROJECT_SOURCE_DIR}/status-notifier-dbus-ifaces.c")
pkg_check_modules(GIOUNIX2 REQUIRED gio-unix-2.0>=2.10)
target_include_directories(graphene-session PRIVATE . ${GIOUNIX2_INCLUDE_DIRS})
target_link_libraries(graphene-session ${GIOUNIX2_LIBRARIES})

install(TARGETS graphene-session DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
file(GLOB wm_SRC "*.c")
add_executable(graphene-wm ${wm_SRC})
pkg_check_modules(LIBMUTTER REQUIRED libmutter>=3.00)
pkg_check_modules(LIBPULSEGLIB REQUIRED libpulse-mainloop-glib>=8.0)
pkg_check_modules(LIBSETTINGS REQUIRED libsettings)
target_link_libraries(graphene-wm
	${LIBMUTTER_LIBRARIES}
	${LIBPULSEGLIB_LIBRARIES}
	${LIBSETTINGS_LIBRARIES}
)
target_include_directories(graphene-wm PRIVATE
	${LIBMUTTER_INCLUDE_DIRS}
	${LIBPULSEGLIB_INCLUDE_DIRS}
	${LIBSETTINGS_INCLUDE_DIRS}
)

install(TARGETS graphene-wm DESTINATION ${GRAPHENE_DATA_DIR})
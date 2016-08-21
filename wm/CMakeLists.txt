file(GLOB wm_SRC "*.c")
file(GLOB common_SRC "${PROJECT_SOURCE_DIR}/common/*.c")
add_executable(graphene-wm ${wm_SRC} ${common_SRC})
pkg_check_modules(LIBMUTTER REQUIRED libmutter>=3.00)
pkg_check_modules(LIBPULSEGLIB REQUIRED libpulse-mainloop-glib>=8.0)
target_link_libraries(graphene-wm
	${LIBMUTTER_LIBRARIES}
	${LIBPULSEGLIB_LIBRARIES}
)
target_include_directories(graphene-wm PRIVATE
	${PROJECT_SOURCE_DIR}
	${LIBMUTTER_INCLUDE_DIRS}
	${LIBPULSEGLIB_INCLUDE_DIRS}
)

install(TARGETS graphene-wm DESTINATION ${GRAPHENE_DATA_DIR})
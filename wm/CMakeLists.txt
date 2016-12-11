# This file is part of graphene-desktop, the desktop environment of VeltOS.
# Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
# This file is licensed under the WTFPL.

pkg_check_modules(LIBMUTTER REQUIRED libmutter>=3.22)
pkg_check_modules(LIBPULSEGLIB REQUIRED libpulse-mainloop-glib>=8.0)
link_directories(${LIBMUTTER_LIBRARY_DIRS})

add_executable(graphene-wm
	wm.c
	dialog.c
	background.c
	${PROJECT_SOURCE_DIR}/common/dbusmonitor.c
	${PROJECT_SOURCE_DIR}/common/sound.c
)
target_link_libraries(graphene-wm
	${LIBMUTTER_LIBRARIES}
	${LIBPULSEGLIB_LIBRARIES}
)
target_include_directories(graphene-wm PRIVATE
	${LIBMUTTER_INCLUDE_DIRS}
	${LIBPULSEGLIB_INCLUDE_DIRS}
)

# Normally, the CMake install functionality removes the executable's rpath,
# which here includes /usr/lib/mutter. This keeps the rpath.
set_target_properties(graphene-wm PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS graphene-wm DESTINATION ${GRAPHENE_DATA_DIR})

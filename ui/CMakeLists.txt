SET(LIBNAME "nemoui")

PKG_CHECK_MODULES(NEMOPLAY REQUIRED nemoplay)
INCLUDE_DIRECTORIES(${NEMOPLAY_INCLUDE_DIRS})
LINK_DIRECTORIES(${NEMOPLAY_LIBRARY_DIRS})

ADD_LIBRARY(${LIBNAME} SHARED
    nemoui-misc.h
    nemoui-graph.h
    nemoui-player.c
    nemoui-player.h
    nemoui-image.cpp
    nemoui-image.h
    nemoui-thread.c
    nemoui-thread.h
    nemoui-path.c
    nemoui-path.h
    nemoui-gesture.c
    nemoui-gesture.h
    nemoui-drawer.c
    nemoui-drawer.h
    nemoui-button.c
    nemoui-button.h
    nemoui-text.c
    nemoui-text.h
    nemoui.h
    $<TARGET_OBJECTS:helper_skia>
    )
TARGET_LINK_LIBRARIES(${LIBNAME}
    ${NEMOAPPS_COMMON_LIBRARIES}
    ${NEMOPLAY_LIBRARIES}
	)

INSTALL(TARGETS ${LIBNAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
INSTALL(FILES
    nemoui-misc.h
    nemoui-graph.h
    nemoui-player.h
    nemoui-image.h
    nemoui-thread.h
    nemoui-path.h
    nemoui-gesture.h
    nemoui-drawer.h
    nemoui-button.h
    nemoui-text.h
    nemoui.h
    DESTINATION ${CMAKE_INSTALL_INCDIR}/${LIBNAME}
    )
CONFIGURE_FILE(${LIBNAME}.pc.in ${LIBNAME}.pc @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LIBNAME}.pc
    DESTINATION ${CMAKE_INSTALL_PKGDIR}
    )

SET(RES_DIR "${DATA_DIR}/ui/res")
ADD_DEFINITIONS(-DRES_DIR="${RES_DIR}")
INSTALL(DIRECTORY
    ${CMAKE_CURRENT_SOURCE_DIR}/res/
    DESTINATION ${RES_DIR}/
    )

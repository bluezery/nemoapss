SET(CONFXML "3d.conf")
SET(APPNAME "nemo3d")
ADD_DEFINITIONS(-DAPPNAME="${APPNAME}")
ADD_DEFINITIONS(-DCONFXML="${CONFXML}")

ADD_EXECUTABLE(${APPNAME}
    3d.cpp
    $<TARGET_OBJECTS:meshhelper>
    $<TARGET_OBJECTS:fbohelper>
    ${CMAKE_SOURCE_DIR}/thirds/tiny_obj_loader.cc
    ${CMAKE_SOURCE_DIR}/thirds/tiny_obj_loader.h
	)
ADD_DEPENDENCIES(${APPNAME} xemoutil nemowidget)

TARGET_LINK_LIBRARIES(${APPNAME}
    xemoutil
    nemowidget
    ${NEMOAPPS_COMMON_LIBRARIES}
    ${GL_LIBRARIES}
	)

INSTALL(TARGETS ${APPNAME}
    DESTINATION bin
    )

CONFIGURE_FILE(${CONFXML}.in ${CONFXML} @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CONFXML}
    DESTINATION ${ETC_DIR}
    )

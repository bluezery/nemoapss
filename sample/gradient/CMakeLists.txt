SET(CONFXML "test_gradient.conf")
SET(APPNAME "nemotest_gradient")

ADD_DEFINITIONS(-DAPPNAME="${APPNAME}")
ADD_DEFINITIONS(-DCONFXML="${CONFXML}")

ADD_EXECUTABLE(${APPNAME}
    gradient.c
	)
ADD_DEPENDENCIES(${APPNAME} xemoutil nemowidget)

TARGET_LINK_LIBRARIES(${APPNAME}
    xemoutil
    nemowidget
    ${NEMOAPPS_COMMON_LIBRARIES}
	)

INSTALL(TARGETS ${APPNAME}
    DESTINATION bin
    )

CONFIGURE_FILE(${CONFXML}.in ${CONFXML} @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CONFXML}
    DESTINATION ${ETC_DIR}
    )

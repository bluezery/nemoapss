SET(APP "udevd")
SET(APPID "${NEMO_DEVID}${APP}")
SET(APPNAME "${APPID}")
SET(APPDATA_ROOT "${APPROOT}/data")
ADD_DEFINITIONS(-DAPPNAME="${APPNAME}")

ADD_EXECUTABLE(${APPNAME}
    ${APP}.c
	)
ADD_DEPENDENCIES(${APPNAME} xemoutil nemowidget nemoui)
TARGET_LINK_LIBRARIES(${APPNAME} xemoutil nemowidget nemoui
    ${NEMOAPPS_COMMON_LIBRARIES}
    )
INSTALL(TARGETS ${APPNAME}
    DESTINATION bin
    )

SET(CONFXML "${APP}.conf")
ADD_DEFINITIONS(-DCONFXML="${CONFXML}")
CONFIGURE_FILE(${CONFXML}.in ${CONFXML} @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CONFXML}
    DESTINATION ${ETC_DIR}
    )

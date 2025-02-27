SET(APP "filebrowser")
SET(APPID "${NEMO_DEVID}.${APP}")
SET(APPNAME "${APPID}")
SET(APPROOT "${NEMO_ROOT}/${APPNAME}")
SET(APPDATA_ROOT "${APPROOT}/data")
ADD_DEFINITIONS(-DAPPNAME="${APPNAME}")

ADD_EXECUTABLE(${APPNAME}
    ${APP}.c
    $<TARGET_OBJECTS:helper_skia>
    )
SET_TARGET_PROPERTIES(${APPNAME} PROPERTIES OUTPUT_NAME "exec")
ADD_DEPENDENCIES(${APPNAME} xemoapp xemoutil nemowidget nemoui)
TARGET_LINK_LIBRARIES(${APPNAME} xemoapp xemoutil nemowidget nemoui 
    ${NEMOAPPS_COMMON_LIBRARIES}
    )
INSTALL(TARGETS ${APPNAME}
    DESTINATION ${APPROOT}
    )

SET(APP_RES_DIR "${APPDATA_ROOT}/res")
ADD_DEFINITIONS(-DAPP_RES_DIR="${APP_RES_DIR}")
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/res/
    DESTINATION  ${APP_RES_DIR}
    )

SET(CONFXML "${APP}.conf")
ADD_DEFINITIONS(-DCONFXML="${CONFXML}")
CONFIGURE_FILE(${CONFXML}.in ${CONFXML} @ONLY)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${CONFXML}
    DESTINATION ${ETC_DIR}
    )

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
    DESTINATION  ${APPROOT}
    )
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons
    DESTINATION  ${APPROOT}
    )

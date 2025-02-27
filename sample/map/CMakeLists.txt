SET(CONFXML "test_map.conf")
SET(APPNAME "nemotest_map")

ADD_DEFINITIONS(-DAPPNAME="${APPNAME}")
ADD_DEFINITIONS(-DCONFXML="${CONFXML}")

ADD_EXECUTABLE(${APPNAME}
    map.c
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

SET(MAP_BG_DIR "${DATA_DIR}/icons/test_map/bg/")
ADD_DEFINITIONS(-DMAP_BG_DIR="${MAP_BG_DIR}")
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons/bg/
    DESTINATION ${MAP_BG_DIR}
    )

SET(MAP_TEXT_DIR "${DATA_DIR}/icons/test_map/text/")
ADD_DEFINITIONS(-DMAP_TEXT_DIR="${MAP_TEXT_DIR}")
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons/text/
    DESTINATION ${MAP_TEXT_DIR}
    )

SET(SIGN_DIR "${DATA_DIR}/icons/test_map/sign/")
ADD_DEFINITIONS(-DSIGN_DIR="${SIGN_DIR}")
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons/sign/
    DESTINATION ${SIGN_DIR}
    )

SET(BACK_DIR "${DATA_DIR}/icons/test_map/back/")
ADD_DEFINITIONS(-DBACK_DIR="${BACK_DIR}")
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons/back/
    DESTINATION ${BACK_DIR}
    )

SET(MAP_DATA_DIR "${DATA_DIR}/datas/test_map/")
ADD_DEFINITIONS(-DMAP_DATA_DIR="${MAP_DATA_DIR}")
SET(MAP_DATA_FILE "data.csv")
ADD_DEFINITIONS(-DMAP_DATA_FILE="${MAP_DATA_FILE}")
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/datas/${MAP_DATA_FILE}
    DESTINATION ${MAP_DATA_DIR}
    )

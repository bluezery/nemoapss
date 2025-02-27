SET(APP "pdf")
SET(APPID "${NEMO_DEVID}.${APP}")
SET(APPNAME "${APPID}")
SET(APPROOT "${NEMO_ROOT}/${APPNAME}")
SET(APPDATA_ROOT "${APPROOT}/data")
ADD_DEFINITIONS(-DAPPNAME="${APPNAME}")

PKG_CHECK_MODULES(MUPDF mupdf libopenjp2)
FIND_LIBRARY(JBIG2DEC0_LIBRARY NAMES libjbig2dec.so)
FIND_LIBRARY(MUJS_LIBRARY NAMES libmujs.a)
SET(MUPDF_LIBRARIES ${MUPDF_LIBRARIES} ${JBIG2DEC0_LIBRARY} ${MUJS_LIBRARY})
INCLUDE_DIRECTORIES(${MUPDF_INCLUDE_DIRS})
LINK_DIRECTORIES(${MUPDF_LIBRARY_DIRS})

ADD_EXECUTABLE(${APPNAME}
    ${APP}.c
	)
SET_TARGET_PROPERTIES(${APPNAME} PROPERTIES OUTPUT_NAME "exec")
ADD_DEPENDENCIES(${APPNAME} xemoutil nemowidget nemoui)
TARGET_LINK_LIBRARIES(${APPNAME} xemoutil nemowidget nemoui
    ${NEMOAPPS_COMMON_LIBRARIES}
    ${MUPDF_LIBRARIES}
    jbig2dec
    z
    mujs
    )
INSTALL(TARGETS ${APPNAME}
    DESTINATION ${APPROOT}
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

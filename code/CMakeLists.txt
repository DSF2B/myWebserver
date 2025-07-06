set(SOURCE_FILES
    buffer/buffer.cpp
    http/httpconn.cpp
    http/httprequest.cpp
    http/httpresponse.cpp
    log/log.cpp
    pool/sqlconnpool.cpp
    server/epoller.cpp
    server/webserver.cpp
    timer/heaptimer.cpp
)

set(HEADER_FILES
    buffer/buffer.h
    http/httpconn.h
    http/httprequest.h
    http/httpresponse.h
    log/blockqueue.h
    log/log.h
    pool/sqlconnpool.h
    pool/sqlconnRAII.h
    pool/threadpool.h
    server/epoller.h
    server/webserver.h
    timer/heaptimer.h
)

add_library(webserver_core STATIC ${SOURCE_FILES} ${HEADER_FILES})

target_include_directories(webserver_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer
    ${CMAKE_CURRENT_SOURCE_DIR}/http
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/pool
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/timer
    ${MySQL_INCLUDE_DIR}
)

target_compile_definitions(webserver_core PUBLIC 
    USE_SQL_CONN_POOL
)

target_link_libraries(webserver_core PUBLIC 
    rt 
    pthread
)
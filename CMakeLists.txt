cmake_minimum_required(VERSION 3.22)

project(myWeberver)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_BUILD_TYPE "Debug")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR})

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

include_directories(
    ${PROJECT_SOURCE_DIR}/code/buffer
    ${PROJECT_SOURCE_DIR}/code/http
    ${PROJECT_SOURCE_DIR}/code/log
    ${PROJECT_SOURCE_DIR}/code/pool
    ${PROJECT_SOURCE_DIR}/code/server
    ${PROJECT_SOURCE_DIR}/code/timer
)

# set(SOURCE_FILES
#     ${PROJECT_SOURCE_DIR}/code/buffer/buffer.cpp
#     ${PROJECT_SOURCE_DIR}/code/http/httpconn.cpp
#     ${PROJECT_SOURCE_DIR}/code/http/httprequest.cpp
#     ${PROJECT_SOURCE_DIR}/code/http/httpresponse.cpp
#     ${PROJECT_SOURCE_DIR}/code/log/log.cpp
#     ${PROJECT_SOURCE_DIR}/code/pool/sqlconnpool.cpp
#     ${PROJECT_SOURCE_DIR}/code/server/epoller.cpp
#     ${PROJECT_SOURCE_DIR}/code/server/subreactor.cpp
#     ${PROJECT_SOURCE_DIR}/code/server/webserver.cpp
#     ${PROJECT_SOURCE_DIR}/code/server/subreactor.cpp
#     ${PROJECT_SOURCE_DIR}/code/timer/heaptimer.cpp
# )
file(GLOB SOURCE_FILES      # 非递归
    ${PROJECT_SOURCE_DIR}/code/*.cpp
    ${PROJECT_SOURCE_DIR}/code/*/*.cpp
)

add_library(webserver_core SHARED ${SOURCE_FILES})
# add_library(webserver_core STATIC ${SOURCE_FILES})

target_link_libraries(
    webserver_core
    rt
    pthread 
    mysqlclient
)

add_executable(myWebserver ${PROJECT_SOURCE_DIR}/code/main.cpp)
target_link_directories(
    myWebserver PUBLIC
    ${PROJECT_SOURCE_DIR}/lib
)
target_link_libraries(
    myWebserver
    webserver_core.so
)
# target_link_libraries(
#     myWebserver
#     webserver_core.a
# )

# add_executable(testLog ${PROJECT_SOURCE_DIR}/test/test.cpp)
# target_link_directories(
#     testLog PUBLIC
#     ${PROJECT_SOURCE_DIR}/lib
# )
# target_link_libraries(
#     testLog
#     webserver_core.so
# )
# target_link_libraries(
#     testLog
#     webserver_core.a
# )
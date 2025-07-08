cmake_minimum_required(VERSION 3.22)

project(myWeberver)

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_BUILD_TYPE "Debug")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

set(LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)

find_package(Threads REQUIRED)

add_subdirectory(code)        # 核心代码

add_executable(${PROJECT_NAME} 
    ${CMAKE_SOURCE_DIR}/code/main.cpp  # 明确指定路径
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    webserver_core      
    Threads::Threads     
    mysqlclient
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/code
    ${MySQL_INCLUDE_DIR}
)

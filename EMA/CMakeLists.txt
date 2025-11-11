# 设置CMake最低版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(EMA)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)

FILE(GLOB SRC ./*.cpp ./*.c)


add_executable(${PROJECT_NAME}
    main.cpp
    ${SRC}    
)

target_include_directories(${PROJECT_NAME}
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_directories(${PROJECT_NAME}
PRIVATE
    
)

target_link_libraries(${PROJECT_NAME}
PRIVATE
    pthread
)

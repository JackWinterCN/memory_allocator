# 设置CMake最低版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(MNNMemoryAllocator)

FILE(GLOB SRC ./*.cpp ./*.c core/*.cpp  core/*.c MNN/*.cpp MNN/*.c)


add_executable(${PROJECT_NAME}
    main.cpp
    ${SRC}    
)

target_include_directories(${PROJECT_NAME}
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/MNN
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)

target_link_directories(${PROJECT_NAME}
PRIVATE
    
)

target_link_libraries(${PROJECT_NAME}

)

cmake_minimum_required (VERSION 2.8)

project(example)
add_executable(${PROJECT_NAME} example.c glfw/deps/glad.c)
add_definitions( "-D _CRT_SECURE_NO_WARNINGS" )
add_subdirectory(glfw)
include_directories(${PROJECT_SOURCE_DIR})
include_directories("glfw/deps") # for glad
include_directories("glfw/include")
target_link_libraries(${PROJECT_NAME} glfw ${GLFW_LIBRARIES})
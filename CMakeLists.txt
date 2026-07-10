cmake_minimum_required(VERSION 3.10)
project(leonida_lights LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(GLEW REQUIRED glew)

add_executable(gta6_clone src/main.cpp)
target_include_directories(gta6_clone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLFW_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
)
target_link_libraries(gta6_clone PRIVATE
    OpenGL::GL ${GLFW_LIBRARIES} ${GLEW_LIBRARIES} GL dl pthread
)
target_compile_options(gta6_clone PRIVATE -Wall -Wextra -O2)

add_executable(gta6_server src/server.cpp)
target_include_directories(gta6_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(gta6_server PRIVATE pthread)
target_compile_options(gta6_server PRIVATE -Wall -Wextra -O2)

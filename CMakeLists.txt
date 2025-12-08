cmake_minimum_required(VERSION 3.10)
project(image_cluster C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

add_executable(image_cluster src/main.c)

target_link_libraries(image_cluster m)

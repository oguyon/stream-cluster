cmake_minimum_required(VERSION 3.10)
project(image_cluster C)

set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -funroll-loops")

find_package(PkgConfig REQUIRED)

# Optional Dependencies
pkg_check_modules(CFITSIO cfitsio)
if (CFITSIO_FOUND)
    add_definitions(-DUSE_CFITSIO)
    message(STATUS "CFITSIO found. FITS format supported.")
else()
    message(WARNING "CFITSIO not found. FITS format disabled.")
endif()

find_package(PNG)
if (PNG_FOUND)
    add_definitions(-DUSE_PNG)
    message(STATUS "PNG found. PNG output supported.")
else()
    message(WARNING "PNG not found. PNG output disabled.")
endif()

pkg_check_modules(FFMPEG libavformat libavcodec libswscale libavutil)
if (FFMPEG_FOUND)
    add_definitions(-DUSE_FFMPEG)
    message(STATUS "FFmpeg found. Video input supported.")
else()
    message(WARNING "FFmpeg not found. Video input disabled.")
endif()

# Check for ImageStreamIO via pkg-config
pkg_check_modules(IMAGESTREAMIO ImageStreamIO)
if (IMAGESTREAMIO_FOUND)
    add_definitions(-DUSE_IMAGESTREAMIO)
    message(STATUS "ImageStreamIO found. Streaming input supported.")
else()
    message(WARNING "ImageStreamIO not found. Streaming input disabled.")
endif()

include_directories(
    ${CFITSIO_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${IMAGESTREAMIO_INCLUDE_DIRS}
    src
)

link_directories(
    ${CFITSIO_LIBRARY_DIRS}
    ${PNG_LIBRARY_DIRS}
    ${FFMPEG_LIBRARY_DIRS}
    ${IMAGESTREAMIO_LIBRARY_DIRS}
)

# Sources
set(CLUSTER_SRCS src/main.c src/cluster_core.c src/cluster_io.c src/framedistance.c src/frameread.c src/png_io.c)

add_executable(image-cluster ${CLUSTER_SRCS})
target_link_libraries(image-cluster
    ${CFITSIO_LIBRARIES}
    ${PNG_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${IMAGESTREAMIO_LIBRARIES}
    m
)



add_executable(image-cluster-mktxtseq src/mktestseq.c)
target_link_libraries(image-cluster-mktxtseq m)

add_executable(image-cluster-plot src/plot_clusters.c)
target_link_libraries(image-cluster-plot ${PNG_LIBRARIES} m)

add_executable(image-cluster-NDmodel src/model_nd.c)
target_link_libraries(image-cluster-NDmodel m)

add_executable(txt2mp4 src/txt2mp4.c)
target_link_libraries(txt2mp4 m)

cmake_minimum_required(VERSION 3.10)
project(image_cluster C)

set(CMAKE_C_STANDARD 17)

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

find_package(OpenMP)
if (OpenMP_C_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    message(STATUS "OpenMP found.")
else()
    message(WARNING "OpenMP not found. Multithreading disabled.")
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

add_executable(gric-cluster ${CLUSTER_SRCS})
if (OpenMP_C_FOUND)
    target_link_libraries(gric-cluster OpenMP::OpenMP_C)
endif()
target_link_libraries(gric-cluster
    ${CFITSIO_LIBRARIES}
    ${PNG_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${IMAGESTREAMIO_LIBRARIES}
    m
)

add_executable(gric-mktxtseq src/mktestseq.c)
target_link_libraries(gric-mktxtseq m)

add_executable(gric-plot src/plot_clusters.c)
target_link_libraries(gric-plot ${PNG_LIBRARIES} m)

add_executable(gric-mkclusteredfile src/mkclusteredfile.c)
target_link_libraries(gric-mkclusteredfile m)

add_executable(gric-NDmodel src/model_nd.c)
target_link_libraries(gric-NDmodel m)

add_executable(gric-ascii-spot-2-video src/ascii-spot-2-video.c)
target_link_libraries(gric-ascii-spot-2-video m ${IMAGESTREAMIO_LIBRARIES})

if (IMAGESTREAMIO_FOUND)
    add_executable(gric-stream-to-pipe src/stream_to_pipe.c)
    target_link_libraries(gric-stream-to-pipe ${IMAGESTREAMIO_LIBRARIES} m)
endif()

cmake_minimum_required(VERSION 3.10)
project(image_cluster C)

set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CFITSIO REQUIRED cfitsio)
find_package(PNG REQUIRED)

include_directories(${CFITSIO_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS} src)
link_directories(${CFITSIO_LIBRARY_DIRS} ${PNG_LIBRARY_DIRS})

add_executable(image-cluster src/main.c src/cluster_core.c src/cluster_io.c src/framedistance.c src/frameread.c)
target_link_libraries(image-cluster ${CFITSIO_LIBRARIES} m)

add_executable(image-cluster-mktxtseq src/mktestseq.c)
target_link_libraries(image-cluster-mktxtseq m)

add_executable(image-cluster-plot src/plot_clusters.c)
target_link_libraries(image-cluster-plot ${PNG_LIBRARIES} m)

add_executable(image-cluster-3Dmodel src/model_3d.c)
target_link_libraries(image-cluster-3Dmodel m)

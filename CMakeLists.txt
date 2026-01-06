cmake_minimum_required(VERSION 3.10)
project(image_cluster C)

set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CFITSIO REQUIRED cfitsio)

include_directories(${CFITSIO_INCLUDE_DIRS} src)
link_directories(${CFITSIO_LIBRARY_DIRS})

add_executable(image_cluster src/main.c src/cluster_core.c src/cluster_io.c src/framedistance.c src/frameread.c)
target_link_libraries(image_cluster ${CFITSIO_LIBRARIES} m)

add_executable(image-cluster-mktxtseq src/mktestseq.c)
target_link_libraries(image-cluster-mktxtseq m)

add_executable(image_cluster_plot src/plot_clusters.c)
target_link_libraries(image_cluster_plot m)

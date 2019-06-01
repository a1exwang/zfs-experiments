cmake_minimum_required(VERSION 3.0)
project(zfs-experiments)

set(CMAKE_CXX_STANDARD 14)
include_directories(/usr/include/libzfs)
include_directories(/usr/include/libspl)
link_libraries(zfs nvpair)

add_executable(zfs_label zfs_label.cpp zfs_if.c)

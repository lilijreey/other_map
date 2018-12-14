cmake_minimum_required(VERSION 3.8)
project(hashtable)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES src/segment_map.hpp)

add_subdirectory("test")
## unit test

##add_library(hashtable ${SOURCE_FILES})

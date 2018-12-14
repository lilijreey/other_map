

enable_testing()

find_package(GTest REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS})

add_executable(segment_test  segment_test.cpp)
target_link_libraries(segment_test gtest_main gtest pthread)

add_test(SegmentTest  segment_test)
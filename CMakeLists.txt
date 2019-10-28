cmake_minimum_required(VERSION 2.8)
project(F19FS)
set(CMAKE_COLOR_MAKEFILE ON)
if(APPLE)
    # Make policy CMP0042 warning go away
    # even though I still don't quite understand it
    set(CMAKE_MACOSX_RPATH ON)
endif(APPLE)
include_directories(include)
add_library(bitmap SHARED src/bitmap.c)
add_library(back_store SHARED src/block_store.c)
add_library(dyn_array SHARED src/dyn_array.c)
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS} include)
# add -DENABLE_MOVE to the CXX flags to activate move tests
set(SHARED_FLAGS " -Wall -Wextra -Wshadow -Werror -g -D_POSIX_C_SOURCE=200809L")
set(CMAKE_CXX_FLAGS "-std=c++11 ${SHARED_FLAGS}")
set(CMAKE_C_FLAGS "-std=c99 ${SHARED_FLAGS}")
add_library(F19FS SHARED src/F19FS.c)
set_target_properties(F19FS PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(F19FS back_store dyn_array bitmap)
add_executable(fs_test test/tests.cpp)

target_compile_definitions(fs_test PRIVATE)

target_link_libraries(fs_test F19FS ${GTEST_LIBRARIES} pthread)
#install(TARGETS F19FS DESTINATION lib)
#install(FILES include/F19FS.h DESTINATION include)
#enable_testing()
#add_test(NAME    fs_test 
#         COMMAND fs_test)

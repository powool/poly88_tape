cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)

if("${CMAKE_BUILD_TYPE}" MATCHES "")
	set(CMAKE_BUILD_TYPE "Debug")
	message("Configuring for '${CMAKE_BUILD_TYPE}'")
	message("Use cmake -DCMAKE_BUILD_TYPE=Release to build for release.")
endif()


project(poly88_tape)

add_custom_target(
	cleanall
	COMMAND make clean
	COMMAND rm -rf CMakeCache.txt CMakeFiles Makefile cmake_install.cmake Debug Release
)

#add_executable(tape_biphase tape_biphase.cpp audio.cpp)
#add_executable(tape_byte tape_byte.cpp audio.cpp)
add_executable(testing testing.cpp audio.cpp)

# see https://runebook.dev/en/docs/qt/cmake-command-reference
# https://stackoverflow.com/questions/71119516/qt6-windows-linker-error-when-using-q-object
# for updates
cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIGURATION>)

if("${CMAKE_BUILD_TYPE}" MATCHES "")
	set(CMAKE_BUILD_TYPE "Debug")
	message("Configuring for '${CMAKE_BUILD_TYPE}'")
	message("Use cmake -DCMAKE_BUILD_TYPE=Release to build for release.")
endif()

project(poly88_tape LANGUAGES CXX)

add_custom_target(
	cleanall
	COMMAND make clean
	COMMAND rm -rf CMakeCache.txt CMakeFiles Makefile cmake_install.cmake Debug Release
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)
# find_package(cpptrace REQUIRED)
find_package(GTest REQUIRED)

# Qt Stuff

qt_standard_project_setup()

qt_add_executable(${PROJECT_NAME}
	PolyWaveToImage.cpp
	audio.cpp
	Record.cpp
)

# target_link_libraries(poly88_tape PRIVATE Qt6::Widgets Qt6::Gui cpptrace::cpptrace)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Gui)

add_executable(mergecas mergecas.cpp Record.cpp)
target_include_directories(mergecas PRIVATE .)

add_executable(dumpcas dumpcas.cpp Record.cpp)
target_include_directories(dumpcas PRIVATE .)

enable_testing()

add_executable(testing tests/empty.cpp tests/test_audio.cpp tests/test_polyphase.cpp tests/test_kansascity.cpp audio.cpp Record.cpp)

target_include_directories(testing PRIVATE .)

target_link_libraries(testing GTest::gtest_main)

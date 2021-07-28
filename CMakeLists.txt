cmake_minimum_required(VERSION 3.0)

project(beckhoff-ads)

set(TARGET ads)

find_package(ASL)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_DEBUG_POSTFIX "d")

set(SRC
	BeckhoffAds.h
	BeckhoffAds.cpp
	main.cpp
)

add_executable(${TARGET} ${SRC})

target_link_libraries(${TARGET} asls)

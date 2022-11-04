cmake_minimum_required(VERSION 3.0)

project(beckhoff-ads)

option(ADS_SAMPLES "Build samples")

set(TARGET beckhoffAds)

if(NOT TARGET asls)
	find_package(ASL)
endif()

set(CMAKE_DEBUG_POSTFIX "d")

set(SRC
	BeckhoffAds.h
	BeckhoffAds.cpp
)

add_library(${TARGET} STATIC ${SRC})

target_link_libraries(${TARGET} asls)
target_include_directories(${TARGET} PUBLIC .)

if(ADS_SAMPLES)
	set(TARGET ads-demo)
	add_executable(${TARGET} main.cpp)
	target_link_libraries(${TARGET} beckhoffAds asls)
endif()


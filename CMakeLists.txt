cmake_minimum_required(VERSION 3.0)

project(beckhoff-ads)

option(ADS_SAMPLES "Build samples")

set(TARGET beckhoffAds)

if(NOT TARGET asls)
	find_package(ASL)
	if(NOT TARGET asls)
		cmake_minimum_required(VERSION 3.14...3.24)
		include(FetchContent)
		FetchContent_Declare(asl URL https://github.com/aslze/asl/archive/1.11.3.zip)
		FetchContent_MakeAvailable(asl)
	endif()
endif()

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


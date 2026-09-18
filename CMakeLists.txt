cmake_minimum_required(VERSION 3.25)

# Copyright (C) 2026 DeadOnKeyboard
# SPDX-License-Identifier: GPL-3.0-or-later

project(
	EnchantingFreedomAE
	VERSION 1.3.0
	DESCRIPTION "Enchanting and disenchanting freedom plugin for Skyrim AE"
	LANGUAGES CXX
)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DEPENDENCY_PREFIX "" CACHE PATH "Optional prebuilt dependency prefix")
if(DEPENDENCY_PREFIX)
	list(PREPEND CMAKE_PREFIX_PATH "${DEPENDENCY_PREFIX}")
endif()

set(COMMONLIBSSE_PATH "" CACHE PATH "Path to a CommonLibSSE-NG source checkout")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REX_OPTION_INI OFF CACHE BOOL "" FORCE)
set(REX_OPTION_JSON OFF CACHE BOOL "" FORCE)
set(REX_OPTION_TOML OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY ON CACHE BOOL "" FORCE)
set(COMMONLIB_ENABLE_IPO OFF CACHE BOOL "" FORCE)

if(COMMONLIBSSE_PATH)
	if(NOT EXISTS "${COMMONLIBSSE_PATH}/CMakeLists.txt")
		message(FATAL_ERROR "CommonLibSSE-NG was not found at '${COMMONLIBSSE_PATH}'")
	endif()
	add_subdirectory("${COMMONLIBSSE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE" EXCLUDE_FROM_ALL)
else()
	FetchContent_Declare(
		commonlibsse_ng
		GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
		GIT_TAG 1504349dddfc622d4d25704bba19e2ade669dc5a
		GIT_SHALLOW FALSE
		GIT_SUBMODULES_RECURSE TRUE
	)
	FetchContent_MakeAvailable(commonlibsse_ng)
endif()

find_package(xbyak CONFIG REQUIRED)

add_library(
	${PROJECT_NAME} SHARED
		src/Config.cpp
		src/Config.h
		src/EnchantingPatches.cpp
		src/EnchantingPatches.h
		src/main.cpp
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		xbyak::xbyak
)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOGDI NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
	OUTPUT_NAME "EnchantingFreedomAE"
	PDB_NAME "EnchantingFreedomAE"
)

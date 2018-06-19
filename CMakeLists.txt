project(hdeem_plugin)

cmake_minimum_required(VERSION 2.8)

option(ENABLE_MPI "Build with MPI support." ON)

# Intialize git submodules if not done already
file(GLOB SUBMODULE_FILES "common/*")
list(LENGTH SUBMODULE_FILES COUNT_COMMON)
file(GLOB SUBMODULE_FILES "hdeem_cxx/*")
list(LENGTH SUBMODULE_FILES COUNT_HDEEM_CXX)
file(GLOB SUBMODULE_FILES "scorep/*")
list(LENGTH SUBMODULE_FILES COUNT_SCOREP)
if(${COUNT_COMMON} EQUAL 0 OR ${COUNT_HDEEM_CXX} EQUAL 0 OR ${COUNT_SCOREP} EQUAL 0)
    message(STATUS "Initializing git submodule")
    execute_process(COMMAND "git" "submodule" "init" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    execute_process(COMMAND "git" "submodule" "update" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/common;${CMAKE_MODULE_PATH}")

include(common/DefaultBuildType.cmake)

find_package(Scorep REQUIRED)
include(scorep/ScorepCXXPlugin.cmake)

include(hdeem_cxx/HDEEM_CXX.cmake)

find_package(MPI)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Wall -pedantic")

option(HDEEM_GET_STATS_TRACE "Enable stats tracing" OFF)
if(HDEEM_GET_STATS_TRACE)
    message(STATUS "Build with tracing")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHDEEM_GET_STATS_TRACE")
endif()

include_directories(include)

# sets the gcc lib path as rpath for build and install directory
string(REGEX REPLACE "bin.*" "lib64" GCC_RPATH ${CMAKE_CXX_COMPILER})
SET(CMAKE_SKIP_BUILD_RPATH  FALSE)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
SET(CMAKE_INSTALL_RPATH "${GCC_RPATH}")

add_library(hdeem_plugin SHARED hdeem_plugin.cpp)
add_library(hdeem_sync_plugin SHARED hdeem_sync_plugin.cpp)

if (MPI_FOUND AND ENABLE_MPI)
    add_definitions(-DHAVE_MPI)
    message(STATUS "Build with MPI support")
    include_directories(${MPI_INCLUDE_PATH})
    set_target_properties(hdeem_sync_plugin PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    set_target_properties(hdeem_sync_plugin PROPERTIES LINK_FLAGS "${MPI_LINK_FLAGS}")
    set_target_properties(hdeem_sync_plugin PROPERTIES LINK_LIBRARIES "${MPI_LIBRARIES}")
elseif(ENABLE_MPI)
    message(FATAL_ERROR "Can't find MPI. Please provide MPI or unset \"ENABLE_MPI\"")
endif()

target_link_libraries(hdeem_plugin ${HDEEM_LIBRARIES})
target_link_libraries(hdeem_sync_plugin ${HDEEM_LIBRARIES})

link_directories(${HOME}/src)

install(TARGETS hdeem_plugin LIBRARY DESTINATION lib)
install(TARGETS hdeem_sync_plugin LIBRARY DESTINATION lib)

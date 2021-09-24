cmake_minimum_required(VERSION 3.5)
project(FlashMob LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/third_party/graphvite/cmake)

# add externals
include(ExternalProject)
set(EXTERNAL_DIR ${PROJECT_SOURCE_DIR}/external)

if (NOT UPDATE)
    set(SKIP_UPDATE TRUE)
else()
    set(SKIP_UPDATE FALSE)
endif()

# import glog
include(cmake/glog.cmake)

# import gflags
include(cmake/gflags.cmake)

# import args
include(cmake/args.cmake)

# import gtest
include(cmake/gtest.cmake)

# enable ctest
include(cmake/test.cmake)

find_package(OpenMP REQUIRED)
if(OPENMP_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

# compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wno-maybe-uninitialized -mclflushopt")

# e.g. -DPROFILE=ALL | DETAIL | NORMAL | BRIEF (default)
# BRIEF: limited log, minimal impact on performance
# NORMAL: statistical log, some impact on performance
# DETAIL: per-step log, some impact on performance
# ALL: per-step, per-partition log, large impact on performance
if (PROFILE)
    if (PROFILE STREQUAL "ALL")
        SET(PROFILE_LEVEL PROFILE_ALL)
    elseif (PROFILE STREQUAL "DETAIL")
        SET(PROFILE_LEVEL PROFILE_DETAIL)
    elseif (PROFILE STREQUAL "NORMAL")
        SET(PROFILE_LEVEL PROFILE_NORMAL)
    else ()
        SET(PROFILE_LEVEL PROFILE_BRIEF)
    endif()
else ()
    SET(PROFILE_LEVEL PROFILE_BRIEF)
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPROFILE_LEVEL=${PROFILE_LEVEL}")

if (PROFILE_BF)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPROFILE_BF")
endif()

if (ENABLE_PERF)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DENABLE_PERF")
elseif (ENABLE_VTUNE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DENABLE_VTUNE")
endif()

message("Top level CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")

# output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# project directories
include_directories(include)
include_directories(third_party)

function(add_exec EXEC_NAME)
    add_executable(${EXEC_NAME} ${EXEC_NAME}.cpp)
    # dynamic link
    target_link_libraries(${EXEC_NAME} PUBLIC ${OpenMP_CXX_LIBRARIES} ${GTEST_LIBRARIES} glog numa)
    # static link glog and gflags (must also link unwind)
    # target_link_libraries(${EXEC_NAME} PUBLIC ${OpenMP_CXX_LIBRARIES} ${GTEST_LIBRARIES} glog.a numa.a gflags.a unwind)
    add_dependencies(${EXEC_NAME} external-args external-gtest ${glog_dependency} ${gflag_dependency})
endfunction(add_exec)

add_subdirectory(src)

install(
    DIRECTORY bin
    DESTINATION ${CMAKE_BINARY_DIR}
    USE_SOURCE_PERMISSIONS
)

set(GTEST_INSTALL_PREFIX "${PROJECT_BINARY_DIR}/gtest")

set (GTEST_CMAKE_ARGS 
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${GTEST_INSTALL_PREFIX}"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
)

ExternalProject_Add (external-gtest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.10.0
    PREFIX gtest
    UPDATE_DISCONNECTED ${SKIP_UPDATE}
    SOURCE_DIR ${EXTERNAL_DIR}/gtest
	CMAKE_ARGS ${GTEST_CMAKE_ARGS}
)

include_directories("${GTEST_INSTALL_PREFIX}/include")
link_directories(${GTEST_INSTALL_PREFIX}/lib)
# lib64 for panther
link_directories(${GTEST_INSTALL_PREFIX}/lib64)
set(GTEST_LIBRARIES "gtest.a" "gtest_main.a")

find_package(GFlags)

if (NOT GFLAGS_FOUND)
    message(WARNING "Can't find gflags library. It will be installed from git repository.")
    ExternalProject_Add(external-gflags
            GIT_REPOSITORY      https://github.com/gflags/gflags
            UPDATE_DISCONNECTED ${SKIP_UPDATE}
            PREFIX              gflags
            SOURCE_DIR          ${EXTERNAL_DIR}/gflags
            BINARY_DIR          ${PROJECT_BINARY_DIR}/gflags
            INSTALL_DIR         ${PROJECT_BINARY_DIR}/gflags
            CONFIGURE_COMMAND   test -e Makefile ||
                                cmake ${EXTERNAL_DIR}/gflags -DCMAKE_INSTALL_PREFIX=${PROJECT_BINARY_DIR}/gflags
            BUILD_COMMAND       test -e lib/libgflags.a || make -j
            INSTALL_COMMAND     test -e include/gflags/gflags.h || make install)
    include_directories(${PROJECT_BINARY_DIR}/gflags/include)
    link_directories(${PROJECT_BINARY_DIR}/gflags/lib)
    set(gflag_dependency "external-gflags" CACHE INTERNAL "")
else()
    get_filename_component(GFLAGS_LIBRARY_DIR ${GFLAGS_LIBRARY} DIRECTORY)
    include_directories(${GFLAGS_INCLUDE_DIR})
    link_directories(${GFLAGS_LIBRARY_DIR})
endif()

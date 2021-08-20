find_package(Glog)

if (NOT GLOG_FOUND)
    message(WARNING "Can't find glog library. It will be installed from git repository.")
    ExternalProject_Add(external-glog
            GIT_REPOSITORY      https://github.com/google/glog.git
            GIT_TAG             v0.4.0
            UPDATE_DISCONNECTED ${SKIP_UPDATE}
            PREFIX              glog
            SOURCE_DIR          ${EXTERNAL_DIR}/glog
            BINARY_DIR          ${PROJECT_BINARY_DIR}/glog
            INSTALL_DIR         ${PROJECT_BINARY_DIR}/glog
            CONFIGURE_COMMAND   test -e Makefile && exit ||
                                cd ${EXTERNAL_DIR}/glog && ./autogen.sh && cd - &&
                                ${EXTERNAL_DIR}/glog/configure
            BUILD_COMMAND       test -e .libs/libglog.a || make -j
            INSTALL_COMMAND     test -e include/glog/logging.h || make install prefix=${PROJECT_BINARY_DIR}/glog)
    include_directories(${PROJECT_BINARY_DIR}/glog/include)
    link_directories(${PROJECT_BINARY_DIR}/glog/.libs)
    set(glog_dependency "external-glog" CACHE INTERNAL "")
else()
    get_filename_component(GLOG_LIBRARY_DIR ${GLOG_LIBRARY} DIRECTORY)
    include_directories(${GLOG_INCLUDE_DIR})
    link_directories(${GLOG_LIBRARY_DIR})
endif ()

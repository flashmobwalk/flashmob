ExternalProject_Add (external-args
    PREFIX external/args 
    GIT_REPOSITORY https://github.com/Taywee/args.git
    GIT_TAG 6.2.2
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/args
    CONFIGURE_COMMAND "" # Disable all other steps
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/args)

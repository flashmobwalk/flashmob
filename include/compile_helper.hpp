#pragma once

#define _unused(x) ((void)(x))

#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)

// enable NDEBUG so that assert can only be active on Debug mode
// #ifndef UNIT_TEST
    // undefine NDEBUG to enable assert in release mode
    // #undef NDEBUG
// #endif

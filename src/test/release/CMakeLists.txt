set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_OPT "-O3 -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_OPT}")
add_exec(test_dataset)

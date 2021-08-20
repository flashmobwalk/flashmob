SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCMAKE_BUILD_TYPE=Debug")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -march=native")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUNIT_TEST")

add_exec(test_graph)
add_exec(test_solver)
add_exec(test_node2vec)
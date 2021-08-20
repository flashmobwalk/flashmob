enable_testing() 

foreach(prog "test_graph" "test_solver" "test_node2vec")
    add_test("${prog}" "./bin/${prog}")
endforeach(prog)

# FlashMob

FlashMob is a shared-memory graph random walk system,
an implementation of the paper *Random Walks on Huge Graphs at Cache Efficiency*, published on SOSP 2021.
This repository also holds the [artifact](https://github.com/flashmobwalk/flashmob/tree/sosp21-ae) of the paper.
The paper and its presentation can be found [here](https://sosp2021.mpi-sws.org/program.html).

## Quick Start

### Install Dependencies

```bash
sudo apt-get update
sudo apt-get install cmake g++ autoconf libtool libnuma-dev -y
```

### Compile FlashMob

```bash
git clone --branch sosp21-ae https://github.com/flashmobwalk/flashmob.git
cd flashmob
mkdir build && cd build
cmake ..
make && make install
```

### Run Tests

Run tests to verify the project is correctly compiled:

```bash
ctest
```

If all goes well, the results of ctest should be:

```bash
Test project /home/ubuntu/flashmob/build
    Start 1: test_graph
1/3 Test #1: test_graph .......................   Passed    0.26 sec
    Start 2: test_solver
2/3 Test #2: test_solver ......................   Passed   22.61 sec
    Start 3: test_node2vec
3/3 Test #3: test_node2vec ....................   Passed   13.27 sec

100% tests passed, 0 tests failed out of 3
```

As in tests the walk and graphs are all random, with a very low probability the tests may fail
when it's detected that the random walk paths don't comply with the expected probability distribution,
in which case just re-run the tests and it shall pass.

## Evaluation

### Download Datasets

**Youtube, Twitter and Friendster graphs**

To download the Youtube, Twitter and Friendster graphs:

```bash
# ./bin/download-small.sh [data-directory]
./bin/download-small.sh ./dataset
```

### Evaluate FlashMob

The script below will evaluate FlashMob on all 5 graphs. If a graph file doesn't exist, the script will skip it and continue to evaluate on other graphs.

Evaluate DeepWalk:

```bash
# ./bin/eval_fmob.sh [dataset-directory] [output-directory] [app]
./bin/eval_fmob.sh ./dataset ./output/deepwalk deepwalk
```

Evaluate node2vec:

```bash
# ./bin/eval_fmob.sh [dataset-directory] [output-directory] [app]
./bin/eval_fmob.sh ./dataset ./output/node2vec node2vec
```

The output could be found at ./output directory.
Suppose only the first 3 graphs have been downloaded:

```bash
output
├── deepwalk
│   ├── friendster.out.txt
│   ├── twitter.out.txt
│   └── youtube.out.txt
└── node2vec
    ├── friendster.out.txt
    ├── twitter.out.txt
    └── youtube.out.txt
```

## Extended FlashMob Options

Use "--help" or "-h" to check all parameters of the programs.

```bash
./bin/deepwalk -h
./bin/node2vec -h
```

### DeepWalk

```bash
  ./bin/deepwalk {OPTIONS}

  OPTIONS:

      -h, --help                        Display this help menu
      -t[threads]                       [optional] number of threads this
                                        program will use
      -s[sockets]                       [optional] number of sockets
      --socket-mapping=[socket-mapping] [optional] example:
                                        --socket-mapping=0,1,2,3
      --mem=[mem]                       [optional] Maximum memory this program
                                        will use (in GiB)
      -f[format]                        graph format: binary | text
      -g[graph]                         graph path
      -e[epoch]                         walk epoch number
      -w[walker]                        walker number
      -l[length]                        walk length
```

The parameters of DeepWalk can be categorized into 3 types.

- **Hardware configurations:**
"-t", "-s", "--socket-mapping", "--mem" are used to customize hardware usage.
Suppose we want to use only the 1st and 3rd sockets (with socket ID of 0 and 2), 8 threads on each of the sockets, and 64GiB memory in total,
then the parameters shall be "-t 16 -s 2 --socket-mapping=0,2 --mem 64".
In default, #threads is set to be #physical-cores, distributed on all sockets, and #mem is set to be 0.9 times global DRAM size.
- **Input configurations:**
"-f", and "-g" are used to specify the path and format of the input graph.
- **Walk configurations:**
"-l" is used to specify the length of each walk.
One and only one of "-e" and "-w" must be used to specify how many walkers there are.
"-e" tells that there are #epoch times |V| walkers.
"-w" tells that there are #walker walkers.

Example usage:

```bash
./bin/deepwalk -f text -g ./dataset/youtube.txt -e 10 -l 80
```

### node2vec

```bash
  ./bin/node2vec {OPTIONS}

  OPTIONS:

      -h, --help                        Display this help menu
      -t[threads]                       [optional] number of threads this
                                        program will use
      -s[sockets]                       [optional] number of sockets
      --socket-mapping=[socket-mapping] [optional] example:
                                        --socket-mapping=0,1,2,3
      --mem=[mem]                       [optional] Maximum memory this program
                                        will use (in GiB)
      -f[format]                        graph format: binary | text
      -g[graph]                         graph path
      -e[epoch]                         walk epoch number
      -w[walker]                        walker number
      -l[length]                        walk length
      -p[p]                             node2vec parameter p
      -q[q]                             node2vec parameter q
```

There are 2 additional parameters for node2vec walk compared with DeepWalk, i.e. "-p" and "-q".
They are the hyper-parameters used in node2vec, called return parameter and in-out parameter.

Example usage:

```bash
./bin/node2vec -f text -g ./dataset/youtube.txt -e 10 -l 80 -p 2 -q 0.5
```

## Publication

Ke Yang, Xiaosong Ma, Saravanan Thirumuruganathan, Kang Chen, Yongwei Wu. Random Walks on Huge Graphs at Cache Efficiency. In ACM SIGOPS 28th Symposium on Operating Systems Principles (SOSP ’21).

## License

Code in this repository, except those under the `third_party` directory, are licensed under the MIT license, found in the `LICENSE` file.

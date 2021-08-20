# FlashMob

This repository holds the artifact of the paper "Random Walks on Huge Graphs at Cache Efficiency", published on SOSP 2021.

## System Specification

All experiments in the SOSP paper use a Dell PowerEdge R740
server running Ubuntu 20.04.1 LTS. It has two 2.60GHz Xeon
Gold 6126 processors, each with 12 cores and 296GB DRAM.
Each core has a private 32KB L1 and 1MB L2 caches, while
all cores within a socket share a 19.75MB L3 cache (LLC).

The instructions and evaluations in this document are also verified on two AWS EC2 instances:
- m5.12xlarge, with Ubuntu Server 20.04 LTS (HVM), SSD Volume Type, 64-bit (x86), 24 cores on 1 NUMA node, and 192GB memory.
- c5n.18xlarge, with Ubuntu Server 20.04 LTS (HVM), SSD Volume Type, 64-bit (x86), 36 cores on 2 NUMA nodes, and 192GB memory.

### Hardware Requirements

In default the evaluation script will download and evaluate 3 graphs, including Youtube, Twitter, and Friendster.
To download and evaluate all 3 graphs, at least 64GB DRAM memory and 80GB free disk space are recommended.

Besides, there are 2 additional graphs evaluated in the paper, i.e. UK-Union and Yahoo. To download and evaluate these 2 graphs, 256GB memory and 250GB additional disk space are recommended.

### Expected Time Usage

On the m5.12xlarge instance, the compilation, installation, and testing take less than 5 min.
The graph downloading takes about 65 min.
And the evaluation of random walk of DeepWalk and node2vec takes about 20 min and 35 min, respectively.

## Setup

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

The following command will automatically download and unzip Youtube, Twitter and Friendster graphs.
Check [here](#UK-and-Yahoo-Graphs) for downloading UK and Yahoo graphs.

```bash
# ./bin/download.sh [data-directory]
./bin/download.sh ./dataset
```

### Evaluate FlashMob

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

The output could be found at ./output directory:
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

The evaluation results on the 2 AWS EC2 instances are listed below.

| DeepWalk (ns/step) | Youtube | Twitter | Friendster |
|--------------------|---------|---------|------------|
| m5.12xlarge        |  21.87  |  31.30  |    32.88   |
| c5n.18xlarge       |  21.99  |  29.78  |    31.40   |

| node2vec (ns/step) | Youtube | Twitter | Friendster |
|--------------------|---------|---------|------------|
| m5.12xlarge        |  102.95 |  263.64 |   233.67   |
| c5n.18xlarge       |  106.38 |  320.06 |   272.30   |

### UK and Yahoo Graphs

- UK-Union: This graph is downloaded from [here](http://law.di.unimi.it/datasets.php). The downloaded graph is highly compressed. The tool to decompress it can also be found [here](http://law.di.unimi.it/index.php).

- Yahoo: This graph is downloaded from [here](https://webscope.sandbox.yahoo.com/). One needs a Yahoo! account and needs to fill an application to get the graph data.

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

## License

Code in this repository, except those under the `third_party` directory, are licensed under the MIT license, found in the `LICENSE` file.
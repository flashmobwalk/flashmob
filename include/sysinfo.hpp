#pragma once

#include <string>
#include <fstream>
#include <limits>

#include "boost/thread.hpp"

/**
 * Execute a shell command and return the output.
 * https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
 */
std::string exec_cmd (const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

/**
 * Get system memory size.
 * https://stackoverflow.com/questions/349889/how-do-you-determine-the-amount-of-linux-system-ram-in-c
 */
uint64_t get_sys_mem() {
    std::string token;
    std::ifstream file("/proc/meminfo");
    while(file >> token) {
        if(token == "MemTotal:") {
            uint64_t mem;
            if(file >> mem) {
                return mem * 1024u;
            } else {
                return 0;
            }
        }
        // Ignore the rest of the line
        file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return 0; // Nothing found
}

/**
 * Get system NUMA socket amount.
 */
int get_max_socket_num() {
    if (numa_available() != -1) {
        return numa_num_configured_nodes();
    } else {
        return 1;
    }
}

/**
 * Get system L2 cache size (per physical core).
 */
uint64_t get_l2_cache_size() {
    std::string str = exec_cmd("lscpu -C -B | grep L2 | awk '{print $2}'");
    std::istringstream iss(str);
    uint64_t value;
    iss >> value;
    return value;
}

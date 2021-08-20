/**
 * The code in this file are adapted from
 * https://muehe.org/posts/profiling-only-parts-of-your-code-with-perf/
 */

#pragma once

#include <sstream>
#include <iostream>
#include <functional>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "env.hpp"

/**
 * Profile cache and memory usage for a function.
 */
struct System
{
    static void profile(const std::string& name,std::function<void()> body) {
#if defined(ENABLE_PERF)
        std::string filename = name.find(".data") == std::string::npos ? (name + ".data") : name;

        // Launch profiler
        pid_t pid;
        std::stringstream s;
        s << getpid();
        // const char* events = "mem_load_retired.l1_hit,mem_load_retired.l1_miss,mem_load_retired.l2_hit,mem_load_retired.l2_miss,mem_load_retired.l3_hit,mem_load_retired.l3_miss,L1-dcache-load-misses,L1-dcache-load,mem_load_uops_retired.hit_lfb";
        // const char* events = "mem_load_retired.l1_hit,mem_load_retired.l1_miss,mem_load_retired.l2_hit,mem_load_retired.l2_miss,mem_load_retired.l3_hit,mem_load_retired.l3_miss,L1-dcache-load,L1-dcache-load-misses,l2_rqsts.DEMAND_DATA_RD_HIT,l2_rqsts.DEMAND_DATA_RD_MISS,LLC-load,LLC-loads-misses";
        const char* events = "mem_load_retired.l1_hit,mem_load_retired.l1_miss,mem_load_retired.l2_hit,mem_load_retired.l2_miss,mem_load_retired.l3_hit,mem_load_retired.l3_miss,offcore_requests.all_data_rd";
        pid = fork();
        if (pid == 0) {
            auto fd=open("/dev/null",O_RDWR);
            dup2(fd,1);
            dup2(fd,2);
            exit(execl("/usr/bin/perf","perf","stat", "-e", events, "-o",filename.c_str(),"-p",s.str().c_str(),nullptr));
        }

        // Wait perf to work
        sleep(3);

        // Run body
        body();

        // Kill profiler
        kill(pid,SIGINT);
        waitpid(pid,nullptr,0);
#elif defined(ENABLE_VTUNE)
        std::string filename = name.find(".data") == std::string::npos ? (name + ".data") : name;

        // Launch profiler
        pid_t pid;
        std::stringstream s;
        s << getpid();

        pid = fork();
        if (pid == 0) {
            int output_fd = open(filename.c_str(), O_WRONLY | O_CREAT, 0644);
            // Replace the child's stdout and stderr handles with the log file handle:
            if (dup2(output_fd, STDOUT_FILENO) < 0) {
                std::perror("dup2 (stdout)");
                std::exit(1);
            }
            if (dup2(output_fd, STDERR_FILENO) < 0) {
                std::perror("dup2 (stderr)");
                std::exit(1);
            }
            // const char* events = "performance-snapshot";
            const char* events = "memory-access";
            exit(execl(VTUNE_PATH,"vtune","-collect", events, "-target-pid", s.str().c_str(), nullptr));
        }

        // Wait vtune to work
        sleep(20);

        // Run body
        body();

        // Kill profiler
        kill(pid,SIGINT);
#else
        body();
#endif
    }

    static void profile(std::function<void()> body) {
        profile("perf.data",body);
    }
};

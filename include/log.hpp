#pragma once

#include <glog/logging.h>

#include "graphvite/io.h"

#define glog_max_length google::LogMessage::kMaxLogMessageLen

#define PROFILE_CLOSE 0
#define PROFILE_BRIEF 10
#define PROFILE_NORMAL 20
#define PROFILE_DETAIL 30
#define PROFILE_ALL 40

#if PROFILE_LEVEL >= PROFILE_BRIEF
#   define PROFILE_IF_BRIEF true
#endif

#if PROFILE_LEVEL >= PROFILE_NORMAL
#   define PROFILE_IF_NORMAL true
#endif

#if PROFILE_LEVEL >= PROFILE_DETAIL
#   define PROFILE_IF_DETAIL true
#endif

#if PROFILE_LEVEL >= PROFILE_ALL
#   define PROFILE_IF_ALL true
#endif

void init_glog(char** argv, decltype(google::INFO) glog_level = google::INFO)
{
    FLAGS_minloglevel = glog_level;
    FLAGS_logtostderr = true;
    FLAGS_log_prefix = false;
    google::InitGoogleLogging(argv[0]);
    LOG(WARNING) << "PROFILE_LEVEL " << PROFILE_LEVEL;
}

std::string percent_string(double val) {
    std::stringstream ss;
    ss.precision(3);
    ss << val * 100 << "%";
    return ss.str();
}

std::string split_line_string() {
    return std::string("=====================================================");
}

double sec2ns(double val) {
    return val * 1000000000;
}

double get_step_cost(double sec, uint64_t steps, int thread_num) {
    if (steps == 0) {
        return 0.0;
    } else {
        return sec2ns(sec) / steps * thread_num;
    }
}

std::string block_layer_str(int layer) {
    return std::string(layer * 2, ' ');
}

std::string block_begin_str(int layer = 0) {
    return block_layer_str(layer) + std::string("[BEGIN] ");
}

std::string block_end_str(int layer = 0) {
    return block_layer_str(layer) + std::string("[ END ] ");
}

std::string block_mid_str(int layer = 0) {
    return block_layer_str(layer) + std::string("- ");
}

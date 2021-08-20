/**
 * The code in this file are adapted from
 * https://github.com/boostorg/thread/blob/4abafccff4bdeb4b5ac516ff0c2bc7c0dad8bafb/src/pthread/thread.cpp
 */

// Copyright (C) 2001-2003
// William E. Kempf
// Copyright (C) 2007-8 Anthony Williams
// (C) Copyright 2011-2012 Vicente J. Botet Escriba
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <string>
#include <fstream>

// Get system physical core amount.
int get_max_core_num()
{
    try {
        using namespace std;

        ifstream proc_cpuinfo ("/proc/cpuinfo", ifstream::in);

        // const string physical_id("physical id"), core_id("core id");
        const string physical_id("physicalid"), core_id("coreid");

        typedef std::pair<unsigned, unsigned> core_entry; // [physical ID, core id]

        set<core_entry> cores;

        core_entry current_core_entry;

        string line;
        while ( getline(proc_cpuinfo, line) ) {
            if (line.empty())
                continue;

            vector<string> key_val;
            // boost::split(key_val, line, boost::is_any_of(":"));
            stringstream ss(line);
            string item;
            while (getline(ss, item, ':')) {
                key_val.push_back(item);
            }

            if (key_val.size() != 2)
                continue;
                //return std::thread::hardware_concurrency();

            string key   = key_val[0];
            string value = key_val[1];
            // boost::trim(key);
            // boost::trim(value);
            key.erase(remove_if(key.begin(), key.end(), ::isspace), key.end());
            value.erase(remove_if(value.begin(), value.end(), ::isspace), value.end());

            if (key == physical_id) {
                // current_core_entry.first = boost::lexical_cast<unsigned>(value);
                current_core_entry.first = stoi(value);
                continue;
            }

            if (key == core_id) {
                // current_core_entry.second = boost::lexical_cast<unsigned>(value);
                current_core_entry.second = stoi(value);
                cores.insert(current_core_entry);
                // LOG(WARNING) << current_core_entry.first << " " << current_core_entry.second;
                continue;
            }
        }
        // Fall back to hardware_concurrency() in case
        // /proc/cpuinfo is formatted differently than we expect.
        return cores.size() != 0 ? cores.size() : std::thread::hardware_concurrency();
    } catch(...) {
        return std::thread::hardware_concurrency();
    }
    return std::thread::hardware_concurrency();
}

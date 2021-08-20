/**
 * Code in this file are adapted from
 * https://github.com/DeepGraphLearning/graphvite/blob/master/include/util/io.h
 */

/**
 * Copyright 2019 MilaGraph. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @author Zhaocheng Zhu
 */

#include <sstream>

std::string size_string(size_t size) {
    std::stringstream ss;
    ss.precision(3);
    if (size >= 1 << 30)
        ss << size / float(1 << 30) << " GiB";
    else if (size >= 1 << 20)
        ss << size / float(1 << 20) << " MiB";
    else if (size >= 1 << 10)
        ss << size / float(1 << 10) << " KiB";
    else
        ss << size << " B";
    return ss.str();
}

std::string number_string(size_t num) {
    std::stringstream ss;
    ss.precision(3);
    if (num >= 1 << 30)
        ss << num / float(1 << 30) << " G";
    else if (num >= 1 << 20)
        ss << num / float(1 << 20) << " M";
    else if (num >= 1 << 10)
        ss << num / float(1 << 10) << " K";
    else
        ss << num << " B";
    return ss.str();
}
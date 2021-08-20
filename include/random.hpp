#pragma once

#include <sys/time.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <time.h>

#include <random>
#include <chrono>

#include "type.hpp"
#include "constants.hpp"
#include "log.hpp"

/**
 * Abstract class for RandNumGenerator
 */
class RandGen
{
public:
    /**
     * Generate random integer from [0, upper_bound)
     */
    virtual uint32_t gen(uint32_t upper_bound) = 0;
    /**
     * Generate random float from [0, upper_bound)
     */
    virtual float gen_float(float upper_bound) = 0;
    /**
     * Print the name of this generator
     */
    virtual std::string name() = 0;
    virtual ~RandGen() {}
};

/**
 * https://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
 */
class MTRandGen: public RandGen
{
    std::random_device *rd;
    std::mt19937 *mt;
    // padding: to avoid false sharing problem
    unsigned padding[CacheLineSize - sizeof(std::random_device*) - sizeof(std::mt19937*)];
public:
    MTRandGen()
    {
        rd = new std::random_device();
        mt = new std::mt19937((*rd)());
    }
    virtual ~MTRandGen()
    {
        delete mt;
        delete rd;
    }
    std::string name() {
        return std::string("std::mt19937");
    }
    uint32_t gen(uint32_t upper_bound)
    {
        std::uniform_int_distribution<vertex_id_t> dis(0, upper_bound - 1);
        return dis(*mt);
    }
    uint32_t gen_uint64(uint64_t upper_bound)
    {
        std::uniform_int_distribution<uint64_t> dis(0, upper_bound - 1);
        return dis(*mt);
    }
    float gen_float(float upper_bound)
    {
        std::uniform_real_distribution<float> dis(0.0, upper_bound);
        return dis(*mt);
    }
};

/**
 * https://linux.die.net/man/3/rand_r
 */
class RandrRandGen: public RandGen
{
    unsigned seed;
public:
    RandrRandGen()
    {
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<unsigned> dis(0, (unsigned) time(NULL));
        seed = dis(mt);
    }
    virtual ~RandrRandGen()
    {
    }
    std::string name() {
        return std::string("rand_r");
    }
    uint32_t gen(uint32_t upper_bound)
    {
        return rand_r(&seed) % upper_bound;
    }
    float gen_float(float upper_bound)
    {
        return (float) rand_r(&seed) / (float) RAND_MAX * upper_bound;
    }
};

/**
 * https://github.com/tmikolov/word2vec/blob/master/word2vec.c
 */
class MulRandGen : public RandGen
{
    uint64_t seed;
public:
    MulRandGen()
    {
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<uint64_t> dis(0, (unsigned) time(NULL));
        seed = dis(mt);
    }
    virtual ~MulRandGen()
    {
    }
    std::string name() {
        return std::string("multiplication");
    }
    uint32_t gen(uint32_t upper_bound)
    {
        // the mod of uint64_t is too slow
        uint32_t ret = (uint32_t) seed % upper_bound;
        seed = seed * (unsigned long long)25214903917 + 11;
        return ret;
    }
    float gen_float(float upper_bound)
    {
        uint32_t temp = seed & 0xFFFF;
        seed = seed * (unsigned long long)25214903917 + 11;
        return (float) temp / (float) 65535 * upper_bound;
    }
};

/**
 * https://en.wikipedia.org/wiki/Xorshift#xorshift*
 */
class XorRandGen : public RandGen
{
    uint64_t seed;
public:
    XorRandGen()
    {
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<uint64_t> dis(0, (unsigned) time(NULL));
        seed = dis(mt);
    }
    virtual ~XorRandGen()
    {
    }
    std::string name() {
        return std::string("xorshift*");
    }
    uint32_t gen(uint32_t upper_bound)
    {
        uint32_t ret = seed * UINT64_C(0x2545F4914F6CDD1D);
        seed ^= seed >> 12;
        seed ^= seed << 25;
        seed ^= seed >> 27;
        return ret % upper_bound;
    }
    float gen_float(float upper_bound)
    {
        uint32_t ret = (seed * UINT64_C(0x2545F4914F6CDD1D)) & 0xFFFF;
        seed ^= seed >> 12;
        seed ^= seed << 25;
        seed ^= seed >> 27;
        return (float) ret / (float) 65535 * upper_bound;
    }
};

// Set the default random number generator
typedef XorRandGen default_rand_t;

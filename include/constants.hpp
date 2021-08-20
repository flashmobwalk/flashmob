#pragma once

#define CacheLineSize 64
#define PageSize 4096
#define FMobDir "./.fmob"

#ifdef UNIT_TEST
    #define max_partition_num 64
    #define max_group_num 8
    #define min_partition_bits 0
    #define SimilarDegreeDirectSamplerMaxHintNum 2
#else
    #define max_partition_num 2048
    #define max_group_num 128
    #define min_partition_bits 4
    #define SimilarDegreeDirectSamplerMaxHintNum 8
#endif

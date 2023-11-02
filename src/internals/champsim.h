#ifndef CHAMPSIM_H
#define CHAMPSIM_H

#include <assert.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <string>

#include "instruction.h"

// USEFUL MACROS
//#define DEBUG_PRINT
#define SANITY_CHECK
#define LLC_BYPASS
#define DRC_BYPASS
#define NO_CRC2_COMPILE

// #define ENABLE_DCLR

#ifdef DEBUG_PRINT
#define DP(x) x
#else
#define DP(x)
#endif

// CPU
#define NUM_CPUS CHAMPSIM_CPU_NUMBER_CORE
#define CPU_FREQ 3800
// WIP: Setting the DRAM IO frequency based on the input from the CMake build system.
#define DRAM_IO_FREQ CHAMPSIM_CPU_DRAM_IO_FREQUENCY
#define PAGE_SIZE 4096
#define LOG2_PAGE_SIZE 12

// CACHE
#define BLOCK_SIZE 64
#define LOG2_BLOCK_SIZE 6
#define MAX_READ_PER_CYCLE 8
#define MAX_FILL_PER_CYCLE 1

#define INFLIGHT 1
#define COMPLETED 2
#define PARTIALLY_COMPLETED 3

#define FILL_L1 1
#define FILL_L2 2
#define FILL_LLC 4
#define FILL_DRC 8
#define FILL_DRAM 16

// DRAM
#define DRAM_CHANNELS \
    2  // default: assuming one DIMM per one channel 4GB * 1 => 4GB off-chip
       // memory
#define LOG2_DRAM_CHANNELS 1
#define DRAM_RANKS 2  // 512MB * 8 ranks => 4GB per DIMM
#define LOG2_DRAM_RANKS 1
#define DRAM_BANKS 8  // 64MB * 8 banks => 512MB per rank
#define LOG2_DRAM_BANKS 3
#define DRAM_ROWS 65536  // 2KB * 32K rows => 64MB per bank
#define LOG2_DRAM_ROWS 16
#define DRAM_COLUMNS \
    128  // 64B * 32 column chunks (Assuming 1B DRAM cell * 8 chips * 8
         // transactions = 64B size of column chunks) => 2KB per row
#define LOG2_DRAM_COLUMNS 7
#define DRAM_ROW_SIZE (BLOCK_SIZE * DRAM_COLUMNS / 1024)

#define DRAM_SIZE \
    (DRAM_CHANNELS * DRAM_RANKS * DRAM_BANKS * DRAM_ROWS * DRAM_ROW_SIZE / 1024)
#define DRAM_PAGES ((DRAM_SIZE << 10) >> 2)
//#define DRAM_PAGES 10

using namespace std;

void print_stats();
uint64_t rotl64(uint64_t n, unsigned int c), rotr64(uint64_t n, unsigned int c),
    va_to_pa(uint32_t cpu, uint64_t instr_id, uint64_t va,
             uint64_t unique_vpage, uint8_t is_code);
uint32_t folded_xor(uint64_t value, uint32_t num_folds);
uint64_t jenkins_hash(uint64_t key);
uint64_t fnv1a64(uint64_t key);

// log base 2 function from efectiu
int lg2(int n);

// smart random number generator
class RANDOM {
   public:
    std::random_device rd;
    std::mt19937_64 engine{rd()};
    std::uniform_int_distribution<uint64_t> dist{
        0, 0xFFFFFFFFF};  // used to generate random physical page numbers

    RANDOM(uint64_t seed) { engine.seed(seed); }

    uint64_t draw_rand() { return dist(engine); };
};

class helper {
   public:
    static uint32_t SWAP_LATENCY, PAGE_TABLE_LATENCY;

    static uint8_t MAX_INSTR_DESTINATIONS,
        knob_cloudsuite, knob_low_bandwidth;

    static uint64_t last_drc_read_mode, last_drc_write_mode,
        drc_blocks, champsim_seed;

    static queue<uint64_t> page_queue;
    static map<uint64_t, uint64_t> page_table, inverse_table, recent_page,
        unique_cl[NUM_CPUS];
    static uint64_t previous_ppage, num_adjacent_page, num_cl[NUM_CPUS],
        allocated_pages, num_page[NUM_CPUS], minor_fault[NUM_CPUS],
        major_fault[NUM_CPUS];

    static RANDOM champsim_rand;
};

namespace champsim::components {
    struct uarch_state_info {
        bool first_access, went_offchip_pred;
        uint64_t pc, last_n_load_pc_sig, last_n_pc_sig, last_n_vpn_sig, vaddr,
            vpage, voffset;
        std::size_t data_index;
        uint32_t v_cl_offset, v_cl_word_offset, v_cl_dword_offset;
    };
}

#endif

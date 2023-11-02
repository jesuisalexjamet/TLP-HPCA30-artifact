#ifndef __CHAMPSIM_PLUGINS_PREFETCHERS_L1D_BERTI_HELPERS_HH__
#define __CHAMPSIM_PLUGINS_PREFETCHERS_L1D_BERTI_HELPERS_HH__

#include <cstdint>
#
#include <internals/champsim.h>

#include <internals/simulator.hh>

#define L1D_PAGE_BLOCKS_BITS (LOG2_PAGE_SIZE - LOG2_BLOCK_SIZE)
#define L1D_PAGE_BLOCKS (1 << L1D_PAGE_BLOCKS_BITS)
#define L1D_PAGE_OFFSET_MASK (L1D_PAGE_BLOCKS - 1)

#define L1D_BERTI_THROTTLING 1
#define L1D_BURST_THROTTLING 7

#define L1D_BURST_THRESHOLD 0.99

#define CONTINUE_BURST
#define PREFETCH_FOR_LONG_REUSE
#define LONG_REUSE_LIMIT 16

// #define BERTI_LATENCIES
// #define JUST_BERTI // No compensation for holes
#define LINNEA
#define WARMUP_NEW_PAGES

#define L1D_TIME_BITS 16
#define L1D_TIME_OVERFLOW ((uint64_t)1 << L1D_TIME_BITS)
#define L1D_TIME_MASK (L1D_TIME_OVERFLOW - 1)

#define L1D_CURRENT_PAGES_TABLE_INDEX_BITS 6
#define L1D_CURRENT_PAGES_TABLE_ENTRIES          \
    ((1 << L1D_CURRENT_PAGES_TABLE_INDEX_BITS) - \
     1)  // Null pointer for prev_request
#define L1D_CURRENT_PAGES_TABLE_NUM_BERTI 8
#define L1D_CURRENT_PAGES_TABLE_NUM_BERTI_PER_ACCESS \
    8  // Better if not more than throttling

#define L1D_PREV_REQUESTS_TABLE_INDEX_BITS 10
#define L1D_PREV_REQUESTS_TABLE_ENTRIES \
    (1 << L1D_PREV_REQUESTS_TABLE_INDEX_BITS)
#define L1D_PREV_REQUESTS_TABLE_MASK (L1D_PREV_REQUESTS_TABLE_ENTRIES - 1)
#define L1D_PREV_REQUESTS_TABLE_NULL_POINTER L1D_CURRENT_PAGES_TABLE_ENTRIES

#define L1D_LATENCIES_TABLE_INDEX_BITS 10
#define L1D_LATENCIES_TABLE_ENTRIES (1 << L1D_LATENCIES_TABLE_INDEX_BITS)
#define L1D_LATENCIES_TABLE_MASK (L1D_LATENCIES_TABLE_ENTRIES - 1)
#define L1D_LATENCIES_TABLE_NULL_POINTER L1D_CURRENT_PAGES_TABLE_ENTRIES

#define L1D_RECORD_PAGES_TABLE_INDEX_BITS 14
#define L1D_RECORD_PAGES_TABLE_ENTRIES \
    ((1 << L1D_RECORD_PAGES_TABLE_INDEX_BITS) - 1)  // Null pointer for ip table
#define L1D_TRUNCATED_PAGE_ADDR_BITS 32             // 4 bytes
#define L1D_TRUNCATED_PAGE_ADDR_MASK \
    (((uint64_t)1 << L1D_TRUNCATED_PAGE_ADDR_BITS) - 1)

#define L1D_IP_TABLE_INDEX_BITS 12
#define L1D_IP_TABLE_ENTRIES (1 << L1D_IP_TABLE_INDEX_BITS)
#define L1D_IP_TABLE_INDEX_MASK (L1D_IP_TABLE_ENTRIES - 1)

namespace champsim {
namespace prefetchers {

/**
 * @brief
 *
 */
typedef struct __l1d_current_page_entry {
    uint64_t page_addr;                                       // 52 bits
    uint64_t u_vector;                                        // 64 bits
    int berti[L1D_CURRENT_PAGES_TABLE_NUM_BERTI];             // 70 bits
    unsigned berti_score[L1D_CURRENT_PAGES_TABLE_NUM_BERTI];  // XXX bits
    int current_berti;                                        // 7 bits
    int stride;  // Divide tables. Long reuse do not need to calculate berties
    bool short_reuse;     // 1 bit
    bool continue_burst;  // 1 bit
    uint64_t lru;         // 6 bits
} l1d_current_page_entry;

/**
 * @brief
 *
 */
typedef struct __l1d_prev_request_entry {
    uint64_t page_addr_pointer;  // 6 bits
    uint64_t offset;             // 6 bits
    uint64_t time;               // 16 bits
} l1d_prev_request_entry;

/**
 * @brief We do not have access to the MSHR, so we aproximate it using this
 * structure.
 *
 */
typedef struct __l1d_latency_entry {
    uint64_t page_addr_pointer;  // 6 bits
    uint64_t offset;             // 6 bits
    uint64_t
        time_lat;    // 16 bits // time if not completed, latency if completed
    bool completed;  // 1 bit
} l1d_latency_entry;

/**
 * @brief
 *
 */
typedef struct __l1d_record_page_entry {
    uint64_t page_addr;    // 4 bytes
    uint64_t linnea;       // 8 bytes
    uint64_t last_offset;  // 6 bits
    bool short_reuse;      // 1 bit
    uint64_t lru;          // 10 bits
} l1d_record_page_entry;

/**
 * @brief
 *
 */
typedef struct __l1d_ip_entry {
    bool current;          // 1 bit
    int berti_or_pointer;  // 7 bits // Berti if current == 0
    bool consecutive;      // 1 bit
    bool short_reuse;      // 1 bit
} l1d_ip_entry;

static l1d_current_page_entry
    l1d_current_pages_table[L1D_CURRENT_PAGES_TABLE_ENTRIES];
static l1d_prev_request_entry
    l1d_prev_requests_table[L1D_PREV_REQUESTS_TABLE_ENTRIES];
static uint64_t l1d_prev_requests_table_head;
static l1d_latency_entry l1d_latencies_table[L1D_LATENCIES_TABLE_ENTRIES];
static uint64_t l1d_latencies_table_head;
static l1d_record_page_entry
    l1d_record_pages_table[L1D_RECORD_PAGES_TABLE_ENTRIES];
static l1d_ip_entry l1d_ip_table[L1D_IP_TABLE_ENTRIES];

// Stats
static uint64_t l1d_ip_misses[L1D_IP_TABLE_ENTRIES];
static uint64_t l1d_ip_hits[L1D_IP_TABLE_ENTRIES];
static uint64_t l1d_ip_late[L1D_IP_TABLE_ENTRIES];
static uint64_t l1d_ip_early[L1D_IP_TABLE_ENTRIES];
static uint64_t l1d_stats_pref_addr;
static uint64_t l1d_stats_pref_ip;
static uint64_t l1d_stats_pref_current;
static uint64_t cache_accesses;
static uint64_t cache_misses;

uint64_t l1d_get_latency(uint64_t cycle, uint64_t cycle_prev);
int l1d_calculate_stride(uint64_t prev_offset, uint64_t current_offset);
uint64_t l1d_count_bit_vector(uint64_t vector);
uint64_t l1d_count_wrong_berti_bit_vector(uint64_t vector, int berti);
uint64_t l1d_count_lost_berti_bit_vector(uint64_t vector, int berti);
bool l1d_all_last_berti_accessed_bit_vector(uint64_t vector, int berti);

void l1d_init_current_pages_table();
uint64_t l1d_get_current_pages_entry(uint64_t page_addr);
void l1d_update_lru_current_pages_table(uint64_t index);
uint64_t l1d_get_lru_current_pages_entry();
void l1d_add_current_pages_table(uint64_t index, uint64_t page_addr);
void l1d_update_current_pages_table(uint64_t index, uint64_t offset);
void l1d_remove_offset_current_pages_table(uint64_t index, uint64_t offset);
void l1d_add_berti_current_pages_table(uint64_t index, int *berti,
                                       unsigned *saved_cycles);
void l1d_sub_berti_current_pages_table(uint64_t index, int distance);
int l1d_get_berti_current_pages_table(uint64_t index);
bool l1d_offset_requested_current_pages_table(uint64_t index, uint64_t offset);

void l1d_init_prev_requests_table();
uint64_t l1d_find_prev_request_entry(uint64_t pointer, uint64_t offset);
void l1d_add_prev_requests_table(uint64_t pointer, uint64_t offset,
                                 uint64_t cycle);
void l1d_reset_pointer_prev_requests(uint64_t pointer);
void l1d_get_berti_prev_requests_table(uint64_t pointer, uint64_t offset,
                                       uint64_t latency, int *berti,
                                       unsigned *saved_cycles,
                                       uint64_t req_time);

void l1d_init_latencies_table();
uint64_t l1d_find_latency_entry(uint64_t pointer, uint64_t offset);
void l1d_add_latencies_table(uint64_t pointer, uint64_t offset, uint64_t cycle);
void l1d_reset_pointer_latencies(uint64_t pointer);
void l1d_reset_entry_latencies_table(uint64_t pointer, uint64_t offset);
uint64_t l1d_get_and_set_latency_latencies_table(uint64_t pointer,
                                                 uint64_t offset,
                                                 uint64_t cycle);
uint64_t l1d_get_latency_latencies_table(uint64_t pointer, uint64_t offset);
bool l1d_ongoing_request(uint64_t pointer, uint64_t offset);
bool l1d_is_request(uint64_t pointer, uint64_t offset);

void l1d_init_record_pages_table();
uint64_t l1d_get_lru_record_pages_entry();
void l1d_update_lru_record_pages_table(uint64_t index);
uint64_t l1d_get_entry_record_pages_table(uint64_t page_addr);
void l1d_add_record_pages_table(uint64_t page_addr, uint64_t new_page_addr,
                                uint64_t last_offset = 0,
                                bool short_reuse = true);

void l1d_init_ip_table();
void l1d_update_ip_table(int pointer, int berti, int stride, bool short_reuse);
uint64_t l1d_evict_lru_current_page_entry();
void l1d_evict_current_page_entry(uint64_t index);
void l1d_remove_current_table_entry(uint64_t index);
}  // namespace prefetchers
}  // namespace champsim

#endif  // __CHAMPSIM_PLUGINS_PREFETCHERS_L1D_BERTI_HELPERS_HH__
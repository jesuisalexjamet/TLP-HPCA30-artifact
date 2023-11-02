#ifndef OOO_CPU_H
#define OOO_CPU_H

#include <vector>
#
#include <chrono>
#
#include "cache.h"
#
#include <instruction_reader.hh>
#
#include <internals/components/cache.hh>
#include <internals/components/irreg_access_pred.hh>
#include <internals/components/miss_map.hh>
#include <internals/components/offchip_pred_perc.hh>
#
#include <internals/policies/fill_path_policies.hh>

#ifdef CRC2_COMPILE
#define STAT_PRINTING_PERIOD 1000000
#else
#define STAT_PRINTING_PERIOD 10000000
#endif
#define DEADLOCK_CYCLE 10000000

using namespace std;
using namespace champsim::cpu;

namespace cc = champsim::components;
namespace cpol = champsim::policies;

// CORE PROCESSOR
#define FETCH_WIDTH \
    6 /*!< The number of instructions that can be fetched in one cycle. */
#define DECODE_WIDTH \
    5 /*!< The number of instructions that can be decoded in one cycle. */
#define EXEC_WIDTH                                                           \
    4 /*!< The number of non-memory instructions that can be executed in one \
         cycle. */
#define LQ_WIDTH                                                              \
    2 /*!< The number of load instructions that can be executed in one cycle. \
       */
#define SQ_WIDTH                                                               \
    2 /*!< The number of store instructions that can be executed in one cycle. \
       */
#define RETIRE_WIDTH \
    5 /*!< The number of instructions that can be retired in one cycle. */
#define SCHEDULER_SIZE 97 /*!<  */
#define BRANCH_MISPREDICT_PENALTY 1
//#define SCHEDULING_LATENCY 0
//#define EXEC_LATENCY 0
//#define DECODE_LATENCY 2

#define STA_SIZE (ROB_SIZE * NUM_INSTR_DESTINATIONS_SPARC)

extern uint32_t SCHEDULING_LATENCY, EXEC_LATENCY, DECODE_LATENCY;

namespace champsim {
namespace cpu {
/**
 * @brief This class provides a framework to describe the modeled CPUs
 * that should be instanciated in order to run the simulation. Such
 * a software architectural design choice allows the user to, virtually,
 * run simulation with any combination of CPUs one can imagine.
 */
struct cpu_descriptor {
   public:
    bool sdc_enabled;

    uint8_t cpu_id;

    uint8_t lp_latency;

    uint32_t warmup_instructions, simulation_instructions;

    uint32_t fetch_width, decode_width, exec_width, load_queue_width,
        store_queue_width, retire_width, scheduler_size,
        branch_misprediction_penalty;

    uint64_t stride_threshold;
    uint64_t irreg_pred_entries, irreg_pred_sets, irreg_pred_ways;

    uint8_t irreg_pred_stride_bits, psel_bits;

    uint64_t metadata_cache_sets, metadata_cache_ways;
    uint64_t pld_threshold_1, pld_threshold_2;

    uint64_t l1d_prefetching_psel_bits, l1d_prefetching_psel_threshold;

    CACHE *llc;

    std::string trace_file;

    std::string l1d_config_file, l1i_config_file, l2c_config_file,
        sdc_config_file;

   public:
    cpu_descriptor() = default;
};
}  // namespace cpu
}  // namespace champsim

// cpu
class O3_CPU {
   private:
    bool _warmup_complete, _simulation_complete;

    uint64_t _current_core_cycle, _stall_cycle;

   public:
    uint32_t cpu;

    // trace
    FILE *trace_file;
    char trace_string[1024];
    char gunzip_command[1024];

    // instruction
#if defined(LEGACY_TRACE)
    input_instr next_instr, current_instr;
#else
    x86_trace_instruction next_instr, current_instr;
#endif
    cloudsuite_instr current_cloudsuite_instr;
    uint64_t instr_unique_id, completed_executions, begin_sim_cycle,
        begin_sim_instr, last_sim_cycle, last_sim_instr, finish_sim_cycle,
        finish_sim_instr, warmup_instructions, simulation_instructions,
        instrs_to_read_this_cycle, instrs_to_fetch_this_cycle,
        next_print_instruction, num_retired;
    uint32_t inflight_reg_executions, inflight_mem_executions, num_searched;
    uint32_t next_ITLB_fetch;

    // reorder buffer, load/store queue, register file
    CORE_BUFFER IFETCH_BUFFER{"IFETCH_BUFFER", FETCH_WIDTH};
    CORE_BUFFER DECODE_BUFFER{"DECODE_BUFFER", DECODE_WIDTH};
    CORE_BUFFER ROB{"ROB", ROB_SIZE};
    LOAD_STORE_QUEUE LQ{"LQ", LQ_SIZE}, SQ{"SQ", SQ_SIZE};

    // store array, this structure is required to properly handle store
    // instructions
    uint64_t STA[STA_SIZE], STA_head, STA_tail;

    // Ready-To-Execute
    uint32_t RTE0[ROB_SIZE], RTE0_head, RTE0_tail, RTE1[ROB_SIZE], RTE1_head,
        RTE1_tail;

    // Ready-To-Load
    uint32_t RTL0[LQ_SIZE], RTL0_head, RTL0_tail, RTL1[LQ_SIZE], RTL1_head,
        RTL1_tail;

    // Ready-To-Store
    uint32_t RTS0[SQ_SIZE], RTS0_head, RTS0_tail, RTS1[SQ_SIZE], RTS1_head,
        RTS1_tail;

    // branch
    int branch_mispredict_stall_fetch;  // flag that says that we should stall
                                        // because a branch prediction was wrong
    int mispredicted_branch_iw_index;  // index in the instruction window of the
                                       // mispredicted branch.  fetch resumes
                                       // after the instruction at this index
                                       // executes
    uint8_t fetch_stall;
    uint64_t fetch_resume_cycle;
    uint64_t num_branch, branch_mispredictions;
    uint64_t total_rob_occupancy_at_branch_mispredict;
    uint64_t total_branch_types[8];

    // TLBs and caches
    CACHE ITLB{"ITLB",       ITLB_SET,     ITLB_WAY,     ITLB_SET *ITLB_WAY,
               ITLB_WQ_SIZE, ITLB_RQ_SIZE, ITLB_PQ_SIZE, ITLB_MSHR_SIZE},
        DTLB{"DTLB",       DTLB_SET,     DTLB_WAY,     DTLB_SET *DTLB_WAY,
             DTLB_WQ_SIZE, DTLB_RQ_SIZE, DTLB_PQ_SIZE, DTLB_MSHR_SIZE},
        STLB{"STLB",       STLB_SET,     STLB_WAY,     STLB_SET *STLB_WAY,
             STLB_WQ_SIZE, STLB_RQ_SIZE, STLB_PQ_SIZE, STLB_MSHR_SIZE};

    cc::location_map _location_map;
    cc::metadata_cache _mm;

    cc::irreg_access_pred irreg_pred;
    std::map<bool, uint64_t> pred_results;

    std::list<trace_header::irreg_array_boundaries> _irreg_boundaries;

    std::map<cc::cache_type, uint64_t> misses_in_cache;
    std::list<uint64_t> _block_ids;

    bool sdc_enabled;

    cc::cache *l1i, *l1d, *l2c, *sdc;

    std::map<cc::cache_type, uint64_t> _request_served_positions;
    std::map<uint64_t, uint64_t> _pc_table;

    std::chrono::time_point<std::chrono::system_clock> _last_heartbeat_time;

    // Fill path policy.
    cpol::abstract_fill_path_policy *fill_path_policy;

    cc::offchip_predictor_perceptron *offchip_pred;
    int32_t write_alloc_psel;

    // trace cache for previously decoded instructions

    // constructor
    O3_CPU();
    ~O3_CPU();

    bool &warmup_complete();
    bool &simulation_complete();

    const uint64_t &current_core_cycle() const;
    uint64_t& stall_cycle();
    void inc_current_core_cycle();

    void print_hearbeat();
    void finish_warmup();
    void finish_simulation();

    // functions
    bool should_read_instruction() const;
    void initialize_instruction(ooo_model_instr &instr);
    void read_from_trace(), fetch_instruction(), decode_and_dispatch(),
        schedule_instruction(), execute_instruction(),
        schedule_memory_instruction(), execute_memory_instruction(),
        do_scheduling(uint32_t rob_index), reg_dependency(uint32_t rob_index),
        do_execution(uint32_t rob_index),
        do_memory_scheduling(uint32_t rob_index), operate_lsq(),
        complete_execution(uint32_t rob_index),
        reg_RAW_dependency(uint32_t prior, uint32_t current,
                           uint32_t source_index),
        reg_RAW_release(uint32_t rob_index),
        mem_RAW_dependency(uint32_t prior, uint32_t current,
                           uint32_t data_index, uint32_t lq_index),
        handle_merged_translation(PACKET *provider),
        handle_merged_load(PACKET *provider),
        release_load_queue(uint32_t lq_index),
        complete_instr_fetch(PACKET_QUEUE *queue, uint8_t is_it_tlb),
        complete_data_fetch(PACKET_QUEUE *queue, uint8_t is_it_tlb);

    void offchip_pred_stats_and_training(const std::size_t &lq_index);
    void issue_ddrp_request(const std::size_t &lq_index, const bool &from_l1d_miss = false);
    void issue_ddrp_request_on_prefetch(const PACKET &packet);
    void issue_dclr_request(const std::size_t &lq_index, const cc::block_location &loc);
    cc::block_location dclr_predict_location_perfect(const uint64_t &paddr);

    void initialize_core(
        const champsim::cpu::cpu_descriptor &desc = cpu_descriptor());
    void add_load_queue(uint32_t rob_index, uint32_t data_index),
        add_store_queue(uint32_t rob_index, uint32_t data_index),
        execute_store(uint32_t rob_index, uint32_t sq_index,
                      uint32_t data_index);
    int execute_load(uint32_t rob_index, uint32_t sq_index,
                     uint32_t data_index);
    void check_dependency(int prior, int current);
    void operate_cache();
    void update_rob();
    void retire_rob();

    uint32_t add_to_rob(ooo_model_instr *arch_instr),
        check_rob(uint64_t instr_id);

    uint32_t add_to_ifetch_buffer(ooo_model_instr *arch_instr);
    uint32_t add_to_decode_buffer(ooo_model_instr *arch_instr);

    uint32_t check_and_add_lsq(uint32_t rob_index);

    uint8_t mem_reg_dependence_resolved(uint32_t rob_index);

    // branch predictor
    uint8_t predict_branch(uint64_t ip);
    void initialize_branch_predictor(),
        last_branch_result(uint64_t ip, uint8_t taken);

    // code prefetching
    void l1i_prefetcher_initialize();
    void l1i_prefetcher_branch_operate(uint64_t ip, uint8_t branch_type,
                                       uint64_t branch_target);
    void l1i_prefetcher_cache_operate(uint64_t v_addr, uint8_t cache_hit,
                                      uint8_t prefetch_hit);
    void l1i_prefetcher_cycle_operate();
    void l1i_prefetcher_cache_fill(uint64_t v_addr, uint32_t set, uint32_t way,
                                   uint8_t prefetch, uint64_t evicted_v_addr);
    void l1i_prefetcher_final_stats();
    int prefetch_code_line(uint64_t pf_v_addr);

    bool is_irreg_data(const uint64_t &vaddr);
    const std::list<trace_header::irreg_array_boundaries>
        &irreg_data_boundaries() const;
};

extern O3_CPU ooo_cpu[NUM_CPUS];

#endif

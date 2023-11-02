#define _BSD_SOURCE

#include <getopt.h>
#include <internals/ooo_cpu.h>
#include <internals/uncore.h>

#include <fstream>
#
#include <internals/simulator.hh>
#
#include <internals/components/sectored_cache.hh>
#
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

using namespace champsim::instrumentations;

namespace cpol = champsim::policies;

static champsim::simulator* simulator = champsim::simulator::instance();

// uint8_t warmup_complete[NUM_CPUS],
//         simulation_complete[NUM_CPUS],
//         all_warmup_complete = 0,
//         all_simulation_complete = 0,
//         MAX_INSTR_DESTINATIONS = NUM_INSTR_DESTINATIONS,
//         knob_cloudsuite = 0,
//         knob_low_bandwidth = 0;

uint64_t warmup_instructions = 1000000, simulation_instructions = 10000000;

// PAGE TABLE
// uint32_t PAGE_TABLE_LATENCY = 0, SWAP_LATENCY = 0;
queue<uint64_t> page_queue;
map<uint64_t, uint64_t> page_table, inverse_table, recent_page,
    unique_cl[NUM_CPUS];
uint64_t previous_ppage, num_adjacent_page, num_cl[NUM_CPUS], allocated_pages,
    num_page[NUM_CPUS], minor_fault[NUM_CPUS], major_fault[NUM_CPUS];

uint64_t access_window_size = 0;

static std::map<cc::cache_type, std::string> cache_type_map = {
    {champsim::components::is_l1i, "L1I"},
    {champsim::components::is_l1d, "L1D"},
    {champsim::components::is_l2c, "L2C"},
    {champsim::components::is_llc, "LLC"},
    {champsim::components::is_dram, "DRAM"},
    {champsim::components::is_sdc, "SDC"},
};

void record_roi_stats(uint32_t cpu, CACHE* cache) {
    for (uint32_t i = 0; i < NUM_TYPES; i++) {
        cache->roi_access[cpu][i] = cache->sim_access[cpu][i];
        cache->roi_hit[cpu][i] = cache->sim_hit[cpu][i];
        cache->roi_miss[cpu][i] = cache->sim_miss[cpu][i];
    }
}

void print_roi_stats(uint32_t cpu, CACHE* cache) {
    uint64_t TOTAL_ACCESS = 0, TOTAL_HIT = 0, TOTAL_MISS = 0;

    for (uint32_t i = 0; i < NUM_TYPES; i++) {
        TOTAL_ACCESS += cache->roi_access[cpu][i];
        TOTAL_HIT += cache->roi_hit[cpu][i];
        TOTAL_MISS += cache->roi_miss[cpu][i];
    }

    cout << cache->NAME;
    cout << " TOTAL     ACCESS: " << setw(10) << TOTAL_ACCESS
         << "  HIT: " << setw(10) << TOTAL_HIT << "  MISS: " << setw(10)
         << TOTAL_MISS << endl;

    cout << cache->NAME;
    cout << " LOAD      ACCESS: " << setw(10) << cache->roi_access[cpu][0]
         << "  HIT: " << setw(10) << cache->roi_hit[cpu][0]
         << "  MISS: " << setw(10) << cache->roi_miss[cpu][0] << endl;

    cout << cache->NAME;
    cout << " RFO       ACCESS: " << setw(10) << cache->roi_access[cpu][1]
         << "  HIT: " << setw(10) << cache->roi_hit[cpu][1]
         << "  MISS: " << setw(10) << cache->roi_miss[cpu][1] << endl;

    cout << cache->NAME;
    cout << " PREFETCH  ACCESS: " << setw(10) << cache->roi_access[cpu][2]
         << "  HIT: " << setw(10) << cache->roi_hit[cpu][2]
         << "  MISS: " << setw(10) << cache->roi_miss[cpu][2] << endl;

    cout << cache->NAME;
    cout << " WRITEBACK ACCESS: " << setw(10) << cache->roi_access[cpu][3]
         << "  HIT: " << setw(10) << cache->roi_hit[cpu][3]
         << "  MISS: " << setw(10) << cache->roi_miss[cpu][3] << endl;

    cout << cache->NAME;
    cout << " PREFETCH  REQUESTED: " << setw(10) << cache->pf_requested
         << "  ISSUED: " << setw(10) << cache->pf_issued;
    cout << "  USEFUL: " << setw(10) << cache->pf_useful
         << "  USELESS: " << setw(10) << cache->pf_useless << endl;

    cout << cache->NAME;
    cout << " AVERAGE MISS LATENCY: "
         << (1.0 * (cache->total_miss_latency)) / TOTAL_MISS << " cycles"
         << endl;
    // cout << " AVERAGE MISS LATENCY: " <<
    // (cache->total_miss_latency)/TOTAL_MISS << " cycles " <<
    // cache->total_miss_latency << "/" << TOTAL_MISS<< endl;
}

void print_sim_stats(uint32_t cpu, CACHE* cache) {
    uint64_t TOTAL_ACCESS = 0, TOTAL_HIT = 0, TOTAL_MISS = 0;

    for (uint32_t i = 0; i < NUM_TYPES; i++) {
        TOTAL_ACCESS += cache->sim_access[cpu][i];
        TOTAL_HIT += cache->sim_hit[cpu][i];
        TOTAL_MISS += cache->sim_miss[cpu][i];
    }

    cout << cache->NAME;
    cout << " TOTAL     ACCESS: " << setw(10) << TOTAL_ACCESS
         << "  HIT: " << setw(10) << TOTAL_HIT << "  MISS: " << setw(10)
         << TOTAL_MISS << endl;

    cout << cache->NAME;
    cout << " LOAD      ACCESS: " << setw(10) << cache->sim_access[cpu][0]
         << "  HIT: " << setw(10) << cache->sim_hit[cpu][0]
         << "  MISS: " << setw(10) << cache->sim_miss[cpu][0] << endl;

    cout << cache->NAME;
    cout << " RFO       ACCESS: " << setw(10) << cache->sim_access[cpu][1]
         << "  HIT: " << setw(10) << cache->sim_hit[cpu][1]
         << "  MISS: " << setw(10) << cache->sim_miss[cpu][1] << endl;

    cout << cache->NAME;
    cout << " PREFETCH  ACCESS: " << setw(10) << cache->sim_access[cpu][2]
         << "  HIT: " << setw(10) << cache->sim_hit[cpu][2]
         << "  MISS: " << setw(10) << cache->sim_miss[cpu][2] << endl;

    cout << cache->NAME;
    cout << " WRITEBACK ACCESS: " << setw(10) << cache->sim_access[cpu][3]
         << "  HIT: " << setw(10) << cache->sim_hit[cpu][3]
         << "  MISS: " << setw(10) << cache->sim_miss[cpu][3] << endl;
}

void print_branch_stats() {
    for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);

        cout << endl << "CPU " << i << " Branch Prediction Accuracy: ";
        cout << (100.0 *
                 (curr_cpu->num_branch - curr_cpu->branch_mispredictions)) /
                    curr_cpu->num_branch;
        cout << "% MPKI: "
             << (1000.0 * curr_cpu->branch_mispredictions) /
                    (curr_cpu->num_retired - curr_cpu->warmup_instructions);
        cout << " Average ROB Occupancy at Mispredict: "
             << (1.0 * curr_cpu->total_rob_occupancy_at_branch_mispredict) /
                    curr_cpu->branch_mispredictions
             << endl
             << endl;

        cout << "Branch types" << endl;
        cout << "NOT_BRANCH: " << curr_cpu->total_branch_types[0] << " "
             << (100.0 * curr_cpu->total_branch_types[0]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl;
        cout << "BRANCH_DIRECT_JUMP: " << curr_cpu->total_branch_types[1] << " "
             << (100.0 * curr_cpu->total_branch_types[1]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl;
        cout << "BRANCH_INDIRECT: " << curr_cpu->total_branch_types[2] << " "
             << (100.0 * curr_cpu->total_branch_types[2]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl;
        cout << "BRANCH_CONDITIONAL: " << curr_cpu->total_branch_types[3] << " "
             << (100.0 * curr_cpu->total_branch_types[3]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl;
        cout << "BRANCH_DIRECT_CALL: " << curr_cpu->total_branch_types[4] << " "
             << (100.0 * curr_cpu->total_branch_types[4]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl;
        cout << "BRANCH_INDIRECT_CALL: " << curr_cpu->total_branch_types[5]
             << " "
             << (100.0 * curr_cpu->total_branch_types[5]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl;
        cout << "BRANCH_RETURN: " << curr_cpu->total_branch_types[6] << " "
             << (100.0 * curr_cpu->total_branch_types[6]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl;
        cout << "BRANCH_OTHER: " << curr_cpu->total_branch_types[7] << " "
             << (100.0 * curr_cpu->total_branch_types[7]) /
                    (curr_cpu->num_retired - curr_cpu->begin_sim_instr)
             << "%" << endl
             << endl;
    }
}

void print_dram_stats() {
    cout << endl;
    cout << "DRAM Statistics" << endl;
    for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
        cout << " CHANNEL " << i << endl;
        cout << " RQ ROW_BUFFER_HIT: " << setw(10)
             << uncore.DRAM.RQ[i].ROW_BUFFER_HIT
             << "  ROW_BUFFER_MISS: " << setw(10)
             << uncore.DRAM.RQ[i].ROW_BUFFER_MISS << endl;
        cout << " DBUS_CONGESTED: " << setw(10)
             << uncore.DRAM.dbus_congested[NUM_TYPES][NUM_TYPES] << endl;
        cout << " WQ ROW_BUFFER_HIT: " << setw(10)
             << uncore.DRAM.WQ[i].ROW_BUFFER_HIT
             << "  ROW_BUFFER_MISS: " << setw(10)
             << uncore.DRAM.WQ[i].ROW_BUFFER_MISS;
        cout << "  FULL: " << setw(10) << uncore.DRAM.WQ[i].FULL << endl;
        cout << endl;
    }

    uint64_t total_congested_cycle = 0;
    for (uint32_t i = 0; i < DRAM_CHANNELS; i++)
        total_congested_cycle += uncore.DRAM.dbus_cycle_congested[i];
    if (uncore.DRAM.dbus_congested[NUM_TYPES][NUM_TYPES])
        cout << " AVG_CONGESTED_CYCLE: "
             << (total_congested_cycle /
                 uncore.DRAM.dbus_congested[NUM_TYPES][NUM_TYPES])
             << endl;
    else
        cout << " AVG_CONGESTED_CYCLE: -" << endl;
}

void finish_warmup() {
    // uint64_t elapsed_second = (uint64_t)(time(NULL) - start_time),
    //          elapsed_minute = elapsed_second / 60,
    //          elapsed_hour = elapsed_minute / 60;
    // elapsed_minute -= elapsed_hour*60;
    // elapsed_second -= (elapsed_hour*3600 + elapsed_minute*60);

    // reset core latency
    // note: since re-ordering he function calls in the main simulation loop,
    // it's no longer necessary to add
    //       extra latency for scheduling and execution, unless you want these
    //       steps to take longer than 1 cycle.
    // SCHEDULING_LATENCY = 0;
    // EXEC_LATENCY = 0;
    // DECODE_LATENCY = 2;
    // PAGE_TABLE_LATENCY = 100;
    // SWAP_LATENCY = 100000;

    cout << endl;
    for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);

        curr_cpu->STLB.pte_usages.clear();
        curr_cpu->STLB.stlb_prediction_accurate.clear();

        curr_cpu->STLB.doa = 0;
        curr_cpu->STLB.no_doa = 0;

        curr_cpu->STLB.pte_used_onchip = 0;
        curr_cpu->STLB.pte_used_offchip = 0;
        curr_cpu->STLB.pte_unused_offchip = 0;
        curr_cpu->STLB.pte_unused_onchip = 0;

        curr_cpu->offchip_pred->reset_stats();
        curr_cpu->_mm.pbp().clear_stats();

        // reset DRAM stats
        for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
            uncore.DRAM.RQ[i].ROW_BUFFER_HIT = 0;
            uncore.DRAM.RQ[i].ROW_BUFFER_MISS = 0;
            uncore.DRAM.WQ[i].ROW_BUFFER_HIT = 0;
            uncore.DRAM.WQ[i].ROW_BUFFER_MISS = 0;
        }
    }

    uncore.llc->reset_stats();

    // Switching the simulator to the actual simulation phase.
    champsim::simulator::instance()->start_simulation();
}

void print_deadlock(uint32_t i) {
    O3_CPU* curr_cpu = simulator->modeled_cpu(i);

    cout << "DEADLOCK! CPU " << i
         << " instr_id: " << curr_cpu->ROB.entry[curr_cpu->ROB.head].instr_id;
    cout << " translated: "
         << +curr_cpu->ROB.entry[curr_cpu->ROB.head].translated;
    cout << " fetched: " << +curr_cpu->ROB.entry[curr_cpu->ROB.head].fetched;
    cout << " scheduled: "
         << +curr_cpu->ROB.entry[curr_cpu->ROB.head].scheduled;
    cout << " executed: " << +curr_cpu->ROB.entry[curr_cpu->ROB.head].executed;
    cout << " is_memory: "
         << +curr_cpu->ROB.entry[curr_cpu->ROB.head].is_memory;
    cout << " event: " << curr_cpu->ROB.entry[curr_cpu->ROB.head].event_cycle;
    cout << " current: " << curr_cpu->current_core_cycle() << endl;

    // print LQ entry
    cout << endl << "Load Queue Entry" << endl;
    for (uint32_t j = 0; j < LQ_SIZE; j++) {
        cout << "[LQ] entry: " << j
             << " instr_id: " << curr_cpu->LQ.entry[j].instr_id
             << " address: " << hex << curr_cpu->LQ.entry[j].virtual_address
             << dec << " translated: " << +curr_cpu->LQ.entry[j].translated
             << " fetched: " << +curr_cpu->LQ.entry[i].fetched << endl;
    }

    // print SQ entry
    cout << endl << "Store Queue Entry" << endl;
    for (uint32_t j = 0; j < SQ_SIZE; j++) {
        cout << "[SQ] entry: " << j
             << " instr_id: " << curr_cpu->SQ.entry[j].instr_id
             << " address: " << hex << curr_cpu->SQ.entry[j].virtual_address
             << dec << " translated: " << +curr_cpu->SQ.entry[j].translated
             << " fetched: " << +curr_cpu->SQ.entry[i].fetched << endl;
    }

    throw std::runtime_error("DEADLOCK");
}

void signal_handler(int signal) {
    cout << "Caught signal: " << signal << endl;
    exit(1);
}

void cpu_l1i_prefetcher_cache_operate(uint32_t cpu_num, uint64_t v_addr,
                                      uint8_t cache_hit, uint8_t prefetch_hit) {
    ooo_cpu[cpu_num].l1i_prefetcher_cache_operate(v_addr, cache_hit,
                                                  prefetch_hit);
}

void cpu_l1i_prefetcher_cache_fill(uint32_t cpu_num, uint64_t addr,
                                   uint32_t set, uint32_t way, uint8_t prefetch,
                                   uint64_t evicted_addr) {
    ooo_cpu[cpu_num].l1i_prefetcher_cache_fill(addr, set, way, prefetch,
                                               evicted_addr);
}

int main(int argc, char** argv) {
    // interrupt signal hanlder
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = signal_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);

    // Parsing command line arguments.
    simulator->parse_args(argc, argv);

    // Initializing the simulator instance.
    simulator->initialize();

    warmup_instructions = simulator->descriptor().warmup_instructions;
    simulation_instructions = simulator->descriptor().simulation_instructions;

    cout << endl
         << "*** ChampSim Multicore Out-of-Order Simulator ***" << endl
         << endl;

    // initialize knobs
    uint8_t show_heartbeat = 1;

    uint32_t seed_number = 0;

    // consequences of knobs
    cout << "Warmup Instructions: " << warmup_instructions << endl;
    cout << "Simulation Instructions: " << simulation_instructions << endl;
    // cout << "Scramble Loads: " << (knob_scramble_loads ? "ture" : "false") <<
    // endl;
    cout << "Number of CPUs: " << NUM_CPUS << endl;

    if (helper::knob_low_bandwidth)
        DRAM_MTPS = DRAM_IO_FREQ / 4;
    else
        DRAM_MTPS = DRAM_IO_FREQ;

    // DRAM access latency
    tRP = (uint32_t)((1.0 * tRP_DRAM_NANOSECONDS * CPU_FREQ) / 1000);
    tRCD = (uint32_t)((1.0 * tRCD_DRAM_NANOSECONDS * CPU_FREQ) / 1000);
    tCAS = (uint32_t)((1.0 * tCAS_DRAM_NANOSECONDS * CPU_FREQ) / 1000);

    // default: 16 = (64 / 8) * (3200 / 1600)
    // it takes 16 CPU cycles to tranfser 64B cache block on a 8B (64-bit) bus
    // note that dram burst length = BLOCK_SIZE/DRAM_CHANNEL_WIDTH
    DRAM_DBUS_RETURN_TIME =
        (uint32_t)std::ceil((float)(BLOCK_SIZE / DRAM_CHANNEL_WIDTH) *
                            ((float)CPU_FREQ / (float)DRAM_MTPS));

    std::cout
        << (boost::format(
                "Off-chip DRAM Size: %u MB, Channels: %u, "
                "Width %u-bits, Data Rate: %u MT/s, Return time: %u cycles") %
            DRAM_SIZE % DRAM_CHANNELS % (8 * DRAM_CHANNEL_WIDTH) % DRAM_MTPS %
            DRAM_DBUS_RETURN_TIME)
        << std::endl;

    for (auto it = simulator->traces().cbegin();
         it != simulator->traces().cend(); it++) {
        std::string decomp_program, fmtstr = "%1$s -dc %2$s", gunzip_command;
        boost::filesystem::path trace_path(*it), ext(trace_path.extension());
        std::size_t i = std::distance(simulator->traces().cbegin(), it);
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);

        if (!boost::filesystem::exists(trace_path)) {
            throw std::runtime_error("Trace File not found.");
        }

        if (ext == ".gz") {
            decomp_program = "gzip";
        } else if (ext == ".xz") {
            decomp_program = "xz";
        } else {
            throw std::runtime_error(
                "ChampSim does not support traces other than gz or xz "
                "compression!");
        }

        sprintf(curr_cpu->gunzip_command, fmtstr.c_str(),
                decomp_program.c_str(), it->c_str());

        // Opening the file stream.
        curr_cpu->trace_file = popen(curr_cpu->gunzip_command, "r");

        if (curr_cpu->trace_file == NULL) {
            throw std::runtime_error("Unexpected error on trace opening.");
        }
    }

    if (simulator->traces().size() != simulator->descriptor().cpus.size()) {
        printf("\n*** Invalid number of trace provided. ***\n\n");
        assert(0);
    }

    // Reading the trace header for the irreg_base and irreg_bound registers.
    for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
        bool is_spec = false, is_qualcomm = false;
        std::size_t pairs = 0;
        champsim::cpu::trace_header head;
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);
        boost::filesystem::path trace_path(simulator->traces()[i]);

        // Is this a SPEC trace.
        is_spec = (trace_path.stem().c_str()[0] == '4') ||
                  (trace_path.stem().c_str()[0] == '6');

        // It this trace is a SPEC or a Qualcomm, we should not mess around with
        // irregular arrays.
        if (is_spec || is_qualcomm) {
            continue;
        }

#if !defined(LEGACY_TRACE)
        // Reading the number of pairs to load from the trace file.
        fread(reinterpret_cast<char*>(&pairs), sizeof(std::size_t), 1,
              curr_cpu->trace_file);

        for (std::size_t j = 0; j < pairs; j++) {
            champsim::cpu::trace_header::irreg_array_boundaries p;

            fread(reinterpret_cast<char*>(&p.first),
                  sizeof(champsim::cpu::trace_header::irreg_array_boundaries::
                             first_type),
                  1, curr_cpu->trace_file);
            fread(reinterpret_cast<char*>(&p.second),
                  sizeof(champsim::cpu::trace_header::irreg_array_boundaries::
                             second_type),
                  1, curr_cpu->trace_file);

            curr_cpu->_irreg_boundaries.push_back(p);

            std::cout << std::hex << p.first << " " << p.second << std::dec
                      << std::endl;
        }
#endif
    }

    // Initializing global latencies.
    SCHEDULING_LATENCY = 0;
    EXEC_LATENCY = 0;
    DECODE_LATENCY = 2;
    helper::PAGE_TABLE_LATENCY = 100;
    helper::SWAP_LATENCY = 100000;

    // TODO: can we initialize these variables from the class constructor?
    srand(seed_number);
    helper::champsim_seed = seed_number;
    for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);

        curr_cpu->cpu = i;
        curr_cpu->warmup_instructions = warmup_instructions;
        curr_cpu->simulation_instructions = simulation_instructions;
        curr_cpu->begin_sim_cycle = 0;
        curr_cpu->begin_sim_instr = warmup_instructions;

        // ROB
        curr_cpu->ROB.cpu = i;

        // BRANCH PREDICTOR
        curr_cpu->initialize_branch_predictor();

        // TLBs
        curr_cpu->ITLB.cpu = i;
        curr_cpu->ITLB.cache_type = IS_ITLB;
        curr_cpu->ITLB.MAX_READ = 2;
        curr_cpu->ITLB.fill_level = FILL_L1;
        curr_cpu->ITLB.lower_level = &curr_cpu->STLB;
        curr_cpu->ITLB.LATENCY = ITLB_LATENCY;

        curr_cpu->DTLB.cpu = i;
        curr_cpu->DTLB.cache_type = IS_DTLB;
        curr_cpu->DTLB.MAX_READ = 2;
        curr_cpu->DTLB.fill_level = FILL_L1;
        curr_cpu->DTLB.lower_level = &curr_cpu->STLB;
        curr_cpu->DTLB.LATENCY = DTLB_LATENCY;

        curr_cpu->STLB.cpu = i;
        curr_cpu->STLB.cache_type = IS_STLB;
        curr_cpu->STLB.MAX_READ = 1;
        curr_cpu->STLB.fill_level = FILL_L2;
        curr_cpu->STLB.upper_level_icache[i] = &curr_cpu->ITLB;
        curr_cpu->STLB.upper_level_dcache[i] = &curr_cpu->DTLB;
        curr_cpu->STLB.LATENCY = STLB_LATENCY;

        curr_cpu->warmup_complete() = false;
        // all_warmup_complete = NUM_CPUS;
        curr_cpu->simulation_complete() = false;
        // helper::stall_cycle[i] = 0;

        previous_ppage = 0;
        num_adjacent_page = 0;
        num_cl[i] = 0;
        allocated_pages = 0;
        num_page[i] = 0;
        minor_fault[i] = 0;
        major_fault[i] = 0;
    }

    uncore.LLC.llc_initialize_replacement();
    uncore.LLC.llc_prefetcher_initialize();

    // First, we create the DRAM controller.
    // uncore.llc = new champsim::components::distill_cache ();
    uncore.llc = new champsim::components::sectored_cache();

    // uncore.llc->init_cache ("../config/distill_cache_llc.json");
    uncore.llc->init_cache(simulator->descriptor().llc_config_file);

    uncore.llc->init_replacement_policy();

    std::vector<champsim::components::memory_system*> upper_l2c, upper_l1d,
        upper_l1i, upper_sdc;

    for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);

        curr_cpu->sdc = new champsim::components::sectored_cache(i);
        curr_cpu->l2c = new champsim::components::sectored_cache(i);
        curr_cpu->l1d = new champsim::components::sectored_cache(i);
        curr_cpu->l1i = new champsim::components::sectored_cache(i);

        upper_sdc.push_back(curr_cpu->sdc);
        upper_l2c.push_back(curr_cpu->l2c);
        upper_l1d.push_back(curr_cpu->l1d);
        upper_l1i.push_back(curr_cpu->l1i);
    }

    // Initializing custom cache hierarchy.
    for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);

        curr_cpu->sdc_enabled = simulator->descriptor().cpus[i].sdc_enabled;
        curr_cpu->sdc->init_cache(
            simulator->descriptor().cpus[i].sdc_config_file);
        curr_cpu->l2c->init_cache(
            simulator->descriptor().cpus[i].l2c_config_file);
        curr_cpu->l1d->init_cache(
            simulator->descriptor().cpus[i].l1d_config_file);
        curr_cpu->l1i->init_cache(
            simulator->descriptor().cpus[i].l1i_config_file);

        // Initializing replacement policies.
        curr_cpu->sdc->init_replacement_policy();
        curr_cpu->l2c->init_replacement_policy();
        curr_cpu->l1d->init_replacement_policy();
        curr_cpu->l1i->init_replacement_policy();

        // Connecting caches.
        curr_cpu->l2c->set_upper_level_irreg_cache(upper_sdc);
        curr_cpu->l2c->set_upper_level_icache(upper_l1i);
        curr_cpu->l2c->set_upper_level_dcache(upper_l1d);
        curr_cpu->l2c->set_lower_level_memory(uncore.llc);

        curr_cpu->sdc->set_lower_level_memory(curr_cpu->l2c);
        curr_cpu->l1d->set_lower_level_memory(curr_cpu->l2c);
        curr_cpu->l1i->set_lower_level_memory(curr_cpu->l2c);

        uncore.llc->set_upper_level_irreg_cache(upper_sdc);
        uncore.llc->set_upper_level_icache(upper_l2c);
        uncore.llc->set_upper_level_dcache(upper_l2c);
        uncore.llc->set_dram(&uncore.DRAM);

        curr_cpu->sdc->set_dram(&uncore.DRAM);
        curr_cpu->l1d->set_dram(&uncore.DRAM);

        // Setting PSEL in the L1D.
        curr_cpu->l1d->set_prefetcher_psel_bits(
            simulator->descriptor().cpus[i].l1d_prefetching_psel_bits);
        curr_cpu->l1d->set_prefetcher_threshold(
            simulator->descriptor().cpus[i].l1d_prefetching_psel_threshold);

        // OFF-CHIP DRAM
        // uncore.DRAM._upper_level_icache_new[i] =  ooo_cpu[i].l2c;
        // uncore.DRAM._upper_level_dcache_new[i] =  ooo_cpu[i].l2c;
        uncore.DRAM._upper_level_icache_new[i] = uncore.llc;
        uncore.DRAM._upper_level_dcache_new[i] = uncore.llc;
        uncore.DRAM._sdc = curr_cpu->sdc;
        uncore.DRAM.fill_level = FILL_DRAM;
        for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
            uncore.DRAM.RQ[i].is_RQ = 1;
            uncore.DRAM.WQ[i].is_WQ = 1;
        }

        // Setting the fill path policies.
        curr_cpu->fill_path_policy = new cpol::conservative_fill_path_policy(i);

        curr_cpu->_mm.pbp().dump_info();
    }

    // Initializing the DRAM.
    uncore.DRAM.initialize();

    // simulation entry point
    simulator->start_warmup();

    uint8_t run_simulation = 1;
    while (run_simulation) {
        for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
            O3_CPU* curr_cpu = simulator->modeled_cpu(i);

            // proceed one cycle
            curr_cpu->inc_current_core_cycle();

            // cout << "Trying to process instr_id: " <<
            // ooo_cpu[i].instr_unique_id << " fetch_stall: " <<
            // +ooo_cpu[i].fetch_stall; cout << " stall_cycle: " <<
            // stall_cycle[i] << " current: " << current_core_cycle[i] << endl;

            // core might be stalled due to page fault or branch misprediction
            if (curr_cpu->stall_cycle() <= curr_cpu->current_core_cycle()) {
                // retire
                if ((curr_cpu->ROB.entry[curr_cpu->ROB.head].executed ==
                     COMPLETED) &&
                    (curr_cpu->ROB.entry[curr_cpu->ROB.head].event_cycle <=
                     curr_cpu->current_core_cycle())) {
                    curr_cpu->retire_rob();
                }

                // complete
                curr_cpu->update_rob();

                // schedule
                curr_cpu->schedule_instruction();
                // execute
                curr_cpu->execute_instruction();

                curr_cpu->update_rob();

                // memory operation
                curr_cpu->schedule_memory_instruction();
                curr_cpu->execute_memory_instruction();

                curr_cpu->update_rob();

                // decode
                if (curr_cpu->DECODE_BUFFER.occupancy > 0) {
                    curr_cpu->decode_and_dispatch();
                }

                // fetch
                curr_cpu->fetch_instruction();

                // read from trace
                if ((curr_cpu->IFETCH_BUFFER.occupancy <
                     curr_cpu->IFETCH_BUFFER.SIZE) &&
                    (curr_cpu->fetch_stall == 0)) {
                    curr_cpu->read_from_trace();
                }
            }

            // heartbeat information
            if (show_heartbeat &&
                (curr_cpu->num_retired >= curr_cpu->next_print_instruction)) {
                curr_cpu->print_hearbeat();

                curr_cpu->next_print_instruction += STAT_PRINTING_PERIOD;

                curr_cpu->last_sim_instr = curr_cpu->num_retired;
                curr_cpu->last_sim_cycle = curr_cpu->current_core_cycle();
            }

            // check for deadlock
            if (curr_cpu->ROB.entry[curr_cpu->ROB.head].ip &&
                (curr_cpu->ROB.entry[curr_cpu->ROB.head].event_cycle +
                 DEADLOCK_CYCLE) <= curr_cpu->current_core_cycle())
                print_deadlock(i);

            // check for warmup
            // warmup complete
            if (!curr_cpu->warmup_complete() &&
                (curr_cpu->num_retired > warmup_instructions)) {
                curr_cpu->warmup_complete() = true;
                // helper::all_warmup_complete++;

                // Finishing the warmup for that specific core.
                simulator->modeled_cpu(i)->finish_warmup();
            }
            if (champsim::simulator::instance()->all_warmup_complete() &&
                champsim::simulator::instance()->state() ==
                    champsim::simulator::warmup) {  // this part is called only
                                                    // once
                                                    // when all cores are
                                                    // warmed up
                // helper::all_warmup_complete++;
                finish_warmup();
            }

            // simulation complete
            // if ((all_warmup_complete > simulator->descriptor ().cpus.size ())
            // && (simulation_complete[i] == 0) && (curr_cpu->num_retired >=
            // (curr_cpu->begin_sim_instr + curr_cpu->simulation_instructions)))
            // {
            //     simulation_complete[i] = 1;
            // 	curr_cpu->finish_simulation ();
            //
            //     all_simulation_complete++;
            // }

            if (curr_cpu->warmup_complete() &&
                !curr_cpu->simulation_complete() &&
                (curr_cpu->num_retired >=
                 (curr_cpu->begin_sim_instr +
                  curr_cpu->simulation_instructions))) {
                curr_cpu->finish_simulation();

                curr_cpu->simulation_complete() = true;
                // helper::all_simulation_complete++;
            }

            if (champsim::simulator::instance()->all_simulation_complete())
                run_simulation = 0;
        }

        uncore.DRAM.operate();
        // uncore.dram->operate ();
        uncore.llc->operate();
    }

    // uint64_t elapsed_second = (uint64_t)(time(NULL) - start_time),
    //          elapsed_minute = elapsed_second / 60,
    //          elapsed_hour = elapsed_minute / 60;
    // elapsed_minute -= elapsed_hour*60;
    // elapsed_second -= (elapsed_hour*3600 + elapsed_minute*60);

    cout << endl << "ChampSim completed all CPUs" << endl;
    if (simulator->descriptor().cpus.size() > 1) {
        cout << endl
             << "Total Simulation Statistics (not including warmup)" << endl;
        for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
            O3_CPU* curr_cpu = simulator->modeled_cpu(i);

            cout << endl
                 << "CPU " << i << " cumulative IPC: "
                 << (float)(curr_cpu->num_retired - curr_cpu->begin_sim_instr) /
                        (curr_cpu->current_core_cycle() -
                         curr_cpu->begin_sim_cycle);
            cout << " instructions: "
                 << curr_cpu->num_retired - curr_cpu->begin_sim_instr
                 << " cycles: "
                 << curr_cpu->current_core_cycle() - curr_cpu->begin_sim_cycle
                 << endl;
        }
    }

    cout << endl << "Region of Interest Statistics" << endl;
    for (int i = 0; i < simulator->descriptor().cpus.size(); i++) {
        O3_CPU* curr_cpu = simulator->modeled_cpu(i);

        std::cout << curr_cpu->_mm << std::endl;

        std::cout << curr_cpu->_pc_table.size()
                  << " different PCs threw memory accesses." << std::endl;

        cout << endl
             << "CPU " << i << " cumulative IPC: "
             << ((float)curr_cpu->finish_sim_instr /
                 curr_cpu->finish_sim_cycle);
        cout << " instructions: " << curr_cpu->finish_sim_instr
             << " cycles: " << curr_cpu->finish_sim_cycle << endl;
        cout << "Major fault: " << helper::major_fault[i]
             << " Minor fault: " << helper::minor_fault[i] << endl
             << endl;

        std::cout << "Stats on irregular data access predictions: Accurate | "
                  << curr_cpu->pred_results[true] << " Inaccurate | "
                  << curr_cpu->pred_results[false] << std::endl;

        std::cout << curr_cpu->irreg_pred.metrics() << std::endl;

        curr_cpu->l1i->report(std::cout, i);
        curr_cpu->l1d->report(std::cout, i);
        curr_cpu->l2c->report(std::cout, i);
        curr_cpu->sdc->report(std::cout, i);

        record_roi_stats(i, &curr_cpu->ITLB);
        record_roi_stats(i, &curr_cpu->DTLB);
        record_roi_stats(i, &curr_cpu->STLB);

        print_roi_stats(i, &curr_cpu->ITLB);
        print_roi_stats(i, &curr_cpu->DTLB);
        print_roi_stats(i, &curr_cpu->STLB);

        uint64_t irreg_doa = std::count_if(
                     curr_cpu->STLB.pte_usages.begin(),
                     curr_cpu->STLB.pte_usages.end(),
                     [](const auto& e) { return (!e.first && e.second); }),
                 irreg_alive = std::count_if(
                     curr_cpu->STLB.pte_usages.begin(),
                     curr_cpu->STLB.pte_usages.end(),
                     [](const auto& e) { return (e.first && e.second); }),
                 reg_doa = std::count_if(
                     curr_cpu->STLB.pte_usages.begin(),
                     curr_cpu->STLB.pte_usages.end(),
                     [](const auto& e) { return (!e.first && !e.second); }),
                 reg_alive = std::count_if(
                     curr_cpu->STLB.pte_usages.begin(),
                     curr_cpu->STLB.pte_usages.end(),
                     [](const auto& e) { return (e.first && !e.second); });

        std::cout << "pte_used/onchip_pred: " << curr_cpu->STLB.pte_used_onchip
                  << std::endl
                  << "pte_used/offchip_pred: "
                  << curr_cpu->STLB.pte_used_offchip << std::endl
                  << "pte_unused/onchip_pred: "
                  << curr_cpu->STLB.pte_unused_onchip << std::endl
                  << "pte_unused/offchip_pred: "
                  << curr_cpu->STLB.pte_unused_offchip << std::endl
                  << std::endl;

        curr_cpu->offchip_pred->dump_stats();
        curr_cpu->_mm.pbp().dump_stats();
    }

    uncore.llc->report(std::cout, 0);

    std::cout << "CORE REQUESTS SERVED FROM" << std::endl;

    for (const auto& e : simulator->modeled_cpu(0)->_request_served_positions) {
        std::cout << cache_type_map[e.first] << " " << e.second << std::endl;
    }

    print_dram_stats();
    print_branch_stats();

    champsim::simulator::destroy();

    return 0;
}

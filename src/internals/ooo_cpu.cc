#include <chrono>
#
#include <internals/simulator.hh>
#
#include "ooo_cpu.h"
#include "set.h"
#include "uncore.h"

using namespace champsim::cpu;

using namespace std::literals;

// out-of-order core
O3_CPU ooo_cpu[NUM_CPUS];
// uint64_t current_core_cycle[NUM_CPUS], stall_cycle[NUM_CPUS];
uint32_t SCHEDULING_LATENCY = 0, EXEC_LATENCY = 0, DECODE_LATENCY = 0;

O3_CPU::O3_CPU()
    : fill_path_policy(nullptr),
      _warmup_complete(false),
      _simulation_complete(false),
      _stall_cycle(0) {
    cpu = 0;

    // trace
    trace_file = NULL;

    // instruction
    instr_unique_id = 0;
    completed_executions = 0;
    begin_sim_cycle = 0;
    begin_sim_instr = 0;
    last_sim_cycle = 0;
    last_sim_instr = 0;
    finish_sim_cycle = 0;
    finish_sim_instr = 0;
    warmup_instructions = 0;
    simulation_instructions = 0;
    instrs_to_read_this_cycle = 0;
    instrs_to_fetch_this_cycle = 0;

    next_print_instruction = STAT_PRINTING_PERIOD;
    num_retired = 0;

    this->_current_core_cycle = 0;

    inflight_reg_executions = 0;
    inflight_mem_executions = 0;
    num_searched = 0;

    next_ITLB_fetch = 0;

    // branch
    branch_mispredict_stall_fetch = 0;
    mispredicted_branch_iw_index = 0;
    fetch_stall = 0;
    fetch_resume_cycle = 0;
    num_branch = 0;
    branch_mispredictions = 0;

    for (uint32_t i = 0; i < 8; i++) {
        total_branch_types[i] = 0;
    }

    for (uint32_t i = 0; i < STA_SIZE; i++) STA[i] = UINT64_MAX;
    STA_head = 0;
    STA_tail = 0;

    for (uint32_t i = 0; i < ROB_SIZE; i++) {
        RTE0[i] = ROB_SIZE;
        RTE1[i] = ROB_SIZE;
    }

    RTE0_head = 0;
    RTE1_head = 0;
    RTE0_tail = 0;
    RTE1_tail = 0;

    for (uint32_t i = 0; i < LQ_SIZE; i++) {
        RTL0[i] = LQ_SIZE;
        RTL1[i] = LQ_SIZE;
    }

    RTL0_head = 0;
    RTL1_head = 0;
    RTL0_tail = 0;
    RTL1_tail = 0;

    for (uint32_t i = 0; i < SQ_SIZE; i++) {
        RTS0[i] = SQ_SIZE;
        RTS1[i] = SQ_SIZE;
    }
    RTS0_head = 0;
    RTS1_head = 0;
    RTS0_tail = 0;
    RTS1_tail = 0;

    // offchip predictor
    this->offchip_pred = new cc::offchip_predictor_perceptron;
    this->write_alloc_psel = 0;
}

O3_CPU::~O3_CPU() {
    delete l1i;
    delete l1d;
    delete l2c;

    if (this->fill_path_policy) {
        delete this->fill_path_policy;
    }
}

/**
 * @brief Returns whether or not the warmup pahse of this core has been
 * completed.
 *
 * @return true The warmup phase has been completed.
 * @return false The warmup phase has not been completed yet.
 */
bool &O3_CPU::warmup_complete() { return this->_warmup_complete; }

/**
 * @brief Returns whether or not the simulation phase of this core has been
 * completed.
 *
 * @return true The simulation phase has been completed.
 * @return false The simulation phase has not been completed yet.
 */
bool &O3_CPU::simulation_complete() { return this->_simulation_complete; }

/**
 * @brief Returns the value of this core's clock.
 *
 * @return const uint64_t& The value of the core's clock.
 */
const uint64_t &O3_CPU::current_core_cycle() const {
    return this->_current_core_cycle;
}

/**
 * @brief Returns the cycle until which the core must be stalled.
 *
 * @return uint64_t& The stall cycle.
 */
uint64_t &O3_CPU::stall_cycle() { return this->_stall_cycle; }

/**
 * @brief Increments the core's clock.
 */
void O3_CPU::inc_current_core_cycle() { this->_current_core_cycle++; }

void O3_CPU::print_hearbeat() {
    float cumulative_ipc, heartbeat_ipc;

    // Updating the heart beat time.
    this->_last_heartbeat_time = std::chrono::system_clock::now();

    if (this->_warmup_complete) {
        cumulative_ipc =
            static_cast<float>(this->num_retired - this->begin_sim_instr) /
            (this->_current_core_cycle - this->begin_sim_cycle);
    } else {
        cumulative_ipc =
            static_cast<float>(this->num_retired) / this->_current_core_cycle;
    }

    heartbeat_ipc =
        static_cast<float>(this->num_retired - this->last_sim_instr) /
        (this->_current_core_cycle - this->last_sim_cycle);

    std::cout << "Heartbeat CPU " << this->cpu
              << " instructions: " << this->num_retired
              << " cycles: " << this->_current_core_cycle
              << " heartbeat IPC: " << heartbeat_ipc
              << " cumulative IPC: " << cumulative_ipc << " (Simulation time: "
              << ((this->_last_heartbeat_time -
                   champsim::simulator::instance()->begin_time()) /
                  1min)
              << " minutes)" << std::endl;
}

/**
 * @brief This method is called at the end of the warmup period of a core. It
 * reset the stats of its different components.
 */
void O3_CPU::finish_warmup() {
    auto duration = std::chrono::system_clock::now() -
                    champsim::simulator::instance()->begin_time();

    std::cout << std::endl
              << "Warmup complete CPU " << this->cpu
              << " instructions: " << this->num_retired
              << " cycles: " << this->_current_core_cycle
              << " (Simulation time: " << (duration / 1min) << " minutes)"
              << std::endl;

    // Resetting stats & marking the beginning point of the actual simulation
    // phase..
    this->begin_sim_cycle = this->_current_core_cycle;
    this->begin_sim_instr = this->num_retired;

    this->num_branch = 0;
    this->branch_mispredictions = 0;
    this->total_rob_occupancy_at_branch_mispredict = 0;

    // Resetting stats on branch types encountered.
    for (auto it = std::begin(this->total_branch_types);
         it != std::end(this->total_branch_types); it++) {
        *it = 0;
    }

    // Restting cache stats.
    this->l1i->reset_stats();
    this->l1d->reset_stats();
    this->l2c->reset_stats();
    this->sdc->reset_stats();

    this->_request_served_positions.clear();

    for (auto &e : this->misses_in_cache) {
        e.second = 0;
    }

    // Resetting stats of the irregular accesses predictor.
    this->irreg_pred.metrics().clear();
    this->_mm.metrics().clear();
}

void O3_CPU::finish_simulation() {
    // Updating the heartbeat time.
    this->_last_heartbeat_time = std::chrono::system_clock::now();

    auto duration = this->_last_heartbeat_time -
                    champsim::simulator::instance()->begin_time();

    this->finish_sim_instr = this->num_retired - this->begin_sim_instr;
    this->finish_sim_cycle = this->_current_core_cycle - this->begin_sim_cycle;

    std::cout << "Finished CPU " << this->cpu
              << " instructions: " << this->finish_sim_instr
              << " cycles: " << this->finish_sim_cycle
              << " (Simulation time: " << (duration / 1min) << " minutes)"
              << std::endl;
}

void O3_CPU::initialize_core(const cpu_descriptor &desc) {
    // Initializing the CPU based on the given properties.
    this->cpu = desc.cpu_id;

    this->warmup_instructions = desc.warmup_instructions;
    this->simulation_instructions = desc.simulation_instructions;
    this->begin_sim_cycle = 0;
    this->begin_sim_instr = desc.warmup_instructions;

    // Roerder Buffer.
    this->ROB.cpu = desc.cpu_id;

    // Initializing the branch predictor.
    this->initialize_branch_predictor();

    // Initializing the MetaData cache.
    this->_mm = cc::metadata_cache(16, 2, 512);
}

void O3_CPU::initialize_instruction(ooo_model_instr &instr) {
    bool reads_sp = false, writes_sp = false, reads_flags = false,
         reads_ip = false, writes_ip = false, reads_other = false;

    if (this->instrs_to_read_this_cycle == 0) {
        this->instrs_to_read_this_cycle =
            std::min(static_cast<uint64_t>(FETCH_WIDTH),
                     static_cast<uint64_t>(this->IFETCH_BUFFER.SIZE -
                                           this->IFETCH_BUFFER.occupancy));
    }

    // We started to read an instruction so we decrement the associated counter.
    this->instrs_to_read_this_cycle--;

    instr.instr_id = instr_unique_id;

    // TODO: move the instruction between the current and the previous.

    for (std::size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
        switch (instr.destination_registers[i]) {
            case REG_STACK_POINTER:
                writes_sp = true;
                break;
            case REG_INSTRUCTION_POINTER:
                writes_ip = true;
                break;
            default:
                break;
        }

        // Counting the number of register operations.
        if (instr.destination_registers[i] != 0) instr.num_reg_ops++;

        if (instr.destination_memory[i] != 0) {
            instr.num_mem_ops++;

            if (instr.num_mem_ops != 0) {
                this->STA[STA_tail] = instr_unique_id;
                STA_tail++;

                if (STA_tail == STA_SIZE) {
                    STA_tail = 0;
                }
            }
        }
    }

    for (std::size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
        switch (instr.source_registers[i]) {
            case 0:
                break;
            case REG_STACK_POINTER:
                reads_sp = true;
                break;
            case REG_FLAGS:
                reads_flags = true;
                break;
            case REG_INSTRUCTION_POINTER:
                reads_ip = true;
                break;
            default:
                reads_other = true;
                break;
        }

        if (instr.source_registers[i] != 0) instr.num_reg_ops++;
        if (instr.source_memory[i] != 0) instr.num_mem_ops++;
    }

    instr.is_memory = ((instr.num_mem_ops != 0) ? 1 : 0);

    // If this instruction is a branch, we determine what kind it is?
    if (!reads_sp && !reads_flags && writes_sp && !reads_other) {
        // Direct jump.
        instr.is_branch = 1;
        instr.branch_taken = 1;
        instr.branch_type = BRANCH_DIRECT_JUMP;
    } else if (!reads_sp && !reads_flags && writes_ip && reads_other) {
        // Indirect jump.
        instr.is_branch = 1;
        instr.branch_taken = 1;
        instr.branch_type = BRANCH_INDIRECT;
    } else if (!reads_sp && reads_ip && !writes_sp && writes_ip &&
               reads_flags && !reads_other) {
        // Conditional branch.
        instr.is_branch = 1;
        instr.branch_taken = instr.branch_taken;
        instr.branch_type = BRANCH_CONDITIONAL;
    } else if (reads_sp && reads_ip && writes_sp && writes_ip && !reads_flags &&
               !reads_other) {
        // Direct call.
        instr.is_branch = 1;
        instr.branch_taken = 1;
        instr.branch_type = BRANCH_DIRECT_CALL;
    } else if (reads_sp && reads_ip && writes_sp && writes_ip && !reads_flags &&
               reads_other) {
        // Indirect call.
        instr.is_branch = 1;
        instr.branch_taken = 1;
        instr.branch_type = BRANCH_INDIRECT_CALL;
    } else if (reads_sp && !reads_ip && writes_sp && writes_ip) {
        // Return.
        instr.is_branch = 1;
        instr.branch_taken = 1;
        instr.branch_type = BRANCH_RETURN;
    } else if (writes_ip) {
        // Some other branch type that doesn't fit the above categories..
        instr.is_branch = 1;
        instr.branch_taken = instr.branch_taken;
        instr.branch_type = BRANCH_OTHER;
    }

    total_branch_types[instr.branch_type]++;

    if ((instr.is_branch == 1) && (instr.branch_taken == 1)) {
        instr.branch_target = this->next_instr.ip;
    }

    // Handle branch prediction.
    if (instr.is_branch) {
        this->num_branch++;

        uint8_t branch_prediction = predict_branch(instr.ip);
        uint64_t predicted_branch_target = instr.branch_target;

        if (branch_prediction == 0) {
            predicted_branch_target = 0;
        }

        // Call code prefetcher every time the branch predictor is used.
        l1i_prefetcher_cache_operate(instr.ip, instr.branch_type,
                                     predicted_branch_target);

        if (instr.branch_taken != branch_prediction) {
            branch_mispredictions++;
            total_rob_occupancy_at_branch_mispredict += ROB.occupancy;

            if (this->_warmup_complete) {
                fetch_stall = 1;
                instrs_to_read_this_cycle = 0;
                instr.branch_mispredicted = 1;
            }
        } else {
            // If correctly predicted taken, then we can't fetch anymore
            // instructions this cycle.
            if (instr.branch_taken == 1) {
                this->instrs_to_read_this_cycle = 0;
            }
        }

        last_branch_result(instr.ip, instr.branch_taken);
    }

    instr.event_cycle = this->_current_core_cycle;

    // Add to IFETCH_BUFFER.
    this->IFETCH_BUFFER.entry[this->IFETCH_BUFFER.tail] = instr;
    this->IFETCH_BUFFER.occupancy++;
    this->IFETCH_BUFFER.tail++;

    if (this->IFETCH_BUFFER.tail >= this->IFETCH_BUFFER.SIZE) {
        this->IFETCH_BUFFER.tail = 0;
    }

    instr_unique_id++;
}

bool O3_CPU::should_read_instruction() const {
    return (this->instrs_to_read_this_cycle >= 0);
}

void O3_CPU::read_from_trace() {
    // actual processors do not work like this but for easier implementation,
    // we read instruction traces and virtually add them in the ROB
    // note that these traces are not yet translated and fetched

    uint8_t continue_reading = 1;
    uint32_t num_reads = 0;
    instrs_to_read_this_cycle = FETCH_WIDTH;

    // first, read PIN trace
    while (continue_reading) {
        size_t instr_size = helper::knob_cloudsuite ? sizeof(cloudsuite_instr)
                                                    : sizeof(input_instr);

        if (helper::knob_cloudsuite) {
            if (!fread(&current_cloudsuite_instr, instr_size, 1, trace_file)) {
                // reached end of file for this trace
                cout << "*** Reached end of trace for Core: " << cpu
                     << " Repeating trace: " << trace_string << endl;

                // close the trace file and re-open it
                pclose(trace_file);
                trace_file = popen(gunzip_command, "r");
                if (trace_file == NULL) {
                    cerr << endl
                         << "*** CANNOT REOPEN TRACE FILE: " << trace_string
                         << " ***" << endl;
                    assert(0);
                }
            } else {  // successfully read the trace

                // copy the instruction into the performance model's instruction
                // format
                ooo_model_instr arch_instr;
                int num_reg_ops = 0, num_mem_ops = 0;

                arch_instr.instr_id = instr_unique_id;
                arch_instr.ip = current_cloudsuite_instr.ip;
                arch_instr.is_branch = current_cloudsuite_instr.is_branch;
                arch_instr.branch_taken = current_cloudsuite_instr.branch_taken;

                arch_instr.asid[0] = current_cloudsuite_instr.asid[0];
                arch_instr.asid[1] = current_cloudsuite_instr.asid[1];

                for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
                    arch_instr.destination_registers[i] =
                        current_cloudsuite_instr.destination_registers[i];
                    arch_instr.destination_memory[i] =
                        current_cloudsuite_instr.destination_memory[i];
                    arch_instr.destination_virtual_address[i] =
                        current_cloudsuite_instr.destination_memory[i];

                    if (arch_instr.destination_registers[i]) num_reg_ops++;
                    if (arch_instr.destination_memory[i]) {
                        num_mem_ops++;

                        // update STA, this structure is required to execute
                        // store instructions properly without deadlock
                        if (num_mem_ops > 0) {
#ifdef SANITY_CHECK
                            if (STA[STA_tail] < UINT64_MAX) {
                                if (STA_head != STA_tail) assert(0);
                            }
#endif
                            STA[STA_tail] = instr_unique_id;
                            STA_tail++;

                            if (STA_tail == STA_SIZE) STA_tail = 0;
                        }
                    }
                }

                for (int i = 0; i < NUM_INSTR_SOURCES; i++) {
                    arch_instr.source_registers[i] =
                        current_cloudsuite_instr.source_registers[i];
                    arch_instr.source_memory[i] =
                        current_cloudsuite_instr.source_memory[i];
                    arch_instr.source_virtual_address[i] =
                        current_cloudsuite_instr.source_memory[i];

                    if (arch_instr.source_registers[i]) num_reg_ops++;
                    if (arch_instr.source_memory[i]) num_mem_ops++;
                }

                arch_instr.num_reg_ops = num_reg_ops;
                arch_instr.num_mem_ops = num_mem_ops;
                if (num_mem_ops > 0) arch_instr.is_memory = 1;

                // add this instruction to the IFETCH_BUFFER
                if (IFETCH_BUFFER.occupancy < IFETCH_BUFFER.SIZE) {
                    uint32_t ifetch_buffer_index =
                        add_to_ifetch_buffer(&arch_instr);
                    num_reads++;

                    // handle branch prediction
                    if (IFETCH_BUFFER.entry[ifetch_buffer_index].is_branch) {
                        DP(if (warmup_complete[cpu]) {
                            cout << "[BRANCH] instr_id: " << instr_unique_id
                                 << " ip: " << hex << arch_instr.ip << dec
                                 << " taken: " << +arch_instr.branch_taken
                                 << endl;
                        });

                        num_branch++;

                        // handle branch prediction & branch predictor update
                        uint8_t branch_prediction = predict_branch(
                            IFETCH_BUFFER.entry[ifetch_buffer_index].ip);

                        if (IFETCH_BUFFER.entry[ifetch_buffer_index]
                                .branch_taken != branch_prediction) {
                            branch_mispredictions++;
                            total_rob_occupancy_at_branch_mispredict +=
                                ROB.occupancy;
                            if (this->_warmup_complete) {
                                fetch_stall = 1;
                                instrs_to_read_this_cycle = 0;
                                IFETCH_BUFFER.entry[ifetch_buffer_index]
                                    .branch_mispredicted = 1;
                            }
                        } else {
                            // correct prediction
                            if (branch_prediction == 1) {
                                // if correctly predicted taken, then we can't
                                // fetch anymore instructions this cycle
                                instrs_to_read_this_cycle = 0;
                            }
                        }

                        last_branch_result(
                            IFETCH_BUFFER.entry[ifetch_buffer_index].ip,
                            IFETCH_BUFFER.entry[ifetch_buffer_index]
                                .branch_taken);
                    }

                    if ((num_reads >= instrs_to_read_this_cycle) ||
                        (IFETCH_BUFFER.occupancy == IFETCH_BUFFER.SIZE))
                        continue_reading = 0;
                }
                instr_unique_id++;
            }
        } else {
#if defined(LEGACY_TRACE)
            input_instr trace_read_instr;
            if (!fread(&trace_read_instr, sizeof(input_instr), 1, trace_file)) {
#else
            x86_trace_instruction trace_read_instr;
            if (!fread(&trace_read_instr, sizeof(x86_trace_instruction), 1,
                       trace_file)) {
#endif
                // reached end of file for this trace
                cout << "*** Reached end of trace for Core: " << cpu
                     << " Repeating trace: " << trace_string << endl;

                // close the trace file and re-open it
                pclose(trace_file);
                trace_file = popen(gunzip_command, "r");
                if (trace_file == NULL) {
                    cerr << endl
                         << "*** CANNOT REOPEN TRACE FILE: " << trace_string
                         << " ***" << endl;
                    assert(0);
                }

                std::size_t pairs = 0;
                champsim::cpu::trace_header head;

                // Reading the number of pairs to load from the trace file.
#if defined(LEGACY_TRACE)
                fread(reinterpret_cast<char *>(&pairs), sizeof(std::size_t), 1,
                      this->trace_file);

                for (std::size_t j = 0; j < pairs; j++) {
                    champsim::cpu::trace_header::irreg_array_boundaries p;

                    fread(reinterpret_cast<char *>(&p.first),
                          sizeof(champsim::cpu::trace_header::
                                     irreg_array_boundaries::first_type),
                          1, this->trace_file);
                    fread(reinterpret_cast<char *>(&p.second),
                          sizeof(champsim::cpu::trace_header::
                                     irreg_array_boundaries::second_type),
                          1, this->trace_file);

                    // ooo_cpu[i]._irreg_boundaries.push_back (p);
                    //
                    // std::cout << std::hex << p.first << " " << p.second <<
                    // std::dec << std::endl;
                }
#endif

            } else {  // successfully read the trace

                if (instr_unique_id == 0) {
                    current_instr = next_instr = trace_read_instr;
                } else {
                    current_instr = next_instr;
                    next_instr = trace_read_instr;
                }

                // copy the instruction into the performance model's instruction
                // format
                ooo_model_instr arch_instr;
                int num_reg_ops = 0, num_mem_ops = 0;

                arch_instr.instr_id = instr_unique_id;
                arch_instr.ip = current_instr.ip;
                arch_instr.is_branch = current_instr.is_branch;
                arch_instr.branch_taken = current_instr.branch_taken;

                arch_instr.asid[0] = cpu;
                arch_instr.asid[1] = cpu;

#if !defined(LEGACY_TRACE)
                arch_instr.instruction_size = current_instr.instruction_size;
#endif

                bool reads_sp = false;
                bool writes_sp = false;
                bool reads_flags = false;
                bool reads_ip = false;
                bool writes_ip = false;
                bool reads_other = false;

                for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
                    arch_instr.destination_registers[i] =
                        current_instr.destination_registers[i];
                    arch_instr.destination_memory[i] =
                        current_instr.destination_memory[i];
#if !defined(LEGACY_TRACE)
                    arch_instr.destination_memory_size[i] =
                        current_instr.destination_memory_size[i];
#endif
                    arch_instr.destination_virtual_address[i] =
                        current_instr.destination_memory[i];

                    switch (arch_instr.destination_registers[i]) {
                        case 0:
                            break;
                        case REG_STACK_POINTER:
                            writes_sp = true;
                            break;
                        case REG_INSTRUCTION_POINTER:
                            writes_ip = true;
                            break;
                        default:
                            break;
                    }

                    /*
                    if((arch_instr.is_branch) &&
                    (arch_instr.destination_registers[i] > 24) &&
                    (arch_instr.destination_registers[i] < 28))
                      {
                        arch_instr.destination_registers[i] = 0;
                      }
                    */

                    if (arch_instr.destination_registers[i]) num_reg_ops++;
                    if (arch_instr.destination_memory[i]) {
                        num_mem_ops++;

                        // update STA, this structure is required to execute
                        // store instructions properly without deadlock
                        if (num_mem_ops > 0) {
#ifdef SANITY_CHECK
                            if (STA[STA_tail] < UINT64_MAX) {
                                if (STA_head != STA_tail) assert(0);
                            }
#endif
                            STA[STA_tail] = instr_unique_id;
                            STA_tail++;

                            if (STA_tail == STA_SIZE) STA_tail = 0;
                        }
                    }
                }

                for (int i = 0; i < NUM_INSTR_SOURCES; i++) {
                    arch_instr.source_registers[i] =
                        current_instr.source_registers[i];
                    arch_instr.source_memory[i] =
                        current_instr.source_memory[i];
#if !defined(LEGACY_TRACE)
                    arch_instr.source_memory_size[i] =
                        current_instr.source_memory_size[i];
#endif
                    arch_instr.source_virtual_address[i] =
                        current_instr.source_memory[i];

                    switch (arch_instr.source_registers[i]) {
                        case 0:
                            break;
                        case REG_STACK_POINTER:
                            reads_sp = true;
                            break;
                        case REG_FLAGS:
                            reads_flags = true;
                            break;
                        case REG_INSTRUCTION_POINTER:
                            reads_ip = true;
                            break;
                        default:
                            reads_other = true;
                            break;
                    }

                    /*
                    if((!arch_instr.is_branch) &&
                    (arch_instr.source_registers[i] > 25) &&
                    (arch_instr.source_registers[i] < 28))
                      {
                        arch_instr.source_registers[i] = 0;
                      }
                    */

                    if (arch_instr.source_registers[i]) num_reg_ops++;
                    if (arch_instr.source_memory[i]) num_mem_ops++;
                }

                arch_instr.num_reg_ops = num_reg_ops;
                arch_instr.num_mem_ops = num_mem_ops;
                if (num_mem_ops > 0) arch_instr.is_memory = 1;

                // determine what kind of branch this is, if any
                if (!reads_sp && !reads_flags && writes_ip && !reads_other) {
                    // direct jump
                    arch_instr.is_branch = 1;
                    arch_instr.branch_taken = 1;
                    arch_instr.branch_type = BRANCH_DIRECT_JUMP;
                } else if (!reads_sp && !reads_flags && writes_ip &&
                           reads_other) {
                    // indirect branch
                    arch_instr.is_branch = 1;
                    arch_instr.branch_taken = 1;
                    arch_instr.branch_type = BRANCH_INDIRECT;
                } else if (!reads_sp && reads_ip && !writes_sp && writes_ip &&
                           reads_flags && !reads_other) {
                    // conditional branch
                    arch_instr.is_branch = 1;
                    arch_instr.branch_taken =
                        arch_instr.branch_taken;  // don't change this
                    arch_instr.branch_type = BRANCH_CONDITIONAL;
                } else if (reads_sp && reads_ip && writes_sp && writes_ip &&
                           !reads_flags && !reads_other) {
                    // direct call
                    arch_instr.is_branch = 1;
                    arch_instr.branch_taken = 1;
                    arch_instr.branch_type = BRANCH_DIRECT_CALL;
                } else if (reads_sp && reads_ip && writes_sp && writes_ip &&
                           !reads_flags && reads_other) {
                    // indirect call
                    arch_instr.is_branch = 1;
                    arch_instr.branch_taken = 1;
                    arch_instr.branch_type = BRANCH_INDIRECT_CALL;
                } else if (reads_sp && !reads_ip && writes_sp && writes_ip) {
                    // return
                    arch_instr.is_branch = 1;
                    arch_instr.branch_taken = 1;
                    arch_instr.branch_type = BRANCH_RETURN;
                } else if (writes_ip) {
                    // some other branch type that doesn't fit the above
                    // categories
                    arch_instr.is_branch = 1;
                    arch_instr.branch_taken =
                        arch_instr.branch_taken;  // don't change this
                    arch_instr.branch_type = BRANCH_OTHER;
                }

                total_branch_types[arch_instr.branch_type]++;

                if ((arch_instr.is_branch == 1) &&
                    (arch_instr.branch_taken == 1)) {
                    arch_instr.branch_target = next_instr.ip;
                }

                // add this instruction to the IFETCH_BUFFER
                if (IFETCH_BUFFER.occupancy < IFETCH_BUFFER.SIZE) {
                    uint32_t ifetch_buffer_index =
                        add_to_ifetch_buffer(&arch_instr);
                    num_reads++;

                    // handle branch prediction
                    if (IFETCH_BUFFER.entry[ifetch_buffer_index].is_branch) {
                        DP(if (warmup_complete[cpu]) {
                            cout << "[BRANCH] instr_id: " << instr_unique_id
                                 << " ip: " << hex << arch_instr.ip << dec
                                 << " taken: " << +arch_instr.branch_taken
                                 << endl;
                        });

                        num_branch++;

                        // handle branch prediction & branch predictor update
                        uint8_t branch_prediction = predict_branch(
                            IFETCH_BUFFER.entry[ifetch_buffer_index].ip);
                        uint64_t predicted_branch_target =
                            IFETCH_BUFFER.entry[ifetch_buffer_index]
                                .branch_target;
                        if (branch_prediction == 0) {
                            predicted_branch_target = 0;
                        }
                        // call code prefetcher every time the branch predictor
                        // is used
                        l1i_prefetcher_branch_operate(
                            IFETCH_BUFFER.entry[ifetch_buffer_index].ip,
                            IFETCH_BUFFER.entry[ifetch_buffer_index]
                                .branch_type,
                            predicted_branch_target);

                        if (IFETCH_BUFFER.entry[ifetch_buffer_index]
                                .branch_taken != branch_prediction) {
                            branch_mispredictions++;
                            total_rob_occupancy_at_branch_mispredict +=
                                ROB.occupancy;
                            if (this->_warmup_complete) {
                                fetch_stall = 1;
                                instrs_to_read_this_cycle = 0;
                                IFETCH_BUFFER.entry[ifetch_buffer_index]
                                    .branch_mispredicted = 1;
                            }
                        } else {
                            // correct prediction
                            if (branch_prediction == 1) {
                                // if correctly predicted taken, then we can't
                                // fetch anymore instructions this cycle
                                instrs_to_read_this_cycle = 0;
                            }
                        }

                        last_branch_result(
                            IFETCH_BUFFER.entry[ifetch_buffer_index].ip,
                            IFETCH_BUFFER.entry[ifetch_buffer_index]
                                .branch_taken);
                    }

                    if ((num_reads >= instrs_to_read_this_cycle) ||
                        (IFETCH_BUFFER.occupancy == IFETCH_BUFFER.SIZE))
                        continue_reading = 0;
                }
                instr_unique_id++;
            }
        }
    }

    // instrs_to_fetch_this_cycle = num_reads;
}

uint32_t O3_CPU::add_to_rob(ooo_model_instr *arch_instr) {
    uint32_t index = ROB.tail;

    // sanity check
    if (ROB.entry[index].instr_id != 0) {
        cerr << "[ROB_ERROR] " << __func__ << " is not empty index: " << index;
        cerr << " instr_id: " << ROB.entry[index].instr_id << endl;
        assert(0);
    }

    ROB.entry[index] = *arch_instr;
    ROB.entry[index].event_cycle = this->_current_core_cycle;

    ROB.occupancy++;
    ROB.tail++;
    if (ROB.tail >= ROB.SIZE) ROB.tail = 0;

    DP(if (warmup_complete[cpu]) {
        cout << "[ROB] " << __func__
             << " instr_id: " << ROB.entry[index].instr_id;
        cout << " ip: " << hex << ROB.entry[index].ip << dec;
        cout << " head: " << ROB.head << " tail: " << ROB.tail
             << " occupancy: " << ROB.occupancy;
        cout << " event: " << ROB.entry[index].event_cycle
             << " current: " << current_core_cycle[cpu] << endl;
    });

#ifdef SANITY_CHECK
    if (ROB.entry[index].ip == 0) {
        cerr << "[ROB_ERROR] " << __func__ << " ip is zero index: " << index;
        cerr << " instr_id: " << ROB.entry[index].instr_id
             << " ip: " << ROB.entry[index].ip << endl;
        assert(0);
    }
#endif

    return index;
}

uint32_t O3_CPU::add_to_ifetch_buffer(ooo_model_instr *arch_instr) {
    /*
    if((arch_instr->is_branch != 0) && (arch_instr->branch_type ==
    BRANCH_OTHER))
      {
        cout << "IP: 0x" << hex << (uint64_t)(arch_instr->ip) << "
    branch_target: 0x" << (uint64_t)(arch_instr->branch_target) << dec << endl;
        cout << (uint32_t)(arch_instr->is_branch) << " " <<
    (uint32_t)(arch_instr->branch_type) << " " <<
    (uint32_t)(arch_instr->branch_taken) << endl; for(uint32_t i=0;
    i<NUM_INSTR_SOURCES; i++)
          {
            cout << (uint32_t)(arch_instr->source_registers[i]) << " ";
          }
        cout << endl;
        for (uint32_t i=0; i<MAX_INSTR_DESTINATIONS; i++)
          {
            cout << (uint32_t)(arch_instr->destination_registers[i]) << " ";
          }
        cout << endl << endl;
      }
    */

    uint32_t index = IFETCH_BUFFER.tail;

    if (IFETCH_BUFFER.entry[index].instr_id != 0) {
        cerr << "[IFETCH_BUFFER_ERROR] " << __func__
             << " is not empty index: " << index;
        cerr << " instr_id: " << IFETCH_BUFFER.entry[index].instr_id << endl;
        assert(0);
    }

    IFETCH_BUFFER.entry[index] = *arch_instr;
    IFETCH_BUFFER.entry[index].event_cycle = this->_current_core_cycle;

    IFETCH_BUFFER.occupancy++;
    IFETCH_BUFFER.tail++;

    if (IFETCH_BUFFER.tail >= IFETCH_BUFFER.SIZE) {
        IFETCH_BUFFER.tail = 0;
    }

    return index;
}

uint32_t O3_CPU::add_to_decode_buffer(ooo_model_instr *arch_instr) {
    uint32_t index = DECODE_BUFFER.tail;

    if (DECODE_BUFFER.entry[index].instr_id != 0) {
        cerr << "[DECODE_BUFFER_ERROR] " << __func__
             << " is not empty index: " << index;
        cerr << " instr_id: " << IFETCH_BUFFER.entry[index].instr_id << endl;
        assert(0);
    }

    DECODE_BUFFER.entry[index] = *arch_instr;
    DECODE_BUFFER.entry[index].event_cycle = this->_current_core_cycle;

    DECODE_BUFFER.occupancy++;
    DECODE_BUFFER.tail++;
    if (DECODE_BUFFER.tail >= DECODE_BUFFER.SIZE) {
        DECODE_BUFFER.tail = 0;
    }

    return index;
}

uint32_t O3_CPU::check_rob(uint64_t instr_id) {
    if ((ROB.head == ROB.tail) && ROB.occupancy == 0) return ROB.SIZE;

    if (ROB.head < ROB.tail) {
        for (uint32_t i = ROB.head; i < ROB.tail; i++) {
            if (ROB.entry[i].instr_id == instr_id) {
                DP(if (warmup_complete[ROB.cpu]) {
                    cout << "[ROB] " << __func__
                         << " same instr_id: " << ROB.entry[i].instr_id;
                    cout << " rob_index: " << i << endl;
                });
                return i;
            }
        }
    } else {
        for (uint32_t i = ROB.head; i < ROB.SIZE; i++) {
            if (ROB.entry[i].instr_id == instr_id) {
                DP(if (warmup_complete[cpu]) {
                    cout << "[ROB] " << __func__
                         << " same instr_id: " << ROB.entry[i].instr_id;
                    cout << " rob_index: " << i << endl;
                });
                return i;
            }
        }
        for (uint32_t i = 0; i < ROB.tail; i++) {
            if (ROB.entry[i].instr_id == instr_id) {
                DP(if (warmup_complete[cpu]) {
                    cout << "[ROB] " << __func__
                         << " same instr_id: " << ROB.entry[i].instr_id;
                    cout << " rob_index: " << i << endl;
                });
                return i;
            }
        }
    }

    cerr << "[ROB_ERROR] " << __func__ << " does not have any matching index! ";
    cerr << " instr_id: " << instr_id << endl;
    assert(0);

    return ROB.SIZE;
}

void O3_CPU::fetch_instruction() {
    int rq_index;

    // TODO: can we model wrong path execusion?
    // probalby not

    // if we had a branch mispredict, turn fetching back on after the branch
    // mispredict penalty
    if ((fetch_stall == 1) &&
        (this->_current_core_cycle >= fetch_resume_cycle) &&
        (fetch_resume_cycle != 0)) {
        fetch_stall = 0;
        fetch_resume_cycle = 0;
    }

    if (IFETCH_BUFFER.occupancy == 0) {
        return;
    }

    // scan through IFETCH_BUFFER to find instructions that need to be
    // translated
    uint32_t index = IFETCH_BUFFER.head;
    for (uint32_t i = 0; i < IFETCH_BUFFER.SIZE; i++) {
        if (IFETCH_BUFFER.entry[index].ip == 0) {
            break;
        }

        if (IFETCH_BUFFER.entry[index].translated == 0) {
            // begin process of fetching this instruction by sending it to the
            // ITLB add it to the ITLB's read queue
            PACKET trace_packet;
            trace_packet.instruction = 1;
            trace_packet.is_data = 0;
            trace_packet.tlb_access = 1;
            trace_packet.fill_level = FILL_L1;
            trace_packet.fill_l1i = 1;
            trace_packet.cpu = cpu;
            trace_packet.address =
                IFETCH_BUFFER.entry[index].ip >> LOG2_PAGE_SIZE;
            if (helper::knob_cloudsuite)
                trace_packet.address =
                    IFETCH_BUFFER.entry[index].ip >> LOG2_PAGE_SIZE;
            else
                trace_packet.address =
                    IFETCH_BUFFER.entry[index].ip >> LOG2_PAGE_SIZE;
            trace_packet.full_addr = IFETCH_BUFFER.entry[index].ip;
            trace_packet.memory_size =
                BLOCK_SIZE;  // As we are requesting a TLB entry, we request a
                             // whole ITLB block.
            trace_packet.instr_id = 0;
            trace_packet.rob_index = i;
            trace_packet.producer =
                0;  // TODO: check if this guy gets used or not
            trace_packet.ip = IFETCH_BUFFER.entry[index].ip;
            trace_packet.type = LOAD;
            trace_packet.asid[0] = 0;
            trace_packet.asid[1] = 0;
            trace_packet.event_cycle = this->_current_core_cycle;

            if ((rq_index = ITLB.add_rq(&trace_packet)) != -2) {
                // successfully sent to the ITLB, so mark all instructions in
                // the IFETCH_BUFFER that match this ip as translated INFLIGHT
                for (uint32_t j = 0; j < IFETCH_BUFFER.SIZE; j++) {
                    if ((((IFETCH_BUFFER.entry[j].ip) >> LOG2_PAGE_SIZE) ==
                         ((IFETCH_BUFFER.entry[index].ip) >> LOG2_PAGE_SIZE)) &&
                        (IFETCH_BUFFER.entry[j].translated == 0)) {
                        IFETCH_BUFFER.entry[j].translated = INFLIGHT;
                        IFETCH_BUFFER.entry[j].fetched = 0;
                    }
                }
            }
        }

        // fetch cache lines that were part of a translated page but not the
        // cache line that initiated the translation
        if ((IFETCH_BUFFER.entry[index].translated == COMPLETED) &&
            (IFETCH_BUFFER.entry[index].fetched == 0)) {
            // add it to the L1-I's read queue
            PACKET fetch_packet;
            fetch_packet.instruction = 1;
            fetch_packet.is_data = 0;
            fetch_packet.fill_level = FILL_L1;
            fetch_packet.fill_l1i = 1;
            fetch_packet.cpu = cpu;
            fetch_packet.address =
                IFETCH_BUFFER.entry[index].instruction_pa >> 6;
            fetch_packet.instruction_pa =
                IFETCH_BUFFER.entry[index].instruction_pa;
            fetch_packet.full_addr = IFETCH_BUFFER.entry[index].instruction_pa;
            fetch_packet.v_address =
                IFETCH_BUFFER.entry[index].ip >> LOG2_PAGE_SIZE;
            fetch_packet.full_v_addr = IFETCH_BUFFER.entry[index].ip;
            fetch_packet.memory_size =
                IFETCH_BUFFER.entry[index].instruction_size;
            fetch_packet.instr_id = 0;
            fetch_packet.rob_index = 0;
            fetch_packet.producer = 0;
            fetch_packet.ip = IFETCH_BUFFER.entry[index].ip;
            fetch_packet.type = LOAD;
            fetch_packet.asid[0] = 0;
            fetch_packet.asid[1] = 0;
            fetch_packet.event_cycle = this->_current_core_cycle;

            /*
            // invoke code prefetcher -- THIS HAS BEEN MOVED TO cache.cc !!!
            int hit_way = L1I.check_hit(&fetch_packet);
            uint8_t prefetch_hit = 0;
            if(hit_way != -1)
              {
                prefetch_hit =
            L1I.block[L1I.get_set(fetch_packet.address)][hit_way].prefetch;
              }
            l1i_prefetcher_cache_operate(fetch_packet.ip, (hit_way != -1),
            prefetch_hit);
            */

            // TODO: Remove this portion as it relates to ChampSim's legacy
            // caches. rq_index = L1I.add_rq(&fetch_packet);

            if ((rq_index = rq_index = this->l1i->add_read_queue(
                     fetch_packet)) != cc::cache::add_queue_failure) {
                // mark all instructions from this cache line as having been
                // fetched
                for (uint32_t j = 0; j < IFETCH_BUFFER.SIZE; j++) {
                    if (((IFETCH_BUFFER.entry[j].ip) >> 6) ==
                            ((IFETCH_BUFFER.entry[index].ip) >> 6) &&
                        (IFETCH_BUFFER.entry[j].fetched == 0)) {
                        IFETCH_BUFFER.entry[j].translated = COMPLETED;
                        IFETCH_BUFFER.entry[j].fetched = INFLIGHT;
                    }
                }
            }
        }

        index++;
        if (index >= IFETCH_BUFFER.SIZE) {
            index = 0;
        }

        if (index == IFETCH_BUFFER.head) {
            break;
        }
    }

    // send to DECODE stage
    bool decode_full = false;
    for (uint32_t i = 0; i < DECODE_WIDTH; i++) {
        if (decode_full) {
            break;
        }

        if (IFETCH_BUFFER.entry[IFETCH_BUFFER.head].ip == 0) {
            break;
        }

        if ((IFETCH_BUFFER.entry[IFETCH_BUFFER.head].translated == COMPLETED) &&
            (IFETCH_BUFFER.entry[IFETCH_BUFFER.head].fetched == COMPLETED)) {
            if (DECODE_BUFFER.occupancy < DECODE_BUFFER.SIZE) {
                uint32_t decode_index = add_to_decode_buffer(
                    &IFETCH_BUFFER.entry[IFETCH_BUFFER.head]);
                DECODE_BUFFER.entry[decode_index].event_cycle = 0;

                ooo_model_instr empty_entry;
                IFETCH_BUFFER.entry[IFETCH_BUFFER.head] = empty_entry;

                IFETCH_BUFFER.head++;
                if (IFETCH_BUFFER.head >= IFETCH_BUFFER.SIZE) {
                    IFETCH_BUFFER.head = 0;
                }
                IFETCH_BUFFER.occupancy--;
            } else {
                decode_full = true;
            }
        }

        index++;
        if (index >= IFETCH_BUFFER.SIZE) {
            index = 0;
        }
    }
}

void O3_CPU::decode_and_dispatch() {
    // dispatch DECODE_WIDTH instructions that have decoded into the ROB
    uint32_t count_dispatches = 0;
    for (uint32_t i = 0; i < DECODE_BUFFER.SIZE; i++) {
        if (DECODE_BUFFER.entry[DECODE_BUFFER.head].ip == 0) {
            break;
        }

        if (((!this->_warmup_complete) && (ROB.occupancy < ROB.SIZE)) ||
            ((DECODE_BUFFER.entry[DECODE_BUFFER.head].event_cycle != 0) &&
             (DECODE_BUFFER.entry[DECODE_BUFFER.head].event_cycle <
              this->_current_core_cycle) &&
             (ROB.occupancy < ROB.SIZE))) {
            // move this instruction to the ROB if there's space
            uint32_t rob_index =
                add_to_rob(&DECODE_BUFFER.entry[DECODE_BUFFER.head]);
            ROB.entry[rob_index].event_cycle = this->_current_core_cycle;

            ooo_model_instr empty_entry;
            DECODE_BUFFER.entry[DECODE_BUFFER.head] = empty_entry;

            DECODE_BUFFER.head++;
            if (DECODE_BUFFER.head >= DECODE_BUFFER.SIZE) {
                DECODE_BUFFER.head = 0;
            }
            DECODE_BUFFER.occupancy--;

            count_dispatches++;
            if (count_dispatches >= DECODE_WIDTH) {
                break;
            }
        } else {
            break;
        }
    }

    // make new instructions pay decode penalty if they miss in the decoded
    // instruction cache
    uint32_t decode_index = DECODE_BUFFER.head;
    uint32_t count_decodes = 0;
    for (uint32_t i = 0; i < DECODE_BUFFER.SIZE; i++) {
        if (DECODE_BUFFER.entry[DECODE_BUFFER.head].ip == 0) {
            break;
        }

        if (DECODE_BUFFER.entry[decode_index].event_cycle == 0) {
            // apply decode latency
            DECODE_BUFFER.entry[decode_index].event_cycle =
                this->_current_core_cycle + DECODE_LATENCY;
        }

        if (decode_index == DECODE_BUFFER.tail) {
            break;
        }
        decode_index++;
        if (decode_index >= DECODE_BUFFER.SIZE) {
            decode_index = 0;
        }

        count_decodes++;
        if (count_decodes > DECODE_WIDTH) {
            break;
        }
    }
}

int O3_CPU::prefetch_code_line(uint64_t pf_v_addr) {
    if (pf_v_addr == 0) {
        cerr << "Cannot prefetch code line 0x0 !!!" << endl;
        assert(0);
    }

    /*
     * TODO: This relates to the legacy ChampSim's caches. We need to update
     * thepefetching infrastructure to support
     */
    // L1I.pf_requested++;
    //
    // if (L1I.PQ.occupancy < L1I.PQ.SIZE)
    //   {
    //     // magically translate prefetches
    //     uint64_t pf_pa = (va_to_pa(cpu, 0, pf_v_addr,
    //     pf_v_addr>>LOG2_PAGE_SIZE, 1) & (~((1 << LOG2_PAGE_SIZE) - 1))) |
    //     (pf_v_addr & ((1 << LOG2_PAGE_SIZE) - 1));
    //
    //     PACKET pf_packet;
    //     pf_packet.instruction = 1; // this is a code prefetch
    //     pf_packet.is_data = 0;
    //     pf_packet.fill_level = FILL_L1;
    //     pf_packet.fill_l1i = 1;
    //     pf_packet.pf_origin_level = FILL_L1;
    //     pf_packet.cpu = cpu;
    //
    //     pf_packet.address = pf_pa >> LOG2_BLOCK_SIZE;
    //     pf_packet.full_addr = pf_pa;
    //
    //     pf_packet.ip = pf_v_addr;
    //     pf_packet.type = PREFETCH;
    //     pf_packet.event_cycle = current_core_cycle[cpu];
    //
    //     L1I.add_pq(&pf_packet);
    //     L1I.pf_issued++;
    //
    //     return 1;
    //   }

    return 0;
}

// TODO: When should we update ROB.schedule_event_cycle?
// I. Instruction is fetched
// II. Instruction is completed
// III. Instruction is retired
void O3_CPU::schedule_instruction() {
    if ((ROB.head == ROB.tail) && ROB.occupancy == 0) return;

    num_searched = 0;
    if (ROB.head < ROB.tail) {
        for (uint32_t i = ROB.head; i < ROB.tail; i++) {
            if ((ROB.entry[i].fetched != COMPLETED) ||
                (ROB.entry[i].event_cycle > this->_current_core_cycle) ||
                (num_searched >= SCHEDULER_SIZE))
                return;

            if (ROB.entry[i].scheduled == 0) do_scheduling(i);

            if (ROB.entry[i].executed == 0) num_searched++;
        }
    } else {
        for (uint32_t i = ROB.head; i < ROB.SIZE; i++) {
            if ((ROB.entry[i].fetched != COMPLETED) ||
                (ROB.entry[i].event_cycle > this->_current_core_cycle) ||
                (num_searched >= SCHEDULER_SIZE))
                return;

            if (ROB.entry[i].scheduled == 0) do_scheduling(i);

            if (ROB.entry[i].executed == 0) num_searched++;
        }
        for (uint32_t i = 0; i < ROB.tail; i++) {
            if ((ROB.entry[i].fetched != COMPLETED) ||
                (ROB.entry[i].event_cycle > this->_current_core_cycle) ||
                (num_searched >= SCHEDULER_SIZE))
                return;

            if (ROB.entry[i].scheduled == 0) do_scheduling(i);

            if (ROB.entry[i].executed == 0) num_searched++;
        }
    }
}

void O3_CPU::do_scheduling(uint32_t rob_index) {
    ROB.entry[rob_index].reg_ready =
        1;  // reg_ready will be reset to 0 if there is RAW dependency

    reg_dependency(rob_index);

    if (ROB.entry[rob_index].is_memory)
        ROB.entry[rob_index].scheduled = INFLIGHT;
    else {
        ROB.entry[rob_index].scheduled = COMPLETED;

        // ADD LATENCY
        if (ROB.entry[rob_index].event_cycle < this->_current_core_cycle)
            ROB.entry[rob_index].event_cycle =
                this->_current_core_cycle + SCHEDULING_LATENCY;
        else
            ROB.entry[rob_index].event_cycle += SCHEDULING_LATENCY;

        if (ROB.entry[rob_index].reg_ready) {
#ifdef SANITY_CHECK
            if (RTE1[RTE1_tail] < ROB_SIZE) assert(0);
#endif
            // remember this rob_index in the Ready-To-Execute array 1
            RTE1[RTE1_tail] = rob_index;

            DP(if (warmup_complete[cpu]) {
                cout << "[RTE1] " << __func__
                     << " instr_id: " << ROB.entry[rob_index].instr_id
                     << " rob_index: " << rob_index << " is added to RTE1";
                cout << " head: " << RTE1_head << " tail: " << RTE1_tail
                     << endl;
            });

            RTE1_tail++;
            if (RTE1_tail == ROB_SIZE) RTE1_tail = 0;
        }
    }
}

void O3_CPU::reg_dependency(uint32_t rob_index) {
    // print out source/destination registers
    DP(if (warmup_complete[cpu]) {
        for (uint32_t i = 0; i < NUM_INSTR_SOURCES; i++) {
            if (ROB.entry[rob_index].source_registers[i]) {
                cout << "[ROB] " << __func__
                     << " instr_id: " << ROB.entry[rob_index].instr_id
                     << " is_memory: " << +ROB.entry[rob_index].is_memory;
                cout << " load  reg_index: "
                     << +ROB.entry[rob_index].source_registers[i] << endl;
            }
        }
        for (uint32_t i = 0; i < MAX_INSTR_DESTINATIONS; i++) {
            if (ROB.entry[rob_index].destination_registers[i]) {
                cout << "[ROB] " << __func__
                     << " instr_id: " << ROB.entry[rob_index].instr_id
                     << " is_memory: " << +ROB.entry[rob_index].is_memory;
                cout << " store reg_index: "
                     << +ROB.entry[rob_index].destination_registers[i] << endl;
            }
        }
    });

    // check RAW dependency
    int prior = rob_index - 1;
    if (prior < 0) prior = ROB.SIZE - 1;

    if (rob_index != ROB.head) {
        if ((int)ROB.head <= prior) {
            for (int i = prior; i >= (int)ROB.head; i--)
                if (ROB.entry[i].executed != COMPLETED) {
                    for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
                        if (ROB.entry[rob_index].source_registers[j] &&
                            (ROB.entry[rob_index].reg_RAW_checked[j] == 0))
                            reg_RAW_dependency(i, rob_index, j);
                    }
                }
        } else {
            for (int i = prior; i >= 0; i--)
                if (ROB.entry[i].executed != COMPLETED) {
                    for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
                        if (ROB.entry[rob_index].source_registers[j] &&
                            (ROB.entry[rob_index].reg_RAW_checked[j] == 0))
                            reg_RAW_dependency(i, rob_index, j);
                    }
                }
            for (int i = ROB.SIZE - 1; i >= (int)ROB.head; i--)
                if (ROB.entry[i].executed != COMPLETED) {
                    for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
                        if (ROB.entry[rob_index].source_registers[j] &&
                            (ROB.entry[rob_index].reg_RAW_checked[j] == 0))
                            reg_RAW_dependency(i, rob_index, j);
                    }
                }
        }
    }
}

void O3_CPU::reg_RAW_dependency(uint32_t prior, uint32_t current,
                                uint32_t source_index) {
    for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
        if (ROB.entry[prior].destination_registers[i] == 0) continue;

        if (ROB.entry[prior].destination_registers[i] ==
            ROB.entry[current].source_registers[source_index]) {
            // we need to mark this dependency in the ROB since the producer
            // might not be added in the store queue yet
            ROB.entry[prior].registers_instrs_depend_on_me.insert(
                current);  // this load cannot be executed until the prior store
                           // gets executed
            ROB.entry[prior].registers_index_depend_on_me[source_index].insert(
                current);  // this load cannot be executed until the prior store
                           // gets executed
            ROB.entry[prior].reg_RAW_producer = 1;

            ROB.entry[current].reg_ready = 0;
            ROB.entry[current].producer_id = ROB.entry[prior].instr_id;
            ROB.entry[current].num_reg_dependent++;
            ROB.entry[current].reg_RAW_checked[source_index] = 1;

            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " instr_id: " << ROB.entry[current].instr_id
                     << " is_memory: " << +ROB.entry[current].is_memory;
                cout << " RAW reg_index: "
                     << +ROB.entry[current].source_registers[source_index];
                cout << " producer_id: " << ROB.entry[prior].instr_id << endl;
            });

            return;
        }
    }
}

void O3_CPU::execute_instruction() {
    if ((ROB.head == ROB.tail) && ROB.occupancy == 0) return;

    // out-of-order execution for non-memory instructions
    // memory instructions are handled by memory_instruction()
    uint32_t exec_issued = 0, num_iteration = 0;

    while (exec_issued < EXEC_WIDTH) {
        if (RTE0[RTE0_head] < ROB_SIZE) {
            uint32_t exec_index = RTE0[RTE0_head];
            if (ROB.entry[exec_index].event_cycle <=
                this->_current_core_cycle) {
                do_execution(exec_index);

                RTE0[RTE0_head] = ROB_SIZE;
                RTE0_head++;
                if (RTE0_head == ROB_SIZE) RTE0_head = 0;
                exec_issued++;
            }
        } else {
            // DP (if (warmup_complete[cpu]) {
            // cout << "[RTE0] is empty head: " << RTE0_head << " tail: " <<
            // RTE0_tail << endl; });
            break;
        }

        num_iteration++;
        if (num_iteration == (ROB_SIZE - 1)) break;
    }

    num_iteration = 0;
    while (exec_issued < EXEC_WIDTH) {
        if (RTE1[RTE1_head] < ROB_SIZE) {
            uint32_t exec_index = RTE1[RTE1_head];
            if (ROB.entry[exec_index].event_cycle <=
                this->_current_core_cycle) {
                do_execution(exec_index);

                RTE1[RTE1_head] = ROB_SIZE;
                RTE1_head++;
                if (RTE1_head == ROB_SIZE) RTE1_head = 0;
                exec_issued++;
            }
        } else {
            // DP (if (warmup_complete[cpu]) {
            // cout << "[RTE1] is empty head: " << RTE1_head << " tail: " <<
            // RTE1_tail << endl; });
            break;
        }

        num_iteration++;
        if (num_iteration == (ROB_SIZE - 1)) break;
    }
}

void O3_CPU::do_execution(uint32_t rob_index) {
    // if (ROB.entry[rob_index].reg_ready && (ROB.entry[rob_index].scheduled ==
    // COMPLETED) && (ROB.entry[rob_index].event_cycle <=
    // current_core_cycle[cpu])) {

    // cout << "do_execution() rob_index: " << rob_index << " cycle: " <<
    // current_core_cycle[cpu] << endl;

    ROB.entry[rob_index].executed = INFLIGHT;

    // ADD LATENCY
    if (ROB.entry[rob_index].event_cycle < this->_current_core_cycle)
        ROB.entry[rob_index].event_cycle =
            this->_current_core_cycle + EXEC_LATENCY;
    else
        ROB.entry[rob_index].event_cycle += EXEC_LATENCY;

    inflight_reg_executions++;

    DP(if (warmup_complete[cpu]) {
        cout << "[ROB] " << __func__
             << " non-memory instr_id: " << ROB.entry[rob_index].instr_id;
        cout << " event_cycle: " << ROB.entry[rob_index].event_cycle << endl;
    });
    //}
}

uint8_t O3_CPU::mem_reg_dependence_resolved(uint32_t rob_index) {
    if (ROB.entry[rob_index].reg_ready) {
        return 1;
    } else {
        uint8_t count_source_regs = 0;
        uint8_t stack_pointer_source = 0;
        for (int i = 0; i < NUM_INSTR_SOURCES; i++) {
            if (ROB.entry[rob_index].source_registers[i] != 0) {
                count_source_regs++;
            }
            if (ROB.entry[rob_index].source_registers[i] == REG_STACK_POINTER) {
                stack_pointer_source = 1;
            }
        }

        if (stack_pointer_source == 1) {
            return 0;
        }

        if ((count_source_regs == 1) &&
            (ROB.entry[rob_index].source_registers[0] ==
             ROB.entry[rob_index].destination_registers[0])) {
            return 1;
        }
    }

    return 0;
}

void O3_CPU::schedule_memory_instruction() {
    if ((ROB.head == ROB.tail) && ROB.occupancy == 0) return;

    // execution is out-of-order but we have an in-order scheduling algorithm to
    // detect all RAW dependencies
    num_searched = 0;
    if (ROB.head < ROB.tail) {
        for (uint32_t i = ROB.head; i < ROB.tail; i++) {
            if ((ROB.entry[i].fetched != COMPLETED) ||
                (ROB.entry[i].event_cycle > this->_current_core_cycle) ||
                (num_searched >= SCHEDULER_SIZE))
                break;

            if (ROB.entry[i].is_memory && mem_reg_dependence_resolved(i) &&
                (ROB.entry[i].scheduled == INFLIGHT))
                do_memory_scheduling(i);

            if (ROB.entry[i].executed == 0) num_searched++;
        }
    } else {
        for (uint32_t i = ROB.head; i < ROB.SIZE; i++) {
            if ((ROB.entry[i].fetched != COMPLETED) ||
                (ROB.entry[i].event_cycle > this->_current_core_cycle) ||
                (num_searched >= SCHEDULER_SIZE))
                break;

            if (ROB.entry[i].is_memory && mem_reg_dependence_resolved(i) &&
                (ROB.entry[i].scheduled == INFLIGHT))
                do_memory_scheduling(i);

            if (ROB.entry[i].executed == 0) num_searched++;
        }
        for (uint32_t i = 0; i < ROB.tail; i++) {
            if ((ROB.entry[i].fetched != COMPLETED) ||
                (ROB.entry[i].event_cycle > this->_current_core_cycle) ||
                (num_searched >= SCHEDULER_SIZE))
                break;

            if (ROB.entry[i].is_memory && mem_reg_dependence_resolved(i) &&
                (ROB.entry[i].scheduled == INFLIGHT))
                do_memory_scheduling(i);

            if (ROB.entry[i].executed == 0) num_searched++;
        }
    }
}

void O3_CPU::execute_memory_instruction() {
    operate_lsq();
    operate_cache();
}

void O3_CPU::do_memory_scheduling(uint32_t rob_index) {
    uint32_t not_available = check_and_add_lsq(rob_index);
    if (not_available == 0) {
        ROB.entry[rob_index].scheduled = COMPLETED;
        if (ROB.entry[rob_index].executed ==
            0)  // it could be already set to COMPLETED due to store-to-load
                // forwarding
            ROB.entry[rob_index].executed = INFLIGHT;

        DP(if (warmup_complete[cpu]) {
            cout << "[ROB] " << __func__
                 << " instr_id: " << ROB.entry[rob_index].instr_id
                 << " rob_index: " << rob_index;
            cout << " scheduled all num_mem_ops: "
                 << ROB.entry[rob_index].num_mem_ops << endl;
        });
    }
}

uint32_t O3_CPU::check_and_add_lsq(uint32_t rob_index) {
    uint32_t num_mem_ops = 0, num_added = 0;

    // load
    for (uint32_t i = 0; i < NUM_INSTR_SOURCES; i++) {
        if (ROB.entry[rob_index].source_memory[i]) {
            num_mem_ops++;
            if (ROB.entry[rob_index].source_added[i])
                num_added++;
            else if (LQ.occupancy < LQ.SIZE) {
                add_load_queue(rob_index, i);
                num_added++;
            } else {
                DP(if (warmup_complete[cpu]) {
                    cout << "[LQ] " << __func__
                         << " instr_id: " << ROB.entry[rob_index].instr_id;
                    cout << " cannot be added in the load queue occupancy: "
                         << LQ.occupancy
                         << " cycle: " << current_core_cycle[cpu] << endl;
                });
            }
        }
    }

    // store
    for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
        if (ROB.entry[rob_index].destination_memory[i]) {
            num_mem_ops++;
            if (ROB.entry[rob_index].destination_added[i])
                num_added++;
            else if (SQ.occupancy < SQ.SIZE) {
                if (STA[STA_head] == ROB.entry[rob_index].instr_id) {
                    add_store_queue(rob_index, i);
                    num_added++;
                }
                // add_store_queue(rob_index, i);
                // num_added++;
            } else {
                DP(if (warmup_complete[cpu]) {
                    cout << "[SQ] " << __func__
                         << " instr_id: " << ROB.entry[rob_index].instr_id;
                    cout << " cannot be added in the store queue occupancy: "
                         << SQ.occupancy
                         << " cycle: " << current_core_cycle[cpu] << endl;
                });
            }
        }
    }

    if (num_added == num_mem_ops) return 0;

    uint32_t not_available = num_mem_ops - num_added;
    if (not_available > num_mem_ops) {
        cerr << "instr_id: " << ROB.entry[rob_index].instr_id << endl;
        assert(0);
    }

    return not_available;
}

void O3_CPU::add_load_queue(uint32_t rob_index, uint32_t data_index) {
    // search for an empty slot
    uint32_t lq_index = LQ.SIZE;
    for (uint32_t i = 0; i < LQ.SIZE; i++) {
        if (LQ.entry[i].virtual_address == 0) {
            lq_index = i;
            break;
        }
    }

    // sanity check
    if (lq_index == LQ.SIZE) {
        cerr << "instr_id: " << ROB.entry[rob_index].instr_id
             << " no empty slot in the load queue!!!" << endl;
        assert(0);
    }

    // add it to the load queue
    ROB.entry[rob_index].lq_index[data_index] = lq_index;
    LQ.entry[lq_index].instr_id = ROB.entry[rob_index].instr_id;
    LQ.entry[lq_index].virtual_address =
        ROB.entry[rob_index].source_memory[data_index];
    LQ.entry[lq_index].memory_size =
        ROB.entry[rob_index].source_memory_size[data_index];
    LQ.entry[lq_index].ip = ROB.entry[rob_index].ip;
    LQ.entry[lq_index].data_index = data_index;
    LQ.entry[lq_index].rob_index = rob_index;
    LQ.entry[lq_index].asid[0] = ROB.entry[rob_index].asid[0];
    LQ.entry[lq_index].asid[1] = ROB.entry[rob_index].asid[1];
    LQ.entry[lq_index].event_cycle =
        this->_current_core_cycle + SCHEDULING_LATENCY;
#if !defined(ENABLE_DCLR)
    LQ.entry[lq_index].went_offchip_pred = this->offchip_pred->predict(
        &ROB.entry[rob_index], data_index,
        &LQ.entry[lq_index]);  // TODO: Here, we should be using an actual
                               // predictor.
#endif                         // defined(ENABLE_DCLR)

    LQ.occupancy++;

    // check RAW dependency
    int prior = rob_index - 1;
    if (prior < 0) prior = ROB.SIZE - 1;

    if (rob_index != ROB.head) {
        if ((int)ROB.head <= prior) {
            for (int i = prior; i >= (int)ROB.head; i--) {
                if (LQ.entry[lq_index].producer_id != UINT64_MAX) break;

                mem_RAW_dependency(i, rob_index, data_index, lq_index);
            }
        } else {
            for (int i = prior; i >= 0; i--) {
                if (LQ.entry[lq_index].producer_id != UINT64_MAX) break;

                mem_RAW_dependency(i, rob_index, data_index, lq_index);
            }
            for (int i = ROB.SIZE - 1; i >= (int)ROB.head; i--) {
                if (LQ.entry[lq_index].producer_id != UINT64_MAX) break;

                mem_RAW_dependency(i, rob_index, data_index, lq_index);
            }
        }
    }

    // check
    // 1) if store-to-load forwarding is possible
    // 2) if there is WAR that are not correctly executed
    uint32_t forwarding_index = SQ.SIZE;
    for (uint32_t i = 0; i < SQ.SIZE; i++) {
        // skip empty slot
        if (SQ.entry[i].virtual_address == 0) continue;

        // forwarding should be done by the SQ entry that holds the same
        // producer_id from RAW dependency check
        if (SQ.entry[i].virtual_address ==
            LQ.entry[lq_index]
                .virtual_address) {  // store-to-load forwarding check

            // forwarding store is in the SQ
            if ((rob_index != ROB.head) && (LQ.entry[lq_index].producer_id ==
                                            SQ.entry[i].instr_id)) {  // RAW
                forwarding_index = i;
                break;  // should be break
            }

            if ((LQ.entry[lq_index].producer_id == UINT64_MAX) &&
                (LQ.entry[lq_index].instr_id <= SQ.entry[i].instr_id)) {  // WAR
                // a load is about to be added in the load queue and we found a
                // store that is "logically later in the program order but
                // already executed" => this is not correctly executed WAR due
                // to out-of-order execution, this case is possible, for example
                // 1) application is load intensive and load queue is full
                // 2) we have loads that can't be added in the load queue
                // 3) subsequent stores logically behind in the program order
                // are added in the store queue first

                // thanks to the store buffer, data is not written back to the
                // memory system until retirement also due to in-order
                // retirement, this "already executed store" cannot be retired
                // until we finish the prior load instruction if we detect WAR
                // when a load is added in the load queue, just let the load
                // instruction to access the memory system no need to mark any
                // dependency because this is actually WAR not RAW

                // do not forward data from the store queue since this is WAR
                // just read correct data from data cache

                LQ.entry[lq_index].physical_address = 0;
                LQ.entry[lq_index].translated = 0;
                LQ.entry[lq_index].fetched = 0;

                DP(if (warmup_complete[cpu]) {
                    cout << "[LQ] " << __func__
                         << " instr_id: " << LQ.entry[lq_index].instr_id
                         << " reset fetched: " << +LQ.entry[lq_index].fetched;
                    cout << " to obey WAR store instr_id: "
                         << SQ.entry[i].instr_id
                         << " cycle: " << current_core_cycle[cpu] << endl;
                });
            }
        }
    }

    if (forwarding_index != SQ.SIZE) {  // we have a store-to-load forwarding

        if ((SQ.entry[forwarding_index].fetched == COMPLETED) &&
            (SQ.entry[forwarding_index].event_cycle <=
             this->_current_core_cycle)) {
            LQ.entry[lq_index].physical_address =
                (SQ.entry[forwarding_index].physical_address &
                 ~(uint64_t)((1 << LOG2_BLOCK_SIZE) - 1)) |
                (LQ.entry[lq_index].virtual_address &
                 ((1 << LOG2_BLOCK_SIZE) - 1));
            LQ.entry[lq_index].translated = COMPLETED;
            LQ.entry[lq_index].fetched = COMPLETED;

            uint32_t fwr_rob_index = LQ.entry[lq_index].rob_index;
            ROB.entry[fwr_rob_index].num_mem_ops--;
            ROB.entry[fwr_rob_index].event_cycle = this->_current_core_cycle;
            if (ROB.entry[fwr_rob_index].num_mem_ops < 0) {
                cerr << "instr_id: " << ROB.entry[fwr_rob_index].instr_id
                     << endl;
                assert(0);
            }
            if (ROB.entry[fwr_rob_index].num_mem_ops == 0)
                inflight_mem_executions++;

            DP(if (warmup_complete[cpu]) {
                cout << "[LQ] " << __func__
                     << " instr_id: " << LQ.entry[lq_index].instr_id << hex;
                cout << " full_addr: " << LQ.entry[lq_index].physical_address
                     << dec << " is forwarded by store instr_id: ";
                cout << SQ.entry[forwarding_index].instr_id
                     << " remain_num_ops: "
                     << ROB.entry[fwr_rob_index].num_mem_ops
                     << " cycle: " << current_core_cycle[cpu] << endl;
            });

            release_load_queue(lq_index);
        } else
            ;  // store is not executed yet, forwarding will be handled by
               // execute_store()
    }

    // succesfully added to the load queue
    ROB.entry[rob_index].source_added[data_index] = 1;

    if (LQ.entry[lq_index].virtual_address &&
        (LQ.entry[lq_index].producer_id ==
         UINT64_MAX)) {  // not released and no forwarding
        RTL0[RTL0_tail] = lq_index;
        RTL0_tail++;
        if (RTL0_tail == LQ_SIZE) RTL0_tail = 0;

        DP(if (warmup_complete[cpu]) {
            cout << "[RTL0] " << __func__
                 << " instr_id: " << LQ.entry[lq_index].instr_id
                 << " rob_index: " << LQ.entry[lq_index].rob_index
                 << " is added to RTL0";
            cout << " head: " << RTL0_head << " tail: " << RTL0_tail << endl;
        });
    }

    DP(if (warmup_complete[cpu]) {
        cout << "[LQ] " << __func__
             << " instr_id: " << LQ.entry[lq_index].instr_id;
        cout << " is added in the LQ address: " << hex
             << LQ.entry[lq_index].virtual_address << dec
             << " translated: " << +LQ.entry[lq_index].translated;
        cout << " fetched: " << +LQ.entry[lq_index].fetched
             << " index: " << lq_index << " occupancy: " << LQ.occupancy
             << " cycle: " << current_core_cycle[cpu] << endl;
    });
}

void O3_CPU::mem_RAW_dependency(uint32_t prior, uint32_t current,
                                uint32_t data_index, uint32_t lq_index) {
    for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
        if (ROB.entry[prior].destination_memory[i] == 0) continue;

        if (ROB.entry[prior].destination_memory[i] ==
            ROB.entry[current]
                .source_memory[data_index]) {  //  store-to-load forwarding
                                               //  check

            // we need to mark this dependency in the ROB since the producer
            // might not be added in the store queue yet
            ROB.entry[prior].memory_instrs_depend_on_me.insert(
                current);  // this load cannot be executed until the prior store
                           // gets executed
            ROB.entry[prior].is_producer = 1;
            LQ.entry[lq_index].producer_id = ROB.entry[prior].instr_id;
            LQ.entry[lq_index].translated = INFLIGHT;

            DP(if (warmup_complete[cpu]) {
                cout << "[LQ] " << __func__
                     << " RAW producer instr_id: " << ROB.entry[prior].instr_id
                     << " consumer_id: " << ROB.entry[current].instr_id
                     << " lq_index: " << lq_index;
                cout << hex
                     << " address: " << ROB.entry[prior].destination_memory[i]
                     << dec << endl;
            });

            return;
        }
    }
}

void O3_CPU::add_store_queue(uint32_t rob_index, uint32_t data_index) {
    uint32_t sq_index = SQ.tail;
#ifdef SANITY_CHECK
    if (SQ.entry[sq_index].virtual_address) assert(0);
#endif

    /*
    // search for an empty slot
    uint32_t sq_index = SQ.SIZE;
    for (uint32_t i=0; i<SQ.SIZE; i++) {
        if (SQ.entry[i].virtual_address == 0) {
            sq_index = i;
            break;
        }
    }

    // sanity check
    if (sq_index == SQ.SIZE) {
        cerr << "instr_id: " << ROB.entry[rob_index].instr_id << " no empty slot
    in the store queue!!!" << endl; assert(0);
    }
    */

    // add it to the store queue
    ROB.entry[rob_index].sq_index[data_index] = sq_index;
    SQ.entry[sq_index].instr_id = ROB.entry[rob_index].instr_id;
    SQ.entry[sq_index].virtual_address =
        ROB.entry[rob_index].destination_memory[data_index];
    SQ.entry[sq_index].memory_size =
        ROB.entry[rob_index].destination_memory_size[data_index];
    SQ.entry[sq_index].ip = ROB.entry[rob_index].ip;
    SQ.entry[sq_index].data_index = data_index;
    SQ.entry[sq_index].rob_index = rob_index;
    SQ.entry[sq_index].asid[0] = ROB.entry[rob_index].asid[0];
    SQ.entry[sq_index].asid[1] = ROB.entry[rob_index].asid[1];
    SQ.entry[sq_index].event_cycle =
        this->_current_core_cycle + SCHEDULING_LATENCY;

    SQ.occupancy++;
    SQ.tail++;
    if (SQ.tail == SQ.SIZE) SQ.tail = 0;

    // succesfully added to the store queue
    ROB.entry[rob_index].destination_added[data_index] = 1;

    STA[STA_head] = UINT64_MAX;
    STA_head++;
    if (STA_head == STA_SIZE) STA_head = 0;

    RTS0[RTS0_tail] = sq_index;
    RTS0_tail++;
    if (RTS0_tail == SQ_SIZE) RTS0_tail = 0;

    DP(if (warmup_complete[cpu]) {
        cout << "[SQ] " << __func__
             << " instr_id: " << SQ.entry[sq_index].instr_id;
        cout << " is added in the SQ translated: "
             << +SQ.entry[sq_index].translated
             << " fetched: " << +SQ.entry[sq_index].fetched
             << " is_producer: " << +ROB.entry[rob_index].is_producer;
        cout << " cycle: " << current_core_cycle[cpu] << endl;
    });
}

void O3_CPU::operate_lsq() {
    // handle store
    uint32_t store_issued = 0, num_iteration = 0;

    while (store_issued < SQ_WIDTH) {
        if (RTS0[RTS0_head] < SQ_SIZE) {
            uint32_t sq_index = RTS0[RTS0_head];
            if (SQ.entry[sq_index].event_cycle <= this->_current_core_cycle) {
                // add it to DTLB
                PACKET data_packet;

                data_packet.tlb_access = 1;
                data_packet.fill_level = FILL_L1;
                data_packet.fill_l1d = 1;
                data_packet.cpu = cpu;
                data_packet.data_index = SQ.entry[sq_index].data_index;
                data_packet.sq_index = sq_index;
                if (helper::knob_cloudsuite)
                    data_packet.address =
                        ((SQ.entry[sq_index].virtual_address >> LOG2_PAGE_SIZE)
                         << 9) |
                        SQ.entry[sq_index].asid[1];
                else
                    data_packet.address =
                        SQ.entry[sq_index].virtual_address >> LOG2_PAGE_SIZE;
                data_packet.full_addr = SQ.entry[sq_index].virtual_address;
                data_packet.memory_size = BLOCK_SIZE;
                data_packet.instr_id = SQ.entry[sq_index].instr_id;
                data_packet.rob_index = SQ.entry[sq_index].rob_index;
                data_packet.went_offchip_pred =
                    this->SQ.entry[sq_index].went_offchip_pred;
                data_packet.ip = SQ.entry[sq_index].ip;
                data_packet.type = RFO;
                data_packet.asid[0] = SQ.entry[sq_index].asid[0];
                data_packet.asid[1] = SQ.entry[sq_index].asid[1];
                data_packet.event_cycle = SQ.entry[sq_index].event_cycle;

                DP(if (warmup_complete[cpu]) {
                    cout << "[RTS0] " << __func__
                         << " instr_id: " << SQ.entry[sq_index].instr_id
                         << " rob_index: " << SQ.entry[sq_index].rob_index
                         << " is popped from to RTS0";
                    cout << " head: " << RTS0_head << " tail: " << RTS0_tail
                         << endl;
                });

                int rq_index = DTLB.add_rq(&data_packet);

                if (rq_index == -2)
                    break;
                else
                    SQ.entry[sq_index].translated = INFLIGHT;

                RTS0[RTS0_head] = SQ_SIZE;
                RTS0_head++;
                if (RTS0_head == SQ_SIZE) RTS0_head = 0;

                store_issued++;
            }
        } else {
            // DP (if (warmup_complete[cpu]) {
            // cout << "[RTS0] is empty head: " << RTS0_head << " tail: " <<
            // RTS0_tail << endl; });
            break;
        }

        num_iteration++;
        if (num_iteration == (SQ_SIZE - 1)) break;
    }

    num_iteration = 0;
    while (store_issued < SQ_WIDTH) {
        if (RTS1[RTS1_head] < SQ_SIZE) {
            uint32_t sq_index = RTS1[RTS1_head];
            if (SQ.entry[sq_index].event_cycle <= this->_current_core_cycle) {
                execute_store(SQ.entry[sq_index].rob_index, sq_index,
                              SQ.entry[sq_index].data_index);

                RTS1[RTS1_head] = SQ_SIZE;
                RTS1_head++;
                if (RTS1_head == SQ_SIZE) RTS1_head = 0;

                store_issued++;
            }
        } else {
            // DP (if (warmup_complete[cpu]) {
            // cout << "[RTS1] is empty head: " << RTS1_head << " tail: " <<
            // RTS1_tail << endl; });
            break;
        }

        num_iteration++;
        if (num_iteration == (SQ_SIZE - 1)) break;
    }

    unsigned load_issued = 0;
    num_iteration = 0;
    while (load_issued < LQ_WIDTH) {
        if (RTL0[RTL0_head] < LQ_SIZE) {
            uint32_t lq_index = RTL0[RTL0_head];
            if (LQ.entry[lq_index].event_cycle <= this->_current_core_cycle) {
                // add it to DTLB
                PACKET data_packet;
                data_packet.fill_level = FILL_L1;
                data_packet.fill_l1d = 1;
                data_packet.cpu = cpu;
                data_packet.data_index = LQ.entry[lq_index].data_index;
                data_packet.lq_index = lq_index;
                if (helper::knob_cloudsuite)
                    data_packet.address =
                        ((LQ.entry[lq_index].virtual_address >> LOG2_PAGE_SIZE)
                         << 9) |
                        LQ.entry[lq_index].asid[1];
                else
                    data_packet.address =
                        LQ.entry[lq_index].virtual_address >> LOG2_PAGE_SIZE;
                data_packet.full_addr = LQ.entry[lq_index].virtual_address;
                data_packet.memory_size = BLOCK_SIZE;
                data_packet.instr_id = LQ.entry[lq_index].instr_id;
                data_packet.rob_index = LQ.entry[lq_index].rob_index;
                data_packet.went_offchip_pred =
                    this->LQ.entry[lq_index].went_offchip_pred;
                data_packet.ip = LQ.entry[lq_index].ip;
                data_packet.type = LOAD;
                data_packet.asid[0] = LQ.entry[lq_index].asid[0];
                data_packet.asid[1] = LQ.entry[lq_index].asid[1];
                data_packet.event_cycle = LQ.entry[lq_index].event_cycle;

                DP(if (warmup_complete[cpu]) {
                    cout << "[RTL0] " << __func__
                         << " instr_id: " << LQ.entry[lq_index].instr_id
                         << " rob_index: " << LQ.entry[lq_index].rob_index
                         << " is popped to RTL0";
                    cout << " head: " << RTL0_head << " tail: " << RTL0_tail
                         << endl;
                });

                int rq_index = DTLB.add_rq(&data_packet);

                if (rq_index == -2)
                    break;  // break here
                else
                    LQ.entry[lq_index].translated = INFLIGHT;

                RTL0[RTL0_head] = LQ_SIZE;
                RTL0_head++;
                if (RTL0_head == LQ_SIZE) RTL0_head = 0;

                load_issued++;
            }
        } else {
            // DP (if (warmup_complete[cpu]) {
            // cout << "[RTL0] is empty head: " << RTL0_head << " tail: " <<
            // RTL0_tail << endl; });
            break;
        }

        num_iteration++;
        if (num_iteration == (LQ_SIZE - 1)) break;
    }

    num_iteration = 0;
    while (load_issued < LQ_WIDTH) {
        if (RTL1[RTL1_head] < LQ_SIZE) {
            uint32_t lq_index = RTL1[RTL1_head];
            if (LQ.entry[lq_index].event_cycle <= this->_current_core_cycle) {
                int rq_index =
                    execute_load(LQ.entry[lq_index].rob_index, lq_index,
                                 LQ.entry[lq_index].data_index);

                if (rq_index != -2) {
                    RTL1[RTL1_head] = LQ_SIZE;
                    RTL1_head++;
                    if (RTL1_head == LQ_SIZE) RTL1_head = 0;

                    load_issued++;
                }
            }
        } else {
            // DP (if (warmup_complete[cpu]) {
            // cout << "[RTL1] is empty head: " << RTL1_head << " tail: " <<
            // RTL1_tail << endl; });
            break;
        }

        num_iteration++;
        if (num_iteration == (LQ_SIZE - 1)) break;
    }
}

void O3_CPU::execute_store(uint32_t rob_index, uint32_t sq_index,
                           uint32_t data_index) {
    SQ.entry[sq_index].fetched = COMPLETED;
    SQ.entry[sq_index].event_cycle = this->_current_core_cycle;

    ROB.entry[rob_index].num_mem_ops--;
    ROB.entry[rob_index].event_cycle = this->_current_core_cycle;
    if (ROB.entry[rob_index].num_mem_ops < 0) {
        cerr << "instr_id: " << ROB.entry[rob_index].instr_id << endl;
        assert(0);
    }
    if (ROB.entry[rob_index].num_mem_ops == 0) inflight_mem_executions++;

    DP(if (warmup_complete[cpu]) {
        cout << "[SQ1] " << __func__
             << " instr_id: " << SQ.entry[sq_index].instr_id << hex;
        cout << " full_address: " << SQ.entry[sq_index].physical_address << dec
             << " remain_mem_ops: " << ROB.entry[rob_index].num_mem_ops;
        cout << " event_cycle: " << SQ.entry[sq_index].event_cycle << endl;
    });

    // resolve RAW dependency after DTLB access
    // check if this store has dependent loads
    if (ROB.entry[rob_index].is_producer) {
        ITERATE_SET(dependent, ROB.entry[rob_index].memory_instrs_depend_on_me,
                    ROB_SIZE) {
            // check if dependent loads are already added in the load queue
            for (uint32_t j = 0; j < NUM_INSTR_SOURCES;
                 j++) {  // which one is dependent?
                if (ROB.entry[dependent].source_memory[j] &&
                    ROB.entry[dependent].source_added[j]) {
                    if (ROB.entry[dependent].source_memory[j] ==
                        SQ.entry[sq_index]
                            .virtual_address) {  // this is required since a
                                                 // single instruction can issue
                                                 // multiple loads

                        // now we can resolve RAW dependency
                        uint32_t lq_index = ROB.entry[dependent].lq_index[j];
#ifdef SANITY_CHECK
                        if (lq_index >= LQ.SIZE) assert(0);
                        if (LQ.entry[lq_index].producer_id !=
                            SQ.entry[sq_index].instr_id) {
                            cerr << "[SQ2] " << __func__
                                 << " lq_index: " << lq_index
                                 << " producer_id: "
                                 << LQ.entry[lq_index].producer_id;
                            cerr << " does not match to the store instr_id: "
                                 << SQ.entry[sq_index].instr_id << endl;
                            assert(0);
                        }
#endif
                        // update correspodning LQ entry
                        LQ.entry[lq_index].physical_address =
                            (SQ.entry[sq_index].physical_address &
                             ~(uint64_t)((1 << LOG2_BLOCK_SIZE) - 1)) |
                            (LQ.entry[lq_index].virtual_address &
                             ((1 << LOG2_BLOCK_SIZE) - 1));
                        LQ.entry[lq_index].translated = COMPLETED;
                        LQ.entry[lq_index].fetched = COMPLETED;
                        LQ.entry[lq_index].event_cycle =
                            this->_current_core_cycle;

                        uint32_t fwr_rob_index = LQ.entry[lq_index].rob_index;
                        ROB.entry[fwr_rob_index].num_mem_ops--;
                        ROB.entry[fwr_rob_index].event_cycle =
                            this->_current_core_cycle;
#ifdef SANITY_CHECK
                        if (ROB.entry[fwr_rob_index].num_mem_ops < 0) {
                            cerr << "instr_id: "
                                 << ROB.entry[fwr_rob_index].instr_id << endl;
                            assert(0);
                        }
#endif
                        if (ROB.entry[fwr_rob_index].num_mem_ops == 0)
                            inflight_mem_executions++;

                        DP(if (warmup_complete[cpu]) {
                            cout << "[LQ3] " << __func__
                                 << " instr_id: " << LQ.entry[lq_index].instr_id
                                 << hex;
                            cout << " full_addr: "
                                 << LQ.entry[lq_index].physical_address << dec
                                 << " is forwarded by store instr_id: ";
                            cout << SQ.entry[sq_index].instr_id
                                 << " remain_num_ops: "
                                 << ROB.entry[fwr_rob_index].num_mem_ops
                                 << " cycle: " << current_core_cycle[cpu]
                                 << endl;
                        });

                        release_load_queue(lq_index);

                        // clear dependency bit
                        if (j == (NUM_INSTR_SOURCES - 1))
                            ROB.entry[rob_index]
                                .memory_instrs_depend_on_me.insert(dependent);
                    }
                }
            }
        }
    }
}

int O3_CPU::execute_load(uint32_t rob_index, uint32_t lq_index,
                         uint32_t data_index) {
    int rq_index = cc::cache::add_queue_failure;

    // add it to L1D
    PACKET data_packet;
    data_packet.fill_level = FILL_L1;
    data_packet.fill_l1d = 1;
    data_packet.cpu = cpu;
    data_packet.data_index = LQ.entry[lq_index].data_index;
    data_packet.lq_index = lq_index;
    data_packet.address =
        LQ.entry[lq_index].physical_address >> LOG2_BLOCK_SIZE;
    data_packet.full_addr = LQ.entry[lq_index].physical_address;
    data_packet.v_address =
        LQ.entry[lq_index].virtual_address >> LOG2_BLOCK_SIZE;
    data_packet.full_v_addr = LQ.entry[lq_index].virtual_address;
    data_packet.memory_size = LQ.entry[lq_index].memory_size;
    data_packet.instr_id = LQ.entry[lq_index].instr_id;
    data_packet.rob_index = LQ.entry[lq_index].rob_index;
    data_packet.went_offchip_pred = LQ.entry[lq_index].went_offchip_pred;
    data_packet.block_location_pred =
        this->LQ.entry[lq_index].block_location_pred;
    data_packet.ip = LQ.entry[lq_index].ip;
    data_packet.type = LOAD;
    data_packet.asid[0] = LQ.entry[lq_index].asid[0];
    data_packet.asid[1] = LQ.entry[lq_index].asid[1];
    data_packet.event_cycle = LQ.entry[lq_index].event_cycle;

    if ((rq_index = this->l1d->add_read_queue(data_packet)) ==
        cc::cache::add_queue_failure) {
        return rq_index;
    } else {
        LQ.entry[lq_index].fetched = INFLIGHT;
    }

    // Adding the PC linked to that memory request to the PC table.
    this->_pc_table.insert(std::make_pair(data_packet.ip, 0));

    return rq_index;
}

void O3_CPU::complete_execution(uint32_t rob_index) {
    if (ROB.entry[rob_index].is_memory == 0) {
        if ((ROB.entry[rob_index].executed == INFLIGHT) &&
            (ROB.entry[rob_index].event_cycle <= this->_current_core_cycle)) {
            ROB.entry[rob_index].executed = COMPLETED;
            inflight_reg_executions--;
            completed_executions++;

            if (ROB.entry[rob_index].reg_RAW_producer)
                reg_RAW_release(rob_index);

            if (ROB.entry[rob_index].branch_mispredicted) {
                fetch_resume_cycle =
                    this->_current_core_cycle + BRANCH_MISPREDICT_PENALTY;
            }

            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " instr_id: " << ROB.entry[rob_index].instr_id;
                cout << " branch_mispredicted: "
                     << +ROB.entry[rob_index].branch_mispredicted
                     << " fetch_stall: " << +fetch_stall;
                cout << " event: " << ROB.entry[rob_index].event_cycle << endl;
            });
        }
    } else {
        if (ROB.entry[rob_index].num_mem_ops == 0) {
            if ((ROB.entry[rob_index].executed == INFLIGHT) &&
                (ROB.entry[rob_index].event_cycle <=
                 this->_current_core_cycle)) {
                ROB.entry[rob_index].executed = COMPLETED;
                inflight_mem_executions--;
                completed_executions++;

                if (ROB.entry[rob_index].reg_RAW_producer)
                    reg_RAW_release(rob_index);

                if (ROB.entry[rob_index].branch_mispredicted) {
                    fetch_resume_cycle =
                        this->_current_core_cycle + BRANCH_MISPREDICT_PENALTY;
                }

                DP(if (warmup_complete[cpu]) {
                    cout << "[ROB] " << __func__
                         << " instr_id: " << ROB.entry[rob_index].instr_id;
                    cout << " is_memory: " << +ROB.entry[rob_index].is_memory
                         << " branch_mispredicted: "
                         << +ROB.entry[rob_index].branch_mispredicted;
                    cout << " fetch_stall: " << +fetch_stall
                         << " event: " << ROB.entry[rob_index].event_cycle
                         << " current: " << current_core_cycle[cpu] << endl;
                });
            }
        }
    }
}

void O3_CPU::reg_RAW_release(uint32_t rob_index) {
    // if (!ROB.entry[rob_index].registers_instrs_depend_on_me.empty())

    ITERATE_SET(i, ROB.entry[rob_index].registers_instrs_depend_on_me,
                ROB_SIZE) {
        for (uint32_t j = 0; j < NUM_INSTR_SOURCES; j++) {
            if (ROB.entry[rob_index].registers_index_depend_on_me[j].search(
                    i)) {
                ROB.entry[i].num_reg_dependent--;

                if (ROB.entry[i].num_reg_dependent == 0) {
                    ROB.entry[i].reg_ready = 1;
                    if (ROB.entry[i].is_memory)
                        ROB.entry[i].scheduled = INFLIGHT;
                    else {
                        ROB.entry[i].scheduled = COMPLETED;

#ifdef SANITY_CHECK
                        if (RTE0[RTE0_tail] < ROB_SIZE) assert(0);
#endif
                        // remember this rob_index in the Ready-To-Execute array
                        // 0
                        RTE0[RTE0_tail] = i;

                        DP(if (warmup_complete[cpu]) {
                            cout << "[RTE0] " << __func__
                                 << " instr_id: " << ROB.entry[i].instr_id
                                 << " rob_index: " << i << " is added to RTE0";
                            cout << " head: " << RTE0_head
                                 << " tail: " << RTE0_tail << endl;
                        });

                        RTE0_tail++;
                        if (RTE0_tail == ROB_SIZE) RTE0_tail = 0;
                    }
                }

                DP(if (warmup_complete[cpu]) {
                    cout << "[ROB] " << __func__
                         << " instr_id: " << ROB.entry[rob_index].instr_id
                         << " releases instr_id: ";
                    cout << ROB.entry[i].instr_id
                         << " reg_index: " << +ROB.entry[i].source_registers[j]
                         << " num_reg_dependent: "
                         << ROB.entry[i].num_reg_dependent
                         << " cycle: " << current_core_cycle[cpu] << endl;
                });
            }
        }
    }
}

void O3_CPU::operate_cache() {
    ITLB.operate();
    DTLB.operate();
    STLB.operate();

    this->l1i->operate();
    this->l1d->operate();
    this->l2c->operate();
    this->sdc->operate();

    // also handle per-cycle prefetcher operation
    l1i_prefetcher_cycle_operate();
}

void O3_CPU::update_rob() {
    if (ITLB.PROCESSED.occupancy &&
        (ITLB.PROCESSED.entry[ITLB.PROCESSED.head].event_cycle <=
         this->_current_core_cycle))
        complete_instr_fetch(&ITLB.PROCESSED, 1);

    // Marks the related entries in either the store queue and the load queue as
    // completed.
    if (this->l1i->processed_queue()->occupancy != 0 &&
        (this->l1i->processed_queue()
             ->entry[this->l1i->processed_queue()->head]
             .event_cycle <= this->_current_core_cycle)) {
        complete_instr_fetch(this->l1i->processed_queue(), false);
    }

    if (DTLB.PROCESSED.occupancy &&
        (DTLB.PROCESSED.entry[DTLB.PROCESSED.head].event_cycle <=
         this->_current_core_cycle))
        complete_data_fetch(&DTLB.PROCESSED, 1);

    // Marks the related entries in either the store queue and the load queue as
    // completed.
    if (this->l1d->processed_queue()->occupancy != 0 &&
        (this->l1d->processed_queue()
             ->entry[this->l1d->processed_queue()->head]
             .event_cycle <= this->_current_core_cycle)) {
        complete_data_fetch(this->l1d->processed_queue(), false);
    }

    if (this->sdc->processed_queue()->occupancy != 0 &&
        (this->sdc->processed_queue()
             ->entry[this->sdc->processed_queue()->head]
             .event_cycle <= this->_current_core_cycle)) {
        complete_data_fetch(this->sdc->processed_queue(), false);
    }

    // if (uncore.DRAM.PROCESSED.occupancy &&
    // (uncore.DRAM.PROCESSED.entry[uncore.DRAM.PROCESSED.head].event_cycle <=
    // current_core_cycle[cpu])) {
    //     complete_data_fetch(&uncore.DRAM.PROCESSED, false);
    // }

    // update ROB entries with completed executions
    if ((inflight_reg_executions > 0) || (inflight_mem_executions > 0)) {
        if (ROB.head < ROB.tail) {
            for (uint32_t i = ROB.head; i < ROB.tail; i++)
                complete_execution(i);
        } else {
            for (uint32_t i = ROB.head; i < ROB.SIZE; i++)
                complete_execution(i);
            for (uint32_t i = 0; i < ROB.tail; i++) complete_execution(i);
        }
    }
}

void O3_CPU::complete_instr_fetch(PACKET_QUEUE *queue, uint8_t is_it_tlb) {
    uint32_t index = queue->head, rob_index = queue->entry[index].rob_index,
             num_fetched = 0;

    uint64_t complete_ip = queue->entry[index].ip;

    if (is_it_tlb) {
        uint64_t instruction_physical_address =
            (queue->entry[index].instruction_pa << LOG2_PAGE_SIZE) |
            (complete_ip & ((1 << LOG2_PAGE_SIZE) - 1));

        // mark the appropriate instructions in the IFETCH_BUFFER as translated
        // and ready to fetch
        for (uint32_t j = 0; j < IFETCH_BUFFER.SIZE; j++) {
            if (((IFETCH_BUFFER.entry[j].ip) >> LOG2_PAGE_SIZE) ==
                ((complete_ip) >> LOG2_PAGE_SIZE)) {
                IFETCH_BUFFER.entry[j].translated = COMPLETED;

                // we did not fetch this instruction's cache line, but we did
                // translated it
                IFETCH_BUFFER.entry[j].fetched = 0;
                // recalculate a physical address for this cache line based on
                // the translated physical page address
                uint64_t instr_pa =
                    (queue->entry[index].instruction_pa << LOG2_PAGE_SIZE) |
                    ((IFETCH_BUFFER.entry[j].ip) & ((1 << LOG2_PAGE_SIZE) - 1));
                IFETCH_BUFFER.entry[j].instruction_pa = instr_pa;
            }
        }

        // remove this entry
        queue->remove_queue(&queue->entry[index]);
    } else {
        // this is the L1I cache, so instructions are now fully fetched, so mark
        // them as such
        for (uint32_t j = 0; j < IFETCH_BUFFER.SIZE; j++) {
            if (((IFETCH_BUFFER.entry[j].ip) >> 6) == ((complete_ip) >> 6)) {
                IFETCH_BUFFER.entry[j].translated = COMPLETED;
                IFETCH_BUFFER.entry[j].fetched = COMPLETED;
            }
        }

        // remove this entry
        queue->remove_queue(&queue->entry[index]);
    }

    return;

    // old function below

#ifdef SANITY_CHECK
    if (rob_index != check_rob(queue->entry[index].instr_id)) assert(0);
#endif

    // update ROB entry
    if (is_it_tlb) {
        ROB.entry[rob_index].translated = COMPLETED;
        ROB.entry[rob_index].instruction_pa =
            (queue->entry[index].instruction_pa << LOG2_PAGE_SIZE) |
            (ROB.entry[rob_index].ip &
             ((1 << LOG2_PAGE_SIZE) - 1));  // translated address
    } else
        ROB.entry[rob_index].fetched = COMPLETED;
    ROB.entry[rob_index].event_cycle = this->_current_core_cycle;
    num_fetched++;

    DP(if (warmup_complete[cpu]) {
        cout << "[" << queue->NAME << "] " << __func__ << " cpu: " << cpu
             << " instr_id: " << ROB.entry[rob_index].instr_id;
        cout << " ip: " << hex << ROB.entry[rob_index].ip
             << " address: " << ROB.entry[rob_index].instruction_pa << dec;
        cout << " translated: " << +ROB.entry[rob_index].translated
             << " fetched: " << +ROB.entry[rob_index].fetched;
        cout << " event_cycle: " << ROB.entry[rob_index].event_cycle << endl;
    });

    // check if other instructions were merged
    if (queue->entry[index].instr_merged) {
        ITERATE_SET(i, queue->entry[index].rob_index_depend_on_me, ROB_SIZE) {
            // update ROB entry
            if (is_it_tlb) {
                ROB.entry[i].translated = COMPLETED;
                ROB.entry[i].instruction_pa =
                    (queue->entry[index].instruction_pa << LOG2_PAGE_SIZE) |
                    (ROB.entry[i].ip &
                     ((1 << LOG2_PAGE_SIZE) - 1));  // translated address
            } else
                ROB.entry[i].fetched = COMPLETED;
            ROB.entry[i].event_cycle =
                this->_current_core_cycle + (num_fetched / FETCH_WIDTH);
            num_fetched++;

            DP(if (warmup_complete[cpu]) {
                cout << "[" << queue->NAME << "] " << __func__
                     << " cpu: " << cpu
                     << " instr_id: " << ROB.entry[i].instr_id;
                cout << " ip: " << hex << ROB.entry[i].ip
                     << " address: " << ROB.entry[i].instruction_pa << dec;
                cout << " translated: " << +ROB.entry[i].translated
                     << " fetched: " << +ROB.entry[i].fetched
                     << " provider: " << ROB.entry[rob_index].instr_id;
                cout << " event_cycle: " << ROB.entry[i].event_cycle << endl;
            });
        }
    }

    // remove this entry
    queue->remove_queue(&queue->entry[index]);
}

void O3_CPU::complete_data_fetch(PACKET_QUEUE *queue, uint8_t is_it_tlb) {
    uint32_t index = queue->head, rob_index = queue->entry[index].rob_index,
             sq_index = queue->entry[index].sq_index,
             lq_index = queue->entry[index].lq_index;

#ifdef SANITY_CHECK
    if (queue->entry[index].type != RFO) {
        if (rob_index != check_rob(queue->entry[index].instr_id)) assert(0);
    }
#endif

    // update ROB entry
    if (is_it_tlb) {  // DTLB

        if (queue->entry[index].type == RFO) {
            SQ.entry[sq_index].physical_address =
                (queue->entry[index].data_pa << LOG2_PAGE_SIZE) |
                (SQ.entry[sq_index].virtual_address &
                 ((1 << LOG2_PAGE_SIZE) - 1));  // translated address
            SQ.entry[sq_index].translated = COMPLETED;
            SQ.entry[sq_index].event_cycle = this->_current_core_cycle;

            RTS1[RTS1_tail] = sq_index;
            RTS1_tail++;
            if (RTS1_tail == SQ_SIZE) RTS1_tail = 0;

            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " RFO instr_id: " << SQ.entry[sq_index].instr_id;
                cout << " DTLB_FETCH_DONE translation: "
                     << +SQ.entry[sq_index].translated << hex << " page: "
                     << (SQ.entry[sq_index].physical_address >> LOG2_PAGE_SIZE);
                cout << " full_addr: " << SQ.entry[sq_index].physical_address
                     << dec
                     << " store_merged: " << +queue->entry[index].store_merged;
                cout << " load_merged: " << +queue->entry[index].load_merged
                     << endl;
            });

            handle_merged_translation(&queue->entry[index]);
        } else {
            LQ.entry[lq_index].physical_address =
                (queue->entry[index].data_pa << LOG2_PAGE_SIZE) |
                (LQ.entry[lq_index].virtual_address &
                 ((1 << LOG2_PAGE_SIZE) - 1));  // translated address
            LQ.entry[lq_index].translated = COMPLETED;
            LQ.entry[lq_index].event_cycle = this->_current_core_cycle;

            RTL1[RTL1_tail] = lq_index;
            RTL1_tail++;
            if (RTL1_tail == LQ_SIZE) RTL1_tail = 0;

#if defined(ENABLE_DCLR)
            this->LQ.entry[lq_index].block_location_pred =
                this->dclr_predict_location_perfect(
                    this->LQ.entry[lq_index].physical_address);

            if (this->LQ.entry[lq_index].block_location_pred ==
                cc::is_in_dram) {
                this->issue_dclr_request(
                    lq_index, this->LQ.entry[lq_index].block_location_pred);
            }
#else
            /**
             * HERMES: translation has been done for this load request.
             * Thus we have the physical address. So now we can issue
             * a DDRP request, if this LQ entry is predicto to go offchip.
             */
            if (this->offchip_pred->consume_from_core(lq_index)) {
                this->issue_ddrp_request(lq_index);
            }
#endif  // defined(ENABLE_DCLR)

            DP(if (warmup_complete[cpu]) {
                cout << "[RTL1] " << __func__
                     << " instr_id: " << LQ.entry[lq_index].instr_id
                     << " rob_index: " << LQ.entry[lq_index].rob_index
                     << " is added to RTL1";
                cout << " head: " << RTL1_head << " tail: " << RTL1_tail
                     << endl;
            });

            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " load instr_id: " << LQ.entry[lq_index].instr_id;
                cout << " DTLB_FETCH_DONE translation: "
                     << +LQ.entry[lq_index].translated << hex << " page: "
                     << (LQ.entry[lq_index].physical_address >> LOG2_PAGE_SIZE);
                cout << " full_addr: " << LQ.entry[lq_index].physical_address
                     << dec
                     << " store_merged: " << +queue->entry[index].store_merged;
                cout << " load_merged: " << +queue->entry[index].load_merged
                     << endl;
            });

            handle_merged_translation(&queue->entry[index]);
        }

        ROB.entry[rob_index].event_cycle = queue->entry[index].event_cycle;
    } else {  // L1D or SDC

        PACKET &packet = queue->entry[index];

        this->_request_served_positions[packet.served_from]++;

        // We forward info to the LP component of the core when a packet
        // returns.
        if (packet.fill_level == cc::cache::fill_sdc) {
            cc::irreg_access_pred::sdc_path_feedback_info info = {.packet =
                                                                      &packet};

            this->irreg_pred.feedback_sdc_path(info);
        } else if (packet.fill_level == cc::cache::fill_l1) {
            cc::irreg_access_pred::l1d_path_feedback_info info = {.packet =
                                                                      &packet};

            this->irreg_pred.feedback_l1d_path(info);
        }

        if (packet.missed_in != 0x0 && packet.type != WRITEBACK) {
            // if (packet.missed_in != 0x0 && packet.full_v_addr >=
            // this->irreg_base && packet.full_v_addr <= this->irreg_bound) {
            this->misses_in_cache[packet.missed_in]++;
        }

        // WIP: Getting the location from which the request was served.
        LQ.entry[lq_index].served_from = packet.served_from;

        if (queue->entry[index].type == RFO)
            handle_merged_load(&queue->entry[index]);
        else {
#ifdef SANITY_CHECK
            if (queue->entry[index].store_merged) assert(0);
#endif
            LQ.entry[lq_index].fetched = COMPLETED;
            ROB.entry[rob_index].num_mem_ops--;
            LQ.entry[lq_index].event_cycle = this->_current_core_cycle;
            ROB.entry[rob_index].event_cycle = queue->entry[index].event_cycle;

#ifdef SANITY_CHECK
            if (ROB.entry[rob_index].num_mem_ops < 0) {
                cerr << "instr_id: " << ROB.entry[rob_index].instr_id << endl;
                assert(0);
            }
#endif
            if (ROB.entry[rob_index].num_mem_ops == 0)
                inflight_mem_executions++;

            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " load instr_id: " << LQ.entry[lq_index].instr_id;
                cout << " L1D_FETCH_DONE fetched: "
                     << +LQ.entry[lq_index].fetched << hex << " address: "
                     << (LQ.entry[lq_index].physical_address >>
                         LOG2_BLOCK_SIZE);
                cout << " full_addr: " << LQ.entry[lq_index].physical_address
                     << dec
                     << " remain_mem_ops: " << ROB.entry[rob_index].num_mem_ops;
                cout << " load_merged: " << +queue->entry[index].load_merged
                     << " inflight_mem: " << inflight_mem_executions << endl;
            });

            release_load_queue(lq_index);
            handle_merged_load(&queue->entry[index]);
        }
    }

    // remove this entry
    queue->remove_queue(&queue->entry[index]);
}

/**
 * @brief Collects stats on a load request that came back to the cache through
 * the cache hierarchy. In addition, the offchip predictor trains on the
 * collected data.
 *
 * @param lq_index Index in the load queue of the load request.
 */
void O3_CPU::offchip_pred_stats_and_training(const std::size_t &lq_index) {
    // stats

    // training
    std::size_t rob_index = this->LQ.entry[lq_index].rob_index,
                data_index = UINT64_MAX;

    for (std::size_t index = 0; index < NUM_INSTR_SOURCES; index++) {
        if (this->ROB.entry[rob_index].lq_index[index] == lq_index) {
            data_index = index;
            break;
        }
    }

    // sanity check
    if (data_index == UINT64_MAX)
        throw std::runtime_error("No matching index found.");

#if !defined(ENABLE_DCLR)
    if (this->LQ.entry[lq_index].perc_feature) {
        // Also training the PC-based predictor.
        this->_mm.pbp().train(this->LQ.entry[lq_index]);

#if defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))
        if (this->LQ.entry[lq_index].served_from == cc::is_l1d &&
            this->LQ.entry[lq_index].went_offchip_pred) {
            this->LQ.entry[lq_index].offchip_pred_hit_l1d = true;
        } else if (this->LQ.entry[lq_index].served_from == cc::is_l2c &&
            this->LQ.entry[lq_index].went_offchip_pred) {
            this->LQ.entry[lq_index].offchip_pred_hit_l2c = true;
        }
#endif  // defined(ENABLE_FSP) && \
    !(defined(ENABLED_DELAYED_FSP) || defined(ENABLED_BIMODAL_FSP))

        this->offchip_pred->train(&this->ROB.entry[rob_index],
                                  (uint32_t)data_index,
                                  &this->LQ.entry[lq_index]);
    }
#endif  // !define(ENABLE_DCLR)
}

/**
 * @brief When the off-chip predictor predicts a memory request to go off-chip
 * this method is called in order to issue an Hermes request. Here, Hermes
 * request refers to a speculative request issued to the memory controller.
 *
 * @param lq_index The index in the load queue of the memory request for which
 * we are willing to throw an Hermes request.
 */
void O3_CPU::issue_ddrp_request(const std::size_t &lq_index,
                                const bool &from_l1d_miss) {
    LSQ_ENTRY &lq_entry = this->LQ.entry[lq_index];

    /**
     * We start with a couple sanity checks (pre-condition):
     *  1. The address translation must have been completed;
     *  2. The load queue entry has to have a valid physical address.
     */
    if (lq_entry.translated != COMPLETED) {
        throw std::runtime_error(
            "[HERMES] Throwing an Hermes request on a memory access for which "
            "the address translation has not been completed yet is not "
            "allowed.");
    }

    if (lq_entry.physical_address == 0ULL) {
        throw std::runtime_error(
            "[HERMES] Throwing an Hermes request on a memory access for which "
            "the physical address is invalid is not allowed.");
    }

    // If the DRAM read queue, we do not insert any packet and simply ignore the
    // Hermes request.
    if (uncore.DRAM.get_occupancy(
            1, lq_entry.physical_address >> LOG2_BLOCK_SIZE) ==
        uncore.DRAM.get_size(1, lq_entry.physical_address >> LOG2_BLOCK_SIZE)) {
        return;
    }

    // Now, we create a packet to be added to the DRAM RQ.
    PACKET data_packet;
    data_packet.fill_level = cc::cache::fill_ddrp;
    data_packet.fill_l1d = 0;
    data_packet.cpu = cpu;
    data_packet.data_index = lq_entry.data_index;
    data_packet.lq_index = lq_index;
    data_packet.address = lq_entry.physical_address >> LOG2_BLOCK_SIZE;
    data_packet.full_addr = lq_entry.physical_address;
    data_packet.instr_id = lq_entry.instr_id;
    data_packet.rob_index = lq_entry.rob_index;
    data_packet.ip = lq_entry.ip;
    data_packet.type = PREFETCH;
    data_packet.asid[0] = lq_entry.asid[0];
    data_packet.asid[1] = lq_entry.asid[1];
    data_packet.l1d_offchip_pred_used = from_l1d_miss;

    if (from_l1d_miss) {
        data_packet.event_cycle =
            this->current_core_cycle() + champsim::simulator::instance()
                                             ->hermes_knobs()
                                             .ddrp_request_latency;
    } else {
        data_packet.event_cycle =
            lq_entry.event_cycle +
            champsim::simulator::instance()
                ->hermes_knobs()
                .ddrp_request_latency;  // Here, we use 6 cycles to reflect
                                        // the Hermes-O design.
    }

    uncore.DRAM.add_rq(&data_packet);
}

void O3_CPU::issue_ddrp_request_on_prefetch(const PACKET &packet) {
    // LSQ_ENTRY &lq_entry = this->LQ.entry[lq_index];

    /**
     * We start with a couple sanity checks (pre-condition):
     *  1. The address translation must have been completed;
     *  2. The load queue entry has to have a valid physical address.
     */
    // if (lq_entry.translated != COMPLETED) {
    //     throw std::runtime_error(
    //         "[HERMES] Throwing an Hermes request on a memory access for which
    //         " "the address translation has not been completed yet is not "
    //         "allowed.");
    // }

    // if (lq_entry.physical_address == 0ULL) {
    //     throw std::runtime_error(
    //         "[HERMES] Throwing an Hermes request on a memory access for which
    //         " "the physical address is invalid is not allowed.");
    // }

    // If the DRAM read queue, we do not insert any packet and simply ignore the
    // Hermes request.
    if (uncore.DRAM.get_occupancy(1, packet.address) ==
        uncore.DRAM.get_size(1, packet.address)) {
        return;
    }

    // Now, we create a packet to be added to the DRAM RQ.
    PACKET data_packet;
    data_packet.fill_level = cc::cache::fill_ddrp;
    data_packet.fill_l1d = 0;
    data_packet.cpu = cpu;
    data_packet.data_index = packet.data_index;
    data_packet.address = packet.address;
    data_packet.full_addr = packet.full_addr;
    // data_packet.instr_id = lq_entry.instr_id;
    // data_packet.rob_index = lq_entry.rob_index;
    // data_packet.ip = lq_entry.ip;
    data_packet.type = PREFETCH;
    data_packet.asid[0] = packet.asid[0];
    data_packet.asid[1] = packet.asid[1];

    data_packet.event_cycle =
        this->current_core_cycle() +
        champsim::simulator::instance()->hermes_knobs().ddrp_request_latency;

    uncore.DRAM.add_rq(&data_packet);
}

/**
 * @brief Issues a DCLR request based on the information contained in the LQ
 * entry provided trough the lq_entry reference. The location to which the
 * request must be sent to is provided through the loc parameter.
 *
 * @note A DCLR request can be thrown to any of the constituents of the memory
 * hierarchy. Either the L1D, the L2C, the LLC of the DRAM.
 *
 * @param lq_entry
 * @param loc
 */
void O3_CPU::issue_dclr_request(const std::size_t &lq_index,
                                const cc::block_location &loc) {
    PACKET dclr_packet;
    LSQ_ENTRY &lq_entry = this->LQ.entry[lq_index];

    // First, let's run some sanity checks...
    /**
     * We start with a couple sanity checks (pre-condition):
     *  1. The address translation must have been completed;
     *  2. The load queue entry has to have a valid physical address.
     */
    if (lq_entry.translated != COMPLETED) {
        throw std::runtime_error(
            "[DCLR] Throwing an Hermes request on a memory access for which "
            "the address translation has not been completed yet is not "
            "allowed.");
    }

    if (lq_entry.physical_address == 0ULL) {
        throw std::runtime_error(
            "[DCLR] Throwing an Hermes request on a memory access for which "
            "the physical address is invalid is not allowed.");
    }

    // Now, we can build the DCLR packet.
    dclr_packet.fill_level = cc::cache::fill_dclr;
    dclr_packet.fill_l1d = 0;
    dclr_packet.cpu = cpu;
    dclr_packet.data_index = lq_entry.data_index;
    dclr_packet.lq_index = lq_index;
    dclr_packet.address = lq_entry.physical_address >> LOG2_BLOCK_SIZE;
    dclr_packet.full_addr = lq_entry.physical_address;
    dclr_packet.instr_id = lq_entry.instr_id;
    dclr_packet.rob_index = lq_entry.rob_index;
    dclr_packet.ip = lq_entry.ip;
    dclr_packet.type = cc::cache::load;
    dclr_packet.asid[0] = lq_entry.asid[0];
    dclr_packet.asid[1] = lq_entry.asid[1];
    dclr_packet.event_cycle =
        lq_entry.event_cycle +
        champsim::simulator::instance()->hermes_knobs().ddrp_request_latency;

    // Adding the caches to the fill path.
    switch (loc) {
        case cc::is_in_l2c:
        case cc::is_in_both:
            dclr_packet.push_fill_path(this->l1d);
            break;

        case cc::is_in_llc:
            dclr_packet.push_fill_path(this->l1d);
            dclr_packet.push_fill_path(this->l2c);
            break;

        case cc::is_in_dram:
            dclr_packet.push_fill_path(this->l1d);
            dclr_packet.push_fill_path(this->l2c);
            dclr_packet.push_fill_path(uncore.llc);
            break;
    }

    // Sending to the appropriate location.
    switch (loc) {
        case cc::is_in_l2c:
        case cc::is_in_both:
            if (this->l2c->read_queue()->occupancy <
                this->l2c->read_queue_size()) {
                this->l2c->add_read_queue(dclr_packet);
            }
            break;

        case cc::is_in_llc:
            if (uncore.llc->read_queue()->occupancy <
                uncore.llc->read_queue_size()) {
                uncore.llc->add_read_queue(dclr_packet);
            }
            break;

        case cc::is_in_dram:
            if (uncore.DRAM.get_occupancy(
                    1, lq_entry.physical_address >> LOG2_BLOCK_SIZE) <
                uncore.DRAM.get_size(
                    1, lq_entry.physical_address >> LOG2_BLOCK_SIZE)) {
                uncore.DRAM.add_rq(&dclr_packet);
            }
            break;
    }
}

/**
 * @brief Provides the actual location in the memory hierarchy hierarchy of
 * memory block.
 *
 * @param paddr
 * @return cc::block_location
 */
cc::block_location O3_CPU::dclr_predict_location_perfect(
    const uint64_t &paddr) {
    auto _helper = [paddr](const cc::cache *c) -> bool {
        uint32_t set = c->get_set(paddr);
        uint16_t way = c->get_way(paddr, set);

        return (way != c->associativity());
    };

    std::map<cc::block_location, cc::cache *> block_cache_map = {
        // {cc::is_in_l1d, this->l1d},
        {cc::is_in_l2c, this->l2c},
        {cc::is_in_llc, uncore.llc}};
    std::list<cc::block_location> res_loc;

    for (auto e : block_cache_map) {
        if (_helper(e.second)) res_loc.push_back(e.first);
    }

    return ((res_loc.empty())
                ? cc::is_in_dram
                : *std::min_element(res_loc.begin(), res_loc.end()));
}

void O3_CPU::handle_merged_translation(PACKET *provider) {
    if (provider->store_merged) {
        ITERATE_SET(merged, provider->sq_index_depend_on_me, SQ.SIZE) {
            SQ.entry[merged].translated = COMPLETED;
            SQ.entry[merged].physical_address =
                (provider->data_pa << LOG2_PAGE_SIZE) |
                (SQ.entry[merged].virtual_address &
                 ((1 << LOG2_PAGE_SIZE) - 1));  // translated address
            SQ.entry[merged].event_cycle = this->_current_core_cycle;

            RTS1[RTS1_tail] = merged;
            RTS1_tail++;
            if (RTS1_tail == SQ_SIZE) RTS1_tail = 0;

            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " store instr_id: " << SQ.entry[merged].instr_id;
                cout << " DTLB_FETCH_DONE translation: "
                     << +SQ.entry[merged].translated << hex << " page: "
                     << (SQ.entry[merged].physical_address >> LOG2_PAGE_SIZE);
                cout << " full_addr: " << SQ.entry[merged].physical_address
                     << dec << " by instr_id: " << +provider->instr_id << endl;
            });
        }
    }
    if (provider->load_merged) {
        ITERATE_SET(merged, provider->lq_index_depend_on_me, LQ.SIZE) {
            LQ.entry[merged].translated = COMPLETED;
            LQ.entry[merged].physical_address =
                (provider->data_pa << LOG2_PAGE_SIZE) |
                (LQ.entry[merged].virtual_address &
                 ((1 << LOG2_PAGE_SIZE) - 1));  // translated address
            LQ.entry[merged].event_cycle = this->_current_core_cycle;

            RTL1[RTL1_tail] = merged;
            RTL1_tail++;
            if (RTL1_tail == LQ_SIZE) RTL1_tail = 0;

#if defined(ENABLE_DCLR)
            this->LQ.entry[merged].block_location_pred =
                this->dclr_predict_location_perfect(
                    this->LQ.entry[merged].physical_address);

            if (this->LQ.entry[merged].block_location_pred == cc::is_in_dram) {
                this->issue_dclr_request(
                    merged, this->LQ.entry[merged].block_location_pred);
            }
#else
            /**
             * HERMES: Translation has been done via merged translation
             * thus we have the physical address. So now we can issue
             * a DDRP request, if this LQ entry is predicted to go off-chip.
             */
            if (this->offchip_pred->consume_from_core(merged)) {
                this->issue_ddrp_request(merged);
            }
#endif  // defined(ENABLE_DCLR)

            DP(if (warmup_complete[cpu]) {
                cout << "[RTL1] " << __func__
                     << " instr_id: " << LQ.entry[merged].instr_id
                     << " rob_index: " << LQ.entry[merged].rob_index
                     << " is added to RTL1";
                cout << " head: " << RTL1_head << " tail: " << RTL1_tail
                     << endl;
            });

            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " load instr_id: " << LQ.entry[merged].instr_id;
                cout << " DTLB_FETCH_DONE translation: "
                     << +LQ.entry[merged].translated << hex << " page: "
                     << (LQ.entry[merged].physical_address >> LOG2_PAGE_SIZE);
                cout << " full_addr: " << LQ.entry[merged].physical_address
                     << dec << " by instr_id: " << +provider->instr_id << endl;
            });
        }
    }
}

void O3_CPU::handle_merged_load(PACKET *provider) {
    ITERATE_SET(merged, provider->lq_index_depend_on_me, LQ.SIZE) {
        uint32_t merged_rob_index = LQ.entry[merged].rob_index;

        /*
         * Relaxing conditions when simulating a parallel look-up design.
         * If the instruction in the ROB and in the LQ do not match, it means
         * that this load has already been processed fully. Thus, we can ignore
         * this iteration.
         */
        // if (LQ.entry[merged].instr_id !=
        // ROB.entry[merged_rob_index].instr_id)
        //     continue;
        if (LQ.entry[merged].virtual_address == 0ULL) continue;

        LQ.entry[merged].fetched = COMPLETED;
        ROB.entry[merged_rob_index].num_mem_ops--;
        LQ.entry[merged].event_cycle = this->_current_core_cycle;
        ROB.entry[merged_rob_index].event_cycle = this->_current_core_cycle;

        LQ.entry[merged].went_offchip =
            LQ.entry[provider->lq_index].went_offchip;

        // WIP: Where was that request served from in the memory sub-system?
        LQ.entry[merged].served_from = LQ.entry[provider->lq_index].served_from;

#ifdef SANITY_CHECK
        if (ROB.entry[merged_rob_index].num_mem_ops < 0) {
            cerr << "instr_id: " << ROB.entry[merged_rob_index].instr_id
                 << " rob_index: " << merged_rob_index << endl;
            assert(0);
        }
#endif

        if (ROB.entry[merged_rob_index].num_mem_ops == 0)
            inflight_mem_executions++;

        DP(if (warmup_complete[cpu]) {
            cout << "[ROB] " << __func__
                 << " load instr_id: " << LQ.entry[merged].instr_id;
            cout << " L1D_FETCH_DONE translation: "
                 << +LQ.entry[merged].translated << hex << " address: "
                 << (LQ.entry[merged].physical_address >> LOG2_BLOCK_SIZE);
            cout << " full_addr: " << LQ.entry[merged].physical_address << dec
                 << " by instr_id: " << +provider->instr_id;
            cout << " remain_mem_ops: "
                 << ROB.entry[merged_rob_index].num_mem_ops << endl;
        });

        release_load_queue(merged);
    }
}

void O3_CPU::release_load_queue(uint32_t lq_index) {
    // release LQ entries
    DP(if (warmup_complete[cpu]) {
        cout << "[LQ] " << __func__
             << " instr_id: " << LQ.entry[lq_index].instr_id
             << " releases lq_index: " << lq_index;
        cout << hex << " full_addr: " << LQ.entry[lq_index].physical_address
             << dec << endl;
    });

#if !defined(ENABLE_DCLR)
    // HERMES: Offchip predictor stats collection and training.
    if (this->LQ.entry[lq_index].perc_feature) {
        this->offchip_pred_stats_and_training(lq_index);

        if (this->LQ.entry[lq_index].perc_feature) {
            delete this->LQ.entry[lq_index].perc_feature;
        }
    }

#endif  // !defined(ENABLE_DCLR)

    LSQ_ENTRY empty_entry;
    LQ.entry[lq_index] = empty_entry;
    LQ.occupancy--;
}

void O3_CPU::retire_rob() {
    for (uint32_t n = 0; n < RETIRE_WIDTH; n++) {
        if (ROB.entry[ROB.head].ip == 0) return;

        // retire is in-order
        if (ROB.entry[ROB.head].executed != COMPLETED) {
            DP(if (warmup_complete[cpu]) {
                cout << "[ROB] " << __func__
                     << " instr_id: " << ROB.entry[ROB.head].instr_id
                     << " head: " << ROB.head << " is not executed yet" << endl;
            });
            return;
        }

        // check store instruction
        uint32_t num_store = 0;
        for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
            if (ROB.entry[ROB.head].destination_memory[i]) num_store++;
        }

        if (num_store) {
            if ((this->l1d->write_queue()->occupancy + num_store) <=
                this->l1d->write_queue()->SIZE) {
                for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
                    if (ROB.entry[ROB.head].destination_memory[i]) {
                        bool pred;

                        PACKET data_packet;
                        uint32_t sq_index = ROB.entry[ROB.head].sq_index[i];

                        // sq_index and rob_index are no longer available after
                        // retirement but we pass this information to avoid
                        // segmentation fault
                        data_packet.fill_level = FILL_L1;
                        data_packet.fill_l1d = 1;
                        data_packet.cpu = cpu;
                        data_packet.data_index = SQ.entry[sq_index].data_index;
                        data_packet.sq_index = sq_index;
                        data_packet.address =
                            SQ.entry[sq_index].physical_address >>
                            LOG2_BLOCK_SIZE;
                        data_packet.full_addr =
                            SQ.entry[sq_index].physical_address;
                        data_packet.v_address =
                            SQ.entry[sq_index].virtual_address >>
                            LOG2_BLOCK_SIZE;
                        data_packet.full_v_addr =
                            SQ.entry[sq_index].virtual_address;
                        data_packet.memory_size =
                            SQ.entry[sq_index].memory_size;
                        data_packet.instr_id = SQ.entry[sq_index].instr_id;
                        data_packet.rob_index = SQ.entry[sq_index].rob_index;
                        data_packet.went_offchip_pred =
                            SQ.entry[sq_index].went_offchip_pred;
                        data_packet.ip = SQ.entry[sq_index].ip;
                        data_packet.type = RFO;
                        data_packet.asid[0] = SQ.entry[sq_index].asid[0];
                        data_packet.asid[1] = SQ.entry[sq_index].asid[1];
                        data_packet.event_cycle = this->_current_core_cycle;

                        // Updating the irreg predictor.
                        this->irreg_pred.update(data_packet.ip,
                                                data_packet.v_address);

                        // pred = this->irreg_pred.predict (data_packet.ip);
                        pred = false;

                        // if (this->sdc_enabled && this->is_irreg_data
                        // (data_packet.full_v_addr)) {
                        if (this->sdc_enabled && pred) {
                            data_packet.fill_level = cc::cache::fill_sdc;

                            // this->l1d->add_write_queue (data_packet);
                            this->sdc->add_write_queue(data_packet);
                            // uncore.DRAM.add_wq (&data_packet);
                        } else {
                            this->l1d->add_write_queue(data_packet);
                        }

                        // Annotating accuracy.
                        this->pred_results[pred ==
                                           this->is_irreg_data(
                                               data_packet.full_v_addr)]++;

                        // Adding the PC linked to that memory request to the PC
                        // table.
                        this->_pc_table.insert(
                            std::make_pair(data_packet.ip, 0));

                        // Estimating non-irregular data footprint.
                        // if (!(data_packet.full_v_addr >= this->irreg_base &&
                        // data_packet.full_v_addr <= this->irreg_bound) &&
                        // all_warmup_complete) {
                        //     if (std::find (this->_block_ids.begin (),
                        //     this->_block_ids.end (), data_packet.full_addr ^
                        //     0x3F) == this->_block_ids.end ()) {
                        //         this->_block_ids.push_back
                        //         (data_packet.full_addr ^ 0x3F);
                        //     }
                        // }
                    }
                }
            } else {
                DP(if (warmup_complete[cpu]) {
                    cout << "[ROB] " << __func__
                         << " instr_id: " << ROB.entry[ROB.head].instr_id
                         << " L1D WQ is full" << endl;
                });

                // TODO: remove and replace this as it relates to legacy
                // ChampSim's caches. L1D.WQ.FULL++; L1D.STALL[RFO]++;

                return;
            }
        }

        // release SQ entries
        for (uint32_t i = 0; i < helper::MAX_INSTR_DESTINATIONS; i++) {
            if (ROB.entry[ROB.head].sq_index[i] != UINT32_MAX) {
                uint32_t sq_index = ROB.entry[ROB.head].sq_index[i];

                DP(if (warmup_complete[cpu]) {
                    cout << "[SQ] " << __func__
                         << " instr_id: " << ROB.entry[ROB.head].instr_id
                         << " releases sq_index: " << sq_index;
                    cout << hex << " address: "
                         << (SQ.entry[sq_index].physical_address >>
                             LOG2_BLOCK_SIZE);
                    cout << " full_addr: "
                         << SQ.entry[sq_index].physical_address << dec << endl;
                });

                LSQ_ENTRY empty_entry;
                SQ.entry[sq_index] = empty_entry;

                SQ.occupancy--;
                SQ.head++;
                if (SQ.head == SQ.SIZE) SQ.head = 0;
            }
        }

        // release ROB entry
        DP(if (warmup_complete[cpu]) {
            cout << "[ROB] " << __func__
                 << " instr_id: " << ROB.entry[ROB.head].instr_id
                 << " is retired" << endl;
        });

        ooo_model_instr empty_entry;
        ROB.entry[ROB.head] = empty_entry;

        ROB.head++;
        if (ROB.head == ROB.SIZE) ROB.head = 0;
        ROB.occupancy--;
        completed_executions--;
        num_retired++;
    }
}

bool O3_CPU::is_irreg_data(const uint64_t &vaddr) {
    for (const auto &e : this->_irreg_boundaries) {
        if (vaddr >= e.first && vaddr <= e.second) {
            return true;
        }
    }

    // Nothing was found.
    return false;
}

const std::list<trace_header::irreg_array_boundaries> &
O3_CPU::irreg_data_boundaries() const {
    return this->_irreg_boundaries;
}

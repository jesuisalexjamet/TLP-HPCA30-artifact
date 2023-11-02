#include <boost/filesystem.hpp>
#
#include "dram_controller.h"
#
#include <internals/champsim.h>
#include <internals/ooo_cpu.h>
#include <internals/uncore.h>
#
#include <internals/simulator.hh>
#
#include <internals/components/cache.hh>

#define CHAMPSIM_RECORD_DRAM_ACCESSES
#undef CHAMPSIM_RECORD_DRAM_ACCESSES

using namespace boost::filesystem;

// initialized in main.cc
uint32_t DRAM_MTPS, DRAM_DBUS_RETURN_TIME, tRP, tRCD, tCAS;

void MEMORY_CONTROLLER::initialize() {
#if defined(CHAMPSIM_RECORD_DRAM_ACCESSES)
    path trace_name = path(champsim::simulator::instance()->traces()[0])
                          .filename()
                          .stem()
                          .stem(),
         trace_path =
             path(champsim::simulator::instance()->memory_trace_directory());

    trace_path += trace_name;
    trace_path += ".memorytrace";

    this->_memory_trace.open(trace_path.string(),
                             std::ios::out | std::ios::trunc);
#endif  // CHAMPSIM_RECORD_DRAM_ACCESSES
}

void MEMORY_CONTROLLER::reset_remain_requests(PACKET_QUEUE *queue,
                                              uint32_t channel) {
    O3_CPU *curr_cpu = nullptr;

    for (uint32_t i = 0; i < queue->SIZE; i++) {
        if (queue->entry[i].scheduled) {
            uint64_t op_addr = queue->entry[i].address;
            uint32_t op_cpu = queue->entry[i].cpu,
                     op_channel = dram_get_channel(op_addr),
                     op_rank = dram_get_rank(op_addr),
                     op_bank = dram_get_bank(op_addr),
                     op_row = dram_get_row(op_addr);

#ifdef DEBUG_PRINT
            // uint32_t op_column = dram_get_column(op_addr);
#endif

            curr_cpu = champsim::simulator::instance()->modeled_cpu(op_cpu);

            // update open row
            if ((bank_request[op_channel][op_rank][op_bank].cycle_available -
                 tCAS) <= curr_cpu->current_core_cycle())
                bank_request[op_channel][op_rank][op_bank].open_row = op_row;
            else
                bank_request[op_channel][op_rank][op_bank].open_row =
                    UINT32_MAX;

            // this bank is ready for another DRAM request
            bank_request[op_channel][op_rank][op_bank].request_index = -1;
            bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
            bank_request[op_channel][op_rank][op_bank].working = 0;
            bank_request[op_channel][op_rank][op_bank].cycle_available =
                curr_cpu->current_core_cycle();
            if (bank_request[op_channel][op_rank][op_bank].is_write) {
                scheduled_writes[channel]--;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
            } else if (bank_request[op_channel][op_rank][op_bank].is_read) {
                scheduled_reads[channel]--;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;
            }

            queue->entry[i].scheduled = 0;
            queue->entry[i].event_cycle = curr_cpu->current_core_cycle();

            DP(if (warmup_complete[op_cpu]) {
                cout << queue->NAME << " instr_id: " << queue->entry[i].instr_id
                     << " swrites: " << scheduled_writes[channel]
                     << " sreads: " << scheduled_reads[channel] << endl;
            });
        }
    }

    update_schedule_cycle(&RQ[channel]);
    update_schedule_cycle(&WQ[channel]);
    update_process_cycle(&RQ[channel]);
    update_process_cycle(&WQ[channel]);

#ifdef SANITY_CHECK
    if (queue->is_WQ) {
        if (scheduled_writes[channel] != 0) assert(0);
    } else {
        if (scheduled_reads[channel] != 0) assert(0);
    }
#endif
}

void MEMORY_CONTROLLER::operate() {
    for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
        // if ((write_mode[i] == 0) && (WQ[i].occupancy >= DRAM_WRITE_HIGH_WM))
        // {
        if ((write_mode[i] == 0) &&
            ((WQ[i].occupancy >= DRAM_WRITE_HIGH_WM) ||
             ((RQ[i].occupancy == 0) &&
              (WQ[i].occupancy > 0)))) {  // use idle cycles to perform writes
            write_mode[i] = 1;

            // reset scheduled RQ requests
            reset_remain_requests(&RQ[i], i);
            // add data bus turn-around time
            dbus_cycle_available[i] += DRAM_DBUS_TURN_AROUND_TIME;
        } else if (write_mode[i]) {
            if (WQ[i].occupancy == 0)
                write_mode[i] = 0;
            else if (RQ[i].occupancy && (WQ[i].occupancy < DRAM_WRITE_LOW_WM))
                write_mode[i] = 0;

            if (write_mode[i] == 0) {
                // reset scheduled WQ requests
                reset_remain_requests(&WQ[i], i);
                // add data bus turnaround time
                dbus_cycle_available[i] += DRAM_DBUS_TURN_AROUND_TIME;
            }
        }

        // handle write
        // schedule new entry
        if (write_mode[i] && (WQ[i].next_schedule_index < WQ[i].SIZE)) {
            if (O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(
                    WQ[i].entry[WQ[i].next_schedule_index].cpu);
                WQ[i].next_schedule_cycle <= curr_cpu->current_core_cycle())
                schedule(&WQ[i]);
        }

        // process DRAM requests
        if (write_mode[i] && (WQ[i].next_process_index < WQ[i].SIZE)) {
            if (O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(
                    WQ[i].entry[WQ[i].next_process_index].cpu);
                WQ[i].next_process_cycle <= curr_cpu->current_core_cycle())
                process(&WQ[i]);
        }

        // handle read
        // schedule new entry
        if ((write_mode[i] == 0) && (RQ[i].next_schedule_index < RQ[i].SIZE)) {
            if (O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(
                    RQ[i].entry[RQ[i].next_schedule_index].cpu);
                RQ[i].next_schedule_cycle <= curr_cpu->current_core_cycle())
                schedule(&RQ[i]);
        }

        // process DRAM requests
        if ((write_mode[i] == 0) && (RQ[i].next_process_index < RQ[i].SIZE)) {
            if (O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(
                    RQ[i].entry[RQ[i].next_process_index].cpu);
                RQ[i].next_process_cycle <= curr_cpu->current_core_cycle())
                process(&RQ[i]);
        }
    }
}

void MEMORY_CONTROLLER::schedule(PACKET_QUEUE *queue) {
    uint64_t read_addr;
    uint32_t read_channel, read_rank, read_bank, read_row;
    uint8_t row_buffer_hit = 0;

    int oldest_index = -1;
    uint64_t oldest_cycle = UINT64_MAX;

    // first, search for the oldest open row hit
    for (uint32_t i = 0; i < queue->SIZE; i++) {
        // already scheduled
        if (queue->entry[i].scheduled) continue;

        // empty entry
        read_addr = queue->entry[i].address;
        if (read_addr == 0) continue;

        read_channel = dram_get_channel(read_addr);
        read_rank = dram_get_rank(read_addr);
        read_bank = dram_get_bank(read_addr);

        // bank is busy
        if (bank_request[read_channel][read_rank][read_bank]
                .working) {  // should we check this or not? how do we know if
                             // bank is busy or not for all requests in the
                             // queue?

            // DP ( if (warmup_complete[0]) {
            // cout << queue->NAME << " " << __func__ << " instr_id: " <<
            // queue->entry[i].instr_id << " bank is busy"; cout << " swrites: "
            // << scheduled_writes[channel] << " sreads: " <<
            // scheduled_reads[channel]; cout << " write: " <<
            // +bank_request[read_channel][read_rank][read_bank].is_write << "
            // read: " <<
            // +bank_request[read_channel][read_rank][read_bank].is_read << hex;
            // cout << " address: " << queue->entry[i].address << dec << "
            // channel: " << read_channel << " rank: " << read_rank << " bank: "
            // << read_bank << endl; });

            continue;
        }

        read_row = dram_get_row(read_addr);
        // read_column = dram_get_column(read_addr);

        // check open row
        if (bank_request[read_channel][read_rank][read_bank].open_row !=
            read_row) {
            /*
            DP ( if (warmup_complete[0]) {
            cout << queue->NAME << " " << __func__ << " instr_id: " <<
            queue->entry[i].instr_id << " row is inactive"; cout << " swrites: "
            << scheduled_writes[channel] << " sreads: " <<
            scheduled_reads[channel]; cout << " write: " <<
            +bank_request[read_channel][read_rank][read_bank].is_write << "
            read: " << +bank_request[read_channel][read_rank][read_bank].is_read
            << hex; cout << " address: " << queue->entry[i].address << dec << "
            channel: " << read_channel << " rank: " << read_rank << " bank: " <<
            read_bank << endl; });
            */

            continue;
        }

        // select the oldest entry
        if (queue->entry[i].event_cycle < oldest_cycle) {
            oldest_cycle = queue->entry[i].event_cycle;
            oldest_index = i;
            row_buffer_hit = 1;
        }
    }

    if (oldest_index == -1) {  // no matching open_row (row buffer miss)

        oldest_cycle = UINT64_MAX;
        for (uint32_t i = 0; i < queue->SIZE; i++) {
            // already scheduled
            if (queue->entry[i].scheduled) continue;

            // empty entry
            read_addr = queue->entry[i].address;
            if (read_addr == 0) continue;

            // bank is busy
            read_channel = dram_get_channel(read_addr);
            read_rank = dram_get_rank(read_addr);
            read_bank = dram_get_bank(read_addr);
            if (bank_request[read_channel][read_rank][read_bank].working)
                continue;

            // read_row = dram_get_row(read_addr);
            // read_column = dram_get_column(read_addr);

            // select the oldest entry
            if (queue->entry[i].event_cycle < oldest_cycle) {
                oldest_cycle = queue->entry[i].event_cycle;
                oldest_index = i;
            }
        }
    }

    // at this point, the scheduler knows which bank to access and if the
    // request is a row buffer hit or miss
    if (oldest_index !=
        -1) {  // scheduler might not find anything if all requests are already
               // scheduled or all banks are busy

        uint64_t LATENCY = 0;
        if (row_buffer_hit)
            LATENCY = tCAS;
        else
            LATENCY = tRP + tRCD + tCAS;

        uint64_t op_addr = queue->entry[oldest_index].address;
        uint32_t op_cpu = queue->entry[oldest_index].cpu,
                 op_channel = dram_get_channel(op_addr),
                 op_rank = dram_get_rank(op_addr),
                 op_bank = dram_get_bank(op_addr),
                 op_row = dram_get_row(op_addr);
#ifdef DEBUG_PRINT
        uint32_t op_column = dram_get_column(op_addr);
#endif

        O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(op_cpu);

        // this bank is now busy
        bank_request[op_channel][op_rank][op_bank].working = 1;
        bank_request[op_channel][op_rank][op_bank].working_type =
            queue->entry[oldest_index].type;
        bank_request[op_channel][op_rank][op_bank].cycle_available =
            curr_cpu->current_core_cycle() + LATENCY;

        bank_request[op_channel][op_rank][op_bank].request_index = oldest_index;
        bank_request[op_channel][op_rank][op_bank].row_buffer_hit =
            row_buffer_hit;
        if (queue->is_WQ) {
            bank_request[op_channel][op_rank][op_bank].is_write = 1;
            bank_request[op_channel][op_rank][op_bank].is_read = 0;
            scheduled_writes[op_channel]++;
        } else {
            bank_request[op_channel][op_rank][op_bank].is_write = 0;
            bank_request[op_channel][op_rank][op_bank].is_read = 1;
            scheduled_reads[op_channel]++;
        }

        // update open row
        bank_request[op_channel][op_rank][op_bank].open_row = op_row;

        queue->entry[oldest_index].scheduled = 1;
        queue->entry[oldest_index].event_cycle =
            curr_cpu->current_core_cycle() + LATENCY;

        update_schedule_cycle(queue);
        update_process_cycle(queue);

        DP(if (warmup_complete[op_cpu]) {
            cout << "[" << queue->NAME << "] " << __func__
                 << " instr_id: " << queue->entry[oldest_index].instr_id;
            cout << " row buffer: "
                 << (row_buffer_hit
                         ? (int)bank_request[op_channel][op_rank][op_bank]
                               .open_row
                         : -1)
                 << hex;
            cout << " address: " << queue->entry[oldest_index].address
                 << " full_addr: " << queue->entry[oldest_index].full_addr
                 << dec;
            cout << " index: " << oldest_index
                 << " occupancy: " << queue->occupancy;
            cout << " ch: " << op_channel << " rank: " << op_rank
                 << " bank: " << op_bank;  // wrong from here
            cout << " row: " << op_row << " col: " << op_column;
            cout << " current: " << current_core_cycle[op_cpu]
                 << " event: " << queue->entry[oldest_index].event_cycle
                 << endl;
        });
    }
}

void MEMORY_CONTROLLER::process(PACKET_QUEUE *queue) {
    uint32_t dbus_return_time = 0;
    uint32_t request_index = queue->next_process_index;
    PACKET &packet = queue->entry[request_index];
    O3_CPU *curr_cpu = champsim::simulator::instance()->modeled_cpu(packet.cpu);

    // sanity check
    if (request_index == queue->SIZE) assert(0);

    // Computing the data-bus return time accordingly to the targeted cache.
    if (queue->entry[request_index].fill_level == cc::cache::fill_sdc) {
        dbus_return_time =
            (uint32_t)std::ceil(
                (float)(champsim::simulator::instance()
                            ->modeled_cpu(queue->entry[request_index].cpu)
                            ->sdc->block_size()) /
                (float)(DRAM_CHANNEL_WIDTH)) *
            (uint32_t)std::ceil(((float)CPU_FREQ / (float)DRAM_MTPS));
    } else {
        dbus_return_time = DRAM_DBUS_RETURN_TIME;
    }

    uint8_t op_type = queue->entry[request_index].type;
    uint64_t op_addr = queue->entry[request_index].address;
    uint32_t op_cpu = queue->entry[request_index].cpu,
             op_channel = dram_get_channel(op_addr),
             op_rank = dram_get_rank(op_addr), op_bank = dram_get_bank(op_addr);
#ifdef DEBUG_PRINT
    uint32_t op_row = dram_get_row(op_addr),
             op_column = dram_get_column(op_addr);
#endif

    // sanity check
    if (bank_request[op_channel][op_rank][op_bank].request_index !=
        (int)request_index) {
        assert(0);
    }

    // paid all DRAM access latency, data is ready to be processed
    if (bank_request[op_channel][op_rank][op_bank].cycle_available <=
        curr_cpu->current_core_cycle()) {
        // check if data bus is available
        if (dbus_cycle_available[op_channel] <=
            curr_cpu->current_core_cycle()) {
            if (queue->is_WQ) {
                // update data bus cycle time
                dbus_cycle_available[op_channel] =
                    curr_cpu->current_core_cycle() + dbus_return_time;

                if (bank_request[op_channel][op_rank][op_bank].row_buffer_hit)
                    queue->ROW_BUFFER_HIT++;
                else
                    queue->ROW_BUFFER_MISS++;

                // this bank is ready for another DRAM request
                bank_request[op_channel][op_rank][op_bank].request_index = -1;
                bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
                bank_request[op_channel][op_rank][op_bank].working = false;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;

                scheduled_writes[op_channel]--;
            } else {
                // update data bus cycle time
                dbus_cycle_available[op_channel] =
                    curr_cpu->current_core_cycle() + dbus_return_time;
                queue->entry[request_index].event_cycle =
                    dbus_cycle_available[op_channel];

                DP(if (warmup_complete[op_cpu]) {
                    cout << "[" << queue->NAME << "] " << __func__
                         << " return data" << hex;
                    cout << " address: " << queue->entry[request_index].address
                         << " full_addr: "
                         << queue->entry[request_index].full_addr << dec;
                    cout << " occupancy: " << queue->occupancy
                         << " channel: " << op_channel << " rank: " << op_rank
                         << " bank: " << op_bank;
                    cout << " row: " << op_row << " column: " << op_column;
                    cout << " current_cycle: " << current_core_cycle[op_cpu]
                         << " event_cycle: "
                         << queue->entry[request_index].event_cycle << endl;
                });

#if defined(ENABLE_DCLR)
                // WIP: Following the provided fill path.
                if (packet.fill_level == cc::cache::fill_metadata) {
                    curr_cpu->_mm.return_data(packet);
                } else {
                    if (packet.fill_path.empty()) {
                        throw std::runtime_error(
                            "A load request in the DRAM cannot come with an "
                            "empty "
                            "fill path.");
                    }

                    // TODO: Eventually this must be removed.
                    assert(packet.fill_path.top()->check_type(cc::is_llc));

                    packet.pop_fill_path()->return_data(packet);
                }
#else
                if (packet.fill_level == cc::cache::fill_metadata) {
                    curr_cpu->_mm.return_data(packet);
                } else if (packet.fill_level < cc::cache::fill_ddrp) {
                    if (packet.fill_path.empty()) {
                        throw std::runtime_error(
                            "A load request in the DRAM cannot come with an "
                            "empty "
                            "fill path.");
                    }

                    // TODO: Eventually this must be removed.
                    assert(packet.fill_path.top()->check_type(cc::is_llc));

                    packet.pop_fill_path()->return_data(packet);
                } else {
                    // HERMES: This has to be a DDRP prefetch.
                    if (packet.type != cc::cache::prefetch) {
                        throw std::runtime_error("");
                    }

                    /**
                     * HERMES: As opposed to the artifact associated to the
                     * Hermes paper, we do not make use of an additional buffer
                     * to store Hermes requests that should otherwise be
                     * discarded because the LLC miss counterpart did not arrive
                     * yet.
                     *
                     * Here, we simple discard the request by not returning it
                     * to the LLC.
                     */
                    // stats
                }
#endif  // !defined(ENABLE_DCRP)

                if (bank_request[op_channel][op_rank][op_bank].row_buffer_hit)
                    queue->ROW_BUFFER_HIT++;
                else
                    queue->ROW_BUFFER_MISS++;

                // this bank is ready for another DRAM request
                bank_request[op_channel][op_rank][op_bank].request_index = -1;
                bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
                bank_request[op_channel][op_rank][op_bank].working = false;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;

                scheduled_reads[op_channel]--;
            }

            // remove the oldest entry
            queue->remove_queue(&queue->entry[request_index]);
            update_process_cycle(queue);
        } else {  // data bus is busy, the available bank cycle time is
                  // fast-forwarded for faster simulation

#if 0
            // TODO: what if we can service prefetching request without dbus congestion?
            // can we have more timely prefetches and improve performance?
            if ((op_type == PREFETCH) || (op_type == LOAD)) {
                // just magically return prefetch request (no need to update data bus cycle time)
                /*
                dbus_cycle_available[op_channel] = current_core_cycle[op_cpu] + DRAM_DBUS_RETURN_TIME;
                queue->entry[request_index].event_cycle = dbus_cycle_available[op_channel];

                DP ( if (warmup_complete[op_cpu]) {
                cout << "[" << queue->NAME << "] " <<  __func__ << " return data" << hex;
                cout << " address: " << queue->entry[request_index].address << " full_addr: " << queue->entry[request_index].full_addr << dec;
                cout << " occupancy: " << queue->occupancy << " channel: " << op_channel << " rank: " << op_rank << " bank: " << op_bank;
                cout << " row: " << op_row << " column: " << op_column;
                cout << " current_cycle: " << current_core_cycle[op_cpu] << " event_cycle: " << queue->entry[request_index].event_cycle << endl; });
                */

                // send data back to the core cache hierarchy
                // upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);
                if (queue->entry[request_index].fill_level == cc::cache::fill_sdc) {
                    // _sdc->return_data(queue->entry[request_index]);
                    _upper_level_dcache_new[op_cpu]->return_data(queue->entry[request_index]);
                } else {
                    _upper_level_dcache_new[op_cpu]->return_data(queue->entry[request_index]);
                }

                if (bank_request[op_channel][op_rank][op_bank].row_buffer_hit)
                    queue->ROW_BUFFER_HIT++;
                else
                    queue->ROW_BUFFER_MISS++;

                // this bank is ready for another DRAM request
                bank_request[op_channel][op_rank][op_bank].request_index = -1;
                bank_request[op_channel][op_rank][op_bank].row_buffer_hit = 0;
                bank_request[op_channel][op_rank][op_bank].working = false;
                bank_request[op_channel][op_rank][op_bank].is_write = 0;
                bank_request[op_channel][op_rank][op_bank].is_read = 0;

                scheduled_reads[op_channel]--;

                // remove the oldest entry
                queue->remove_queue(&queue->entry[request_index]);
                update_process_cycle(queue);

                return;
            }
#endif

            dbus_cycle_congested[op_channel] +=
                (dbus_cycle_available[op_channel] -
                 curr_cpu->current_core_cycle());
            bank_request[op_channel][op_rank][op_bank].cycle_available =
                dbus_cycle_available[op_channel];
            dbus_congested[NUM_TYPES][NUM_TYPES]++;
            dbus_congested[NUM_TYPES][op_type]++;
            dbus_congested[bank_request[op_channel][op_rank][op_bank]
                               .working_type][NUM_TYPES]++;
            dbus_congested[bank_request[op_channel][op_rank][op_bank]
                               .working_type][op_type]++;

            DP(if (warmup_complete[op_cpu]) {
                cout << "[" << queue->NAME << "] " << __func__
                     << " dbus_occupied" << hex;
                cout << " address: " << queue->entry[request_index].address
                     << " full_addr: " << queue->entry[request_index].full_addr
                     << dec;
                cout << " occupancy: " << queue->occupancy
                     << " channel: " << op_channel << " rank: " << op_rank
                     << " bank: " << op_bank;
                cout << " row: " << op_row << " column: " << op_column;
                cout << " current_cycle: " << current_core_cycle[op_cpu]
                     << " event_cycle: "
                     << bank_request[op_channel][op_rank][op_bank]
                            .cycle_available
                     << endl;
            });
        }
    }
}

int MEMORY_CONTROLLER::add_rq(PACKET *packet) {
    bool return_data_to_core = true;

    if (packet->fill_level >= cc::cache::fill_ddrp) {
        return_data_to_core = false;
    }

    // Marking the packet as served from DRAM.
    packet->served_from = cc::is_dram;

    // check for the latest wirtebacks in the write queue
    uint32_t channel = dram_get_channel(packet->address);
    int wq_index = check_dram_queue(&WQ[channel], packet);
    if (wq_index != -1) {
        packet->data = WQ[channel].entry[wq_index].data;

        if (return_data_to_core && packet->fill_path.empty()) {
            throw std::runtime_error(
                "A load request in the DRAM cannot come with an empty "
                "fill path.");
        }

        // HERMES: We return data to the core only if it is not a DDRP request.
        if (return_data_to_core) packet->pop_fill_path()->return_data(*packet);

        DP(if (packet->cpu) {
            cout << "[" << NAME << "_RQ] " << __func__
                 << " instr_id: " << packet->instr_id
                 << " found recent writebacks";
            cout << hex << " read: " << packet->address
                 << " writeback: " << WQ[channel].entry[wq_index].address << dec
                 << endl;
        });

        ACCESS[1]++;
        HIT[1]++;

        WQ[channel].FORWARD++;
        RQ[channel].ACCESS++;
        // assert(0);

        return -1;
    }

    // check for duplicates in the read queue
    int index = check_dram_queue(&RQ[channel], packet);
    if (index != -1) {
        PACKET &rq_entry = this->RQ[channel].entry[index];

#if defined(ENABLE_DCLR)
        if (rq_entry.fill_level == cc::cache::fill_dclr) {
            if (packet->fill_level <= cc::cache::fill_llc) {
                /**
                 * DCLR: Here, there's already a Hermes request in the DRAM's
                 * RQ and the incoming request comes from the core's cache
                 * hierarchy.
                 */
                uint8_t tmp_scheduled = rq_entry.scheduled;
                uint64_t tmp_event_cycle = rq_entry.event_cycle,
                         tmp_cycle_enqueued = rq_entry.cycle_enqueued;

                // merge
                rq_entry = *packet;

                // update
                rq_entry.scheduled = tmp_scheduled;
                rq_entry.event_cycle = tmp_event_cycle;
                rq_entry.cycle_enqueued = tmp_cycle_enqueued;
            } else if (packet->fill_level == cc::cache::fill_dclr) {
                /**
                 * HERMES: Here, there's nothing to do as this is a DRRP
                 * request hitting another DRRP request in the DRAM's RQ.
                 */
                // stats
            } else {
                throw std::runtime_error("");
                return -2;
            }
        }
#else
        if (rq_entry.fill_level == cc::cache::fill_ddrp) {
            if (packet->fill_level <= cc::cache::fill_llc) {
                /**
                 * HERMES: Here, there's already a Hermes request in the DRAM's
                 * RQ and the incoming request comes from the core's cache
                 * hierarchy.
                 */
                bool tmp_l1d_offchip_pred_used = rq_entry.l1d_offchip_pred_used;
                uint8_t tmp_scheduled = rq_entry.scheduled;
                uint64_t tmp_event_cycle = rq_entry.event_cycle,
                         tmp_cycle_enqueued = rq_entry.cycle_enqueued;

                // merge
                rq_entry = *packet;

                // update
                rq_entry.l1d_offchip_pred_used = tmp_l1d_offchip_pred_used;
                rq_entry.scheduled = tmp_scheduled;
                rq_entry.event_cycle = tmp_event_cycle;
                rq_entry.cycle_enqueued = tmp_cycle_enqueued;

                // stats
            } else if (packet->fill_level == cc::cache::fill_ddrp) {
                /**
                 * HERMES: Here, there's nothing to do as this is a DRRP
                 * request hitting another DRRP request in the DRAM's RQ.
                 */
                // stats
            } else {
                throw std::runtime_error("");
                return -2;
            }
        } else if (rq_entry.fill_level <= cc::cache::fill_llc) {
            if (packet->fill_level <= cc::cache::fill_llc) {
                /**
                 * HERMES: Incoming LLC miss. The incoming packet and RQ's
                 * packet both cannot be LLC miss requests.
                 */
                throw std::runtime_error("");
                return -2;
            } else if (packet->fill_level == cc::cache::fill_ddrp) {
                /**
                 * HERMES: Incoming DDRP request. Here, nothing to do.
                 */
                // stats
            } else {
                throw std::runtime_error("");
                return -2;
            }
        } else {
            throw std::runtime_error("");
            return -2;
        }
#endif  // !defined(ENABLE_DCLR)

        return index;  // merged index
    }

    // search for the empty index
    for (index = 0; index < DRAM_RQ_SIZE; index++) {
        if (RQ[channel].entry[index].address == 0) {
            RQ[channel].entry[index] = *packet;
            RQ[channel].occupancy++;

#ifdef DEBUG_PRINT
            uint32_t channel = dram_get_channel(packet->address),
                     rank = dram_get_rank(packet->address),
                     bank = dram_get_bank(packet->address),
                     row = dram_get_row(packet->address),
                     column = dram_get_column(packet->address);
#endif

            DP(if (warmup_complete[packet->cpu]) {
                cout << "[" << NAME << "_RQ] " << __func__
                     << " instr_id: " << packet->instr_id << " address: " << hex
                     << packet->address;
                cout << " full_addr: " << packet->full_addr << dec
                     << " ch: " << channel;
                cout << " rank: " << rank << " bank: " << bank
                     << " row: " << row << " col: " << column;
                cout << " occupancy: " << RQ[channel].occupancy
                     << " current: " << current_core_cycle[packet->cpu]
                     << " event: " << packet->event_cycle << endl;
            });

            break;
        }
    }

    update_schedule_cycle(&RQ[channel]);

    // We monitor the number of prefetch request from the L2C reaching the DRAM.
    if (packet->type == cc::cache::prefetch &&
        packet->pf_origin_level == cc::cache::fill_l2) {
        champsim::simulator::instance()
            ->modeled_cpu(packet->cpu)
            ->_mm.metrics()
            .l2c_pref_reached_dram++;
    }

#if defined(CHAMPSIM_RECORD_DRAM_ACCESSES)
    // Filling the memory trace with the read packet.
    if (champsim::simulator::instance()->all_warmup_complete()) {
        this->_memory_trace << std::hex << std::showbase << packet->full_addr
                            << " R" << std::noshowbase << std::dec << std::endl;
    }
#endif  // CHAMPSIM_RECORD_DRAM_ACCESSES

    return -1;
}

int MEMORY_CONTROLLER::add_wq(PACKET *packet) {
    bool is_slowtrack_old = false, is_slowtrack_new = false, same_cpu = false,
         should_merge = false;

    // Marking the packet as served from DRAM.
    packet->served_from = cc::is_dram;

    // simply drop write requests before the warmup
    // if (all_warmup_complete < NUM_CPUS)
    //     return -1;

    // check for duplicates in the write queue
    uint32_t channel = dram_get_channel(packet->address);
    int index = check_dram_queue(&WQ[channel], packet);
    if (index != -1) {
        return index;
    }

    // search for the empty index
    for (index = 0; index < DRAM_WQ_SIZE; index++) {
        if (WQ[channel].entry[index].address == 0) {
            WQ[channel].entry[index] = *packet;
            WQ[channel].occupancy++;

#ifdef DEBUG_PRINT
            uint32_t channel = dram_get_channel(packet->address),
                     rank = dram_get_rank(packet->address),
                     bank = dram_get_bank(packet->address),
                     row = dram_get_row(packet->address),
                     column = dram_get_column(packet->address);
#endif

            DP(if (warmup_complete[packet->cpu]) {
                cout << "[" << NAME << "_WQ] " << __func__
                     << " instr_id: " << packet->instr_id << " address: " << hex
                     << packet->address;
                cout << " full_addr: " << packet->full_addr << dec
                     << " ch: " << channel;
                cout << " rank: " << rank << " bank: " << bank
                     << " row: " << row << " col: " << column;
                cout << " occupancy: " << WQ[channel].occupancy
                     << " current: " << current_core_cycle[packet->cpu]
                     << " event: " << packet->event_cycle << endl;
            });

            break;
        }
    }

    update_schedule_cycle(&WQ[channel]);

#if defined(CHAMPSIM_RECORD_DRAM_ACCESSES)
    // Filling the memory trace with the write packet.
    if (champsim::simulator::instance()->all_warmup_complete()) {
        this->_memory_trace << std::hex << std::showbase << packet->full_addr
                            << " W" << std::noshowbase << std::dec << std::endl;
    }
#endif  // CHAMPSIM_RECORD_DRAM_ACCESSES

    return -1;
}

int MEMORY_CONTROLLER::add_pq(PACKET *packet) { return -1; }

void MEMORY_CONTROLLER::update_fill_path(PACKET &packet) {
    uint32_t channel = dram_get_channel(packet.address);
    PACKET_QUEUE *queue = nullptr;

    // Checking write queues.
    queue = &this->WQ[channel];

    // If there is such an entry in the write queues we update its fill path.
    if (std::size_t index = this->check_dram_queue(queue, &packet);
        index != -1) {
        // queue->entry[index].fill_path = packet.fill_path;
        queue->entry[index].merge_fill_path(packet);
    }

    // CHecking the read queues.
    queue = &this->RQ[channel];

    if (std::size_t index = this->check_dram_queue(queue, &packet);
        index != -1) {
        // queue->entry[index].fill_path = packet.fill_path;
        queue->entry[index].merge_fill_path(packet);
    }
}

void MEMORY_CONTROLLER::return_data(PACKET *packet) {}

void MEMORY_CONTROLLER::update_schedule_cycle(PACKET_QUEUE *queue) {
    // update next_schedule_cycle
    uint64_t min_cycle = UINT64_MAX;
    uint32_t min_index = queue->SIZE;
    for (uint32_t i = 0; i < queue->SIZE; i++) {
        /*
        DP (if (warmup_complete[queue->entry[min_index].cpu]) {
        cout << "[" << queue->NAME << "] " <<  __func__ << " instr_id: " <<
        queue->entry[i].instr_id; cout << " index: " << i << " address: " << hex
        << queue->entry[i].address << dec << " scheduled: " <<
        +queue->entry[i].scheduled; cout << " event: " <<
        queue->entry[i].event_cycle << " min_cycle: " << min_cycle << endl;
        });
        */

        if (queue->entry[i].address && (queue->entry[i].scheduled == 0) &&
            (queue->entry[i].event_cycle < min_cycle)) {
            min_cycle = queue->entry[i].event_cycle;
            min_index = i;
        }
    }

    queue->next_schedule_cycle = min_cycle;
    queue->next_schedule_index = min_index;
    if (min_index < queue->SIZE) {
        DP(if (warmup_complete[queue->entry[min_index].cpu]) {
            cout << "[" << queue->NAME << "] " << __func__
                 << " instr_id: " << queue->entry[min_index].instr_id;
            cout << " address: " << hex << queue->entry[min_index].address
                 << " full_addr: " << queue->entry[min_index].full_addr;
            cout << " data: " << queue->entry[min_index].data << dec;
            cout << " event: " << queue->entry[min_index].event_cycle
                 << " current: "
                 << current_core_cycle[queue->entry[min_index].cpu]
                 << " next: " << queue->next_schedule_cycle << endl;
        });
    }
}

void MEMORY_CONTROLLER::update_process_cycle(PACKET_QUEUE *queue) {
    // update next_process_cycle
    uint64_t min_cycle = UINT64_MAX;
    uint32_t min_index = queue->SIZE;
    for (uint32_t i = 0; i < queue->SIZE; i++) {
        if (queue->entry[i].scheduled &&
            (queue->entry[i].event_cycle < min_cycle)) {
            min_cycle = queue->entry[i].event_cycle;
            min_index = i;
        }
    }

    queue->next_process_cycle = min_cycle;
    queue->next_process_index = min_index;
    if (min_index < queue->SIZE) {
        DP(if (warmup_complete[queue->entry[min_index].cpu]) {
            cout << "[" << queue->NAME << "] " << __func__
                 << " instr_id: " << queue->entry[min_index].instr_id;
            cout << " address: " << hex << queue->entry[min_index].address
                 << " full_addr: " << queue->entry[min_index].full_addr;
            cout << " data: " << queue->entry[min_index].data << dec
                 << " num_returned: " << queue->num_returned;
            cout << " event: " << queue->entry[min_index].event_cycle
                 << " current: "
                 << current_core_cycle[queue->entry[min_index].cpu]
                 << " next: " << queue->next_process_cycle << endl;
        });
    }
}

int MEMORY_CONTROLLER::check_dram_queue(PACKET_QUEUE *queue, PACKET *packet) {
    // search write queue
    for (uint32_t index = 0; index < queue->SIZE; index++) {
        if (queue->entry[index].address == packet->address &&
            queue->entry[index].cpu == packet->cpu) {
            DP(if (warmup_complete[packet->cpu]) {
                cout << "[" << queue->NAME << "] " << __func__
                     << " same entry instr_id: " << packet->instr_id
                     << " prior_id: " << queue->entry[index].instr_id;
                cout << " address: " << hex << packet->address
                     << " full_addr: " << packet->full_addr << dec << endl;
            });

            return index;
        }
    }

    DP(if (warmup_complete[packet->cpu]) {
        cout << "[" << queue->NAME << "] " << __func__
             << " new address: " << hex << packet->address;
        cout << " full_addr: " << packet->full_addr << dec << endl;
    });

    DP(if (warmup_complete[packet->cpu] && (queue->occupancy == queue->SIZE)) {
        cout << "[" << queue->NAME << "] " << __func__ << " mshr is full";
        cout << " instr_id: " << packet->instr_id
             << " mshr occupancy: " << queue->occupancy;
        cout << " address: " << hex << packet->address;
        cout << " full_addr: " << packet->full_addr << dec;
        cout << " cycle: " << current_core_cycle[packet->cpu] << endl;
    });

    return -1;
}

uint32_t MEMORY_CONTROLLER::dram_get_channel(uint64_t address) {
    if (LOG2_DRAM_CHANNELS == 0) return 0;

    int shift = 0;

    return (uint32_t)(address >> shift) & (DRAM_CHANNELS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_bank(uint64_t address) {
    if (LOG2_DRAM_BANKS == 0) return 0;

    int shift = LOG2_DRAM_CHANNELS;

    return (uint32_t)(address >> shift) & (DRAM_BANKS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_column(uint64_t address) {
    if (LOG2_DRAM_COLUMNS == 0) return 0;

    int shift = LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS;

    return (uint32_t)(address >> shift) & (DRAM_COLUMNS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_rank(uint64_t address) {
    if (LOG2_DRAM_RANKS == 0) return 0;

    int shift = LOG2_DRAM_COLUMNS + LOG2_DRAM_BANKS + LOG2_DRAM_CHANNELS;

    return (uint32_t)(address >> shift) & (DRAM_RANKS - 1);
}

uint32_t MEMORY_CONTROLLER::dram_get_row(uint64_t address) {
    if (LOG2_DRAM_ROWS == 0) return 0;

    int shift = LOG2_DRAM_RANKS + LOG2_DRAM_COLUMNS + LOG2_DRAM_BANKS +
                LOG2_DRAM_CHANNELS;

    return (uint32_t)(address >> shift) & (DRAM_ROWS - 1);
}

uint32_t MEMORY_CONTROLLER::get_occupancy(uint8_t queue_type,
                                          uint64_t address) {
    uint32_t channel = dram_get_channel(address);
    if (queue_type == 1)
        return RQ[channel].occupancy;
    else if (queue_type == 2)
        return WQ[channel].occupancy;

    return 0;
}

uint32_t MEMORY_CONTROLLER::get_size(uint8_t queue_type, uint64_t address) {
    uint32_t channel = dram_get_channel(address);
    if (queue_type == 1)
        return RQ[channel].SIZE;
    else if (queue_type == 2)
        return WQ[channel].SIZE;

    return 0;
}

void MEMORY_CONTROLLER::increment_WQ_FULL(uint64_t address) {
    uint32_t channel = dram_get_channel(address);
    WQ[channel].FULL++;
}

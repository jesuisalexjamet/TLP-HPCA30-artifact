#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <cstdint>
#
#include <fstream>
#
#include <list>
#include <utility>

// instruction format
#define ROB_SIZE 224
#define LQ_SIZE 72
#define SQ_SIZE 56
#define NUM_INSTR_DESTINATIONS_SPARC 4
#define NUM_INSTR_DESTINATIONS 2
#define NUM_INSTR_SOURCES 4

// special registers that help us identify branches
#define REG_STACK_POINTER 6
#define REG_FLAGS 25
#define REG_INSTRUCTION_POINTER 26

// branch types
#define NOT_BRANCH           0
#define BRANCH_DIRECT_JUMP   1
#define BRANCH_INDIRECT      2
#define BRANCH_CONDITIONAL   3
#define BRANCH_DIRECT_CALL   4
#define BRANCH_INDIRECT_CALL 5
#define BRANCH_RETURN        6
#define BRANCH_OTHER         7

#include "set.h"

using namespace std;

class input_instr {
  public:

    // instruction pointer or PC (Program Counter)
    uint64_t ip;

    // branch info
    uint8_t is_branch;
    uint8_t branch_taken;

    uint8_t destination_registers[NUM_INSTR_DESTINATIONS]; // output registers
    uint8_t source_registers[NUM_INSTR_SOURCES]; // input registers

    uint64_t destination_memory[NUM_INSTR_DESTINATIONS]; // output memory
    uint64_t source_memory[NUM_INSTR_SOURCES]; // input memory

#if !defined(LEGACY_TRACE)
    uint32_t destination_memory_size[NUM_INSTR_DESTINATIONS_SPARC]; // output memory
    uint32_t source_memory_size[NUM_INSTR_SOURCES]; // input memory

    uint32_t instruction_size;
#endif // !defined(LEGACY_TRACE)

    input_instr() {
        ip = 0;
        is_branch = 0;
        branch_taken = 0;

        for (uint32_t i=0; i<NUM_INSTR_SOURCES; i++) {
            source_registers[i] = 0;
            source_memory[i] = 0;
            // source_memory_size[i] = 0;
        }

        for (uint32_t i=0; i<NUM_INSTR_DESTINATIONS; i++) {
            destination_registers[i] = 0;
            destination_memory[i] = 0;
            // destination_memory_size[i] = 0;
        }
    };
};

class cloudsuite_instr {
  public:

    // instruction pointer or PC (Program Counter)
    uint64_t ip;

    // branch info
    uint8_t is_branch;
    uint8_t branch_taken;

    uint8_t destination_registers[NUM_INSTR_DESTINATIONS_SPARC]; // output registers
    uint8_t source_registers[NUM_INSTR_SOURCES]; // input registers

    uint64_t destination_memory[NUM_INSTR_DESTINATIONS_SPARC]; // output memory
    uint64_t source_memory[NUM_INSTR_SOURCES]; // input memory

    uint8_t asid[2];

    cloudsuite_instr() {
        ip = 0;
        is_branch = 0;
        branch_taken = 0;

        for (uint32_t i=0; i<NUM_INSTR_SOURCES; i++) {
            source_registers[i] = 0;
            source_memory[i] = 0;
        }

        for (uint32_t i=0; i<NUM_INSTR_DESTINATIONS_SPARC; i++) {
            destination_registers[i] = 0;
            destination_memory[i] = 0;
        }

        asid[0] = UINT8_MAX;
        asid[1] = UINT8_MAX;
    };
};

class ooo_model_instr {
  public:
    uint64_t instr_id,
             ip,
             fetch_producer,
             producer_id,
             translated_cycle,
             fetched_cycle,
             execute_begin_cycle,
             retired_cycle,
             event_cycle;

    uint8_t is_branch,
            is_memory,
            branch_taken,
            branch_mispredicted,
            branch_prediction_made,
            translated,
            data_translated,
            source_added[NUM_INSTR_SOURCES],
            destination_added[NUM_INSTR_DESTINATIONS_SPARC],
            is_producer,
            is_consumer,
            reg_RAW_producer,
            reg_ready,
            mem_ready,
            asid[2],
            reg_RAW_checked[NUM_INSTR_SOURCES];

    uint8_t branch_type;
    uint64_t branch_target;

    uint32_t fetched, scheduled;
    int num_reg_ops, num_mem_ops, num_reg_dependent;

    // executed bit is set after all dependencies are eliminated and this instr is chosen on a cycle, according to EXEC_WIDTH
    int executed;

    uint8_t destination_registers[NUM_INSTR_DESTINATIONS_SPARC]; // output registers

    uint8_t source_registers[NUM_INSTR_SOURCES]; // input registers

    // these are instruction ids of other instructions in the window
    //int64_t registers_instrs_i_depend_on[NUM_INSTR_SOURCES];
    // these are indices of instructions in the window that depend on me
    //uint8_t registers_instrs_depend_on_me[ROB_SIZE], registers_index_depend_on_me[ROB_SIZE][NUM_INSTR_SOURCES];
    fastset
	registers_instrs_depend_on_me, registers_index_depend_on_me[NUM_INSTR_SOURCES];


    // memory addresses that may cause dependencies between instructions
    uint64_t instruction_pa, data_pa, virtual_address, physical_address;
    uint32_t instruction_size;  // Size of the instruction in bytes.
    uint64_t destination_memory[NUM_INSTR_DESTINATIONS_SPARC]; // output memory
    uint64_t source_memory[NUM_INSTR_SOURCES]; // input memory
    uint32_t destination_memory_size[NUM_INSTR_DESTINATIONS_SPARC]; // output memory
    uint32_t source_memory_size[NUM_INSTR_SOURCES]; // input memory
    //int source_memory_outstanding[NUM_INSTR_SOURCES];  // a value of 2 here means the load hasn't been issued yet, 1 means it has been issued, but not returned yet, and 0 means it has returned

    // keep around a record of what the original virtual addresses were
    uint64_t destination_virtual_address[NUM_INSTR_DESTINATIONS_SPARC];
    uint64_t source_virtual_address[NUM_INSTR_SOURCES];

    // these are instruction ids of other instructions in the window
    //uint32_t memory_instrs_i_depend_on[NUM_INSTR_SOURCES];

    // these are indices of instructions in the ROB that depend on me
    //uint8_t memory_instrs_depend_on_me[ROB_SIZE];
    fastset memory_instrs_depend_on_me;

    uint32_t lq_index[NUM_INSTR_SOURCES],
             sq_index[NUM_INSTR_DESTINATIONS_SPARC],
             forwarding_index[NUM_INSTR_DESTINATIONS_SPARC];

    ooo_model_instr() {
        instr_id = 0;
        ip = 0;
        fetch_producer = 0;
        producer_id = 0;
        translated_cycle = 0;
        fetched_cycle = 0;
        execute_begin_cycle = 0;
        retired_cycle = 0;
        event_cycle = 0;

        is_branch = 0;
        is_memory = 0;
        branch_taken = 0;
        branch_mispredicted = 0;
	branch_prediction_made = 0;
        translated = 0;
        data_translated = 0;
        is_producer = 0;
        is_consumer = 0;
        reg_RAW_producer = 0;
        fetched = 0;
        scheduled = 0;
        executed = 0;
        reg_ready = 0;
        mem_ready = 0;
        asid[0] = UINT8_MAX;
        asid[1] = UINT8_MAX;

	branch_type = NOT_BRANCH;
	branch_target = 0;

        instruction_pa = 0;
        data_pa = 0;
        virtual_address = 0;
        physical_address = 0;

        // Arbitrarily providing an instruction size. Just for testing purpose.
        instruction_size = 0;

        num_reg_ops = 0;
        num_mem_ops = 0;
        num_reg_dependent = 0;

        for (uint32_t i=0; i<NUM_INSTR_SOURCES; i++) {
            source_registers[i] = 0;
            source_memory[i] = 0;
            source_virtual_address[i] = 0;
            source_memory_size[i] = 0;
            source_added[i] = 0;
            lq_index[i] = UINT32_MAX;
            reg_RAW_checked[i] = 0;
        }

        for (uint32_t i=0; i<NUM_INSTR_DESTINATIONS_SPARC; i++) {
            destination_memory[i] = 0;
            destination_registers[i] = 0;
            destination_virtual_address[i] = 0;
            destination_memory_size[i] = 0;
            destination_added[i] = 0;
            sq_index[i] = UINT32_MAX;
            forwarding_index[i] = 0;
        }

#if 0
        for (uint32_t i=0; i<ROB_SIZE; i++) {
            registers_instrs_depend_on_me[i] = 0;
            memory_instrs_depend_on_me[i] = 0;

            for (uint32_t j=0; j<NUM_INSTR_SOURCES; j++)
                registers_index_depend_on_me[i][j] = 0;
        }
#endif
    };

  void print_instr()
  {
    cout << "*** " << instr_id << " ***" << endl;
    cout << hex << "0x" << (uint64_t)ip << dec << endl;
    cout << (uint32_t)is_branch << " " << (uint32_t)branch_taken << endl;
    for(uint32_t i=0; i<NUM_INSTR_SOURCES; i++)
      {
	cout << (uint32_t)source_registers[i] << " ";
      }
    cout << endl;
    for(uint32_t i=0; i<NUM_INSTR_SOURCES; i++)
      {
	cout << hex << "0x" << (uint32_t)source_memory[i] << dec << " ";
      }
    cout << endl;
    for(uint32_t i=0; i<NUM_INSTR_DESTINATIONS; i++)
      {
	cout << (uint32_t)destination_registers[i] << " ";
      }
    cout << endl;
    for(uint32_t i=0; i<NUM_INSTR_DESTINATIONS; i++)
      {
        cout << hex << "0x" << (uint32_t)destination_memory[i] << dec << " ";
      }
    cout << endl;

    cout << endl;
  }
};

namespace champsim {
    namespace cpu {
        enum class trace_instruction_type {
            x86 = 0x0,
            x86_legacy = 0x1,
            sparc = 0x2,
        };

        struct trace_header {
			using irreg_array_boundaries = std::pair<uint64_t, uint64_t>;

			std::list<irreg_array_boundaries> irreg_arrays;
		};

        std::fstream& operator>> (std::fstream& is, trace_header& th);
        std::fstream& operator<< (std::fstream& os, const trace_header& th);

        template <trace_instruction_type InstrT> struct trace_instruction;

        template <>
        struct trace_instruction<trace_instruction_type::x86> {
        public:
            uint64_t ip;

            uint64_t destination_registers[NUM_INSTR_DESTINATIONS],
                     destination_memory[NUM_INSTR_DESTINATIONS];
            uint64_t source_registers[NUM_INSTR_SOURCES],
                     source_memory[NUM_INSTR_SOURCES];
            uint32_t destination_memory_size[NUM_INSTR_DESTINATIONS];
            uint32_t source_memory_size[NUM_INSTR_SOURCES];

            uint32_t instruction_size;
            bool is_branch, branch_taken;

        public:
            trace_instruction () = default;
            trace_instruction (const trace_instruction& o) :
                ip (o.ip),
                instruction_size (o.instruction_size), is_branch (o.is_branch), branch_taken (o.branch_taken) {
                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = o.destination_registers[i];
                    this->destination_memory[i] = o.destination_memory[i];
                    this->destination_memory_size[i] = o.destination_memory_size[i];
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = o.source_registers[i];
                    this->source_memory[i] = o.source_memory[i];
                    this->source_memory_size[i] = o.source_memory_size[i];
                }
            }

            trace_instruction (trace_instruction&& o) :
                ip (std::move (o.ip)),
                instruction_size (std::move (o.instruction_size)),
                is_branch (std::move (o.is_branch)),
                branch_taken (std::move (o.branch_taken)) {
                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = std::move (o.destination_registers[i]);
                    this->destination_memory[i] = std::move (o.destination_memory[i]);
                    this->destination_memory_size[i] = std::move (o.destination_memory_size[i]);
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = std::move (o.source_registers[i]);
                    this->source_memory[i] = std::move (o.source_memory[i]);
                    this->source_memory_size[i] = std::move (o.source_memory_size[i]);
                }
            }

            trace_instruction& operator= (const trace_instruction& o) {
                this->ip = o.ip;

                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = o.destination_registers[i];
                    this->destination_memory[i] = o.destination_memory[i];
                    this->destination_memory_size[i] = o.destination_memory_size[i];
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = o.source_registers[i];
                    this->source_memory[i] = o.source_memory[i];
                    this->source_memory_size[i] = o.source_memory_size[i];
                }

                this->ip = o.ip;
                this->instruction_size = o.instruction_size;

                this->is_branch = o.is_branch;
                this->branch_taken = o.branch_taken;

                return *this;
            }

            trace_instruction& operator= (trace_instruction&& o) {
                this->ip = std::move (o.ip);

                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = std::move (o.destination_registers[i]);
                    this->destination_memory[i] = std::move (o.destination_memory[i]);
                    this->destination_memory_size[i] = std::move (o.destination_memory_size[i]);
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = std::move (o.source_registers[i]);
                    this->source_memory[i] = std::move (o.source_memory[i]);
                    this->source_memory_size[i] = std::move (o.source_memory_size[i]);
                }

                this->ip = std::move (o.ip);
                this->instruction_size = std::move (o.instruction_size);

                this->is_branch = std::move (o.is_branch);
                this->branch_taken = std::move (o.branch_taken);

                return *this;
            }

            ooo_model_instr convert () const {
              ooo_model_instr instr;

              std::copy (std::begin (this->destination_registers), std::end (this->destination_registers), std::begin(instr.destination_registers));
              std::copy (std::begin (this->destination_memory), std::end (this->destination_memory), std::begin(instr.destination_memory));
              std::copy (std::begin (this->destination_memory_size), std::end (this->destination_memory_size), std::begin(instr.destination_memory_size));
              std::copy (std::begin (this->source_registers), std::end (this->source_registers), std::begin(instr.source_registers));
              std::copy (std::begin (this->source_memory), std::end (this->source_memory), std::begin(instr.source_memory));
              std::copy (std::begin (this->source_memory_size), std::end (this->source_memory_size), std::begin(instr.source_memory_size));

              instr.ip = this->ip;
              instr.instruction_size = this->instruction_size;

              instr.is_branch = this->is_branch;
              instr.branch_taken = this->branch_taken;

              // TODO: Needs to be fixed for future multi-core simulations.
              instr.asid[0] = 0x0;
              instr.asid[1] = 0x0;

              return instr;
            }
        };

        template <>
        struct trace_instruction<trace_instruction_type::x86_legacy> {
        public:
            uint64_t ip;

            uint64_t destination_registers[NUM_INSTR_DESTINATIONS],
                     destination_memory[NUM_INSTR_DESTINATIONS];
            uint64_t source_registers[NUM_INSTR_SOURCES],
                     source_memory[NUM_INSTR_SOURCES];

            bool is_branch, branch_taken;

        public:
            trace_instruction () = default;
            trace_instruction (const trace_instruction& o) :
                ip (o.ip), is_branch (o.is_branch), branch_taken (o.branch_taken) {
                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = o.destination_registers[i];
                    this->destination_memory[i] = o.destination_memory[i];
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = o.source_registers[i];
                    this->source_memory[i] = o.source_memory[i];
                }
            }

            trace_instruction (trace_instruction&& o) :
                ip (std::move (o.ip)),
                is_branch (std::move (o.is_branch)),
                branch_taken (std::move (o.branch_taken)) {
                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = std::move (o.destination_registers[i]);
                    this->destination_memory[i] = std::move (o.destination_memory[i]);
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = std::move (o.source_registers[i]);
                    this->source_memory[i] = std::move (o.source_memory[i]);
                }
            }

            trace_instruction& operator= (const trace_instruction& o) {
                this->ip = o.ip;

                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = o.destination_registers[i];
                    this->destination_memory[i] = o.destination_memory[i];
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = o.source_registers[i];
                    this->source_memory[i] = o.source_memory[i];
                }

                this->is_branch = o.is_branch;
                this->branch_taken = o.branch_taken;

                return *this;
            }

            trace_instruction& operator= (trace_instruction&& o) {
                this->ip = std::move (o.ip);

                for (size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
                    this->destination_registers[i] = std::move (o.destination_registers[i]);
                    this->destination_memory[i] = std::move (o.destination_memory[i]);
                }

                for (size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
                    this->source_registers[i] = std::move (o.source_registers[i]);
                    this->source_memory[i] = std::move (o.source_memory[i]);
                }

                this->is_branch = std::move (o.is_branch);
                this->branch_taken = std::move (o.branch_taken);

                return *this;
            }

            ooo_model_instr convert () const {
              ooo_model_instr instr;

              std::copy (std::begin (this->destination_registers), std::end (this->destination_registers), std::begin (instr.destination_registers));
              std::copy (std::begin (this->destination_memory), std::end (this->destination_memory), std::begin (instr.destination_memory));
              std::copy (std::begin (this->source_registers), std::end (this->source_registers), std::begin (instr.source_registers));
              std::copy (std::begin (this->source_memory), std::end (this->source_memory), std::begin (instr.source_memory));

              instr.ip = this->ip;
              instr.is_branch = this->is_branch;
              instr.branch_taken = this->branch_taken;

              // TODO: Needs to be fixed for future multi-core simulations.
              instr.asid[0] = 0x0;
              instr.asid[1] = 0x0;

              return instr;
            }
        };

        template <>
        struct trace_instruction<trace_instruction_type::sparc> {
        private:
        };

        // Defining aliases on instruction types.
        using x86_trace_instruction = trace_instruction<trace_instruction_type::x86>;
        using x86_legacy_trace_instruction = trace_instruction<trace_instruction_type::x86_legacy>;
        using sparc_trace_instruction = trace_instruction<trace_instruction_type::sparc>;

        // Defining operators using template specialization.
        std::fstream& operator<< (std::fstream& os, const x86_trace_instruction& cti);
        std::fstream& operator>> (std::fstream& is, x86_trace_instruction& ti);

        std::istream& operator>> (std::istream& is, x86_trace_instruction& ti);
    }
}

#endif

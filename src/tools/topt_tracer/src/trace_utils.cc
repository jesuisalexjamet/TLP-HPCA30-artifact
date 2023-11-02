#include <cstdint>
#
#include <iostream>
#
#include <boost/format.hpp>
#
#include <internals/instruction.h>
#
#include "trace_utils.hh"

using boost::format;

void traverse_trace (FILE* fs, std::ofstream& ofs, const irreg_arrays_t& arrays) {
	std::size_t ret = 0,
				id = 0;
	champsim::cpu::x86_trace_instruction instr;

	while ((ret = fread (&instr, sizeof (champsim::cpu::x86_trace_instruction), 1, fs)) == 1) {
		uint32_t num_mem_ops = 0;
		ooo_model_instr arch_instr = instr.convert ();

		// Checking if this is a memory instruction.
		for (std::size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
			if (arch_instr.source_memory[i]) {
				num_mem_ops++;
			}
		}

		for (std::size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
			if (arch_instr.destination_memory[i]) {
				num_mem_ops++;
			}
		}

		// We now know that it is a memory instructions. Let's get the index in its respective irreg array if any.
		if (num_mem_ops > 0) {
			for (std::size_t i = 0; i < NUM_INSTR_SOURCES; i++) {
				if (arch_instr.source_memory[i]) {
					auto info = belong_to_irreg_array (arch_instr.source_memory[i], arrays);

					// Memory reference belongs to an irreg array.
					if (info.first) {
						ofs.write (reinterpret_cast<char *> (&info.second), sizeof (uint32_t));
					}
				}
			}

			for (std::size_t i = 0; i < NUM_INSTR_DESTINATIONS; i++) {
				if (arch_instr.destination_memory[i]) {
					auto info = belong_to_irreg_array (arch_instr.destination_memory[i], arrays);

					// Memory reference belongs to an irreg array.
					if (info.first) {
						ofs.write (reinterpret_cast<char *> (&info.second), sizeof (uint32_t));
					}
				}
			}
		}

		id++;
	}
}

std::pair<bool, uint32_t> belong_to_irreg_array (const uint64_t& vaddr, const irreg_arrays_t& arrays) {
	std::size_t array_id = 0;
	uint32_t vertex_id = 0;

	for (; array_id < arrays.size (); array_id++) {
		if (vaddr >= arrays[array_id].first && vaddr <= arrays[array_id].second) {
			break;
		}
	}

	// Not found.
	if (array_id == arrays.size ()) {
		return std::make_pair (false, UINT32_MAX);
	}

	// Computing the vertex id.
	vertex_id = ((vaddr - arrays[array_id].first) / 4);

	return std::make_pair (true, vertex_id);
}

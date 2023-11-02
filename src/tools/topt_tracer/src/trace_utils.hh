#ifndef __CHAMPSIM_TOOLS_TOPT_TRACER_TRACE_UTILS_HH__
#define __CHAMPSIM_TOOLS_TOPT_TRACER_TRACE_UTILS_HH__

#include <cstdio>
#
#include <fstream>
#
#include <vector>
#include <utility>

using irreg_arrays_t = std::vector<champsim::cpu::trace_header::irreg_array_boundaries>;

void traverse_trace (FILE* fs, std::ofstream& ofs, const irreg_arrays_t& arrays);
std::pair<bool, uint32_t> belong_to_irreg_array (const uint64_t& vaddr, const irreg_arrays_t& arrays);

#endif // __CHAMPSIM_TOOLS_TOPT_TRACER_TRACE_UTILS_HH__

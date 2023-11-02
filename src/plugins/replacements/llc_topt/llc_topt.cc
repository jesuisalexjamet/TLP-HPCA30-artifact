#include <algorithm>
#
#include <internals/champsim.h>
#
#include <internals/components/cache.hh>
#include <internals/simulator.hh>
#
#include <plugins/replacements/llc_topt/llc_topt.hh>
#
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

namespace cc = champsim::components;
namespace cr = champsim::replacements;

using boost::format;
using namespace boost::filesystem;

/**
 * @brief Constructor of the class.
 */
cr::llc_topt_replacement_policy::llc_topt_replacement_policy () {

}

/**
 * @brief Destructor of the class.
 */
cr::llc_topt_replacement_policy::~llc_topt_replacement_policy () {

}

void cr::llc_topt_replacement_policy::update_replacement_state (const ch::cache_access_descriptor& desc) {

}

std::size_t cr::llc_topt_replacement_policy::find_victim (const ch::cache_access_descriptor& desc) {
	bool all_irreg_data = true;
	uint64_t vertex_id = UINT64_MAX;
	std::size_t victim = 0,
				found_count = 0;
	std::vector<std::size_t> victim_table;
	O3_CPU *curr_cpu = champsim::simulator::instance ()->modeled_cpu (desc.cpu);
	std::vector<reuse_distance_pair> rdpv (this->_cache_inst->associativity ());

	vertex_id = this->_inverse_address_translation (desc, desc.full_addr);

	// First, we seek for invalid cache blocks.
	for (uint16_t i = 0; i < this->_cache_inst->associativity (); i++) {
		// If the current way is not valid, it is a victim.
		if (!this->_cache_inst->is_valid (desc.set, i)) {
			return i;
		}
	}

	// Checking if all ways belong to graph data.
	if (vertex_id != UINT64_MAX) {
		for (std::size_t i = 0; i < this->_cache_inst->associativity () && all_irreg_data; i++) {
			uint64_t curr_line_vaddr = this->_inverse_address_translation (desc, this->_cache_inst->get_paddr (desc.set, i));

			for (const auto& e: curr_cpu->irreg_data_boundaries ()) {
				// First cache line containing streaming data encountered. It is given for eviction.
				if (!(curr_line_vaddr >= e.first && curr_line_vaddr <= e.second)) {
					return i;
				}
			}
		}

		// if (all_irreg_data) {
		// 	// No invalid cache lines found, so we look for T-OPT victims.
		// 	for (std::size_t i = 0; i < this->_cache_inst->associativity (); i++) {
		// 		rdpv[i].first = this->_inverse_address_translation (desc, this->_cache_inst->get_paddr (desc.set, i));
		// 		rdpv[i].second = UINT64_MAX;
		//
		// 		this->_get_reuse_distance (desc, rdpv[i]);
		// 	}
		//
		// 	victim = std::distance (rdpv.begin (), std::max_element (rdpv.begin (), rdpv.end (),
		// 		[] (const auto& a, const auto& b) -> bool {
		// 			return a.second < b.second;
		// 		}));
		// }

		if (all_irreg_data) {
			// No invalid cache lines found, so we look for T-OPT victims.
			for (std::size_t i = 0; i < this->_cache_inst->associativity (); i++) {
				rdpv[i].first = this->_inverse_address_translation (desc, this->_cache_inst->get_paddr (desc.set, i));
				rdpv[i].second = UINT64_MAX;
			}

			this->_browse_trace (vertex_id, rdpv, desc);

			// From here we know if these vertices will be reused at some point in the trace. Here we roughly have three
			// cases. (1) All blocks have been found in the trace. (2) None have been found. (3) Only a handful has been found.
			found_count = std::count_if (rdpv.begin (), rdpv.end (),
				[] (const reuse_distance_pair& e) -> bool {
					return e.second != UINT64_MAX;
				});

			switch (found_count) {
				case 11:
					victim = std::distance (rdpv.begin (), std::max_element (rdpv.begin (), rdpv.end (),
						[] (const auto& a, const auto& b) -> bool {
							return a.second < b.second;
						}));
					break;
				case 0:
					victim = this->_dist (this->_gen);
					break;
				default:
					for (std::size_t i = 0; i < rdpv.size (); i++) {
						if (rdpv[i].second == UINT64_MAX) {
							victim_table.push_back (i);
						}
					}

					this->_dist_back_up = std::uniform_int_distribution<> (0, victim_table.size ());

					victim = victim_table[this->_dist_back_up (this->_gen)];

					break;
			}
		}
	} else { // It is not graph-data, so we fall back into something simple.
		victim = this->_dist (this->_gen);
	}

	return victim;
}

uint64_t cr::llc_topt_replacement_policy::_get_vertex_id (const ch::cache_access_descriptor& desc, const uint64_t& vaddr) {
	const auto& irreg_data_boundaries = champsim::simulator::instance ()->modeled_cpu (desc.cpu)->irreg_data_boundaries ();

	for (const auto& e: irreg_data_boundaries) {
		if (!(vaddr >= e.first && vaddr <= e.second)) {
			continue;
		}

		// It belongs to this irreg data array, so we simply compute the vertex id as it.
		return ((vaddr - e.first) / 0x4);
	}

	// Reaching that point should not be an option. This is a error code that is returned.
	return UINT64_MAX;
}

void cr::llc_topt_replacement_policy::_get_reuse_distance (const ch::cache_access_descriptor& desc, reuse_distance_pair& rdv) {
	uint64_t vertex_id = UINT64_MAX;
	uint64_t trimed_vaddr = rdv.first & (UINT64_MAX ^ 0x3F);

	// For each graph element of the cache block.
	for (std::size_t i = 0; i < (BLOCK_SIZE / 4); i++) {
		if ((vertex_id = this->_get_vertex_id (desc, trimed_vaddr + (0x4 * i))) != UINT64_MAX) {
			rdv.second = std::min (rdv.second, this->_get_vertex_reuse_distance (vertex_id, desc.cpu));
		}
	}
}

uint64_t cr::llc_topt_replacement_policy::_get_vertex_reuse_distance (const uint64_t& vertex_id, const uint8_t& cpu) {
	std::vector<uint32_t>::iterator lookahead_it = this->_trace_it[cpu];

	// Forwarding the lookahead iterator until we reach the requested vertex id.
	while (*lookahead_it != vertex_id) {
		lookahead_it++;

		// Not found... :'(
		if (lookahead_it == this->_vertices_trace[cpu].end ()) {
			return UINT64_MAX;
		}
	}

	return std::distance (this->_trace_it[cpu], lookahead_it);
}

uint64_t cr::llc_topt_replacement_policy::_inverse_address_translation (const ch::cache_access_descriptor& desc, const uint64_t& paddr) {
	uint64_t ppage = paddr >> LOG2_PAGE_SIZE,
			 page_offset = (paddr & (PAGE_SIZE - 1ULL)),
			 vpage = 0ULL,
			 vaddr = 0ULL;
	std::map <uint64_t, uint64_t>::const_iterator it;

	// Not looking good.
	if ((it = desc.inverse_table->find (ppage)) == desc.inverse_table->cend ()) {
		// std::cerr << "Physical page not found in the page table." << std::endl;

		assert(0);
	}

	vpage = it->second;
	// vaddr = (vpage << LOG2_PAGE_SIZE) | (page_offset & ((PAGE_SIZE - 1ULL) ^ (BLOCK_SIZE - 1ULL)));
	vaddr = (vpage << LOG2_PAGE_SIZE) | (page_offset);

	return vaddr;
}

void cr::llc_topt_replacement_policy::_browse_trace (const uint32_t& curr_vertex, reuse_distance_vector& rdv, const ch::cache_access_descriptor& desc) {
	const auto& irreg_data_boundaries = champsim::simulator::instance ()->modeled_cpu (desc.cpu)->irreg_data_boundaries ().front ();

	std::vector<uint32_t>::iterator it = std::find (this->_vertices_trace[desc.cpu].begin (), this->_vertices_trace[desc.cpu].end (), curr_vertex);

	// If the vertex looked up doesn't exist in the trace, we return. (shoudl not happen).
	if (it == this->_vertices_trace[desc.cpu].end ()) {
		return;
	}

	// We start at the beginning of the trace and we look for vertices belonging to rdv.
	for (; it != this->_vertices_trace[desc.cpu].end (); it++) {
		uint64_t curr_vertex_vaddr = (*it * 0x4) + irreg_data_boundaries.first;

		for (reuse_distance_pair& e: rdv) {
			// The vertex encoded in e has been encountered in the trace. Next one!
			if (e.first == curr_vertex_vaddr) {
				e.second = std::distance (this->_vertices_trace[desc.cpu].begin (), it);

				break;
			}
		}
	}

	// We can possibly exit that loop whilst not all vertices have been found (rdv[...].second == UINT64_MAX).
}

/**
 * @brief This method is used to create an instance of the prefetcher and provide
 * it to the performance model.
 */
cr::ireplacementpolicy* cr::llc_topt_replacement_policy::create_replacementpolicy () {
	return new cr::llc_topt_replacement_policy ();
}

void cr::llc_topt_replacement_policy::_init (const pt::ptree& props, cc::cache* cache_inst) {
	std::size_t trace_size = 0;
	std::fstream trace_file;

	cr::ireplacementpolicy::_init (props, cache_inst);

	// Setting up the random number generator.
	this->_dist = std::uniform_int_distribution<> (0, this->_cache_inst->associativity () - 1);

	this->_vertices_trace = std::vector<std::vector<uint32_t>> (champsim::simulator::instance ()->traces ().size ());
	this->_trace_it = std::vector<std::vector<uint32_t>::iterator> (champsim::simulator::instance ()->traces ().size ());

	for (std::size_t i = 0; i < champsim::simulator::instance ()->traces ().size (); i++) {
		path trace_name = path (champsim::simulator::instance ()->traces ()[i]).filename ().stem ().stem (),
			 trace_path = path (props.get<std::string> ("graph_data_directory"));
		trace_name += ".topttrace";
		trace_path += trace_name;

		std::cout << "Based on treace file, the graph data trace is then named " << trace_path << "." << std::endl;

		trace_size = file_size (trace_path);

		std::cout << (format ("Trace file: %1s has %2d vertex entries.") % trace_path % (trace_size / 4)) << std::endl;

		// Opening the trace file in order to read it.
		trace_file.open (trace_path.string(), std::ios::in | std::ios::binary);

		// Allocating an array to store the graph data access trace.
		this->_vertices_trace[i] = std::vector<uint32_t> ((trace_size / 4) + 1, 0x0);

		this->_vertices_trace[i][0] = UINT32_MAX;

		for (std::size_t j = 1; j < this->_vertices_trace.size (); j++) {
			// Doing the actual reads.
			uint32_t curr_vertex_id;
			trace_file.read (reinterpret_cast<char *> (&curr_vertex_id), sizeof (uint32_t));

			this->_vertices_trace[i][j] = curr_vertex_id;
		}

		this->_trace_it[i] = this->_vertices_trace[i].begin ();

		std::cout << (format ("Trace file: %1s loaded in memory and ready to use.") % trace_path) << std::endl;

		trace_file.close ();
	}
}

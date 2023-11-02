#include <plugins/prefetchers/l2c_spp/l2c_spp.hh>
#
#include <boost/property_tree/json_parser.hpp>

namespace cp = champsim::prefetchers;

/**
 * Constructor of the class.
 */
cp::l2c_spp_prefetcher::l2c_spp_prefetcher () {

}

cp::l2c_spp_prefetcher::l2c_spp_prefetcher (const cp::l2c_spp_prefetcher& o) :
 	_desc (o._desc),
	_st (o._st), _pt (o._pt), _filter (o._filter), _ghr (o._ghr),
    iprefetcher (o) {

}

/**
 * Destructor of the class.
 */
cp::l2c_spp_prefetcher::~l2c_spp_prefetcher () {

}

/**
 * @brief This method performs the actual operation of the prefetcher. This method is meant to
 * be called on every prefetch request opportunity.
 * @param desc A descriptor filled with information regarding the prefetch request.
 */
void cp::l2c_spp_prefetcher::operate (const cp::prefetch_request_descriptor& desc) {
	uint64_t 	page = desc.addr >> LOG2_PAGE_SIZE;
	uint32_t 	page_offset = (desc.addr >> LOG2_BLOCK_SIZE) & (PAGE_SIZE / BLOCK_SIZE - 1),
				last_sig = 0,
				curr_sig = 0,
				depth = 0;
	int32_t		delta = 0;
	std::vector<uint32_t> confidence_q (this->_cache_inst->mshr_size (), 0);
	std::vector<int32_t> delta_q (this->_cache_inst->mshr_size (), 0);

	confidence_q[0] = 100;

	this->_ghr.global_accuracy = this->_ghr.pf_issued ? ((100 * this->_ghr.pf_useful) / this->_ghr.pf_issued) : 0;

	/*
	 * Stage 1: Read and update a signature stored in the ST.
	 * last_sig and delta are used to update (sig, delta) correlation in PT.
	 * curr_sig is used to read prefetch candidates in PT.
	 */
	this->_st.read_and_update_sig (page, page_offset, last_sig, curr_sig, delta, this->_ghr);

	// Also check the prefetch filter in parallel to update accuracy counters.
	this->_filter.check (desc.addr, details::prefetch_filter::l2c_demand, this->_ghr);

	// Stage 2: Update delta ppatterns stored in the PT.
	if (last_sig) {
		this->_pt.update_pattern (last_sig, delta);
	}

	// Stage 3: Start prefetching.
	uint64_t base_addr = desc.addr;
	uint32_t lookahead_conf = 100,
			 pf_q_head = 0,
			 pf_q_tail = 0;
	bool do_lookahead = false;

	do {
		uint32_t lookahead_way = this->_desc.pt_ways;
		this->_pt.read_pattern (curr_sig, lookahead_way, lookahead_conf, pf_q_tail, depth, delta_q, confidence_q, this->_ghr);

		do_lookahead = false;

		for (uint32_t i = pf_q_head; i < pf_q_tail; i++) {
			if (confidence_q[i] >= this->_desc.pf_threshold) {
				uint64_t pf_addr = (base_addr & ~(BLOCK_SIZE - 1)) + (delta_q[i] << LOG2_BLOCK_SIZE);

				// Prefetch request is in the same physical page.
				if ((base_addr & ~(PAGE_SIZE - 1)) == (pf_addr & ~(PAGE_SIZE - 1))) {
					if (this->_filter.check (pf_addr, ((confidence_q[i] >= this->_desc.fill_threshold) ? details::prefetch_filter::spp_l2c_prefetch : details::prefetch_filter::spp_llc_prefetch), this->_ghr)) {
						this->_cache_inst->prefetch_line (desc.cpu, BLOCK_SIZE, desc.ip, base_addr, pf_addr, ((confidence_q[i] >= this->_desc.fill_threshold) ? cc::cache::fill_l2 : cc::cache::fill_llc), 0);
					}

					if (confidence_q[i] >= this->_desc.fill_threshold) {
						this->_ghr.pf_issued++;

						if (this->_ghr.pf_issued > this->_desc.global_counter_max) {
							this->_ghr.pf_issued >>= 1;
							this->_ghr.pf_useful >>= 1;
						}
					}
				} else { // Prefetch request is crossing the physical page boundary.
					// Store this prefetch request in the GHT to bootstrap SPP learning when we see a ST miss (i.e., acessing a new page).
					this->_ghr.update_entry (curr_sig, confidence_q[i], (pf_addr >> LOG2_BLOCK_SIZE) & 0x3F, delta_q[i]);
				}

				do_lookahead = true;
				pf_q_head++;
			}
		}

		// Update base_addr and cur_sig.
		if (lookahead_way < this->_desc.pt_ways) {
			uint32_t set = details::helpers::hash (curr_sig) % this->_desc.pt_sets;
			base_addr += (this->_pt.delta[set][lookahead_way] << LOG2_BLOCK_SIZE);

			// PT.delta uses a 7-bit sign magnitude representation to generate sig_delta
			int sig_delta = (this->_pt.delta[set][lookahead_way] < 0) ? (((-1) * this->_pt.delta[set][lookahead_way]) + (1 << (this->_desc.sig_delta_bits - 1))) : this->_pt.delta[set][lookahead_way];
			curr_sig = ((curr_sig << this->_desc.sig_shift) ^ sig_delta) & this->_desc.sig_mask;
		}
	} while (do_lookahead);
}

void cp::l2c_spp_prefetcher::fill (const champsim::helpers::cache_access_descriptor& desc) {
    this->_filter.check (desc.victim_addr, details::prefetch_filter::l2c_evict, this->_ghr);
}

cp::l2c_spp_prefetcher* cp::l2c_spp_prefetcher::clone () {
	return new l2c_spp_prefetcher (*this);
}

void cp::l2c_spp_prefetcher::clone (l2c_spp_prefetcher* o) {
    cp::iprefetcher::operator= (*o);

    this->_desc = o->_desc;
    this->_st = o->_st;
    this->_pt = o->_pt;
    this->_filter = o->_filter;
    this->_ghr = o->_ghr;
}

/**
 * This method is used to create an instance of the prefetcher and provide it to the performance model.
 */
cp::iprefetcher* cp::l2c_spp_prefetcher::create_prefetcher () {
	return new cp::l2c_spp_prefetcher ();
}

void cp::l2c_spp_prefetcher::_init (const pt::ptree& props, cc::cache* cache_inst) {
	// Calling the version of the parent class first.
	cp::iprefetcher::_init (props, cache_inst);

	// Getting signature table parameters.
	this->_desc.st_sets = props.get<std::size_t> ("signature.table.sets");
	this->_desc.st_ways = props.get<std::size_t> ("signature.table.ways");
	this->_desc.st_tag_bits = props.get<std::size_t> ("signature.table.tag_bits");
	this->_desc.sig_shift = props.get<std::size_t> ("signature.shift");
	this->_desc.sig_bits = props.get<std::size_t> ("signature.bits");
	this->_desc.sig_delta_bits = props.get<std::size_t> ("signature.delta_bits");

	this->_desc.st_tag_mask = ((1ULL << this->_desc.st_tag_bits) - 1ULL);
	this->_desc.sig_mask = ((1ULL << this->_desc.sig_bits) - 1ULL);

	// Getting pattern table parameters.
	this->_desc.pt_sets = props.get<std::size_t> ("pattern.table.sets");
	this->_desc.pt_ways = props.get<std::size_t> ("pattern.table.ways");
	this->_desc.c_sig_bits = props.get<std::size_t> ("pattern.table.signature_counter_bits");
	this->_desc.c_delta_bits = props.get<std::size_t> ("pattern.table.delta_counter_bits");

	this->_desc.c_sig_max = ((1ULL << this->_desc.c_sig_bits) - 1ULL);
	this->_desc.c_delta_max = ((1ULL << this->_desc.c_delta_max) - 1ULL);

	// Getting Prefetch filter parameters.
	this->_desc.quotient_bits = props.get<std::size_t> ("prefetch_filter.quotient_bits");
	this->_desc.remainder_bits = props.get<std::size_t> ("prefetch_filter.remainder_bits");
	this->_desc.fill_threshold = props.get<std::size_t> ("prefetch_filter.fill_threshold");
	this->_desc.pf_threshold = props.get<std::size_t> ("prefetch_filter.prefetch_threshold");

	this->_desc.hash_bits = this->_desc.quotient_bits + this->_desc.remainder_bits;
	this->_desc.filter_sets = (1ULL << this->_desc.quotient_bits);

	// Getting global register parameters.
	this->_desc.global_counter_bits = props.get<std::size_t> ("global_register.counter_bits");
	this->_desc.ghr_size = props.get<std::size_t> ("global_register.size");

	this->_desc.global_counter_max = ((1ULL << this->_desc.global_counter_bits) - 1ULL);

	// Initilizing internal structures.
	this->_st.init (this->_desc);
	this->_pt.init (this->_desc);
	this->_filter.init (this->_desc);
	this->_ghr.init (this->_desc);
}

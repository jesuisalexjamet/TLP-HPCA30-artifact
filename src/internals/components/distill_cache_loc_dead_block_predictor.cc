#include <cmath>
#
#include <internals/components/distill_cache_loc_dead_block_predictor.hh>

namespace cc = champsim::components;

cc::distill_cache_loc_dead_block_predictor::distill_cache_loc_dead_block_predictor () {

}

cc::distill_cache_loc_dead_block_predictor::~distill_cache_loc_dead_block_predictor () {

}

void cc::distill_cache_loc_dead_block_predictor::init (const pt::ptree& props) {
	std::size_t curr_sampler_set_idx = 0,
				prediction_table_size = 0;

	this->_sampler_sets = props.get<std::size_t> ("sampler.sets");
	this->_sampler_ways = props.get<std::size_t> ("sampler.ways");
	this->_cache_sets = props.get<std::size_t> ("set_degree");
	this->_block_size = props.get<std::size_t> ("block_size");
	prediction_table_size = props.get<std::size_t> ("sampler.prediction_table.size");

	// Random number generator device.
	std::mt19937 gen;
	std::uniform_int_distribution<std::size_t> dis (0, this->_cache_sets - 1);

	// Generating the sampling map.
	while (this->_sampling_map.size () != this->_sampler_sets) {
		std::size_t random_cache_set = dis (gen);

		// Looking for the generated random cache set.
		if (this->_sampling_map.find (random_cache_set) != this->_sampling_map.end ()) {
			continue;
		}

		// The random set hasn't been found in the sampling map, thus we can insert it.
		this->_sampling_map.insert (std::make_pair (random_cache_set, curr_sampler_set_idx));
		curr_sampler_set_idx++;
	}

	// Filling the sampler.
	this->_samp = sampler (this->_sampler_sets, sampler_set (this->_sampler_ways, sampler_entry ()));

	// Setting replacement state in the sampler.
	for (std::size_t set = 0; set < this->_sampler_sets; set++) {
		for (std::size_t way = 0; way < this->_sampler_ways; way++) {
			this->_samp[set][way].lru = this->_sampler_ways - 1;
		}
	}

	// Filling the prediction table.
	this->_pred_table = std::vector<uint8_t> (prediction_table_size, 0);
}

bool cc::distill_cache_loc_dead_block_predictor::is_cache_set_sampled (const std::size_t& set_idx) const {
	return (this->_sampling_map.find (set_idx) != this->_sampling_map.end ());
}

void cc::distill_cache_loc_dead_block_predictor::update_sampler (const helpers::cache_access_descriptor& desc) {
	uint64_t tag = this->_get_tag (desc);
	std::size_t sampler_hit_way = this->_is_sampler_hit (desc),
				sampler_set_idx;
	std::size_t pred_table_idx;

	// If the set is not sampled there is no need to go further.
	if (!this->is_cache_set_sampled (desc.set)) {
		return;
	}

	// Returning the associated sampler set.
	sampler_set_idx = this->_sampling_map[desc.set];

	if (sampler_hit_way != UINT64_MAX) {
		sampler_entry& entry = this->_samp[sampler_set_idx][sampler_hit_way];
		pred_table_idx = entry.pc % this->_pred_table.size ();

		// On a sampler hit we update the prediction table accordingly to access just seen.
		this->_pred_table[pred_table_idx] = std::max (0, this->_pred_table[pred_table_idx] - 1);

		entry.type = static_cast<cache::access_types> (desc.type);
		entry.used = true;
	} else {
		sampler_entry& victim = this->_sampler_victim (desc);
		pred_table_idx = victim.pc % this->_pred_table.size ();

		// Updating the prediction table accordingly to the victim entry.
		if (!victim.used && victim.valid) {
			this->_pred_table[pred_table_idx] = std::min (this->_pred_table[pred_table_idx] + 1, 0x7);
		}

		// Filling the victim entry with new data.
		victim.tag = tag;
		victim.pc = desc.pc;
		victim.type = static_cast<cache::access_types> (desc.type);
		victim.used = false;
		victim.valid = true;

		// Getting the victim's way.
		sampler_hit_way = std::distance (this->_samp[sampler_set_idx].begin (), std::find_if (this->_samp[sampler_set_idx].begin (), this->_samp[sampler_set_idx].end (), [&tag] (const sampler_entry& e) -> bool {
			return (e.tag == tag);
		}));
	}

	// Updating replacement state.
	std::for_each (this->_samp[sampler_set_idx].begin (), this->_samp[sampler_set_idx].end (),
					[touch_lru = this->_samp[sampler_set_idx][sampler_hit_way].lru] (sampler_entry& e) -> void {
						if (e.lru < touch_lru) {
							e.lru++;
						}
					});

	this->_samp[sampler_set_idx][sampler_hit_way].lru = 0x0;
}

const uint8_t& cc::distill_cache_loc_dead_block_predictor::operator[] (const uint64_t& pc) const {
	return this->_pred_table[pc % this->_pred_table.size ()];
}

uint64_t cc::distill_cache_loc_dead_block_predictor::_get_tag (const helpers::cache_access_descriptor& desc) const {
	static uint64_t displacement = static_cast<uint64_t> (std::log2 (this->_block_size * this->_cache_sets));

	return (desc.full_addr >> displacement);
}

std::size_t cc::distill_cache_loc_dead_block_predictor::_is_sampler_hit (const champsim::helpers::cache_access_descriptor& desc) {
	uint64_t tag = this->_get_tag (desc);
	std::size_t sampler_set_idx;

	sampler_set::iterator it;

	// If the cache is not sampled, there is no wa this can be a hit.
	if (!this->is_cache_set_sampled (desc.set)) {
		return UINT64_MAX;
	}

	// Getting the sampler set index.
	sampler_set_idx = this->_sampling_map[desc.set];

	// Searching the sampler set for a valid entry and a matching tag.
	it = std::find_if (this->_samp[sampler_set_idx].begin (), this->_samp[sampler_set_idx].end (),
					[&tag] (const sampler_entry& e) -> bool {
						return (e.tag == tag && e.valid);
					});

	if (it == this->_samp[sampler_set_idx].end ()) {
		return UINT64_MAX;
	} else {
		return std::distance (this->_samp[sampler_set_idx].begin (), it);
	}
}

cc::distill_cache_loc_dead_block_predictor::sampler_entry& cc::distill_cache_loc_dead_block_predictor::_sampler_victim (const helpers::cache_access_descriptor& desc) {
	std::size_t sampler_set_idx;
	sampler_set::iterator it;

	// Returning dumb shit.
	if (!this->is_cache_set_sampled (desc.set)) {
		// throw std::runtime_error ()
	}

	sampler_set_idx = this->_sampling_map[desc.set];

	it = std::find_if (this->_samp[sampler_set_idx].begin (), this->_samp[sampler_set_idx].end (),
						  [this] (const sampler_entry& e) -> bool {
							  return (!e.valid || e.lru == this->_sampler_ways - 1);
						  });

	return *it;
}

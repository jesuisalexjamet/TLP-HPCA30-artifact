#include <cstdlib>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <iomanip>
#
#include <internals/simulator.hh>
#include <internals/components/sectored_cache.hh>
#
#include <instrumentations/reuse_tracker.hh>

#define PREDICTOR_THRESHOLD 0x0
#define INDIVIDUAL_PREDICTION_THRESHOLD 0x0
#define PADDR_OFFSET_MASK 0x3F
#define PADDR_OFFSET_FILTER(paddr) \
  (paddr & (UINT64_MAX ^ PADDR_OFFSET_MASK))

uint64_t reuse_tracker::usages = 0;

reuse_tracker::accuracy_metrics::accuracy_metrics () :
  never_reused (0), true_positives (0), true_negatives (0), false_positives (0),
  false_negatives (0), cache_friendly (0), cache_averse (0), prediction_friendly (0),
  prediction_averse (0), sensitivity (0.0f), specificity (0.0f), minority_wrongs (0),
  wrong_features (0), avg_wrong_features (0.0f), minory_wrong_rate (0.0f),
  minority_corrects (0), correct_features (0), avg_correct_features (0.0f), minority_correct_rate (0.0f) {
  // Initializing the mass functions.
  for (std::size_t i = 0; i < 16; i++) {
    this->wrong_features_mass_func[i] = 0x0;
    this->correct_features_mass_func[i] = 0x0;
  }
}

reuse_tracker::reuse_tracker (const std::size_t& distance_limit, champsim::components::cache *cache) :
    _distance_limit (distance_limit),
    _report_file (dynamic_cast<champsim::components::sectored_cache *> (cache)->report_filename (), std::ios::out | std::ios::trunc) {

}

reuse_tracker::~reuse_tracker () {

}

reuse_tracker::accuracy_metrics& reuse_tracker::metrics () {
  return this->_metrics;
}

const reuse_tracker::accuracy_metrics& reuse_tracker::metrics () const {
  return this->_metrics;
}

reuse_tracker::accuracy_metrics_map& reuse_tracker::per_entry_metrics () {
  return this->_per_entry_metrics;
}

void reuse_tracker::add_usage (uint64_t vaddr, uint64_t paddr, uint64_t ip, bool cache_hit) {
  uint64_t block_id = PADDR_OFFSET_FILTER (paddr);
  iterator it;
  block_usage_descriptor desc;
  bool found = false;

  desc.stack_distance () = 0;
  desc.cache_hit () = cache_hit;
  desc.paddr () = block_id;
  desc.vaddr () = vaddr;
  desc.ip () = ip;

  // Updating the counter of usages.
  usages++;

  // First, we try to find a descriptor for the block being accesses.
  found = ((it = std::find (this->_usages.begin (), this->_usages.end (), block_id)) != this->_usages.end ());

  /**
   * We then increment all the measured distances even though the block we were
   * looking has not been found in the registry.
   */
  std::for_each (this->_usages.begin (), this->_usages.end (),
    [found] (block_usage_descriptor &e) -> void {
      e.stack_distance ()++;
    });

  /*
   * Eventually, depending on the outcome of the search for the block, we triger
   * a different set of actions.
   *
   * If the block has been found we update the metrics for this set based on the
   * measured reuse distance and the associated prediction. Conversely, if the
   * block is not found, we add it the registry.
   */
  if (!found) {
    this->_usages.push_back (desc);
  } else {
    this->update_metrics (*it);
    this->update_metrics (*it, this->_per_entry_metrics[ip]);

    // Reinitializing the descriptor.
    it->stack_distance () = 0;
    it->cache_hit () = cache_hit;
  }
}

std::size_t reuse_tracker::size () const {
  return this->_usages.size ();
}

reuse_tracker::iterator reuse_tracker::begin () noexcept {
  return this->_usages.begin ();
}

reuse_tracker::const_iterator reuse_tracker::begin () const noexcept {
  return this->_usages.begin ();
}

reuse_tracker::const_iterator reuse_tracker::cbegin () const noexcept {
  return this->_usages.cbegin ();
}

reuse_tracker::iterator reuse_tracker::end () noexcept {
  return this->_usages.end ();
}

reuse_tracker::const_iterator reuse_tracker::end () const noexcept {
  return this->_usages.end ();
}

reuse_tracker::const_iterator reuse_tracker::cend () const noexcept {
  return this->_usages.cend ();
}

reuse_tracker::reverse_iterator reuse_tracker::rbegin () noexcept {
  return this->_usages.rbegin ();
}

reuse_tracker::const_reverse_iterator reuse_tracker::rbegin () const noexcept {
  return this->_usages.rbegin ();
}

reuse_tracker::const_reverse_iterator reuse_tracker::crbegin () const noexcept {
  return this->_usages.crbegin ();
}

reuse_tracker::reverse_iterator reuse_tracker::rend () noexcept {
  return this->_usages.rend ();
}

reuse_tracker::const_reverse_iterator reuse_tracker::rend () const noexcept {
  return this->_usages.rend ();
}

reuse_tracker::const_reverse_iterator reuse_tracker::crend () const noexcept {
  return this->_usages.crend ();
}

void reuse_tracker::wrap () {
	for (block_usage_descriptor &e : this->_usages) {
		if (e.stack_distance () > this->_distance_limit) {
			this->update_metrics (e);
		}
	}
}

void reuse_tracker::update_metrics (block_usage_descriptor &b) {
  this->update_metrics (b, this->_metrics);
}

void reuse_tracker::update_metrics (block_usage_descriptor &b, accuracy_metrics &target) {
  bool cache_friendly = false, error = false;
  // uint32_t cache_line_id = (b.vaddr () - ooo_cpu[0].irreg_base) / BLOCK_SIZE;
  std::size_t wrongs = 0x0,
              corrects = 0x0;

  if (b.cache_hit ()) {
      b.reuses ()++;
  } else {
      b.reuses () = 0;
  }

  if (champsim::simulator::instance()->all_warmup_complete()) {
      // if ((cache_friendly = b.stack_distance () <= this->_distance_limit)) {
      //     target.cache_friendly++;
      //
      //     if (b.vaddr () >= ooo_cpu[0].irreg_base && b.vaddr () <= ooo_cpu[0].irreg_bound) {
      //         target.reuse_heat_map[cache_line_id]++;
      //     }
      // } else {
      //     if (b.vaddr () >= ooo_cpu[0].irreg_base && b.vaddr () <= ooo_cpu[0].irreg_bound) {
      //         target.reuse_heat_map[cache_line_id]--;
      //     }
      //
      //     target.cache_averse++;
      // }

      if (target.reuse_heat_map.size () >= 4096) {
          for (const auto& e: target.reuse_heat_map) {
              this->_report_file << e.first << " " << e.second << std::endl;
          }

          target.reuse_heat_map.clear ();
      }
  }
	//
  // if (b.prediction () < Helpers::tau_0 ()) {
  //   target.prediction_friendly++;
  // } else {
  //   target.prediction_averse++;
  // }
	//
  // if (b.prediction () < Helpers::tau_0 () && b.stack_distance () < this->_distance_limit) {
  //   target.true_positives++;
  // } else if (b.prediction () >= Helpers::tau_0 () && b.stack_distance () >= this->_distance_limit) {
  //   target.true_negatives++;
  // } else if (b.prediction () < Helpers::tau_0 () && b.stack_distance () >= this->_distance_limit) {
  //   target.false_positives++;
	//
  //   error = true;
  // } else if (b.prediction () >= Helpers::tau_0 () && b.stack_distance () < this->_distance_limit) {
  //   target.false_negatives++;
	//
  //   error = true;
  // }
}

std::fstream& operator<< (std::fstream& s, const reuse_tracker& t) {
  std::for_each (t.cbegin (), t.cend (),
      [&s] (const block_usage_descriptor& e) {
        // We only write in the file when the usgae descriptor accounts for a finished one.
        s << e.cache_hit () << ";"
          << e.stack_distance () << std::endl;
      });

  return s;
}

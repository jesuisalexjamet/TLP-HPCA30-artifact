#ifndef __CHAMPSIM_INTERNALS_COMPONENTS_LMP_HH__
#define __CHAMPSIM_INTERNALS_COMPONENTS_LMP_HH__

#include <map>
#
#include <ostream>
#
#include <block.h>

namespace champsim {
	namespace components {
		class load_miss_predictor {
		private:
			using level_one_prediction_table = std::map<uint64_t, uint32_t>;
			using level_two_prediction_table = std::map<uint32_t, bool>;

		public:
			struct lmp_stats {
			public:
				uint64_t accurate, inaccurate;

			public:
				lmp_stats () = default;

				lmp_stats (const lmp_stats& o) {
					this->accurate = o.accurate;
					this->inaccurate = o.inaccurate;
				}

				lmp_stats& operator= (const lmp_stats& o) {
					this->accurate = o.accurate;
					this->inaccurate = o.inaccurate;

					return *this;
				}
			};

		public:
			load_miss_predictor () = default;
			load_miss_predictor (const std::size_t &num_pc, const std::size_t &num_history);
			load_miss_predictor (const load_miss_predictor &o);
			load_miss_predictor (load_miss_predictor &&o);

			load_miss_predictor& operator= (const load_miss_predictor &o);
			load_miss_predictor& operator= (load_miss_predictor &&o);

			bool predict (const uint64_t &ip) const;

			void update (const uint64_t &ip, const bool &cache_miss);
			void update (const uint64_t &ip, const PACKET &packet);

			lmp_stats& metrics ();
			const lmp_stats& metrics () const;

		private:
			std::size_t _num_pc, _num_history;

			level_one_prediction_table _l1pt;
			level_two_prediction_table _l2pt;

			lmp_stats _metrics_tracker;
		};
	}
}

std::ostream& operator<< (std::ostream& os, const champsim::components::load_miss_predictor& lmp);

#endif // __CHAMPSIM_INTERNALS_COMPONENTS_LMP_HH__

#ifndef __CHAMPSIM_INTERNALS_REPLACEMENTS_IREPLACEMENTPOLICY_HH__
#define __CHAMPSIM_INTERNALS_REPLACEMENTS_IREPLACEMENTPOLICY_HH__

#include <map>
#include <string>
#
#include <exception>
#
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

static std::map<std::string, cc::cache_type> replacement_type_map = {
	{ "itlb", cc::is_itlb },
	{ "dtlb", cc::is_dtlb },
	{ "stlb", cc::is_stlb },
	{ "l1i", cc::is_l1i },
	{ "l1d", cc::is_l1d },
	{ "l2c", cc::is_l2c },
	{ "llc", cc::is_llc },
    { "sdc", cc::is_sdc },
};

namespace cc = champsim::components;
namespace ch = champsim::helpers;
namespace pt = boost::property_tree;

namespace champsim {
	namespace replacements {
		class ireplacementpolicy {
		public:
			ireplacementpolicy () = default;

			virtual ~ireplacementpolicy () {

			}

			void init (const std::string& config_file, cc::cache* cache_inst) {
				pt::ptree props;

				pt::read_json (config_file, props);

				// Calling the actuall initialization.
				this->_init (props, cache_inst);
			}

			const std::string& name () const {
				return this->_name;
			}

			virtual void report (std::ostream& os) {

			}

			virtual void update_replacement_state (const ch::cache_access_descriptor& desc) = 0;

			virtual std::size_t find_victim (const ch::cache_access_descriptor& desc) = 0;

		protected:
			virtual void _init (const pt::ptree& props, cc::cache* cache_inst) {
				std::string cache_type;
				std::map<std::string, cc::cache_type>::iterator cache_type_it;

				// Getting meta-data about the prefetcher.
				this->_name = props.get<std::string> ("name");
				cache_type = props.get<std::string> ("cache_type");

				if ((cache_type_it = replacement_type_map.find (cache_type)) == replacement_type_map.end ()) {
					// TODO: Throw exception.
				}

				this->_cache_type = cache_type_it->second;

				// Throwing some sanity checks.
				if (!cache_inst->check_type (this->_cache_type)) {
					throw std::runtime_error ("Mismatch between cache and its associated replacement policy.");
				}

				this->_cache_inst = cache_inst;
			}

		private:
			ireplacementpolicy (ireplacementpolicy&&) = delete;

			ireplacementpolicy& operator= (ireplacementpolicy&&) = delete;

		protected:
			cc::cache* _cache_inst;

		private:
			cc::cache_type _cache_type;
			std::string _name;
		};
	}
}

#endif // __CHAMPSIM_INTERNALS_REPLACEMENTS_IREPLACEMENTPOLICY_HH__

#include <internals/replacements/ireplacementpolicy.hh>
#
#include <cstdint>
#include <vector>
#include <random>
#
#include <boost/property_tree/ptree.hpp>
#
#include <boost/dll.hpp>
#include <boost/dll/alias.hpp>

namespace dll = boost::dll;
namespace pt = boost::property_tree;

namespace champsim {
	namespace replacements {
	/**
     * @brief An implementation of a L1D prefetcher that does nothing.
     */
    class sdc_random_replacement_policy : public ireplacementpolicy {
		public:
			sdc_random_replacement_policy ();
			virtual ~sdc_random_replacement_policy ();

			virtual void report (std::ostream& os) final;

			virtual void update_replacement_state (const ch::cache_access_descriptor& desc) final;

			virtual std::size_t find_victim (const ch::cache_access_descriptor& desc) final;

			static ireplacementpolicy* create_replacementpolicy ();

		protected:
			void _init (const pt::ptree& props, cc::cache* cache_inst) final;

		private:
			std::mt19937 _gen;
			std::uniform_int_distribution<> _dist;
		};

    // Exporting the symbol used for module loading.
		BOOST_DLL_ALIAS(
			champsim::replacements::sdc_random_replacement_policy::create_replacementpolicy,
			create_replacementpolicy
		)
	}
}

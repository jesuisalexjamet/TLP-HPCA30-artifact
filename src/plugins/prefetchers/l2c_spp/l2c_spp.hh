#ifndef __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_HH__
#define __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_HH__

#include <internals/prefetchers/iprefetcher.hh>
#
#include <plugins/prefetchers/l2c_spp/details.hh>
#
#include <boost/shared_ptr.hpp>
#
#include <boost/property_tree/ptree.hpp>
#
#include <boost/dll.hpp>
#include <boost/dll/alias.hpp>

namespace dll = boost::dll;
namespace pt = boost::property_tree;

namespace champsim {
	namespace prefetchers {
		/**
		 * @brief An implementation of a L2C prefetcher that does nothing.
		 */
		class l2c_spp_prefetcher : public iprefetcher {
		public:
			virtual ~l2c_spp_prefetcher ();

			virtual void operate (const prefetch_request_descriptor& desc) final;
      virtual void fill (const champsim::helpers::cache_access_descriptor& desc) final;

			virtual l2c_spp_prefetcher* clone () final;
			virtual void clone (l2c_spp_prefetcher* o) final;

			static iprefetcher* create_prefetcher ();

		protected:
			l2c_spp_prefetcher ();

		private:
			l2c_spp_prefetcher (const l2c_spp_prefetcher& o);

			virtual void _init (const pt::ptree& props, cc::cache* cache_inst) final;

		private:
			details::spp_descriptor		_desc;

			details::signature_table 	_st;
			details::pattern_table 		_pt;
			details::prefetch_filter 	_filter;
			details::global_register 	_ghr;
		};
	}
}

// Exporting the symbol used for module loading.
BOOST_DLL_ALIAS(
	champsim::prefetchers::l2c_spp_prefetcher::create_prefetcher,
	create_prefetcher
)

#endif // __CHAMPSIM_PLUGINS_PREFETCHERS_L2C_SPP_HH__

#include <internals/prefetchers/iprefetcher.hh>
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
     * @brief An implementation of a L1D prefetcher that does nothing.
     */
    class l1d_no_prefetcher : public iprefetcher {
		public:
			virtual ~l1d_no_prefetcher ();

			virtual void operate (const prefetch_request_descriptor& desc) final;

			virtual l1d_no_prefetcher* clone () final;

			static iprefetcher* create_prefetcher ();

		protected:
			l1d_no_prefetcher (const l1d_no_prefetcher& o);

			l1d_no_prefetcher ();
		};

    // Exporting the symbol used for module loading.
		BOOST_DLL_ALIAS(
			champsim::prefetchers::l1d_no_prefetcher::create_prefetcher,
			create_prefetcher
		)
	}
}

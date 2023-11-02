#ifndef __CHAMPSIM_INTERNALS_INSTRUCTION_READER_HH__
#define __CHAMPSIM_INTERNALS_INSTRUCTION_READER_HH__

#include <cstdio>
#include <string>
#include <istream>
#include <sstream>
#include <exception>
#
#include <instruction.h>

namespace champsim {
  namespace cpu {
	class base_instruction_reader {
	protected:
	  FILE* _trace_file_fd;
	  std::string _filename;

	protected:
	  base_instruction_reader (const std::string& filename) : _filename (filename) {
          // Quick sanity check on the provided: Does it exist?
          std::stringstream ss_e;
          std::ifstream test_file (filename);

          if (test_file.fail ()) {
              ss_e << "Trace file \"" << this->_filename << "\" could not be opened.";
              throw std::runtime_error (ss_e.str ());
          }
      }

	private:
	  virtual void _open_trace () = 0;
	  virtual void _close_trace () = 0;

  public:
	  virtual ~base_instruction_reader () { }
	};

	template <typename TraceInstrT = x86_trace_instruction, typename InstrT = ooo_model_instr>
	class instruction_reader : public base_instruction_reader {
	public:
		instruction_reader (const std::string &filename) :
			base_instruction_reader (filename) {
			// Everything went well so we open the trace file.
			this->_open_trace ();
		}

	  virtual ~instruction_reader () {
		  // Closing the trace file.
		  this->_close_trace ();
	  }

	  InstrT read_instruction () {
		TraceInstrT trace_instruction;

		while (!fread (&trace_instruction, sizeof (TraceInstrT), 1, this->_trace_file_fd)) {
			this->_close_trace ();
			this->_open_trace ();
		}

		return trace_instruction.convert ();
	  }

	private:
		void _open_trace () final {
			// Using a string stream to build the xz command.
			std::stringstream ss_xz_cmd, ss_e;
			ss_xz_cmd << "xz -dc " << this->_filename;

			if ((_trace_file_fd = popen (ss_xz_cmd.str ().c_str (), "r")) == NULL) {
				ss_e << "Trace file\"" << this->_filename << " could not be opened.";
				throw std::runtime_error (ss_e.str ());
			}
		}

		void _close_trace () final {
			std::stringstream ss_e;

			if (this->_trace_file_fd == NULL) return;

			if (pclose (this->_trace_file_fd) == -1) {
				ss_e << "Trace file\"" << this->_filename << " could not be closed.";
				throw std::runtime_error (ss_e.str ());
			}
		}
	};
  }
}

#endif // __CHAMPSIM_INTERNALS_INSTRUCTION_READER_HH__

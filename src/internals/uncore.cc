#include "uncore.h"

// uncore
UNCORE uncore;

// constructor
UNCORE::UNCORE() {

}

UNCORE::~UNCORE () {
	delete this->llc;
	delete this->dram;
}

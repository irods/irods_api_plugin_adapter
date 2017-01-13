

#include "rodsError.h"
#include "irods_api_endpoint.hpp"

namespace irods {
    api_endpoint::api_endpoint(const std::string& _ctx) :
        context_(_ctx) {
    }
   
    api_endpoint::~api_endpoint() {
    }

    int api_endpoint::status(rError_t*) { return status_; }

    bool api_endpoint::done() { return done_flag_; }

}; // namespace irods




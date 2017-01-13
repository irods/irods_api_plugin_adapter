


#ifndef IRODS_API_ENPOINT_HPP
#define IRODS_API_ENPOINT_HPP

#include "avro/Encoder.hh"
#include "avro/Decoder.hh"
#include "avro/Specific.hh"

#include "irods_message_broker.hpp"
#include "boost/any.hpp"
#include "boost/shared_ptr.hpp"

namespace irods {

    static const std::string API_EP_CLIENT("api_endpoint_client");
    static const std::string API_EP_SERVER("api_endpoint_server");
    static const std::string API_EP_SVR_TO_SVR("api_endpoint_svr_to_svr");

    class api_endpoint {
    public:
        api_endpoint(const std::string& _ctx);
        virtual ~api_endpoint();
        virtual int status(rError_t*);
        virtual bool done();
        virtual void initialize(const std::vector<uint8_t>&) = 0;
        virtual void finalize(std::vector<uint8_t>*&) = 0;
        virtual void invoke() = 0;
        void done(bool _b) { done_flag_ = _b; }
    protected:
        std::string      context_;
        boost::any       payload_;
        std::atomic_bool done_flag_;
        std::atomic_int  status_;

    }; // class api_endpoint

}; // namespace irods

#endif // IRODS_API_ENPOINT_HPP



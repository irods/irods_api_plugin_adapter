// =-=-=-=-=-=-=-
// irods includes
#include "apiHandler.hpp"
#include "rodsPackInstruct.h"
#include "objStat.h"

#include "irods_stacktrace.hpp"
#include "irods_server_api_call.hpp"
#include "irods_re_serialization.hpp"

#include "irods_api_envelope.hpp"
#include "irods_api_endpoint.hpp"
#include "irods_api_plugin_adapter_test_request.hpp"

#include "boost/any.hpp"
#include "boost/lexical_cast.hpp"

// =-=-=-=-=-=-=-
// stl includes
#include <sstream>
#include <string>
#include <iostream>
#include <thread>

typedef irods::api_plugin_adapter_test_request api_req_t;

extern "C" {
void api_adapter_test_executor_server_to_server(
    irods::api_endpoint*  _endpoint ) {
    return;
} // api_adapter_test_executor

void api_adapter_test_executor_server(
    irods::api_endpoint*  _endpoint ) {
    typedef irods::message_broker::data_type data_t;

    // =-=-=-=-=-=-=-
    //TODO: parameterize
    irods::message_broker bro("ZMQ_REP");

    const int start_port = irods::get_server_property<const int>(
                               irods::CFG_SERVER_PORT_RANGE_START_KW);
    const int  end_port = irods::get_server_property<const int>(
                              irods::CFG_SERVER_PORT_RANGE_END_KW);
    int port = bro.bind_to_port_in_range(start_port, end_port);
    _endpoint->port_for_bind(port);

    // =-=-=-=-=-=-=-
    // fetch the payload to extract the response string
    api_req_t api_req;
    try {
        _endpoint->payload<api_req_t>(api_req);
    }
    catch(const boost::bad_any_cast& _e) {
        // end of protocol
        irods::log(LOG_ERROR, _e.what());
        _endpoint->done(true);
        //TODO: notify client of failure
        return;
    }

    data_t req_data;
    bro.recieve(req_data);

    std::string req_string;
    req_string.assign(req_data.begin(), req_data.end());
    std::cout << "SERVER received request from client [" << req_string << "]" << std::endl;

    // "do stuff"
    std::cout << "SERVER doing some work" << std::endl;
    std::cout << "SERVER doing some more work" << std::endl;

    // =-=-=-=-=-=-=-
    // copy generic test response string to a data buffer
    std::string resp_string("this is a test [");
    resp_string += api_req.response_string;
    resp_string += "], this is only a test";

    data_t resp_data;
    resp_data.assign(resp_string.begin(), resp_string.end()); 
    std::cout << "SERVER sending response [" << resp_string << "]" << std::endl;
    
    // =-=-=-=-=-=-=-
    // set the message for sending, then block 
    bro.send( resp_data );
    
    // end of protocol
    _endpoint->done(true);

    return;

} // api_adapter_test_executor

void api_adapter_test_executor_client(
    irods::api_endpoint*  _endpoint ) {
    typedef irods::message_broker::data_type data_t;
    // =-=-=-=-=-=-=-
    //TODO: parameterize
    irods::message_broker bro("ZMQ_REQ");

    int port = _endpoint->port_for_bind();
    std::stringstream conn_sstr;
    conn_sstr << "tcp://localhost:";
    conn_sstr << port;
    bro.connect(conn_sstr.str());

    // =-=-=-=-=-=-=-
    // fetch the payload to extract the request string
    api_req_t api_req;
    try {
        _endpoint->payload<api_req_t>(api_req);
    }
    catch(const boost::bad_any_cast& _e) {
        // end of protocol
        std::cerr << _e.what() << std::endl;
        _endpoint->done(true);
        //TODO: notify server of failure
        return;
    }

    // =-=-=-=-=-=-=-
    // copy generic test request string to a data buffer
    std::string req_string("this is a test [");
    req_string += api_req.request_string;
    req_string += "], this is only a test.";

    data_t req_data;
    req_data.assign(req_string.begin(), req_string.end()); 

    // =-=-=-=-=-=-=-
    // set the message for sending, then block 
    std::cout << "CLIENT sending: [" << req_string << "]" << std::endl;
    bro.send( req_data );
    
    // "do stuff"
    std::cout << "CLIENT doing some work" << std::endl;
    std::cout << "CLIENT doing some work" << std::endl;

    data_t resp_data;
    bro.recieve(resp_data);
    
    std::cout << "CLIENT doing some more work" << std::endl;

    std::string resp_string;
    resp_string.assign(resp_data.begin(), resp_data.end());
    std::cout << "CLIENT RECEIVED response: [" << resp_string << "]" << std::endl;
   
    // end of protocol
    _endpoint->done(true);

    return;

} // api_adapter_test_executor
}; // extern C

class api_adapter_test_api_endpoint : public irods::api_endpoint {
    public:
        // =-=-=-=-=-=-=-
        // provide thread executors to the invoke() method
        void capture_executors(
                thread_executor& _cli,
                thread_executor& _svr,
                thread_executor& _svr_to_svr) {
            _cli        = api_adapter_test_executor_client;
            _svr        = api_adapter_test_executor_server;
            _svr_to_svr = api_adapter_test_executor_server_to_server;
        }

        api_adapter_test_api_endpoint(const std::string& _ctx) :
            irods::api_endpoint(_ctx),
            status_(0) {
        }

        // =-=-=-=-=-=-=-
        // used for client-side initialization
        void init_and_serialize_payload(
            int                   _argc,
            char*                 _argv[],
            std::vector<uint8_t>& _out) {
            for( auto i=0; i<_argc; ++i) {
                std::cout << "arg["<<i<<"] = " << _argv[i] << std::endl;
            }

            api_req_t req;
            req.request_string = "DEFAULT_REQUEST";
            req.response_string = "DEFAULT_RESPONSE";
            if(_argc >= 3 ) {
                req.request_string  = _argv[2];
            }
            if(_argc >= 4 ) {
                req.response_string = _argv[3];
            }

            auto out = avro::memoryOutputStream();
            auto enc = avro::binaryEncoder();
            enc->init( *out );
            avro::encode( *enc, req );
            auto data = avro::snapshot( *out );

            // copy for transmission to server
            _out = *data;

            // copy for client side use also
            payload_ = req;
        }

        // =-=-=-=-=-=-=-
        // used for server-side initialization
        void decode_and_assign_payload(
            const std::vector<uint8_t>& _in) {
            auto in = avro::memoryInputStream(
                          &_in[0],
                          _in.size());
            auto dec = avro::binaryDecoder();
            dec->init( *in );
            api_req_t req;
            avro::decode( *dec, req );
            payload_ = req;
        }

        // =-=-=-=-=-=-=-
        // provide an error code and string to the client
        int status(rError_t* _err) {
            if(status_ < 0) {
                addRErrorMsg(
                    _err,
                    status_,
                    error_message_.str().c_str());
            }
            return status_;
        }

    private:
        int status_;
        std::stringstream error_message_;

}; // class api_endpoint

extern "C" {
    irods::api_endpoint* plugin_factory(
        const std::string&,     //_inst_name
        const std::string& _context ) { // _context
            return new api_adapter_test_api_endpoint(_context);
    }
};


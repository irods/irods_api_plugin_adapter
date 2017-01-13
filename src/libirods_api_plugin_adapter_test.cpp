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

void api_adapter_test_executor_server_to_server(
    irods::api_endpoint*  _endpoint ) {
    return;
} // api_adapter_test_executor

void api_adapter_test_executor_server(
    irods::api_endpoint*  _endpoint ) {

    irods::message_broker bro("tcp://*:1246", "ZMQ_REP");

    irods::message_broker::data_type req_data;
    bro.recieve(req_data);

    std::string req_string;
    req_string.assign(req_data.begin(), req_data.end());
    std::cout << "SERVER RECEIVED request: " << req_string << std::endl;
   
    // "do stuff"
    std::cout << "SERVER doing some work" << std::endl;
    std::cout << "SERVER doing some more work" << std::endl;

    // =-=-=-=-=-=-=-
    // copy generic test response string to a data buffer
    std::string resp_string("this is a test RESPONSE.  this is only a test.");
    irods::message_broker::data_type resp_data;
    resp_data.assign(resp_string.begin(), resp_string.end()); 
    
    std::cout << "SERVER doing some more work" << std::endl;

    // =-=-=-=-=-=-=-
    // set the message for sending, then block 
    std::cout << "SERVER sending: " << resp_string << std::endl;

    bro.send( resp_data );
    
    std::cout << "SERVER doing some more work" << std::endl;
    
    // end of protocol
    _endpoint->done(true);

    return;

} // api_adapter_test_executor

void api_adapter_test_executor_client(
    irods::api_endpoint*  _endpoint ) {
    irods::message_broker bro("tcp://localhost:1246", "ZMQ_REQ");

    // =-=-=-=-=-=-=-
    // copy generic test request string to a data buffer
    std::string req_string("this is a test *REQUEST*.  this is only a test.");
    irods::message_broker::data_type req_data;
    req_data.assign(req_string.begin(), req_string.end()); 

    // =-=-=-=-=-=-=-
    // set the message for sending, then block 
    std::cout << "CLIENT sending: " << req_string << std::endl;
    bro.send( req_data );
    
    // "do stuff"
    std::cout << "CLIENT doing some work" << std::endl;
    std::cout << "CLIENT doing some work" << std::endl;

    irods::message_broker::data_type resp_data;
    bro.recieve(resp_data);
    
    std::cout << "CLIENT doing some more work" << std::endl;

    std::string resp_string;
    resp_string.assign(resp_data.begin(), resp_data.end());
    std::cout << "CLIENT RECEIVED response: " << resp_string << std::endl;
   
    // end of protocol
    _endpoint->done(true);

    return;

} // api_adapter_test_executor

class api_adapter_test_api_endpoint : public irods::api_endpoint {
    public:
        api_adapter_test_api_endpoint(const std::string& _ctx) :
            irods::api_endpoint(_ctx) {
        }

        ~api_adapter_test_api_endpoint() {
        }

        void initialize(const std::vector<uint8_t>& _payload) {
            if(_payload.empty()) {
                return;
            }

            try {
                std::auto_ptr<avro::InputStream> in = avro::memoryInputStream(
                                                          &_payload[0],
                                                          _payload.size());
                avro::DecoderPtr dec = avro::binaryDecoder();
                dec->init( *in );

                irods::api_plugin_adapter_test_request t_req;
                avro::decode( *dec, t_req );

                payload_ = t_req;
            } catch( const avro::Exception& _e ) {
                rodsLog(
                    LOG_ERROR,
                    "[%s]:[%d] exception caught [%s]",
                    __FUNCTION__,
                    __LINE__,
                    _e.what());
                throw;
            }
        } // initialize

        // =-=-=-=-=-=-=- 
        // function which captures any final output to respond back
        // to the client using the legacy protocol
        void finalize(std::vector<uint8_t>*& _out) {
            char msg[] = { "this is the OUTPUT message from FINALIZE" };
            _out = new std::vector<uint8_t>();

            _out->resize(sizeof(msg));
            memcpy(_out->data(), msg, sizeof(msg));
        }

        void invoke() {//irods::message_broker& _msg_broker) {
            // =-=-=-=-=-=-=- 
            // start thread based on context string 
            try {
                irods::api_plugin_adapter_test_request test_req;
                if(!payload_.empty()) {
                    test_req = boost::any_cast<irods::api_plugin_adapter_test_request>(payload_);
                }

                if(irods::API_EP_CLIENT == context_) {
                    thread_ = std::unique_ptr<std::thread>(new std::thread(
                                  api_adapter_test_executor_client, this));
                }
                else if(irods::API_EP_SERVER == context_) {
                    thread_ = std::unique_ptr<std::thread>(new std::thread(
                                  api_adapter_test_executor_server, this));
                }
                else if(irods::API_EP_SVR_TO_SVR == context_) {
                    thread_ = std::unique_ptr<std::thread>(new std::thread(
                                  api_adapter_test_executor_server_to_server, this));
                }
                else {
                    //TODO: be very angry here
                    rodsLog(
                        LOG_ERROR,
                        "[%s]:[%d] invalid ctx [%s]",
                        __FUNCTION__,
                        __LINE__,
                        context_.c_str());
                }

                // wait for the api thread to finish
                thread_->join();

            } catch( const boost::bad_any_cast& ) {
                rodsLog(
                    LOG_ERROR,
                    "[%s]:[%d] exception caught - bad any cast",
                    __FUNCTION__,
                    __LINE__);
                throw;
            }
        } // invoke

    private:
        std::stringstream error_message_;
        std::unique_ptr<std::thread> thread_;
}; // class api_endpoint

extern "C" {
    irods::api_endpoint* plugin_factory(
        const std::string&,     //_inst_name
        const std::string& _context ) { // _context
            return new api_adapter_test_api_endpoint(_context);
    }
};


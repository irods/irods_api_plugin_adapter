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
#include "boost/program_options.hpp"

// =-=-=-=-=-=-=-
// stl includes
#include <sstream>
#include <string>
#include <iostream>
#include <thread>

typedef irods::api_plugin_adapter_test_request api_req_t;

namespace po = boost::program_options;

extern "C" {
    void api_adapter_test_executor_server_to_server(
        std::shared_ptr<irods::api_endpoint>  _ep_ptr ) {
        return;
    } // api_adapter_test_executor

    void api_adapter_test_executor_server(
        std::shared_ptr<irods::api_endpoint>  _ep_ptr ) {

        // =-=-=-=-=-=-=-
        //TODO: parameterize
        irods::message_broker bro("ZMQ_REP");

        const int start_port = irods::get_server_property<const int>(
                                   irods::CFG_SERVER_PORT_RANGE_START_KW);
        const int  end_port = irods::get_server_property<const int>(
                                  irods::CFG_SERVER_PORT_RANGE_END_KW);
        int port = bro.bind_to_port_in_range(start_port, end_port);
        _ep_ptr->port(port);

        // =-=-=-=-=-=-=-
        // fetch the payload to extract the response string
        api_req_t api_req;
        try {
            _ep_ptr->payload<api_req_t>(api_req);
        }
        catch(const boost::bad_any_cast& _e) {
            // end of protocol
            irods::log(LOG_ERROR, _e.what());
            _ep_ptr->done(true);
            //TODO: add to rError for client
            return;
        }

        while(true) {
            const auto req_data = bro.receive();

            std::string req_string;
            req_string.assign(req_data.begin(), req_data.end());

            if("quit" == req_string) {
                // respond to the client side
                bro.send(std::string{"ACK"});
                break;
            }

            std::cout << "SERVER received request from client [" << req_string << "]" << std::endl;

            // "do stuff"
            std::cout << "SERVER doing some work" << std::endl;
            std::cout << "SERVER doing some more work" << std::endl;

            // =-=-=-=-=-=-=-
            // copy generic test response string to a data buffer
            std::string resp_string("this is a test [");
            resp_string += api_req.response_string;
            resp_string += "], this is only a test";

            std::cout << "SERVER sending response [" << resp_string << "]" << std::endl;

            // =-=-=-=-=-=-=-
            // set the message for sending, then block
            bro.send( resp_string );
        } // while true

        std::cout << "api_adapter_test_executor - done" << std::endl;
        // end of protocol
        _ep_ptr->done(true);

        return;

    } // api_adapter_test_executor

    void api_adapter_test_executor_client(
        std::shared_ptr<irods::api_endpoint>  _ep_ptr ) {

        zmq::socket_t cmd_skt(*_ep_ptr->ctrl_ctx(), ZMQ_REP);
        cmd_skt.connect("inproc://client_comms");

        try {
            irods::message_broker bro("ZMQ_REQ");

            int port = _ep_ptr->port();
            std::stringstream conn_sstr;
            conn_sstr << "tcp://localhost:";
            conn_sstr << port;
            bro.connect(conn_sstr.str());

            // =-=-=-=-=-=-=-
            // fetch the payload to extract the request string
            api_req_t api_req;
            try {
                _ep_ptr->payload<api_req_t>(api_req);
            }
            catch(const boost::bad_any_cast& _e) {
                // end of protocol
                std::cerr << _e.what() << std::endl;
                _ep_ptr->done(true);
                //TODO: notify server of failure
                return;
            }

            while(true) {
                zmq::message_t rcv_msg;
                bool ret = cmd_skt.recv( &rcv_msg, ZMQ_DONTWAIT);
                if( ret || rcv_msg.size() > 0) {

                    std::string in_str;
                    in_str.assign(
                        (char*)rcv_msg.data(),
                        ((char*)rcv_msg.data())+rcv_msg.size());

                    std::cout << "CMD RECV - [" << in_str << "]" << std::endl;
                    // =-=-=-=-=-=--=
                    // process events from the client
                    if("quit" == in_str) {
                        std::cout << "CMD sending quit" << std::endl;
                        bro.send(in_str);

                        const auto resp_data = bro.receive();
                        bro.send(std::string{"ACK"});

                        break;
                    }

                    bro.send(std::string{"ACK"});

                } // if event

                // =-=-=-=-=-=-=-
                // copy generic test request string to a data buffer
                std::string req_string("this is a test [");
                req_string += api_req.request_string;
                req_string += "], this is only a test.";

                // =-=-=-=-=-=-=-
                // set the message for sending, then block
                //std::cout << "CLIENT sending: [" << req_string << "]" << std::endl;
                bro.send( req_string );

                // "do stuff"
                //std::cout << "CLIENT doing some work" << std::endl;
                //std::cout << "CLIENT doing some work" << std::endl;

                const auto resp_data = bro.receive();

                //std::cout << "CLIENT doing some more work" << std::endl;

                //std::string resp_string;
                //resp_string.assign(resp_data.begin(), resp_data.end());
                //std::cout << "CLIENT RECEIVED response: [" << resp_string << "]" << std::endl;

            } // while

            // end of protocol
            _ep_ptr->done(true);
        }
        catch(const zmq::error_t& _e) {
            std::cerr << _e.what() << std::endl;
        }

        return;

    } // api_adapter_test_executor

}; // extern C

class api_adapter_test_api_endpoint : public irods::api_endpoint {
    public:
        const std::string TEST_KW{"test"};
        const std::string request_string_kw{"request_string"};
        const std::string response_string_kw{"response_string"};

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

        api_adapter_test_api_endpoint(const irods::connection_t _connection_type) :
            irods::api_endpoint(_connection_type),
            status_(0) {
                name_ = "api_plugin_adapter_test";
        }

        std::set<std::string> provides() {
            return {TEST_KW};
        }

        const std::tuple<std::string, po::options_description, po::positional_options_description>& get_program_options_and_usage(const std::string& _subcommand) {
            static const std::map<std::string, std::tuple<std::string, po::options_description, po::positional_options_description>> options_and_usage_map{
                {TEST_KW, {
                        "[" + request_string_kw + "] [" + response_string_kw + "]",
                        [this]() {
                            po::options_description desc{"iRODS v5 api test"};
                            desc.add_options()
                                (request_string_kw.c_str(), po::value<std::string>(), "String to use in the request message during the test. Defaults to \"DEFAULT_REQUEST\".")
                                (response_string_kw.c_str(), po::value<std::string>(), "String to use in the response message during the test. Defaults to \"DEFAULT RESPONSE\".")
                                ;
                            return desc;
                        }(),
                        [this]() {
                            po::positional_options_description positional_desc{};
                            positional_desc.add(request_string_kw.c_str(), 1);
                            positional_desc.add(response_string_kw.c_str(), 1);
                            return positional_desc;
                        }()
                    }
                }
            };
            return options_and_usage_map.at(_subcommand);
        }

        api_req_t get_request_from_command_args(const std::string& _subcommand, const std::vector<std::string>& _args) {
            auto& program_options_and_usage = get_program_options_and_usage(_subcommand);
            po::variables_map vm;
            po::store(po::command_line_parser(_args).
                    options(std::get<po::options_description>(program_options_and_usage)).
                    positional(std::get<po::positional_options_description>(program_options_and_usage)).
                    run(), vm);
            po::notify(vm);

            api_req_t req{};
            req.request_string  = vm.count(request_string_kw) ?
                vm[request_string_kw].as<std::string>() :
                "DEFAULT_REQUEST";
            req.response_string  = vm.count(response_string_kw) ?
                vm[response_string_kw].as<std::string>() :
                "DEFAULT_RESPONSE";

            return req;
        }

        // =-=-=-=-=-=-=-
        // used for client-side initialization
        void init_and_serialize_payload(
            const std::string&              _subcommand,
            const std::vector<std::string>& _args,
            std::vector<uint8_t>&           _out ) {


            api_req_t req = get_request_from_command_args(_subcommand, _args);

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
        const irods::connection_t& _connection_type ) { // _context
            return new api_adapter_test_api_endpoint(_connection_type);
    }
};


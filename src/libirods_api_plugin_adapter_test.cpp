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

namespace po = boost::program_options;

extern "C" {
    void api_adapter_test_executor_client(std::shared_ptr<irods::api_endpoint>  _ep_ptr );
    void api_adapter_test_executor_server(std::shared_ptr<irods::api_endpoint>  _ep_ptr );
    void api_adapter_test_executor_server_to_server(std::shared_ptr<irods::api_endpoint>  _ep_ptr );
};

class api_adapter_test_api_endpoint :
    public virtual irods::api_endpoint,
    public virtual irods::with_request<irods::api_plugin_adapter_test_request>,
    public virtual irods::with_response<std::string>,
    public virtual irods::without_context {
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

        const std::set<std::string>& provides() const {
            static std::set<std::string> provided{TEST_KW};
            return provided;
        }

        const std::tuple<std::string, po::options_description, po::positional_options_description>& get_program_options_and_usage(
                const std::string& _subcommand) const {
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

        request_t get_request_from_command_args(const std::string& _subcommand, const std::vector<std::string>& _args) const {
            auto& program_options_and_usage = get_program_options_and_usage(_subcommand);
            po::variables_map vm;
            po::store(po::command_line_parser(_args).
                    options(std::get<po::options_description>(program_options_and_usage)).
                    positional(std::get<po::positional_options_description>(program_options_and_usage)).
                    run(), vm);
            po::notify(vm);

            request_t req{};
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
        void initialize_from_command(
            const std::string&              _subcommand,
            const std::vector<std::string>& _args) {

            request(get_request_from_command_args(_subcommand, _args));
        };

        // =-=-=-=-=-=-=-
        // provide an error code and string to the client
        int status(rError_t* _err) const {
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
    void api_adapter_test_executor_server_to_server(
        std::shared_ptr<irods::api_endpoint>  _ep_ptr ) {
        return;
    } // api_adapter_test_executor

    void api_adapter_test_executor_server(
        std::shared_ptr<irods::api_endpoint>  _ep_ptr ) {

        auto test_ep_ptr = std::dynamic_pointer_cast<api_adapter_test_api_endpoint>(_ep_ptr);

        // =-=-=-=-=-=-=-
        //TODO: parameterize
        irods::message_broker bro(irods::zmq_type::RESPONSE);

        const int start_port = irods::get_server_property<const int>(
                                   irods::CFG_SERVER_PORT_RANGE_START_KW);
        const int  end_port = irods::get_server_property<const int>(
                                  irods::CFG_SERVER_PORT_RANGE_END_KW);
        int port = bro.bind_to_port_in_range(start_port, end_port);
        test_ep_ptr->port(port);

        const auto& api_req = test_ep_ptr->request();

        while(true) {
            const auto req_string = bro.receive<std::string>();

            if("quit" == req_string) {
                // respond to the client side
                bro.send("ACK");
                break;
            }

            std::cout << "SERVER received request from client [" << req_string << "]" << std::endl;

            // "do stuff"
            std::cout << "SERVER doing some work" << std::endl;
            std::cout << "SERVER doing some more work" << std::endl;

            // =-=-=-=-=-=-=-
            // copy generic test response string to a data buffer
            test_ep_ptr->response("this is a test [");
            test_ep_ptr->response() += api_req.response_string;
            test_ep_ptr->response() += "], this is only a test";

            std::cout << "SERVER sending response [" << test_ep_ptr->response() << "]" << std::endl;

            // =-=-=-=-=-=-=-
            // set the message for sending, then block
            bro.send( test_ep_ptr->response() );
        } // while true

        std::cout << "api_adapter_test_executor - done" << std::endl;
        // end of protocol
        test_ep_ptr->done(true);

        return;

    } // api_adapter_test_executor

    void api_adapter_test_executor_client(
        std::shared_ptr<irods::api_endpoint>  _ep_ptr ) {

        auto test_ep_ptr = std::dynamic_pointer_cast<api_adapter_test_api_endpoint>(_ep_ptr);

        zmq::socket_t cmd_skt(*test_ep_ptr->ctrl_ctx(), ZMQ_REP);
        cmd_skt.connect("inproc://client_comms");

        try {
            irods::message_broker bro(irods::zmq_type::REQUEST);

            int port = test_ep_ptr->port();
            std::stringstream conn_sstr;
            conn_sstr << "tcp://localhost:";
            conn_sstr << port;
            bro.connect(conn_sstr.str());

            const auto& api_req = test_ep_ptr->request();

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

                        zmq::message_t snd_msg(3);
                        memcpy(snd_msg.data(), "ACK", 3);
                        cmd_skt.send(snd_msg);

                        break;
                    }

                    zmq::message_t snd_msg(3);
                    memcpy(snd_msg.data(), "ACK", 3);
                    cmd_skt.send(snd_msg);

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

                test_ep_ptr->response() = bro.receive<std::string>();

                //std::cout << "CLIENT doing some more work" << std::endl;
                //std::cout << "CLIENT RECEIVED response: [" << resp_string << "]" << std::endl;

            } // while

            // end of protocol
            test_ep_ptr->done(true);
        }
        catch(const zmq::error_t& _e) {
            std::cerr << _e.what() << std::endl;
        }

        return;

    } // api_adapter_test_executor

}; // extern C

extern "C" {
    irods::api_endpoint* plugin_factory(
        const std::string&,     //_inst_name
        const irods::connection_t& _connection_type ) { // _context
            return new api_adapter_test_api_endpoint(_connection_type);
    }
};


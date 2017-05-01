/*
 * iapitest - test pluggable apis
*/
#include "rodsClient.h"
#include "parseCommandLine.h"
#include "rodsPath.h"
#include "lsUtil.h"
#include "oprComplete.h"

#include "irods_client_api_table.hpp"
#include "irods_pack_table.hpp"
#include "irods_buffer_encryption.hpp"
#include "irods_load_plugin.hpp"

#include "irods_api_envelope.hpp"
#include "irods_api_endpoint.hpp"
#include "irods_message_broker.hpp"
#include "irods_api_plugin_adapter_test_request.hpp"

#include "zmq.hpp"

#include <string>
#include <iostream>
#include <thread>

int client_interaction(
    zmq::context_t&    _zmq_ctx,
    const std::string& _ep_name) {

    zmq::socket_t zmq_skt(_zmq_ctx, ZMQ_REQ);
    zmq_skt.bind("inproc://client_comms");

    try {
        while(true) {
            std::string in_str("quit");
            // TODO: set up proper console - std::cout << _ep_name << "> ";
            // TODO: set up proper console - std::cin >> in_str;

            zmq::message_t snd_msg(in_str.size());
            memcpy(snd_msg.data(), in_str.data(), in_str.size());
            zmq_skt.send(snd_msg);

            zmq::message_t rcv_msg;
            while(true) {
                zmq_skt.recv( &rcv_msg );
                if(rcv_msg.size()<= 0) {
                //TODO: need backoff
                    continue;
                }
                break;
            }

            std::cout << (char*)rcv_msg.data() << std::endl;

            if("quit" == in_str) {
                std::cout << "Exiting." << std::endl;
                break;
            }
        } // while
    }
    catch(const zmq::error_t& _e) {
        std::cerr << _e.what() << std::endl;
    }

    return 0;
}

int main( int _argc, char* _argv[] ) {
    if(1 >= _argc) {
        std::cerr << "iapi_adapter_test api_v5_endpoint [...]" << std::endl;
        return 1;
    }

    signal( SIGPIPE, SIG_IGN );

    rodsEnv myEnv;
    int status = getRodsEnv( &myEnv );
    if ( status < 0 ) {
        rodsLogError( LOG_ERROR, status, "main: getRodsEnv error. " );
        return 2;
    }

    // =-=-=-=-=-=-=-
    // initialize pluggable api table
    irods::pack_entry_table& pk_tbl = irods::get_pack_table();
    irods::api_entry_table& api_tbl = irods::get_client_api_table();
    init_api_table( api_tbl, pk_tbl );

    std::vector<std::string> arg_vec;
    for(auto i = 0; i <_argc; ++i) {
        arg_vec.push_back(_argv[i]);
    }
    
    try {
        zmq::context_t zmq_ctx(1);
        irods::api_v5_call_client(
            myEnv.rodsHost,
            myEnv.rodsPort,
            myEnv.rodsZone,
            myEnv.rodsUserName,
            zmq_ctx,
            client_interaction,
            _argv[1],
            arg_vec);
    }
    catch(const irods::exception& _e) {
        std::cerr << _e.what() << std::endl;
        return _e.code();
    }

    return 0;
}


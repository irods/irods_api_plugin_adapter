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

#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>

#include <string>
#include <iostream>
#include <thread>

namespace po = boost::program_options;

int client_interaction(
    std::shared_ptr<zmq::context_t>    _zmq_ctx,
    const std::string& _ep_name) {

    zmq::socket_t zmq_skt(*_zmq_ctx, ZMQ_REQ);
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

            //std::cout << (char*)rcv_msg.data() << std::endl;

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

std::tuple<po::variables_map, std::vector<std::string>> get_variable_map_and_subcommand_options(
        int _argc,
        char* _argv[],
        const po::options_description& _desc,
        const po::positional_options_description& _positional_desc) {

    const std::string unrecognized_positional_options_kw{"unrecognized_positional_options"};

    po::options_description desc_with_unrecognized{_desc};
    desc_with_unrecognized.add_options() (unrecognized_positional_options_kw.c_str(), po::value<std::vector<std::string>>(), "Unrecognized positional options -- SHOULD NEVER BE VISIBLE IN OUTPUT");

    po::positional_options_description positional_desc_with_unrecognized{_positional_desc};
    positional_desc_with_unrecognized.add(unrecognized_positional_options_kw.c_str(), -1);

    po::variables_map vm;
    auto parsed = po::command_line_parser(_argc, _argv).
            options(desc_with_unrecognized).positional(positional_desc_with_unrecognized).allow_unregistered().run();
    auto subcommand_options = po::collect_unrecognized(parsed.options, po::include_positional);
    auto subcommand_options_without_positional = po::collect_unrecognized(parsed.options, po::exclude_positional);
    std::size_t positional_options_to_erase = _positional_desc.max_total_count();
    for ( auto it_without_positional = subcommand_options_without_positional.begin(), it = subcommand_options.begin();
            positional_options_to_erase != 0 && it != subcommand_options.end(); ) {
        if (it_without_positional == subcommand_options_without_positional.end() ||
                *it_without_positional != *it) {
            it = subcommand_options.erase(it);
            --positional_options_to_erase;
        } else {
            ++it;
            ++it_without_positional;
        }
    }
    po::store(parsed, vm);
    po::notify(vm);

    return {vm, subcommand_options};
}

int main( int _argc, char* _argv[] ) {
    const std::string help_kw = "help";
    const std::string subcommand_kw = "subcommand";
    const std::string api_plugin_kw = "api_plugin";

    po::options_description desc{"Allowed options"};
    desc.add_options()
        (help_kw.c_str(), "Display help text")
        (subcommand_kw.c_str(), po::value<std::string>(), "The name of the subcommand to be performed")
        (api_plugin_kw.c_str(), po::value<std::string>(), "The name of the api plugin to be used")
        ;

    po::positional_options_description positional_desc{};
    positional_desc.add(subcommand_kw.c_str(), 1);

    po::variables_map vm{};
    std::vector<std::string> subcommand_options{};
    std::tie(vm, subcommand_options) = get_variable_map_and_subcommand_options(_argc, _argv, desc, positional_desc);

    // =-=-=-=-=-=-=-
    // initialize pluggable api table
    init_api_table(irods::get_client_api_table(), irods::get_pack_table());

    std::vector<std::shared_ptr<irods::api_endpoint>> available_plugins{};
    if (vm.count(api_plugin_kw)) {
        if ( auto available_plugin = irods::create_command_object(vm[api_plugin_kw].as<std::string>(), irods::API_EP_CLIENT) ) {
            available_plugins.push_back(available_plugin);
        }
    } else {
        std::string plugin_path;
        irods::error ret_for_resolve_plugin_path = irods::resolve_plugin_path("api_v5", plugin_path);
        if ( !ret_for_resolve_plugin_path.ok() ) {
            std::cerr << "Couldn't find the plugin path, error code: " << ret_for_resolve_plugin_path.code();
            return -1;
        }
        irods::plugin_name_generator name_gen;
        irods::plugin_name_generator::plugin_list_t plugin_list;
        irods::error ret_for_list_plugins = name_gen.list_plugins(plugin_path, plugin_list);
        if( !ret_for_list_plugins.ok() ) {
            std::cerr << "Couldn't list the plugins, error code: " << ret_for_list_plugins.code();
            return -1;
        }
        for ( const auto& plugin_name : plugin_list ) {
            std::string suffix{"_client"};
            if (boost::algorithm::ends_with(plugin_name, suffix)) {
                auto plugin_name_base = plugin_name.substr(0, plugin_name.size() - suffix.size());
                auto ep_ptr = irods::create_command_object(plugin_name_base, irods::API_EP_CLIENT);
                if (ep_ptr && !ep_ptr->provides().empty()) {
                    available_plugins.push_back(ep_ptr);
                }
            }
        }
    }

    std::map<std::string, std::vector<std::shared_ptr<irods::api_endpoint>>> command_map;
    for ( auto ep_ptr : available_plugins ) {
        for ( const auto& command : ep_ptr->provides() ) {
            command_map[command].push_back(ep_ptr);
        }
    }

    if (vm.count(help_kw)) {
        std::cout << "Usage: " << _argv[0] << " [--api_plugin=] subcommand [SUBCOMMAND OPTIONS]..." << std::endl <<
            desc << std::endl;
        for (auto ep_ptr : available_plugins) {
            if (vm.count(subcommand_kw) && !ep_ptr->provides().count(vm[subcommand_kw].as<std::string>())) {
                continue;
            }
            std::cout << "Commands for " << ep_ptr->name() << ":" << std::endl;
            for (const auto& command : ep_ptr->provides()) {
                auto& usage_and_options = ep_ptr->get_program_options_and_usage(command);
                std::cout << _argv[0] << " " << command << " " << std::get<std::string>(usage_and_options) << std::endl;
                std::cout << std::get<po::options_description>(usage_and_options) << std::endl;
            }
        }
        return 0;
    }

    if (vm.count(subcommand_kw) != 1) {
        std::cerr << "Exactly one subcommand must be specified.";
    }
    std::string subcommand = vm[subcommand_kw].as<std::string>();

    signal( SIGPIPE, SIG_IGN );

    rodsEnv myEnv;
    int status = getRodsEnv( &myEnv );
    if ( status < 0 ) {
        rodsLogError( LOG_ERROR, status, "main: getRodsEnv error. " );
        return 2;
    }

    auto& possible_endpoints = command_map[subcommand];
    if ( possible_endpoints.size() == 0) {
        std::cerr << "Invalid subcommand: \"" << subcommand << "\"" << std::endl;
        return -1;
    } else if ( possible_endpoints.size() > 1) {
        std::cerr << "Multiple libraries provide the \"" << subcommand << "\" subcommand. Specify the library to use with the --" << api_plugin_kw << " switch." << std::endl <<
            "Possible libraries:" << std::endl;
        for (auto& ep_ptr : possible_endpoints) {
            std::cerr << ep_ptr->name() << std::endl;
        }
        return -1;
    }

    try {
        auto zmq_ctx = std::make_shared<zmq::context_t>(1);
        irods::api_v5_call_client(
            myEnv.rodsHost,
            myEnv.rodsPort,
            myEnv.rodsZone,
            myEnv.rodsUserName,
            zmq_ctx,
            client_interaction,
            possible_endpoints[0],
            subcommand,
            subcommand_options);
    }
    catch(const irods::exception& _e) {
        std::cerr << _e.what() << std::endl;
        return _e.code();
    }

    return 0;
}


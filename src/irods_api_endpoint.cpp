

#include "rodsError.h"
#include "rodsClient.h"
#include "oprComplete.h"

#include "irods_api_endpoint.hpp"
#include "irods_load_plugin.hpp"
#include "irods_api_envelope.hpp"

namespace irods {

    std::shared_ptr<api_endpoint> create_command_object(
        const std::string& _endpoint_name,
        const connection_t _connection_type) {

        const std::string suffix = [_connection_type]() {
            switch (_connection_type) {
                case API_EP_CLIENT:
                    return "_client";
                case API_EP_SERVER:
                    return "_server";
                case API_EP_SERVER_TO_SERVER:
                    return "_server";
                default:
                    return "_unknown_connection_type";
            }
        }();

        api_endpoint* endpoint;
        error ret = irods::load_plugin<api_endpoint>(
                        endpoint,
                        _endpoint_name + suffix,
                        "api_v5",
                        "version_5_endpoint",
                        _connection_type);
        if(!ret.ok()) {
            THROW(ret.code(), ret.result());
        }

        return std::shared_ptr<api_endpoint>{endpoint};
    } // create_command_object

    void api_v5_call_client(
        const std::string&              _host,
        const int                       _port,
        const std::string&              _zone,
        const std::string&              _user,
        zmq::context_t&                 _zmq_ctx,
        client_fcn_t                    _cli_fcn,
        std::shared_ptr<api_endpoint>   _ep_ptr,
        const std::string&              _subcommand,
        const std::vector<std::string>& _args ) {
        // =-=-=-=-=-=-=-
        // connect to the irods server
        rErrMsg_t errMsg;
        rcComm_t *conn;
        conn = rcConnect(
                   _host.c_str(),
                   _port,
                   _user.c_str(),
                   _zone.c_str(),
                   0, &errMsg );
        if ( conn == NULL ) {
            THROW(SYS_SOCK_CONNECT_ERR, "failed to connect");
        }

        // =-=-=-=-=-=-=-
        // login using rodsEnv
        int status = clientLogin( conn );
        if ( status != 0 ) {
            rcDisconnect( conn );
            THROW(status, "clientLogin failed");
        }

        api_v5_call_client(conn, _zmq_ctx, _cli_fcn, _ep_ptr, _subcommand, _args);

    } // api_v5_call_client

    void api_v5_call_client(
        rcComm_t*                       _conn,
        zmq::context_t&                 _zmq_ctx,
        client_fcn_t                    _cli_fcn,
        std::shared_ptr<api_endpoint>   _ep_ptr,
        const std::string&              _subcommand,
        const std::vector<std::string>& _args ) {
        try {
            // =-=-=-=-=-=-=-
            // create the envelope for the given endpoint
            irods::api_envelope envelope;
            envelope.endpoint_name = _ep_ptr->name();
            envelope.payload.clear();

            // =-=-=-=-=-=-=-
            // initialize the client-side of the endpoint
            try {
                _ep_ptr->initialize(_conn, &_zmq_ctx, _subcommand, _args, envelope.payload);
            }
            catch(const irods::exception& _e) {
                std::cerr << _e.what() << std::endl;
                std::string msg = "failed to initialize with endpoint: ";
                msg += _ep_ptr->name();
                THROW(SYS_INVALID_INPUT_PARAM, msg);
            }

            envelope.connection_type = irods::API_EP_SERVER;

            auto out = avro::memoryOutputStream();
            auto enc = avro::binaryEncoder();
            enc->init( *out );
            avro::encode( *enc, envelope );
            auto data = avro::snapshot( *out );

            bytesBuf_t inp{
                .len = static_cast<int>(data->size()),
                .buf = data->data()
            };

            void *tmp_out = nullptr;
            int status = procApiRequest(
                             _conn, 5000, &inp, nullptr,
                             &tmp_out, nullptr );
            if ( status < 0 ) {
                //printErrorStack( _conn->rError );
                THROW(status, "v5 API failed");
            }
            else {
                if ( tmp_out != nullptr ) {
                    portalOprOut_t* portal = static_cast<portalOprOut_t*>( tmp_out );
                    _ep_ptr->port(portal->portList.portNum);

                    _ep_ptr->invoke();
                    _cli_fcn(_zmq_ctx, _ep_ptr->name());
                    _ep_ptr->wait();
                }
                else {
                    printf( "ERROR: the 'out' variable is null\n" );
                }
            }

        }
        catch(const irods::exception&) {
            throw;
        }

        rcOprComplete(_conn, 0);

        rcDisconnect( _conn );

    } // api_v5_call_client

    api_endpoint::api_endpoint(const connection_t _connection_type) :
        connection_type_(_connection_type),
        status_(0),
        done_flag_(false),
        port_(UNINITIALIZED_PORT) {
    }

    api_endpoint::~api_endpoint() {
        // wait for the api thread to finish
    }

    int api_endpoint::status(rError_t*) { return status_; }

    bool api_endpoint::done() { return done_flag_; }

}; // namespace irods




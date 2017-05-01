

#include "rodsError.h"
#include "rodsClient.h"
#include "oprComplete.h"

#include "irods_api_endpoint.hpp"
#include "irods_load_plugin.hpp"
#include "irods_api_envelope.hpp"

namespace irods {
    static irods::error create_client_command_object(
        const std::string&    _ep_name,
        irods::api_endpoint*& _endpoint ) {

        error ret = irods::load_plugin<api_endpoint>(
                        _endpoint,
                        _ep_name + "_client",
                        "api_v5",
                        "version_5_endpoint",
                        API_EP_CLIENT);
        if(!_endpoint || !ret.ok()) {
            return PASS(ret);
        }

        return SUCCESS();
    } // create_client_command_object

    void api_v5_call_client(
        const std::string&             _host,
        const int                      _port,
        const std::string&             _zone,
        const std::string&             _user,
        zmq::context_t&                _zmq_ctx,
        client_fcn_t                   _cli_fcn,
        const std::string&             _endpoint,
        const std::vector<std::string> _args ) {
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

        api_v5_call_client(conn, _zmq_ctx, _cli_fcn, _endpoint, _args);

    } // api_v5_call_client

    void api_v5_call_client(
        rcComm_t*                      _conn,
        zmq::context_t&                _zmq_ctx,
        client_fcn_t                   _cli_fcn,
        const std::string&             _endpoint,
        const std::vector<std::string> _args ) {
        try {
            // =-=-=-=-=-=-=-
            // create the envelope for the given endpoint
            irods::api_envelope envelope;
            envelope.endpoint = _args[1];
            envelope.payload.clear();

            // =-=-=-=-=-=-=-
            // initialize the client-side of the endpoint
            // NOTE: ep_ptr worker thread joins on dtor
            irods::api_endpoint* ep_ptr = nullptr;
            irods::error ret = create_client_command_object(
                                   envelope.endpoint,
                                   ep_ptr);
            if(!ret.ok()) {
                THROW(ret.code(), ret.result());
            }

            // =-=-=-=-=-=-=-
            // initialize the client-side of the endpoint
            try {
                ep_ptr->initialize(_conn, &_zmq_ctx, _args, envelope.payload);
            }
            catch(const irods::exception& _e) {
                std::string msg = "failed to initialize with endpoint: ";
                msg += envelope.endpoint;
                THROW(SYS_INVALID_INPUT_PARAM, msg);
            }

            auto out = avro::memoryOutputStream();
            auto enc = avro::binaryEncoder();
            enc->init( *out );
            avro::encode( *enc, envelope );
            auto data = avro::snapshot( *out );

            bytesBuf_t inp;
            memset(&inp, 0, sizeof(bytesBuf_t));
            inp.len = data->size();
            inp.buf = data->data();

            void *tmp_out = NULL;
            int status = procApiRequest(
                             _conn, 5000, &inp, NULL,
                             &tmp_out, NULL );
            if ( status < 0 ) {
                //printErrorStack( _conn->rError );
                THROW(status, "v5 API failed");
            }
            else {
                if ( tmp_out != NULL ) {
                    portalOprOut_t* portal = static_cast<portalOprOut_t*>( tmp_out );
                    ep_ptr->port(portal->portList.portNum);
                    
                    ep_ptr->invoke();
                    _cli_fcn(_zmq_ctx, _endpoint);
                    ep_ptr->wait();
                }
                else {
                    printf( "ERROR: the 'out' variable is null\n" );
                }
            }

            delete ep_ptr;
        }
        catch(const irods::exception&) {
            throw;
        }

        rcOprComplete(_conn, 0);

        rcDisconnect( _conn );

    } // api_v5_call_client

    api_endpoint::api_endpoint(const std::string& _ctx) :
        context_(_ctx),
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




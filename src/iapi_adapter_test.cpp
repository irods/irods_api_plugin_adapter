/*
 * iapitest - test pluggable apis
*/
#include "rodsClient.h"
#include "parseCommandLine.h"
#include "rodsPath.h"
#include "lsUtil.h"

#include "irods_client_api_table.hpp"
#include "irods_pack_table.hpp"
#include "irods_buffer_encryption.hpp"
#include "irods_load_plugin.hpp"

#include "irods_api_envelope.hpp"
#include "irods_api_endpoint.hpp"
#include "irods_api_plugin_adapter_test_request.hpp"

#include "zmq.hpp"

#include <string>
#include <iostream>
#include <thread>

irods::error create_command_object(
    const std::string&    _ep_name,
    irods::api_endpoint*& _endpoint ) {

    irods::error ret = irods::load_plugin<irods::api_endpoint>(
                           _endpoint,
                           _ep_name + "_client",
                           "api_v5",
                           "version_5_endpoint",
                           irods::API_EP_CLIENT);
    if(!_endpoint || !ret.ok()) {
        return PASS(ret);
    }

    return SUCCESS();
}

void client_thread_executor( irods::api_endpoint* _ep_ptr ) {
    try {
        // =-=-=-=-=-=-=-
        // start the client api thread
        try {
            _ep_ptr->invoke();
        }
        catch( const irods::exception& _e ) {
            irods::log(_e);
            throw;
        }
    }
    catch ( const zmq::error_t& _e) {
        std::cerr << _e.what() << std::endl;
    }

} // client_thread_executor


int main( int _argc, char* _argv[] ) {
    if(1 >= _argc) {
        std::cerr << "iapi_adapter_test api_v5_endpoint [...]" << std::endl;
        return 1;
    }

    // =-=-=-=-=-=-=-
    // create the envelope for the given endpoint
    irods::api_envelope envelope;
    envelope.endpoint = _argv[1];
    envelope.length = 0;
    //TODO: parameterize
    envelope.control_channel_port = 1246;
    envelope.payload.clear();

    // =-=-=-=-=-=-=-
    // initialize the client-side of the endpoint
    irods::api_endpoint* ep_ptr = nullptr;
    irods::error ret = create_command_object(
                           envelope.endpoint,
                           ep_ptr);
    if(!ret.ok()) {
        std::cout << "invalid endpoint: "
                  << envelope.endpoint
                  << std::endl;
        return ret.code();
    }

    signal( SIGPIPE, SIG_IGN );

    rodsEnv myEnv;
    int status = getRodsEnv( &myEnv );
    if ( status < 0 ) {
        rodsLogError( LOG_ERROR, status, "main: getRodsEnv error. " );
        exit( 1 );
    }

    rErrMsg_t errMsg;
    rcComm_t *conn;
    conn = rcConnect(
               myEnv.rodsHost,
               myEnv.rodsPort,
               myEnv.rodsUserName,
               myEnv.rodsZone,
               0, &errMsg );

    if ( conn == NULL ) {
        exit( 2 );
    }

    // =-=-=-=-=-=-=-
    // initialize pluggable api table
    irods::pack_entry_table& pk_tbl = irods::get_pack_table();
    irods::api_entry_table& api_tbl = irods::get_client_api_table();
    init_api_table( api_tbl, pk_tbl );

    if ( strcmp( myEnv.rodsUserName, PUBLIC_USER_NAME ) != 0 ) {
        status = clientLogin( conn );
        if ( status != 0 ) {
            rcDisconnect( conn );
            exit( 7 );
        }
    }

    // =-=-=-=-=-=-=-
    // initialize the client-side of the endpoint
    try {
        ep_ptr->initialize(_argc, _argv, envelope.payload);
    }
    catch(const irods::exception& _e) {
        std::cerr << "failed to initialize endpoint " 
                  << envelope.endpoint 
                  << std::endl;
        return 1;
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

    std::thread client_thread(client_thread_executor, ep_ptr);

    void *tmp_out = NULL;
    status = procApiRequest( conn, 5000, &inp, NULL,
                             &tmp_out, NULL );

    if ( status < 0 ) {
        printErrorStack( conn->rError );
    }
    else {
        if ( tmp_out != NULL ) {
            bytesBuf_t* out = static_cast<bytesBuf_t*>( tmp_out );
            printf( "\n\nresponse [%s]\n", out->buf );
        }
        else {
            printf( "ERROR: the 'out' variable is null\n" );
        }
    }
    
    client_thread.join();

    rcDisconnect( conn );
        
    return status;
}


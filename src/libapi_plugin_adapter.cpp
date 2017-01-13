// =-=-=-=-=-=-=-
// irods includes
#include "apiHandler.hpp"
#include "rodsPackInstruct.h"
#include "objStat.h"
#include "rcMisc.h"

#include "irods_stacktrace.hpp"
#include "irods_server_api_call.hpp"
#include "irods_re_serialization.hpp"
#include "irods_load_plugin.hpp"
#include "irods_server_properties.hpp"

#include "irods_api_envelope.hpp"
#include "irods_api_endpoint.hpp"

#include "zmq.hpp"

#include "boost/lexical_cast.hpp"

// =-=-=-=-=-=-=-
// stl includes
#include <sstream>
#include <string>
#include <iostream>

int call_api_plugin_adapter(
    irods::api_entry* _api,
    rsComm_t*         _comm,
    bytesBuf_t*       _inp,
    bytesBuf_t**      _out ) {
    return _api->call_handler<
               bytesBuf_t*,
               bytesBuf_t** >(
                   _comm,
                   _inp,
                   _out );
}

void clear_bytes_buf(void* _in) {
    if(!_in) return;
    bytesBuf_t* in = (bytesBuf_t*)_in;
    free(in->buf);
    in->len = 0;
    in->buf = 0;
}

#ifdef RODS_SERVER
    #define CALL_API_PLUGIN_ADAPTER call_api_plugin_adapter 
#else
    #define CALL_API_PLUGIN_ADAPTER NULL 
#endif

irods::error create_command_object(
    const std::string&    _ep_name,
    irods::api_endpoint*& _endpoint ) {

    irods::error ret = irods::load_plugin<irods::api_endpoint>(
                           _endpoint,
                           _ep_name + "_server",
                           "api_v5",
                           "version_5_endpoint",
                           irods::API_EP_SERVER);
    if(!_endpoint || !ret.ok()) {
        return PASS(ret);
    }

    return SUCCESS();
}

// =-=-=-=-=-=-=-
// api function to be referenced by the entry
int rs_api_plugin_adapter(
    rsComm_t*    _comm,
    bytesBuf_t*  _inp,
    bytesBuf_t** _out ) {

    try {
        // =-=-=-=-=-=-=-
        // unpack the avro api envelope 
        std::auto_ptr<avro::InputStream> in = avro::memoryInputStream(
                static_cast<const uint8_t*>( _inp->buf ),
                _inp->len );
        avro::DecoderPtr dec = avro::binaryDecoder();
        dec->init( *in );
        irods::api_envelope envelope;
        avro::decode( *dec, envelope );

        rodsLog(
            LOG_DEBUG
            "api_plugin_adapter calling endpoint [%s]",
            envelope.endpoint.c_str() );

        // =-=-=-=-=-=-=-
        // load the api_v5 plugin and get the handle
        irods::api_endpoint* ep_ptr = nullptr;
        irods::error ret = create_command_object(envelope.endpoint, ep_ptr);
        if(!ret.ok()) {
            irods::log(PASS(ret));
            return ret.code();
        }

        // =-=-=-=-=-=-=-
        // initialize the API plugin with the payload
        try {
            ep_ptr->initialize(envelope.payload);
        }
        catch(const irods::exception& _e) {
            addRErrorMsg(
                &_comm->rError,
                SYS_NULL_INPUT,
                "failed to initialize api endpoint" );
            return SYS_INVALID_INPUT_PARAM;
        }

        try {
            // =-=-=-=-=-=-=-
            // start the api thread
            try {
                ep_ptr->invoke();
            }
            catch( const irods::exception& _e ) {
                irods::log(_e);
                throw;
            }
        }
        catch( const zmq::error_t& _e ) {
            std::cerr << _e.what() << std::endl;
            return 1;
        }

        try {
            std::vector< uint8_t >* out;
            ep_ptr->finalize(out);

            if(out && !out->empty()) {
                *_out = (bytesBuf_t*)malloc(sizeof(bytesBuf_t));
                memset(*_out, 0, sizeof(bytesBuf_t));
                (*_out)->len = out->size();
                (*_out)->buf = out->data();;
            }
            else {
                delete out;
            }
        }
        catch( const irods::exception& _e ) {
            irods::log(_e);
        }

        delete ep_ptr;
    }
    catch( const avro::Exception& _e ) {

    }

    return 0;
}

extern "C" {
    // =-=-=-=-=-=-=-
    // factory function to provide instance of the plugin
    irods::api_entry* plugin_factory(
        const std::string&,     //_inst_name
        const std::string& ) { // _context
        // =-=-=-=-=-=-=-
        // create a api def object
        irods::apidef_t def = { 5000,             // api number
                                RODS_API_VERSION, // api version
                                NO_USER_AUTH,     // client auth
                                NO_USER_AUTH,     // proxy auth
                                "BytesBuf_PI", 0, // in PI / bs flag
                                "BytesBuf_PI", 0, // out PI / bs flag
                                std::function<
                                    int( rsComm_t*, bytesBuf_t*, bytesBuf_t**)>(
                                        rs_api_plugin_adapter), // operation
								"rs_api_plugin_adapter",        // operation name
                                clear_bytes_buf,  // output clear fcn
                                (funcPtr)CALL_API_PLUGIN_ADAPTER
                              };
        // =-=-=-=-=-=-=-
        // create an api object
        irods::api_entry* api = new irods::api_entry( def );

        // =-=-=-=-=-=-=-
        // assign the pack struct key and value
        api->in_pack_key   = "BytesBuf_PI";
        api->in_pack_value = BytesBuf_PI;

        api->out_pack_key   = "BytesBuf_PI";
        api->out_pack_value = BytesBuf_PI;

        return api;

    } // plugin_factory

}; // extern "C"

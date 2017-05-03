// =-=-=-=-=-=-=-
// irods includes
#include "apiHandler.hpp"
#include "rodsPackInstruct.h"
#include "objStat.h"
#include "rcMisc.h"
#include "rsApiHandler.hpp"

#include "irods_stacktrace.hpp"
#include "irods_server_api_call.hpp"
#include "irods_re_serialization.hpp"
#include "irods_server_properties.hpp"
#include "irods_random.hpp"

#include "irods_api_envelope.hpp"
#include "irods_api_endpoint.hpp"

#include "zmq.hpp"

#include "boost/lexical_cast.hpp"

// =-=-=-=-=-=-=-
// stl includes
#include <sstream>
#include <string>
#include <iostream>

void clear_portal(void* _in) {
}

#ifdef RODS_SERVER
#include "irods_load_plugin.hpp"


int call_api_plugin_adapter(
    irods::api_entry* _api,
    rsComm_t*         _comm,
    bytesBuf_t*       _inp,
    portalOprOut_t**  _out ) {
    return _api->call_handler<
               bytesBuf_t*,
               portalOprOut_t** >(
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


    #define CALL_API_PLUGIN_ADAPTER call_api_plugin_adapter 
#else
    #define CALL_API_PLUGIN_ADAPTER NULL 
#endif

#ifdef RODS_SERVER
//TODO: throw irods::exception
std::unique_ptr<irods::api_endpoint> create_command_object(
    const std::string& _ep_name,
    const std::string& _ep_type) {

    // TODO: consider server-to-server redirection
    irods::api_endpoint* ep_ptr = nullptr;
    irods::error ret = irods::load_plugin<irods::api_endpoint>(
                           ep_ptr,
                           _ep_name + "_server",
                           "api_v5",
                           "version_5_endpoint",                           
                           _ep_type);//XXXX - irods::API_EP_SERVER);
    if(!ep_ptr || !ret.ok()) {
        THROW(ret.code(), ret.result());
    }

    return std::unique_ptr<irods::api_endpoint>(ep_ptr);
}




// =-=-=-=-=-=-=-
// api function to be referenced by the entry
int rs_api_plugin_adapter(
    rsComm_t*        _comm,
    bytesBuf_t*      _inp,
    portalOprOut_t** _portal_out ) {

    _comm->portalOpr = 0;

    try {
        // =-=-=-=-=-=-=-
        // unpack the avro api envelope 
        auto in = avro::memoryInputStream(
                      static_cast<const uint8_t*>( _inp->buf ),
                      _inp->len );
        auto dec = avro::binaryDecoder();
        dec->init( *in );
        irods::api_envelope envelope;
        avro::decode( *dec, envelope );

        rodsLog(
            LOG_DEBUG,
            "api_plugin_adapter calling endpoint [%s]",
            envelope.endpoint.c_str() );

        // =-=-=-=-=-=-=-
        // TODO: wire up dynPEP rule invocation here for PRE
        // =-=-=-=-=-=-=-
        
        // =-=-=-=-=-=-=-
        // load the api_v5 plugin and get the handle
        std::unique_ptr<irods::api_endpoint> ep_ptr = create_command_object(
                                                          envelope.endpoint,
                                                          envelope.connection_type);
        // =-=-=-=-=-=-=-
        // initialize the API plugin with the payload
        zmq::context_t zmq_ctx(1); 
        ep_ptr->initialize(_comm, &zmq_ctx, std::vector<std::string>(), envelope.payload);

        // =-=-=-=-=-=-=-
        // start the api thread
        ep_ptr->invoke();

        // =-=-=-=-=-=-=-
        // capture the port bound by zmq and pack it
        // into the portalOprOut
        (*_portal_out) = (portalOprOut_t*) malloc(sizeof(portalOprOut_t));
        memset(*_portal_out, 0, sizeof(portalOprOut_t));
        (*_portal_out)->portList.portNum = ep_ptr->port();
        (*_portal_out)->portList.cookie = ( int )( irods::getRandom<unsigned int>() >> 1 );
        // TODO: FIXME
        strcpy( (*_portal_out)->portList.hostAddr, "avogadro.renci.org");

        int ret = sendAndRecvBranchMsg(
                      _comm,
                      _comm->apiInx,
                      0,
                      (void*) *_portal_out,
                      NULL);
        if(ret < 0) {
            rodsLog(
                LOG_ERROR,
                "%s - sendAndRecvBranchMsg: %d",
                __FUNCTION__,
                ret);
        }
        
        ep_ptr->wait();

        // =-=-=-=-=-=-=-
        // TODO: wire up dynPEP rule invocation here for POST?
        // =-=-=-=-=-=-=-
    }
    catch( const irods::exception& _e ) {
        //TODO: add irods exception to _out?
        irods::log(_e);
        return _e.code();
    }
    catch( const zmq::error_t& _e ) {
        //TODO: add irods exception to _out?
        irods::log(LOG_ERROR, _e.what());
        return SYS_SOCK_CONNECT_ERR;
    }

    return SYS_NO_HANDLER_REPLY_MSG;;
}

std::function<int( rsComm_t*, bytesBuf_t*, portalOprOut_t**)> plugin_op = rs_api_plugin_adapter;
#else
std::function<int( rsComm_t*, bytesBuf_t*, portalOprOut_t**)> plugin_op;
#endif

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
                                "PortalOprOut_PI", 0, // out PI / bs flag
                                plugin_op,        // operation
								"rs_api_plugin_adapter", // operation name
                                clear_portal,  // output clear fcn
                                (funcPtr)CALL_API_PLUGIN_ADAPTER
                              };
        // =-=-=-=-=-=-=-
        // create an api object
        irods::api_entry* api = new irods::api_entry( def );

        // =-=-=-=-=-=-=-
        // assign the pack struct key and value
        api->in_pack_key   = "BytesBuf_PI";
        api->in_pack_value = BytesBuf_PI;

        api->out_pack_key   = "PortalOprOut_PI";
        api->out_pack_value = PortalOprOut_PI;

        return api;

    } // plugin_factory

}; // extern "C"

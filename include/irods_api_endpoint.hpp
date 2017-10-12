#ifndef IRODS_API_ENPOINT_HPP
#define IRODS_API_ENPOINT_HPP

#include "avro/Encoder.hh"
#include "avro/Decoder.hh"
#include "avro/Specific.hh"

#include "irods_message_broker.hpp"
#include "irods_api_envelope.hpp"

#include "boost/any.hpp"
#include "boost/program_options.hpp"

#include <thread>
#include <set>

namespace irods {
    typedef std::function<int(zmq::context_t&, const std::string&)> client_fcn_t;

    static const int UNINITIALIZED_PORT = -1;

    class api_endpoint : public std::enable_shared_from_this<api_endpoint> {
    public:
        typedef std::function<void(std::shared_ptr<api_endpoint>)> thread_executor;

        api_endpoint(const connection_t _ctx);
        virtual ~api_endpoint();
        virtual int status(rError_t*);
        virtual bool done();
        virtual void init_and_serialize_payload(
                    const std::string&,
                    const std::vector<std::string>&,
                    std::vector<uint8_t>&) = 0;
        virtual void decode_and_assign_payload(const std::vector<uint8_t>&) = 0;
        virtual void capture_executors(thread_executor&,thread_executor&,thread_executor&) = 0;
        virtual std::set<std::string> provides() = 0;
        virtual const std::tuple<std::string,
                                 boost::program_options::options_description,
                                 boost::program_options::positional_options_description>&
                    get_program_options_and_usage(const std::string& command) = 0;

        int port() {
            if(UNINITIALIZED_PORT == port_) {
                std::unique_lock<std::mutex> l(mut_);
                cond_.wait(l);
                return port_;
            }
            else {
                return port_;
            }
        }

        void port( int _p ) {
            port_= _p;
            cond_.notify_one();
        }

        void done(bool _b) {
            done_flag_ = _b;
        }

        template<class T>
        void payload(T& _val) {
            try {
                _val = boost::any_cast<T>(payload_);
            }
            catch(const boost::bad_any_cast& _e) {
                THROW(SYS_INVALID_INPUT_PARAM, _e.what());
            }
        }

        zmq::context_t* ctrl_ctx() { return ctrl_ctx_; }

        template<class T>
        void comm(T& _val) {
            try {
                _val = boost::any_cast<T>(comm_);
            }
            catch(const boost::bad_any_cast& _e) {
                THROW(SYS_INVALID_INPUT_PARAM, _e.what());
            }
        }

        template<class T>
        void initialize(
            boost::any      _comm,
            zmq::context_t* _zmq_ctx,
            const T&        _payload ) {
            comm_     = _comm;
            ctrl_ctx_ = _zmq_ctx;
            payload_  = _payload;

        } // initialize

        void initialize(
            boost::any                      _comm,
            zmq::context_t*                 _zmq_ctx,
            const std::string&              _subcommand,
            const std::vector<std::string>& _args,
            std::vector<uint8_t>&           _payload ) {
            // initialize the legacy connection handle - rs/rcConn_t*
            comm_ = _comm;

            // initialize control channel zmq context pointer
            ctrl_ctx_ = _zmq_ctx;


            if(_payload.empty()) {
                // =-=-=-=-=-=-=-
                // initialize payload for client-side and transmission
                if(irods::API_EP_CLIENT == connection_type_) {
                    try {
                        init_and_serialize_payload(_subcommand, _args, _payload);
                    } catch( const avro::Exception& _e ) {
                        std::cerr << _e.what() << std::endl;
                        THROW(-1, _e.what());
                    }
                } // if client init
            }

            // =-=-=-=-=-=-=-
            // initialize the payload for server-side
            try {
                decode_and_assign_payload(_payload);
            } catch( const avro::Exception& _e ) {
                THROW(-1,_e.what());
            }
        } // initialize

        void invoke() {
            thread_executor client_exec, server_exec, svr_to_svr_exec;
            capture_executors( client_exec, server_exec, svr_to_svr_exec );

            // =-=-=-=-=-=-=-
            // start thread based on context string
            try {
                if(irods::API_EP_CLIENT == connection_type_) {
                    thread_ = std::thread{client_exec, shared_from_this()};
                }
                else if(irods::API_EP_SERVER == connection_type_) {
                    thread_ = std::thread{server_exec, shared_from_this()};
                }
                else if(irods::API_EP_SERVER_TO_SERVER == connection_type_) {
                    thread_ = std::thread{svr_to_svr_exec, shared_from_this()};
                }
                else {
                    //TODO: be very angry here
                    rodsLog(
                        LOG_ERROR,
                        "[%s]:[%d] invalid ctx [%d]",
                        __FUNCTION__,
                        __LINE__,
                        connection_type_);
                }

            } catch(const boost::bad_any_cast& _e) {
                THROW(SYS_INVALID_INPUT_PARAM, _e.what());
            }
        } // invoke

        void wait() {
            thread_.join();
        }

        const std::string& name() {
            return name_;
        }

    protected:
        zmq::context_t*  ctrl_ctx_;
        boost::any       comm_;
        boost::any       payload_;
        connection_t     connection_type_;
        std::string      name_;
        std::atomic_int  status_;
        std::atomic_bool done_flag_;

        std::mutex                   mut_;
        std::atomic_int              port_;
        std::condition_variable      cond_;
        std::thread thread_;
    }; // class api_endpoint

    std::shared_ptr<api_endpoint> create_command_object(
        const std::string& _endpoint_name,
        const connection_t _connection_type);

    void api_v5_call_client(
        const std::string&              _host,
        const int                       _port,
        const std::string&              _zone,
        const std::string&              _user,
        zmq::context_t&                 _zmq_ctx,
        client_fcn_t                    _cli_fcn,
        std::shared_ptr<api_endpoint>   _ep_ptr,
        const std::string&              _subcommand,
        const std::vector<std::string>& _args );

    void api_v5_call_client(
        rcComm_t*                       _comm,
        zmq::context_t&                 _zmq_ctx,
        client_fcn_t                    _cli_fcn,
        std::shared_ptr<api_endpoint>   _ep_ptr,
        const std::string&              _subcommand,
        const std::vector<std::string>& _args );

    template<class T>
    void api_v5_to_v5_call_client(
        rcComm_t*                            _conn,
        std::shared_ptr<irods::api_endpoint> _ep_ptr,
        zmq::context_t*                      _zmq_ctx,
        const T&                             _payload ) {
        // =-=-=-=-=-=-=-
        // initialize the client-side of the endpoint
        try {
            _ep_ptr->initialize<T>(_conn, _zmq_ctx, _payload);
        }
        catch(const irods::exception& _e) {
            std::string msg = "failed to initialize with endpoint: ";
            msg += _ep_ptr->name();
            THROW(SYS_INVALID_INPUT_PARAM, msg);
        }

        try {
            // portalOprOut_t* portal = static_cast<portalOprOut_t*>( tmp_out );
            // _ep_ptr->port(portal->portList.portNum);
            _ep_ptr->invoke();
        }
        catch(const irods::exception&) {
            throw;
        }

    } // api_v5_to_v5_call_client

    template<class T>
    void api_v5_to_v5_call_server(
            rsComm_t*                            _conn,
            std::shared_ptr<irods::api_endpoint> _ep_ptr,
            zmq::context_t*                      _zmq_ctx,
            const T&                             _payload ) {

        try {
            _ep_ptr->initialize<T>(_conn, _zmq_ctx, _payload);
        }
        catch(const irods::exception& _e) {
            std::string msg = "failed to initialize with endpoint: ";
            msg += _ep_ptr->name();
            THROW(SYS_INVALID_INPUT_PARAM, msg);
        }

        _ep_ptr->invoke();

    } // api_v5_to_v5_call_server

}; // namespace irods

#endif // IRODS_API_ENPOINT_HPP




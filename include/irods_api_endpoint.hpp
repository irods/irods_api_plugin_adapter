


#ifndef IRODS_API_ENPOINT_HPP
#define IRODS_API_ENPOINT_HPP

#include "avro/Encoder.hh"
#include "avro/Decoder.hh"
#include "avro/Specific.hh"

#include "irods_message_broker.hpp"

#include "boost/any.hpp"
#include "boost/shared_ptr.hpp"

#include <thread>

namespace irods {
    typedef std::function<int(zmq::context_t&)> client_fcn_t;
    static const std::string API_EP_CLIENT("api_endpoint_client");
    static const std::string API_EP_SERVER("api_endpoint_server");
    static const std::string API_EP_SVR_TO_SVR("api_endpoint_svr_to_svr");

    static const int UNINITIALIZED_PORT = -1;

    void api_v5_call_client(
        const std::string&             _host,
        const int                      _port,
        const std::string&             _zone,
        const std::string&             _user,
        zmq::context_t&                _zmq_ctx,
        client_fcn_t                   _cli_fcn,
        const std::string&             _endpoint,
        const std::vector<std::string> _args );

    void api_v5_call_client(
        rcComm_t*                      _comm,
        zmq::context_t&                _zmq_ctx,
        client_fcn_t                   _cli_fcn,
        const std::string&             _endpoint,
        const std::vector<std::string> _args );

    class api_endpoint {
    public:
        typedef std::function<void(api_endpoint*)> thread_executor;

        api_endpoint(const std::string& _ctx);
        virtual ~api_endpoint();
        virtual int status(rError_t*);
        virtual bool done();
        virtual void init_and_serialize_payload(
                    const std::vector<std::string>&,
                    std::vector<uint8_t>&) = 0;
        virtual void decode_and_assign_payload(const std::vector<uint8_t>&) = 0;
        virtual void capture_executors(thread_executor&,thread_executor&,thread_executor&) = 0;

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

        void initialize(
            boost::any                      _comm,
            zmq::context_t*                 _zmq_ctx,
            const std::vector<std::string>& _args,
            std::vector<uint8_t>&           _payload ) {
            // initialize the legacy connection handle - rs/rcConn_t*
            comm_ = _comm;

            // initialize control channel zmq context pointer
            ctrl_ctx_ = _zmq_ctx;


            if(_payload.empty()) {
                // =-=-=-=-=-=-=-
                // initialize payload for client-side and transmission
                if(irods::API_EP_CLIENT == context_) {
                    try {
                        init_and_serialize_payload(_args, _payload);
                    } catch( const avro::Exception& _e ) {
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
                if(irods::API_EP_CLIENT == context_) {
                    thread_ = std::make_unique<std::thread>(client_exec, this);
                }
                else if(irods::API_EP_SERVER == context_) {
                    thread_ = std::make_unique<std::thread>(server_exec, this);
                }
                else if(irods::API_EP_SVR_TO_SVR == context_) {
                    thread_ = std::make_unique<std::thread>(svr_to_svr_exec, this);
                }
                else {
                    //TODO: be very angry here
                    rodsLog(
                        LOG_ERROR,
                        "[%s]:[%d] invalid ctx [%s]",
                        __FUNCTION__,
                        __LINE__,
                        context_.c_str());
                }

            } catch(const boost::bad_any_cast& _e) {
                THROW(SYS_INVALID_INPUT_PARAM, _e.what());
            }
        } // invoke

        void wait() {
            thread_->join();
        }

    protected:
        zmq::context_t*  ctrl_ctx_;
        boost::any       comm_;
        boost::any       payload_;
        std::string      context_;
        std::atomic_int  status_;
        std::atomic_bool done_flag_;
        
        std::mutex                   mut_;
        std::atomic_int              port_;
        std::condition_variable      cond_;
        std::unique_ptr<std::thread> thread_;
    }; // class api_endpoint

}; // namespace irods

#endif // IRODS_API_ENPOINT_HPP




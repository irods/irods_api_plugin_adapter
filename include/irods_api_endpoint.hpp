


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

    static const std::string API_EP_CLIENT("api_endpoint_client");
    static const std::string API_EP_SERVER("api_endpoint_server");
    static const std::string API_EP_SVR_TO_SVR("api_endpoint_svr_to_svr");

    static const int UNINITIALIZED_PORT = -1;
    class api_endpoint {
    public:
        typedef std::function<void(api_endpoint*)> thread_executor;

        api_endpoint(const std::string& _ctx);
        virtual ~api_endpoint();
        virtual int status(rError_t*);
        virtual bool done();
        virtual void init_and_serialize_payload(int, char*[], std::vector<uint8_t>&) = 0;
        virtual void decode_and_assign_payload(const std::vector<uint8_t>&) = 0;
        virtual void capture_executors(thread_executor&,thread_executor&,thread_executor&) = 0;

        int port_for_bind() {
            if(UNINITIALIZED_PORT == port_for_bind_) {
                std::unique_lock<std::mutex> l(mut_);
                cond_.wait(l);
                return port_for_bind_;
            }
            else {
                return port_for_bind_;
            }
        }

        void port_for_bind( int _p ) {
            port_for_bind_ = _p;
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
                std::cerr << _e.what() << std::endl;
                throw;
            }
        }

        void initialize(
            const int             _argc,
            char*                 _argv[],
            std::vector<uint8_t>& _payload) {
            if(_payload.empty()) {
                // =-=-=-=-=-=-=-
                // initialize payload for client-side and transmission
                if(irods::API_EP_CLIENT == context_) {
                    try {
                        init_and_serialize_payload(_argc, _argv, _payload);
                    } catch( const avro::Exception& _e ) {
                        rodsLog(
                            LOG_ERROR,
                            "[%s]:[%d] exception caught [%s]",
                            __FUNCTION__,
                            __LINE__,
                            _e.what());
                        throw;
                    }
                } // if client init

                return;
            }

            // =-=-=-=-=-=-=-
            // initialize the payload for server-side
            try {
                decode_and_assign_payload(_payload);
            } catch( const avro::Exception& _e ) {
                rodsLog(
                    LOG_ERROR,
                    "[%s]:[%d] exception caught [%s]",
                    __FUNCTION__,
                    __LINE__,
                    _e.what());
                throw;
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

            } catch( const boost::bad_any_cast& ) {
                rodsLog(
                    LOG_ERROR,
                    "[%s]:[%d] exception caught - bad any cast",
                    __FUNCTION__,
                    __LINE__);
                throw;
            }
        } // invoke

    protected:
        std::string      context_;
        boost::any       payload_;
        std::atomic_bool done_flag_;
        std::atomic_int  status_;
        std::atomic_int  port_for_bind_;
        std::unique_ptr<std::thread> thread_;

        std::condition_variable cond_;
        std::mutex              mut_;
    }; // class api_endpoint

}; // namespace irods

#endif // IRODS_API_ENPOINT_HPP




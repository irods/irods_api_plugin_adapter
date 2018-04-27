#ifndef IRODS_API_ENPOINT_HPP
#define IRODS_API_ENPOINT_HPP

#include "avro/Encoder.hh"
#include "avro/Decoder.hh"
#include "avro/Specific.hh"

#include "irods_message_broker.hpp"
#include "irods_api_envelope.hpp"
#include "irods_load_plugin.hpp"

#include "boost/any.hpp"
#include "boost/program_options.hpp"

#include <cstddef>
#include <thread>
#include <set>

namespace irods {
    typedef std::function<int(zmq::context_t&, const std::string&)> client_fcn_t;

    static const int UNINITIALIZED_PORT = -1;

    class api_endpoint : public std::enable_shared_from_this<api_endpoint> {
    public:
        typedef std::function<void(std::shared_ptr<api_endpoint>)> thread_executor;

        api_endpoint(const connection_t _ctx);
        virtual ~api_endpoint() {}
        virtual int status(rError_t*) const;
        virtual bool done() const;
        virtual std::vector<uint8_t> get_request_as_bytes() const = 0;
        virtual std::vector<uint8_t> get_response_as_bytes() const = 0;
        virtual std::vector<uint8_t> get_context_as_bytes() const = 0;
        virtual void set_request_from_bytes(const std::vector<uint8_t>&) = 0;
        virtual void set_response_from_bytes(const std::vector<uint8_t>&) = 0;
        virtual void set_context_from_bytes(const std::vector<uint8_t>&) = 0;
        virtual void setup_from_api_request(const std::vector<uint8_t>&) = 0;
        virtual void capture_executors(thread_executor&,thread_executor&,thread_executor&) = 0;
        virtual const std::set<std::string>& provides() const = 0;
        virtual const std::tuple<std::string,
                                 boost::program_options::options_description,
                                 boost::program_options::positional_options_description>&
                    get_program_options_and_usage(const std::string& command) const = 0;

        virtual void initialize_from_command(
                const std::string&,
                const std::vector<std::string>&) = 0;

        virtual void initialize_from_context(
                const std::vector<uint8_t>&) = 0;

        int port() {
            if(UNINITIALIZED_PORT == port_) {
                std::unique_lock<std::mutex> l(mut_);
                //random wake-ups? move to future/promises
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

        zmq::context_t* ctrl_ctx() { return ctrl_ctx_; }

        void ctrl_ctx(zmq::context_t* _ctrl_ctx) {
            ctrl_ctx_ = _ctrl_ctx;
        }

        template<class T>
        T& comm() {
            try {
                return boost::any_cast<T&>(comm_);
            }
            catch(const boost::bad_any_cast& _e) {
                THROW(SYS_INVALID_INPUT_PARAM, _e.what());
            }
        }

        template<class T>
        void comm(T& _comm) {
                comm_ = _comm;
        }

        void invoke() {
            thread_executor client_exec, server_exec, svr_to_svr_exec;
            capture_executors( client_exec, server_exec, svr_to_svr_exec );

            // =-=-=-=-=-=-=-
            // start thread based on context string
            try {
                //switch/case statement
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

        const std::string& name() const {
            return name_;
        }

    protected:

        zmq::context_t*  ctrl_ctx_;
        boost::any       comm_;
        connection_t     connection_type_;
        std::string      name_;
        std::atomic_int  status_;
        std::atomic_bool done_flag_;

        std::mutex                   mut_;
        std::atomic_int              port_;
        std::condition_variable      cond_;
        std::thread thread_;
    }; // class api_endpoint

    template<typename T>
    std::vector<uint8_t> convert_to_bytes(const T& _to_convert) {
        auto out = avro::memoryOutputStream();
        auto enc = avro::binaryEncoder();
        enc->init( *out );
        avro::encode( *enc, _to_convert );
        auto data = avro::snapshot( *out );

        return *data;
    }

    template<>
    std::vector<uint8_t> convert_to_bytes<std::nullptr_t>(const std::nullptr_t&) {
        return {};
    }

    template<typename T>
    void assign_from_bytes(
            const std::vector<uint8_t>& _bytes,
            T& _to_assign) {
        auto in = avro::memoryInputStream(
                &_bytes[0],
                _bytes.size());
        auto dec = avro::binaryDecoder();
        dec->init( *in );
        avro::decode( *dec, _to_assign );
    }

    template<>
    void assign_from_bytes<std::nullptr_t>(
            const std::vector<uint8_t>& _bytes,
            std::nullptr_t&) {
    }

    template<typename ... Ts>
    std::shared_ptr<api_endpoint> create_command_object(
        const std::string& _endpoint_name,
        const connection_t _connection_type,
        const Ts& ... _args ) {

        const std::string suffix = [_connection_type]() {
            switch (_connection_type) {
                case API_EP_CLIENT:
                    return "_client";
                case API_EP_SERVER:
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
                        _connection_type,
                        _args...);
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
        const std::vector<std::string>& _args );

    void api_v5_call_client(
        rcComm_t*                       _comm,
        zmq::context_t&                 _zmq_ctx,
        client_fcn_t                    _cli_fcn,
        std::shared_ptr<api_endpoint>   _ep_ptr,
        const std::string&              _subcommand,
        const std::vector<std::string>& _args );

    template<typename CommType>
    void api_v5_to_v5_call_endpoint(
        CommType*                            _conn,
        std::shared_ptr<irods::api_endpoint> _ep_ptr,
        zmq::context_t*                      _zmq_ctx) {
        // =-=-=-=-=-=-=-
        // initialize the client-side of the endpoint
        _ep_ptr->comm(_conn);
        _ep_ptr->ctrl_ctx(_zmq_ctx);

        try {
            // portalOprOut_t* portal = static_cast<portalOprOut_t*>( tmp_out );
            // _ep_ptr->port(portal->portList.portNum);
            _ep_ptr->invoke();
        }
        catch(const irods::exception&) {
            throw;
        }

    } // api_v5_to_v5_call_endpoint

    template<typename RequestType>
    class with_request : protected virtual api_endpoint {

        public:
            using request_t = RequestType;

            const RequestType&
                request() const {
                    return request_;
                }

            RequestType&
                request() {
                    return request_;
                }

            void
                request(
                        const RequestType& _request) {
                    request_ = _request;
                }

            std::vector<uint8_t> get_request_as_bytes() const {
                return convert_to_bytes(request());
            }

            // =-=-=-=-=-=-=-
            // used for server-side initialization
            void
                set_request_from_bytes(
                        const std::vector<uint8_t>& _in) {
                    assign_from_bytes(_in, request());
                }
        private:
            RequestType request_;
    };

    class without_request : public virtual with_request<std::nullptr_t> {
    };

    template<typename ResponseType>
    class with_response : protected virtual api_endpoint {
        public:
            using response_t = ResponseType;

            const ResponseType&
                response() const {
                    return response_;
                }

            ResponseType&
                response() {
                    return response_;
                }

            void
                response(
                        const ResponseType& _response) {
                    response_ = _response;
                }

            std::vector<uint8_t> get_response_as_bytes() const {
                return convert_to_bytes(response());
            }

            void set_response_from_bytes(
                    const std::vector<uint8_t>& _in) {
                assign_from_bytes(_in, response());
            }


        private:
            ResponseType response_;

    };

    class without_response : public virtual with_response<std::nullptr_t> {
    };

    template<typename ContextType>
    class with_context : protected virtual api_endpoint {
        public:
            using context_t = ContextType;

            ContextType&
                context() {
                    return context_;
                }

            const ContextType&
                context() const {
                    return context_;
                }

            void
                context(
                        const ContextType& _context) {
                    context_ = _context;
                }

            std::vector<uint8_t>
                get_context_as_bytes() const {
                    return convert_to_bytes(context());
                }

            void
                set_context_from_bytes(
                        const std::vector<uint8_t>& _in) {
                    assign_from_bytes(_in, context());
                }

        private:
            ContextType context_;
    };

    class without_context_initialization : protected virtual api_endpoint {

        const std::string context_initialization_disabled_message{"This plugin does not support initialization from context"};
        public:

        void
            initialize_from_context(
                    const std::vector<uint8_t>&) {
                THROW(SYS_NOT_SUPPORTED, context_initialization_disabled_message);
            }
    };

    class without_context : public virtual with_context<std::nullptr_t>,
                            public virtual without_context_initialization {
    };

    class with_cli_disabled : protected virtual api_endpoint {
        const std::string commands_disabled_message{"This plugin does not support commands"};
        public:

        const std::set<std::string>& provides() const {
            const static std::set<std::string> none_provided{};
            return none_provided;
        }
        const std::tuple<std::string,
              boost::program_options::options_description,
              boost::program_options::positional_options_description>&
                  get_program_options_and_usage(const std::string& command) const {
                      THROW(SYS_NOT_SUPPORTED, commands_disabled_message);
                  }

        void initialize_from_command(
                const std::string&,
                const std::vector<std::string>&) {
            THROW(SYS_NOT_SUPPORTED, commands_disabled_message);
        }

    };

    class with_api_request_as_request : protected virtual api_endpoint {
        public:

        void setup_from_api_request(const std::vector<uint8_t>& _api_request_bytes) {
            set_request_from_bytes(_api_request_bytes);
        }
    };

    class with_api_request_as_context : protected virtual api_endpoint {
        public:

        void setup_from_api_request(const std::vector<uint8_t>& _api_request_bytes) {
            set_context_from_bytes(_api_request_bytes);
        }
    };

    class without_api_request : protected virtual api_endpoint {
        public:

        void setup_from_api_request(const std::vector<uint8_t>&) {
        }
    };

}; // namespace irods

#endif // IRODS_API_ENPOINT_HPP

#ifndef IRODS_MESSAGE_QUEUE_HPP
#define IRODS_MESSAGE_QUEUE_HPP

#include <vector>
#include <iostream>
#include <unistd.h>

#include "zmq.hpp"

#include "irods_server_properties.hpp"
#include "irods_log.hpp"

namespace irods {

    enum zmq_type{ RESPONSE, REQUEST };

    struct broker_settings {
        int timeout = -1;
        int retries = 0;
    };

    class message_broker {
    public:
        typedef std::vector<uint8_t> data_type;

        message_broker(const zmq_type _type, const broker_settings& _settings, std::shared_ptr<zmq::context_t> _zmq_ctx_ptr) :
            ctx_ptr_{_zmq_ctx_ptr}, settings_{_settings} {
            try {
                create_socket(_type);
                zmq_setsockopt(ctx_ptr_.get(), ZMQ_RCVTIMEO, &_settings.timeout, sizeof(_settings.timeout));
                zmq_setsockopt(ctx_ptr_.get(), ZMQ_SNDTIMEO, &_settings.timeout, sizeof(_settings.timeout));
            }
            catch ( const zmq::error_t& _e) {
                THROW(INVALID_OPERATION, _e.what());
            }
        }

        message_broker(const zmq_type _type, const broker_settings& _settings) :
            ctx_ptr_{std::make_shared<zmq::context_t>(1)}, settings_{_settings} {
            try {
                create_socket(_type);
                zmq_setsockopt(ctx_ptr_.get(), ZMQ_RCVTIMEO, &_settings.timeout, sizeof(_settings.timeout));
                zmq_setsockopt(ctx_ptr_.get(), ZMQ_SNDTIMEO, &_settings.timeout, sizeof(_settings.timeout));
            }
            catch ( const zmq::error_t& _e) {
                THROW(INVALID_OPERATION, _e.what());
            }
        }

        ~message_broker() {
            skt_ptr_->close();
        }

        template<typename T>
        void send(const T& _data) const;

        void send(const char* _data) const;

        template <typename T = data_type>
        T receive(const int flags=0, const bool debug=false) const;

        void connect(const std::string& _conn) {
            try {
                skt_ptr_->connect(_conn);
            }
            catch(const zmq::error_t& _e) {
                THROW(SYS_SOCK_CONNECT_ERR, _e.what());
            }
        }

        void bind(const std::string& _conn) {
            try {
                skt_ptr_->bind(_conn);
            }
            catch(const zmq::error_t& _e) {
                THROW(SYS_SOCK_CONNECT_ERR, _e.what());
            }
        }

        void bind_to_port(size_t _port) {
            try {
                std::stringstream csstr;
                csstr << "tcp://*:" << _port;
                skt_ptr_->bind(csstr.str().c_str());
            }
            catch(const zmq::error_t& _e) {
                THROW(SYS_SOCK_CONNECT_ERR, _e.what());
            }

        } // bind_to_open_port

        int bind_to_port_in_range(size_t _first, size_t _last) {
            for(auto port = _first; port < _last; ++port) {
                try {
                    bind_to_port(port);
                    return port;
                }
                catch(irods::exception& _e) {
                    continue;
                }
            } // for

            // did not find a socket in range
            std::stringstream ss;
            ss << "failed to find point in range "
               << _first << " to " << _last;
            THROW(SYS_SOCK_CONNECT_ERR, ss.str());

        } // bind_to_open_port


    private:
        void create_socket(const zmq_type _type) {
            try {
                int time_out = 1500;
#if 0
                try {
                    // TODO: need a new parameter
                    time_out = irods::get_server_property<const int>(
                            irods::CFG_SERVER_CONTROL_PLANE_TIMEOUT);
                } catch ( const irods::exception& _e ) {
                    irods::log(_e);
                    return;
                }
#endif
                switch (_type) {
                    case zmq_type::REQUEST: {
                        skt_ptr_ = std::unique_ptr<zmq::socket_t>(
                                    std::make_unique<zmq::socket_t>(
                                        *ctx_ptr_, ZMQ_REQ));
                        break;
                    }
                    case zmq_type::RESPONSE: {
                        skt_ptr_ = std::unique_ptr<zmq::socket_t>(
                                    std::make_unique<zmq::socket_t>(
                                        *ctx_ptr_, ZMQ_REP));
                        break;
                    }
                }

                skt_ptr_->setsockopt( ZMQ_RCVTIMEO, &time_out, sizeof( time_out ) );
                skt_ptr_->setsockopt( ZMQ_SNDTIMEO, &time_out, sizeof( time_out ) );
                skt_ptr_->setsockopt( ZMQ_LINGER, 0 );
            }
            catch ( const zmq::error_t& _e) {
                THROW(INVALID_OPERATION, _e.what());
            }
        }

        void send_zmq(zmq::message_t& _data) const {
            try {
                while(!skt_ptr_->send( _data ) ) {
                    //TODO: need backoff
                        continue;
                }
            }
            catch ( const zmq::error_t& _e) {
                std::cerr << _e.what() << std::endl;
            }
        }

        zmq::message_t receive_zmq(const int flags, const bool debug) const {
            try {
                zmq::message_t msg{};
                int retries = 0;
                while(true) {
                    int ret = skt_ptr_->recv( &msg, flags );
                    if(-1 == ret && ZMQ_DONTWAIT == flags) {
                        if(debug) {
                            std::cout << "dontwait failed in receive" << std::endl; fflush(stdout);
                        }
                        if(zmq_errno() == EAGAIN) {
                            if(debug) {
                                std::cout << "dontwait with EAGAIN" << std::endl; fflush(stdout);
                            }
                            break;
                        }
                    }
                    else if(ret <= 0 && ZMQ_DONTWAIT != flags) {
                        int eno = zmq_errno();
                        //std::cout << "read error :: ret - " << ret << "    errno - " << eno << std::endl;
                        if(EAGAIN == ret || EAGAIN == eno) {
                            retries++;
                            if (retries > settings_.retries) {
                                THROW(SYS_SOCK_READ_ERR, boost::format("read error on recv. Connection timed out and maximum retry count exceeded."));
                            }
                        } else {
                            THROW(SYS_SOCK_READ_ERR, boost::format("read error on recv. return code: %d, errno: %d") % ret % eno);
                        }

                        //TODO: need backoff
                        continue;
                    }
                    break;
                }
                return msg;
            }
            catch ( const zmq::error_t& _e) {
                std::cerr << _e.what() << std::endl;
            }
            return {};
        }

        std::shared_ptr<zmq::context_t> ctx_ptr_;
        std::unique_ptr<zmq::socket_t> skt_ptr_;
        const broker_settings settings_;

    }; // class message_broker

    template<typename T>
    void message_broker::send(const T& _data) const {
        auto out = avro::memoryOutputStream();
        auto enc = avro::binaryEncoder();
        enc->init( *out );
        avro::encode( *enc, _data );
        const auto encoded_data = avro::snapshot( *out );

        send(*encoded_data);
    }

    template<>
    void message_broker::send(const data_type& _data) const {
        zmq::message_t msg( _data.size() );
        memcpy(
            msg.data(),
            _data.data(),
            _data.size() );
        send_zmq(msg);
    }

    template<>
    void message_broker::send(const std::string& _data) const {
        zmq::message_t msg( _data.size() );
        memcpy(
            msg.data(),
            _data.data(),
            _data.size() );
        send_zmq(msg);
    }

    void message_broker::send(const char* _data) const {
        std::size_t size = std::strlen(_data);
        zmq::message_t msg( size );
        memcpy(
            msg.data(),
            _data,
            size);
        send_zmq(msg);
    }

    template <typename T>
    T message_broker::receive(const int flags, const bool debug) const {
        const auto rcv_msg = receive<data_type>(flags, debug);
        auto in = avro::memoryInputStream(
                    &rcv_msg[0],
                    rcv_msg.size());
        auto dec = avro::binaryDecoder();
        dec->init( *in );
        T value{};
        avro::decode( *dec, value );
        return value;
    }

    template <>
    std::string message_broker::receive(const int flags, const bool debug) const {
        const auto msg = receive_zmq(flags, debug);
        if(msg.size() > 0) {
            std::string str{};
            const char* msg_data = static_cast<const char*>(msg.data());
            str.assign(msg_data, msg_data + msg.size());
            return str;
        }
        return {};
    }

    template <>
    message_broker::data_type message_broker::receive(const int flags, const bool debug) const {
        const auto msg = receive_zmq(flags, debug);
        if(msg.size() > 0) {
            message_broker::data_type data{};
            data.resize(msg.size());
            std::memcpy(data.data(), msg.data(), msg.size());
            return data;
        }
        return {};
    }

}; // namespace irods

std::ostream& operator<<(
    std::ostream& _os,
    const irods::message_broker::data_type& _dt) {
    std::string msg;
    msg.assign(_dt.begin(), _dt.end());
        _os << msg;
        return _os;
} // operator<<

#endif // IRODS_MESSAGE_QUEUE_HPP




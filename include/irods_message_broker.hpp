


#ifndef IRODS_MESSAGE_QUEUE_HPP
#define IRODS_MESSAGE_QUEUE_HPP

#include <vector>
#include "zmq.hpp"

#include "irods_server_properties.hpp"
#include "irods_log.hpp"

namespace irods {

    class message_broker {
    public:
        typedef std::vector<uint8_t> data_type;

        message_broker(const std::string& _ctx) : zmq_ctx_(1) {
            try {
                create_socket(_ctx);
            }
            catch ( const zmq::error_t& _e) {
                THROW(INVALID_OPERATION, _e.what());
            }
        }

        ~message_broker() {
            skt_ptr_->close();
        }

        void send(const data_type& _data) {
            try {
                zmq::message_t msg( _data.size() );
                memcpy(
                    msg.data(),
                    _data.data(),
                    _data.size() );
                while(!skt_ptr_->send( msg ) ) {
                    //TODO: need backoff
                        continue;
                }
            }
            catch ( const zmq::error_t& _e) {
                std::cerr << _e.what() << std::endl;
            }
        }

        void recieve(data_type& _data) {
            try {
                zmq::message_t msg;
                while(true) {
                    skt_ptr_->recv( &msg );
                    if(msg.size()<= 0) {
                    //TODO: need backoff
                        continue;
                    }
                    break;
                }
                _data.resize(msg.size());
                std::memcpy(_data.data(), msg.data(), msg.size());

            }
            catch ( const zmq::error_t& _e) {
                std::cerr << _e.what() << std::endl;
            }
        }

        void connect(const std::string& _conn) {
            try {
                skt_ptr_->connect(_conn);
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
        void create_socket(const std::string _ctx) {
            try {
                if("ZMQ_REQ" == _ctx ) {
                    skt_ptr_ = std::unique_ptr<zmq::socket_t>(
                            new zmq::socket_t(zmq_ctx_, ZMQ_REQ));
                }
                else {
                    int time_out = 0;
                    try {
                        // TODO: need a new parameter
                        time_out = irods::get_server_property<const int>(
                                irods::CFG_SERVER_CONTROL_PLANE_TIMEOUT);
                    } catch ( const irods::exception& _e ) {
                        irods::log(_e);
                        return;
                    }

                    skt_ptr_ = std::unique_ptr<zmq::socket_t>(
                            new zmq::socket_t(zmq_ctx_, ZMQ_REP));
                    skt_ptr_->setsockopt( ZMQ_RCVTIMEO, &time_out, sizeof( time_out ) );
                    skt_ptr_->setsockopt( ZMQ_SNDTIMEO, &time_out, sizeof( time_out ) );
                    skt_ptr_->setsockopt( ZMQ_LINGER, 0 );
                }
            }
            catch ( const zmq::error_t& _e) {
                THROW(INVALID_OPERATION, _e.what());
            }
        }

        zmq::context_t                 zmq_ctx_;
        std::unique_ptr<zmq::socket_t> skt_ptr_;

    }; // class message_broker

}; // namespace irods


#endif // IRODS_MESSAGE_QUEUE_HPP




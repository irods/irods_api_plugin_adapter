


#ifndef IRODS_MESSAGE_QUEUE_HPP
#define IRODS_MESSAGE_QUEUE_HPP

#include <vector>
#include <thread>
#include "zmq.hpp"

#include "irods_server_properties.hpp"
#include "irods_log.hpp"

namespace irods {

    class message_broker {
    public:
        typedef std::vector<uint8_t> data_type;

        message_broker(const std::string& _conn, const std::string _ctx) : zmq_ctx_(1) {
            try {
                if("ZMQ_REQ" == _ctx ) {
                    skt_ptr_ = std::unique_ptr<zmq::socket_t>(
                                   new zmq::socket_t(zmq_ctx_, ZMQ_REQ));
                    skt_ptr_->connect( _conn.c_str() );
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
                    skt_ptr_->bind( _conn.c_str() );
                }
            }
            catch ( const zmq::error_t& _e) {
                std::cerr << _e.what() << std::endl;
                throw;
            }
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

    private:
        zmq::context_t                 zmq_ctx_;
        std::unique_ptr<zmq::socket_t> skt_ptr_;

    }; // class message_broker

}; // namespace irods


#endif // IRODS_MESSAGE_QUEUE_HPP




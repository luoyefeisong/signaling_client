#include <string>
#include <boost/asio.hpp>

#include "signaling_client.h"

using namespace boost::asio;
namespace gwecom {
	namespace network {
    SignalingClient::SignalingClient(io_service& io_service):io_service_(io_service),
                                                             control_socket_(io_service),
                                                             hanging_get_(io_service) {
      printf("start\n");
    }

    SignalingClient::~SignalingClient() {;}

    void SignalingClient::Connect(const std::string& server,
                                  int port,
                                  const std::string& client_name) {
      if (server.empty() || client_name.empty()) {
        printf("Failed to connect server %s ,client name %s", server.c_str(), client_name.c_str());
        return;
      }

      if (port <= 0)
        port = kDefaultServerPort;
      
      server_ip_ = server;
      server_port_ = port;
      client_name_ = client_name;

      tcp::resolver resolver(io_service_);
			ep_iter_ = resolver.resolve({ ip::address::from_string(server_ip_), 
                                                  server_port_ });                     
      DoConnect();
      return;
    }

    void SignalingClient::DoConnect() {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "GET /sign_in?%s HTTP/1.0\r\n\r\n",
              client_name_.c_str());
      onconnect_data_ = buffer;

      bool ret = ConnectControlSocket();
      if (ret)
        state_ = SIGNING_IN;
    }

    void SignalingClient::OnConnect() {
      control_socket_.send(onconnect_data_.c_str(), onconnect_data_.length());
      onconnect_data_.clear();

      return;
    }

    void SignalingClient::OnHangingGetConnect() {
      char buffer[1024];
      snprintf(buffer, sizeof(buffer), "GET /wait?peer_id=%i HTTP/1.0\r\n\r\n",
              my_id_);
      int len = static_cast<int>(strlen(buffer));
      hanging_get_.async_send(buffer, len);
    }

    bool SignalingClient::ConnectControlSocket() {
  
      try {
        async_connect(control_socket_, ep_iter_, &SignalingClient::OnConnect);
      }
      catch (boost::system::system_error& e) {
        printf("%s\n", e.what());
        return false;
      }
      return true;
    }

    bool SignalingClient::SendToPeer(int peer_id, const std::string& message) {
      if (state_ != CONNECTED)
        return false;
      
      if (control_socket_.available() <= 0)
        return false;

      if (!is_connected() || peer_id == -1)
        return false;

      char headers[1024];
      snprintf(headers, sizeof(headers),
              "POST /message?peer_id=%i&to=%i HTTP/1.0\r\n"
              "Content-Length: %zu\r\n"
              "Content-Type: text/plain\r\n"
              "\r\n",
              my_id_, peer_id, message.length());
      onconnect_data_ = headers;
      onconnect_data_ += message;
      return ConnectControlSocket();
    }

    bool SignalingClient::SendHangUp(int peer_id) {
      return SendToPeer(peer_id, kByeMessage);
    }

    void SignalingClient::OnRead() {
      size_t content_length = 0;
      if (ReadIntoBuffer(control_socket_, &control_data_, &content_length)) {
        size_t peer_id = 0, eoh = 0;
        bool ok =
            ParseServerResponse(control_data_, content_length, &peer_id, &eoh);
        if (ok) {
          if (my_id_ == -1) {
            // First response.  Let's store our server assigned ID.
            VOID_RETURN_CHECK(state_ == SIGNING_IN);
            my_id_ = static_cast<int>(peer_id);
            VOID_RETURN_CHECK(my_id_ != -1);

            // The body of the response will be a list of already connected peers.
            if (content_length) {
              size_t pos = eoh + 4;
              while (pos < control_data_.size()) {
                size_t eol = control_data_.find('\n', pos);
                if (eol == std::string::npos)
                  break;
                int id = 0;
                std::string name;
                bool connected;
                if (ParseEntry(control_data_.substr(pos, eol - pos), &name, &id,
                              &connected) &&
                    id != my_id_) {
                  peers_[id] = name;
                }
                pos = eol + 1;
              }
            }
            VOID_RETURN_CHECK(is_connected());
          } else if (state_ == SIGNING_OUT) {
            Close();
          } else if (state_ == SIGNING_OUT_WAITING) {
            SignOut();
          }
        }

        control_data_.clear();

        if (state_ == SIGNING_IN) {
          VOID_RETURN_CHECK(hanging_get_.available());
          state_ = CONNECTED;
          async_connect(hanging_get_, ep_iter_, &SignalingClient::OnHangingGetRead);
        }
      }
    }

    void SignalingClient::OnHangingGetRead() {
      size_t content_length = 0;
      if (ReadIntoBuffer(hanging_get_, &notification_data_, &content_length)) {
        size_t peer_id = 0, eoh = 0;
        bool ok =
            ParseServerResponse(notification_data_, content_length, &peer_id, &eoh);

        if (ok) {
          // Store the position where the body begins.
          size_t pos = eoh + 4;

          if (my_id_ == static_cast<int>(peer_id)) {
            // A notification about a new member or a member that just
            // disconnected.
            int id = 0;
            std::string name;
            bool connected = false;
            if (ParseEntry(notification_data_.substr(pos), &name, &id,
                          &connected)) {
              if (connected) {
                peers_[id] = name;
              } else {
                peers_.erase(id);
              }
            }
          } else {
            OnMessageFromPeer(static_cast<int>(peer_id),
                              notification_data_.substr(pos));
          }
        }
        notification_data_.clear();
      }

      if (!hanging_get_.available() && state_ == CONNECTED) {
        async_connect(hanging_get_, ep_iter_, &SignalingClient::OnHangingGetRead);
      }
    }

    bool SignalingClient::ReadIntoBuffer( tcp::socket& socket,
                                          std::string* data,
                                          size_t* content_length) {
      char buffer[0xffff];
      memset(buffer, 0x0, sizeof(buffer));
   
      socket.receive(boost::asio::buffer(buffer));
      data->append(buffer);

      bool ret = false;
      size_t i = data->find("\r\n\r\n");
      if (i != std::string::npos) {
        if (GetHeaderValue(*data, i, "\r\nContent-Length: ", content_length)) {
          size_t total_response_size = (i + 4) + *content_length;
          if (data->length() >= total_response_size) {
            ret = true;
            std::string should_close;
            const char kConnection[] = "\r\nConnection: ";
            if (GetHeaderValue(*data, i, kConnection, &should_close) &&
                should_close.compare("close") == 0) {
              socket.close();
            }
          } else {
            // We haven't received everything.  Just continue to accept data.
          }
        } else {
          printf("No content length field specified by the server.\n");
        }
      }
      return ret;
    }

    int SignalingClient::GetResponseStatus(const std::string& response) {
      int status = -1;
      size_t pos = response.find(' ');
      if (pos != std::string::npos)
        status = atoi(&response[pos + 1]);
      return status;
    }

    bool SignalingClient::ParseServerResponse(const std::string& response,
                                                  size_t content_length,
                                                  size_t* peer_id,
                                                  size_t* eoh) {
      int status = GetResponseStatus(response.c_str());
      if (status != 200) {
        printf("Received error from server\n");
        Close();
        return false;
      }

      *eoh = response.find("\r\n\r\n");
      BOOL_RETURN_CHECK(*eoh != std::string::npos);
      if (*eoh == std::string::npos)
        return false;

      *peer_id = -1;

      // See comment in peer_channel.cc for why we use the Pragma header and
      // not e.g. "X-Peer-Id".
      GetHeaderValue(response, *eoh, "\r\nPragma: ", peer_id);

      return true;
    }


    bool SignalingClient::GetHeaderValue(const std::string& data,
                                          size_t eoh,
                                          const char* header_pattern,
                                          std::string* value) {
      BOOL_RETURN_CHECK(value != NULL);
      size_t found = data.find(header_pattern);
      if (found != std::string::npos && found < eoh) {
        size_t begin = found + strlen(header_pattern);
        size_t end = data.find("\r\n", begin);
        if (end == std::string::npos)
          end = eoh;
        value->assign(data.substr(begin, end - begin));
        return true;
      }
      return false;
    }


    bool SignalingClient::ParseEntry(const std::string& entry,
                                      std::string* name,
                                      int* id,
                                      bool* connected) {
      BOOL_RETURN_CHECK(name != NULL);
      BOOL_RETURN_CHECK(id != NULL);
      BOOL_RETURN_CHECK(connected != NULL);
      BOOL_RETURN_CHECK(!entry.empty());

      *connected = false;
      size_t separator = entry.find(',');
      if (separator != std::string::npos) {
        *id = atoi(&entry[separator + 1]);
        name->assign(entry.substr(0, separator));
        separator = entry.find(',', separator + 1);
        if (separator != std::string::npos) {
          *connected = atoi(&entry[separator + 1]) ? true : false;
        }
      }
      return !name->empty();
    }

    bool SignalingClient::SignOut() {
      if (state_ == NOT_CONNECTED || state_ == SIGNING_OUT)
        return true;

      hanging_get_.close();

      if (!control_socket_.available()) {
        state_ = SIGNING_OUT;

        if (my_id_ != -1) {
          char buffer[1024];
          snprintf(buffer, sizeof(buffer),
                  "GET /sign_out?peer_id=%i HTTP/1.0\r\n\r\n", my_id_);
          onconnect_data_ = buffer;
          return ConnectControlSocket();
        } else {
          // Can occur if the app is closed before we finish connecting.
          return true;
        }
      } else {
        state_ = SIGNING_OUT_WAITING;
      }

      return true;
    }

    void SignalingClient::Close() {
      control_socket_.close();
      hanging_get_.close();
      onconnect_data_.clear();
      peers_.clear();
      my_id_ = -1;
      state_ = NOT_CONNECTED;
    }

    }
}

    


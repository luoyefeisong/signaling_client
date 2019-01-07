#include <string>
#include <map>
#include <boost/asio.hpp>

#define VOID_RETURN_CHECK(value) \
        if (value != true) \
          return; \

#define BOOL_RETURN_CHECK(value) \
        if (value != true) \
          return false; \

typedef enum State {
  NOT_CONNECTED,
  SIGNING_IN,
  CONNECTED,
  SIGNING_OUT_WAITING,
  SIGNING_OUT,
}State;

// This is our magical hangup signal.
const char kByeMessage[] = "BYE";

typedef std::map<int, std::string> Peers;

using namespace boost::asio::ip;
const unsigned short kDefaultServerPort = 8888;

namespace gwecom {
  namespace network {
    class SignalingClient {
     public:
      SignalingClient(boost::asio::io_service& io_service);
      ~SignalingClient();        

      void Connect(const std::string& server,
                   int port,
                   const std::string& client_name);

      bool ConnectControlSocket();
      void ConnectHangingGet();
      void DoConnect();
      void OnConnect();
      void OnHangingGetConnect();

      bool SendToPeer(int peer_id, const std::string& message);
      void OnMessageFromPeer(int peer, const std::string& message); 

      bool is_connected() const { return my_id_ != -1;}
      // Quick and dirty support for parsing HTTP header values.
      bool GetHeaderValue(const std::string& data,
                          size_t eoh,
                          const char* header_pattern,
                          size_t* value);

      bool GetHeaderValue(const std::string& data,
                          size_t eoh,
                          const char* header_pattern,
                          std::string* value);

      // Returns true if the whole response has been read.
      bool ReadIntoBuffer(tcp::socket& socket,
                          std::string* data,
                          size_t* content_length);

      void OnRead();

      void OnHangingGetRead();

      // Parses a single line entry in the form "<name>,<id>,<connected>"
      bool ParseEntry(const std::string& entry,
                      std::string* name,
                      int* id,
                      bool* connected);

      int GetResponseStatus(const std::string& response);

      bool ParseServerResponse(const std::string& response,
                              size_t content_length,
                              size_t* peer_id,
                              size_t* eoh);
      bool SendHangUp(int peer_id);

      bool SignOut();
      void Close();


     private:
      tcp::socket control_socket_;
      tcp::socket hanging_get_;
      std::string onconnect_data_;
      std::string control_data_;
      std::string notification_data_;
      std::string client_name_;
      Peers peers_;
      State state_;
      int my_id_;
      std::string server_ip_;
      int server_port_;
      tcp::resolver::iterator ep_iter_;
      boost::asio::io_service &io_service_;
    };
  }  // namespace network
}  // namespace gwecom
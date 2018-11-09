#include <stdio.h>
#include <stdlib.h>
#include <pjlib.h>
#include <pj/sock.h>
#include <assert.h>
#include <defaults.h>

//should be in header file

#define MAX_CLIENT_NAME_LEN 512
#define MAX_ONCONNECT_DATA_LEN  2048

typedef enum State {
  NOT_CONNECTED,
  RESOLVING,
  SIGNING_IN,
  CONNECTED,
  SIGNING_OUT_WAITING,
  SIGNING_OUT,
}State;

typedef struct signaling_client_observer_callback {
  void (*OnSignedIn)(void);  // Called when we're logged on.
  void (*OnDisconnected)(void);
  void (*OnPeerConnected)(int id, const char* name, int name_len);
  void (*OnPeerDisconnected)(int peer_id);
  void (*OnMessageFromPeer)(int peer_id, const char* message, int msg_len);
  void (*OnMessageSent)(int err);
  void (*OnServerConnectionFailure)(void);
} sc_observer_callback;

typedef struct signaling_client {
  State state;
  pj_sockaddr_in saddr; //server address info
  pj_sock_t sock;
  int my_id;
  char onconnect_data[MAX_ONCONNECT_DATA_LEN];
  sc_observer_callback *callback;
} signaling_client;
//should be in header file

// This is our magical hangup signal.
const char kByeMessage[] = "BYE";
// Delay between server connection retries, in milliseconds
const int kReconnectDelay = 2000;

signaling_client SignalingClient = {0};

pj_bool_t SignalingClient_is_connected(signaling_client *SC)
{
  if (SC == NULL)
    return;
  return SC->my_id != -1;
}

void SignalingClient_RegisterObserver(signaling_client *SC,
                                      sc_observer_callback* callback) {
  if (!callback || !SC)
    return;

  memcpy(SC->callback, callback, sizeof(sc_observer_callback));  
}

pj_sock_t SignalingClient_SocketCreate(signaling_client *SC) 
{
  if (SC == NULL)
    return PJ_INVALID_SOCKET;
  pj_sock_t sock = PJ_INVALID_SOCKET;
  pj_status_t status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, &sock);
  if (PJ_SUCCESS != status) {
    printf("%s create sock error!\n", __FUNCTION__);
    return PJ_INVALID_SOCKET;
  }
  SC->sock = sock;
  return sock;
}

void SignalingClient_SocketClose(pj_sock_t sock) {
  pj_sock_close(sock);
}

int SignalingClient_DoConnect(signaling_client *SC,
                            const pj_sockaddr_in* server,  
                            const char* client_name)
{
  pj_status_t status = PJ_FALSE;

  if (NULL == server || NULL == client_name || SC == NULL) {
    printf("(ERROR) nothing in server or client_name is null or SC is null\n");
    return;
  }

  if (SC->state != NOT_CONNECTED) {
    printf("(WARNING) The client must not be connected before you can call Connect()\n");
    return;
  }

  memcpy(&SC->saddr, server, sizeof(SC->saddr));
  if (SC->saddr.sin_port == 0 )
    SC->saddr.sin_port = kDefaultServerPort;

  status = pj_sock_connect(SC->sock, &SC->saddr, sizeof(SC->saddr));
  if (status != PJ_SUCCESS)
    return PJ_FALSE;
  return status;
}

void SignalingClient_OnConnect(signaling_client *SC) 
{
  if (SC->onconnect_data == NULL || strlen(SC->onconnect_data) == 0)
    return;
  pj_status_t status = pj_sock_send(SC->sock, SC->onconnect_data,
                                    strlen(SC->onconnect_data), 0);
  if (status != PJ_SUCCESS) {
    printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
    return PJ_FALSE;
  }
  memset(SC->onconnect_data, 0x0, sizeof(SC->onconnect_data));
}

pj_bool_t SignalingClient_SendToPeer( signaling_client *SC,
                                      int peer_id, 
                                      const char* message,
                                      int message_len)
{
  if (SC == NULL || message == NULL)
    return;
  
  if (SC->state != CONNECTED)
    return PJ_FALSE;
  if (!is_connected() || peer_id == -1)
    return PJ_FALSE;

  char headers[1024];
  snprintf(headers, sizeof(headers),
           "POST /message?peer_id=%i&to=%i HTTP/1.0\r\n"
           "Content-Length: %zu\r\n"
           "Content-Type: text/plain\r\n"
           "\r\n",
           SC->my_id, peer_id, message_len);
  if (strlen(headers) + message_len >= sizeof(SC->onconnect_data) ) {
    printf("%s message is too long\n", __FUNCTION__);
    return PJ_FALSE;
  }

  memset(SC->onconnect_data, 0x0, sizeof(SC->onconnect_data));
  strcpy(SC->onconnect_data, headers);
  strcat(SC->onconnect_data, message);

  pj_status_t status = pj_sock_send(SC->sock, SC->onconnect_data, 
                                    strlen(SC->onconnect_data), 0);
  if (status != PJ_SUCCESS) {
    printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
    return PJ_FALSE;
  }
  return PJ_SUCCESS;
}

pj_bool_t SignalingClient_SendHangUp(signaling_client *SC, int peer_id) {
  return SignalingClient_SendToPeer(SC, peer_id, kByeMessage, sizeof(kByeMessage));
}
/*
pj_bool_t SignalingClient_IsSendingMessage(signaling_client *SC) {
  return SC->state == CONNECTED &&
         control_socket_->GetState() != rtc::Socket::CS_CLOSED;
}
*/

pj_bool_t SignalingClient_SignOut(signaling_client *SC)
{
  if (SC == NULL)
    return;
  if (SC->state == NOT_CONNECTED || SC->state == SIGNING_OUT)
    return PJ_TRUE;
  
  SC->state = SIGNING_OUT;

  if (SC->my_id != -1) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
              "GET /sign_out?peer_id=%i HTTP/1.0\r\n\r\n", SC->my_id);
    
    memset(SC->onconnect_data, 0x0, sizeof(SC->onconnect_data));
    strcpy(SC->onconnect_data, buffer, strlen(buffer));
    
    pj_status_t status = pj_sock_send(SC->sock, SC->onconnect_data, 
                                    strlen(SC->onconnect_data), 0);
    if (status != PJ_SUCCESS) {
      printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
      return PJ_FALSE;
    }
    
    SignalingClient_Close(SC);
  }
  else {
    // Can occur if the app is closed before we finish connecting.
    printf("%s no connect before closed\n", __FUNCTION__);
    return PJ_TRUE;
  }
  return PJ_TRUE;  
}

void SignalingClient_Close(signaling_client *SC)
{
  if (SC == NULL)
    return;
  SignalingClient_SocketClose(SC->sock);
  memset(SC, 0x0, sizeof(signaling_client));
  SC->my_id = -1;
  SC->state = NOT_CONNECTED;
}

void SignalingClient_OnHangingGetConnect(signaling_client *SC) 
{
  if (!SC)
    return;

  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "GET /wait?peer_id=%i HTTP/1.0\r\n\r\n",
           SC->my_id);
  int len = strlen(buffer);
  pj_status_t status = pj_sock_send(SC->sock, buffer, len, 0);
  if (status != PJ_SUCCESS) {
    printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
    return PJ_FALSE;
  }
  return PJ_TRUE;
}

void SignalingClient_OnMessageFromPeer(signaling_client *SC,
                                       int peer_id,
                                       const char* message,
                                       int msg_len) 
{
  if (msg_len == (sizeof(kByeMessage) - 1) &&
      strncmp(message, kByeMessage, sizeof(kByeMessage) - 1) == 0) {
    SC->callback->OnPeerDisconnected(peer_id);
  } else {
    SC->callback->OnMessageFromPeer(peer_id, message, msg_len);
  }
}

pj_bool_t SignalingClient_GetHeaderValue( signaling_client *SC,
                                          const char* data,
                                          int data_len,
                                          int eoh,
                                          const char* header_pattern,
                                          int header_pattern_len,
                                          int* value) 
{
  RTC_DCHECK(value != NULL);
  size_t found = data.find(header_pattern);
  if (found != std::string::npos && found < eoh) {
    *value = atoi(&data[found + strlen(header_pattern)]);
    return true;
  }
  return false;
}
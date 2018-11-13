#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "defaults.h"
#include <string.h>
#include <pjlib.h>

#include "peer_connection_client.h"

extern app_t app;


static void on_ioqueue_hanging_read(pj_ioqueue_key_t *key, 
                            pj_ioqueue_op_key_t *op_key,
                            pj_ssize_t bytes_read)
{
  pj_status_t rc = 0;
  signaling_client* SC = NULL;
  SC = (signaling_client*)pj_ioqueue_get_user_data(key);
  if (!SC) {
    printf("%s get userdata failed !\n", __FUNCTION__);
  }

  SignalingClient_OnHangingGetRead(SC, SC->sock);
}

static pj_ioqueue_callback hanging_cb = 
{
    &on_ioqueue_hanging_read,
    NULL,
    NULL,
    NULL,
};

// This is our magical hangup signal.
const char kByeMessage[] = "BYE";
// Delay between server connection retries, in milliseconds
const int kReconnectDelay = 2000;

signaling_client SignalingClient = {0};

signaling_client* SignalingClient_Create()
{
  signaling_client* SC;
  SC = (signaling_client*)malloc(sizeof(signaling_client));
  if (SC == NULL)
  {
    return NULL;
  }
  memset(SC, 0x0, sizeof(signaling_client));
  SC->my_id = -1;
  SC->state = NOT_CONNECTED;
  for (int i =0; i < MAX_PEERS_NUMBER; ++i)
    SC->peers[i].id = -1;
  return SC;
}

void SignalingClient_Destroy(signaling_client* SC)
{
  free(SC);
  SC = NULL;
}

void SignalingClient_Close(signaling_client* SC)
{
  if (SC == NULL)
    return;
  SignalingClient_SocketClose(SC);
  memset(SC, 0x0, sizeof(signaling_client));
  SC->my_id = -1;
  SC->state = NOT_CONNECTED;
  for (int i =0; i < MAX_PEERS_NUMBER; ++i)
    SC->peers[i].id = -1;
}

int SignalingClient_AllocPeer(signaling_client* SC)
{
  for (int i =0; i < MAX_PEERS_NUMBER; ++i)
    if (SC->peers[i].id == -1)
      return i;
  return -1; 
}

pj_status_t SignalingClient_DestroyPeer(signaling_client* SC, int peer_id)
{
  for (int i =0; i < MAX_PEERS_NUMBER; ++i)
    if (SC->peers[i].id == peer_id) {
      SC->peers[i].id = -1;
      memset(SC->peers[i].name, 0x0, sizeof(SC->peers[i].name));
      return PJ_TRUE;
    }
  return PJ_FALSE;
}

pj_bool_t SignalingClient_is_connected(signaling_client *SC)
{
  if (SC == NULL)
    return PJ_FALSE;
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

void SignalingClient_SocketClose(signaling_client* SC) 
{
  pj_sock_close(SC->sock);
  SC->sock = PJ_INVALID_SOCKET;
  SC->is_connected = PJ_FALSE;
}

int SignalingClient_Connect(signaling_client *SC,
                              pj_sockaddr_in* server)
{
  pj_status_t status = PJ_FALSE;

  if (NULL == server || SC == NULL) {
    printf("(ERROR) nothing in server is null or SC is null\n");
    return PJ_FALSE;
  }

  if (SC->is_connected == PJ_TRUE) {
    printf("(WARNING) The client must not be connected before you can call Connect()\n");
    return PJ_FALSE;
  }

  memcpy(&SC->saddr, server, sizeof(SC->saddr));
  if (SC->saddr.sin_port == 0 )
    SC->saddr.sin_port = kDefaultServerPort;
  //SC->saddr.sin_port = pj_htons(SC->saddr.sin_port);
  GetPeerName(SC->client_name);

  status = pj_sock_connect(SC->sock, &SC->saddr, sizeof(SC->saddr));
  if (status != PJ_SUCCESS)
    return PJ_FALSE;
  else
  {
    SC->is_connected = PJ_TRUE;
  }
       
  return status;
}

void SignalingClient_DoConnect(signaling_client *SC) {
  char buffer[1024] = {0};
  snprintf(buffer, sizeof(buffer), "GET /sign_in?%s HTTP/1.0\r\n\r\n",
           SC->client_name);
  strcpy(SC->onconnect_data, buffer);
  SC->state = SIGNING_IN;
}

void SignalingClient_OnConnect(signaling_client *SC) 
{
  if (SC->onconnect_data == NULL || strlen(SC->onconnect_data) == 0)
    return;
  int len = strlen(SC->onconnect_data);
  pj_status_t status = pj_sock_send(SC->sock, SC->onconnect_data,
                                    &len, 0);
  if (status != PJ_SUCCESS ) {
    printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
    return;
  }
  memset(SC->onconnect_data, 0x0, sizeof(SC->onconnect_data));

  SignalingClient_OnRead(SC);
}

pj_bool_t SignalingClient_SendToPeer( signaling_client *SC,
                                      int peer_id, 
                                      const char* message,
                                      int message_len)
{
  if (SC == NULL || message == NULL)
    return PJ_FALSE;
  
  if (SC->state != CONNECTED)
    return PJ_FALSE;
  if (!SignalingClient_is_connected(SC) || peer_id == -1)
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
  int len = strlen(SC->onconnect_data);
  pj_status_t status = pj_sock_send(SC->sock, SC->onconnect_data, 
                                    &len, 0);
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
    return PJ_FALSE;
  if (SC->state == NOT_CONNECTED || SC->state == SIGNING_OUT)
    return PJ_TRUE;
  
  SC->state = SIGNING_OUT;

  if (SC->my_id != -1) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
              "GET /sign_out?peer_id=%i HTTP/1.0\r\n\r\n", SC->my_id);
    
    memset(SC->onconnect_data, 0x0, sizeof(SC->onconnect_data));
    strcpy(SC->onconnect_data, buffer);
    int len = strlen(SC->onconnect_data);
    pj_status_t status = pj_sock_send(SC->sock, SC->onconnect_data, 
                                      &len, 0);
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


pj_bool_t SignalingClient_OnHangingGetConnect(signaling_client *SC) 
{
  pj_status_t rc = 0;
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "GET /wait?peer_id=%i HTTP/1.0\r\n\r\n",
           SC->my_id);
  int len = strlen(buffer);

  pj_status_t status = pj_sock_send(SC->sock, buffer, &len, 0);
  if (status != PJ_SUCCESS) {
    printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
    return PJ_FALSE;
  }
  rc = pj_ioqueue_register_sock(app.pool, app.ioqueue, SC->sock, NULL, &hanging_cb, &app.key);
  if (rc != PJ_SUCCESS) {
    printf("%s register sock failed !\n", __FUNCTION__);
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
                                          int* value) 
{
  int found = FindSubStr(data, header_pattern);
  if (found != '\0' && found < eoh) {
    *value = atoi(&data[found + strlen(header_pattern)]);
    return PJ_TRUE;
  }
  return PJ_FALSE;
}

pj_bool_t SignalingClient_GetHeaderValueStr( signaling_client *SC,
                                          const char* data,
                                          int data_len,
                                          int eoh,
                                          const char* header_pattern,
                                          char* value) 
{
  int found = FindSubStr(data, header_pattern);
  if (found != '\0' && found < eoh) {
    int begin = found + strlen(header_pattern);
    int end = FindSubStr(data + begin, "\r\n") + begin;
    if (end == '\0')
      end = eoh;
    memcpy(value, data + begin, end - begin);
    return PJ_TRUE;
  }
  return PJ_FALSE;
}

pj_bool_t SignalingClient_ReadIntoBuffer(signaling_client *SC,
                                         char* data,
                                         int data_len,
                                         int* content_length) 
{
  //if (!data || !SC || !content_length)
    //return;
  pj_status_t status;
  char buffer[0xffff];
  int buffer_len = sizeof(buffer);

  do {
    status = pj_sock_recv(SC->sock, buffer, &buffer_len, 0);
    if (status != PJ_SUCCESS)
      break;
    if (data_len > (int)strlen(data) + buffer_len)
      memcpy(data + strlen(data), buffer, buffer_len);
    else
      printf("%s buffer len is too long!\n", __FUNCTION__);
  } while (PJ_TRUE);

  pj_bool_t ret = PJ_FALSE;
  int i = FindSubStr(data, "\r\n\r\n");
  if (i != '\0') {
    printf("(INFO) << Headers received\n");
    if (SignalingClient_GetHeaderValue(SC, data, data_len, i, "\r\nContent-Length: ", content_length)) {
      int total_response_size = (i + 4) + *content_length;
      if ((int)strlen(data) >= total_response_size) {
        ret = PJ_TRUE;
        char should_close[10] = {0};
        const char kConnection[] = "\r\nConnection: ";
        if (SignalingClient_GetHeaderValueStr(SC, data, data_len, i, kConnection, should_close) &&
            strncmp(should_close, "close", 5) == 0) {
          SignalingClient_SocketClose(SC);
          // Since we closed the socket, there was no notification delivered
          // to us.  Compensate by letting ourselves know.
        }
      } else {
        // We haven't received everything.  Just continue to accept data.
      }
    } else {
      printf("(LS_ERROR) << No content length field specified by the server.\n");
    }
  }
  return ret;
}

int SignalingClient_GetResponseStatus(const char* response) {
  int status = -1;
  if (!response)
    return status;
  int pos = FindSubStr(response, " ");
  if (pos != '\0')
    status = atoi(&response[pos + 1]);
  return status;
}

pj_bool_t SignalingClient_ParseServerResponse(signaling_client *SC, 
                                              const char* response,
                                              int content_length,
                                              int* peer_id,
                                              int* eoh) 
{
  int status = SignalingClient_GetResponseStatus(response);
  if (status != 200) {
    printf("(LS_ERROR) << Received error from server\n");
	  SignalingClient_Close(SC);
    return PJ_FALSE;
  }

  *eoh = FindSubStr(response, "\r\n\r\n");
  assert(*eoh != '\0');
  if (*eoh == '\0')
    return PJ_FALSE;

  *peer_id = -1;

  // See comment in peer_channel.cc for why we use the Pragma header and
  // not e.g. "X-Peer-Id".
  SignalingClient_GetHeaderValue(SC, response,strlen(response), *eoh, "\r\nPragma: ", peer_id);
  return PJ_TRUE;
}

void SignalingClient_OnRead(signaling_client *SC) 
{
  int content_length = 0;
  if (SignalingClient_ReadIntoBuffer(SC, SC->control_data, sizeof(SC->control_data), &content_length)) {
    int peer_id = 0, eoh = 0;
    pj_bool_t ok =
		SignalingClient_ParseServerResponse(SC, SC->control_data, content_length, &peer_id, &eoh);
    if (ok) {
      if (SC->my_id == -1) {
        // First response.  Let's store our server assigned ID.
        if (SC->state != SIGNING_IN) {
          printf("ERROR: state is not signing in \n");
          return;
        }
          
        SC->my_id = peer_id;
        if (SC->my_id == -1) {
          return;
        }
          
        // The body of the response will be a list of already connected peers.
        if (content_length) {
          int pos = eoh + 4;
          while (pos < (int)strlen(SC->control_data)) {
            int eol = FindSubStr((char*)(SC->control_data) + pos, "\n") + pos;
            if (eol == '\0')
              break;
            int id = 0;
            char name[MAX_CLIENT_NAME_LEN];
            pj_bool_t connected;
            char sub_control_data[1024] = {0};
            memcpy(sub_control_data, (char*)SC->control_data + pos, eol - pos);
            if (SignalingClient_ParseEntry(sub_control_data, name, &id,&connected) &&
                id != SC->my_id) {
              int index = SignalingClient_AllocPeer(SC);
              if (index != -1) {
                SC->peers[index].id = id;
                strcpy(SC->peers[index].name, name);
              } else {
                printf("%s ERROR: alloc peer failed. peer_id %d peer_name %s\n", 
                            __FUNCTION__, id, name);
                return; 
              }

              //SC->callback->OnPeerConnected(id, name, strlen(name));
            }
            pos = eol + 1;
          }
        }
        assert(SignalingClient_is_connected(SC));
        //SC->callback->OnSignedIn();
      } else if (SC->state == SIGNING_OUT) {
		    SignalingClient_Close(SC);
      } else if (SC->state == SIGNING_OUT_WAITING) {
		    SignalingClient_SignOut(SC);
      }
    }
    memset(SC->control_data, 0x0, sizeof(SC->control_data));

    if (SC->state == SIGNING_IN) {
	    SC->state = CONNECTED;
      SignalingClient_SocketCreate(SC);
      SignalingClient_Connect(SC, &SC->saddr);
      SignalingClient_OnHangingGetConnect(SC);

    } 
  }
}

void SignalingClient_OnHangingGetRead(signaling_client *SC,
                                      pj_sock_t socket) 
{
  printf("%s (INFO)", __FUNCTION__);
  int content_length = 0;
  if (SignalingClient_ReadIntoBuffer(SC, SC->notification_data, sizeof(SC->notification_data), &content_length)) {
    int peer_id = 0, eoh = 0;
    pj_bool_t ok =
		SignalingClient_ParseServerResponse(SC, SC->notification_data, content_length, &peer_id, &eoh);

    if (ok) {
      // Store the position where the body begins.
      int pos = eoh + 4;

      if (SC->my_id == peer_id) {
        // A notification about a new member or a member that just
        // disconnected.
        int id = 0;
        char name[MAX_CLIENT_NAME_LEN];
        pj_bool_t connected = PJ_FALSE;
        if (SignalingClient_ParseEntry((char*)SC->notification_data + pos, name, &id,
                       &connected)) {
          if (connected) {
            int index = SignalingClient_AllocPeer(SC);
            if (index != -1) {
              SC->peers[index].id = id;
              strcpy(SC->peers[index].name, name);
            } else {
                printf("%s ERROR: alloc peer failed. peer_id %d peer_name %s\n", 
                            __FUNCTION__, id, name);
                return; 
            }


            //SC->callback->OnPeerConnected(id, name, strlen(name));
          } else {
            SignalingClient_DestroyPeer(SC, id);
            //SC->callback->OnPeerDisconnected(id);

          }
        }
      } else {
        char sub_control_data[1024] = {0};
        memcpy(sub_control_data, (char*)SC->notification_data + pos, sizeof(sub_control_data) - 1);
        SignalingClient_OnMessageFromPeer(SC, peer_id, 
                                          sub_control_data, strlen(sub_control_data));
      }
    }

    memset(SC->notification_data, 0x0, sizeof(SC->notification_data));
  }

  if (SC->is_connected == NOT_CONNECTED) {
	  SignalingClient_SocketCreate(SC);
	  SignalingClient_Connect(SC, &SC->saddr);
  }
}

pj_bool_t SignalingClient_ParseEntry( const char* entry,
                                      char* name,
                                      int* id,
                                      pj_bool_t* connected) {
  assert(name != NULL);
  assert(id != NULL);
  assert(connected != NULL);
  assert(entry);

  *connected = PJ_FALSE;
  int separator = FindSubStr(entry, ",");
  if (separator != '\0') {
    *id = atoi(&entry[separator + 1]);
    memcpy(name, entry, separator);
    separator = FindSubStr(entry + separator + 1, ",") + separator + 1;
    if (separator != '\0') {
      *connected = atoi(&entry[separator + 1]) ? PJ_TRUE : PJ_FALSE;
    }
  }
  return strlen(name) ? PJ_TRUE : PJ_FALSE;
}

void SignalingClient_OnClose(signaling_client *SC, int err) {
  printf("(INFO) %s\n", __FUNCTION__);
}
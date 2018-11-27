#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "defaults.h"
#include <string.h>
#include <pjlib.h>
#include <pj/list.h>
#include "peer_connection_client.h"


extern global_app_t app;


static void on_hanging_read_complete(pj_ioqueue_key_t *key, 
                            pj_ioqueue_op_key_t *op_key,
                            pj_ssize_t bytes_read)
{
  pj_status_t rc = 0;
  signaling_client* SC = NULL;

  //memset(&app.op_key, 0x0, sizeof(app.op_key));

  SC = (signaling_client*)pj_ioqueue_get_user_data(key);
  if (!SC) {
    printf("get userdata failed !\n");
  }

  if (bytes_read < 0) {
    char errmsg[PJ_ERR_MSG_SIZE];

    rc = (pj_status_t)-bytes_read;
    pj_strerror(rc, errmsg, sizeof(errmsg));
    printf("%s read error: %s\n", __FUNCTION__, errmsg);

	  goto read_next_packet;
  } else if (bytes_read == 0) {
    goto read_next_packet;
  } else {
    SC->notification_data_len = bytes_read;
    SignalingClient_OnHangingGetRead(SC);

  }
  return;

read_next_packet:
  
  rc = pj_ioqueue_recv(app.key, &app.op_key, 
                      SC->notification_data, &SC->notification_data_len,
                      PJ_IOQUEUE_ALWAYS_ASYNC);

  if (rc != PJ_EPENDING && rc != PJ_ECANCELLED) {
    char errmsg[PJ_ERR_MSG_SIZE];

    printf("%s ioqueue read error: %s\n", __FUNCTION__, errmsg);

    pj_assert(!"Unhandled error");
  }

}

static pj_ioqueue_callback hanging_cb = 
{
    &on_hanging_read_complete,
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

/* �˺������߳����⣬�Ȳ��� */
void SignalingClient_Close(signaling_client* SC)
{
  if (SC == NULL)
    return;
  SignalingClient_SocketClose(SC, SC->sock);
  SignalingClient_SocketClose(SC, SC->sock_get);
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

int SignalingClient_FindPeer(signaling_client* SC, int peer_id)
{
  for (int i =0; i < MAX_PEERS_NUMBER; ++i) {
    if (SC->peers[i].id == -1) {
      continue;
    } else {
      if (SC->peers[i].id == peer_id)
        return i;
      else
        continue;  
    }
  }
  return -1;
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

int SignalingClient_GetPeerByName(signaling_client *SC, pj_str_t* name)
{
  for (int i = 0; i < MAX_PEERS_NUMBER; ++i) {
    if (SC->peers[i].id == -1) {
      continue;
    } else {
      if (!strncmp(SC->peers[i].name, name->ptr, name->slen))
        return SC->peers[i].id;
      else
        continue;
    }
  }
  return -1;   
}

pj_sock_t SignalingClient_SocketCreate(signaling_client *SC, pj_bool_t is_get) 
{
  if (SC == NULL)
    return PJ_INVALID_SOCKET;
  pj_sock_t sock = PJ_INVALID_SOCKET;
  pj_status_t status = pj_sock_socket(pj_AF_INET(), pj_SOCK_STREAM(), 0, &sock);
  if (PJ_SUCCESS != status) {
    return PJ_INVALID_SOCKET;
  }

  if (is_get) {
    SC->sock_get = sock;
  }
  else
    SC->sock = sock;
  return sock;
}

void SignalingClient_SocketClose(signaling_client* SC, pj_sock_t socket) 
{
 
  pj_sock_close(socket);

  if (socket == SC->sock) {
    SC->sock = PJ_INVALID_SOCKET;
    SC->is_connected = PJ_FALSE;
  }

  if (SC->if_register && socket == SC->sock_get) {
    if (app.key) {
      pj_ioqueue_unregister(app.key);
      app.key = NULL;
      SC->if_register = PJ_FALSE;
    }
    SC->sock_get = PJ_INVALID_SOCKET;
    SC->is_connected_get = PJ_FALSE;
  } 
}

int SignalingClient_Connect(signaling_client *SC,
                            pj_sockaddr_in* server,
                            pj_sock_t socket)
{
  pj_status_t status = PJ_FALSE;

  if (NULL == server || SC == NULL) {
    printf("(ERROR) nothing in server is null or SC is null\n");
    return PJ_FALSE;
  }

  if ( (SC->is_connected == PJ_TRUE && SC->sock == socket) || 
        (SC->is_connected_get == PJ_TRUE && SC->sock_get == socket) ) {
    printf("(WARNING) The client must not be connected before you can call Connect()\n");
    return PJ_FALSE;
  }

  memcpy(&SC->saddr, server, sizeof(SC->saddr));
  if (SC->saddr.sin_port == 0 )
    SC->saddr.sin_port = kDefaultServerPort;
  
  GetPeerName(SC->client_name);

  status = pj_sock_connect(socket, &SC->saddr, sizeof(SC->saddr));
  if (status != PJ_SUCCESS)
    return PJ_FALSE;
  else
  {
    if (socket == SC->sock)
      SC->is_connected = PJ_TRUE;
    else
      SC->is_connected_get = PJ_TRUE;
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

void SignalingClient_OnConnect(signaling_client *SC, pj_sock_t socket) 
{
  if (SC->onconnect_data == NULL || strlen(SC->onconnect_data) == 0)
    return;
  int len = strlen(SC->onconnect_data);
  pj_status_t status = pj_sock_send(socket, SC->onconnect_data,
                                    &(pj_ssize_t)len, 0);
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

  pj_sock_t socket = SignalingClient_SocketCreate(SC, PJ_FALSE);
  pj_status_t rc = SignalingClient_Connect(SC, &SC->saddr, socket);
  if (rc != PJ_SUCCESS) {
    return PJ_FALSE;
  }

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
                                    &(pj_ssize_t)len, 0);
  if (status != PJ_SUCCESS) {
    printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
    return PJ_FALSE;
  }

  SignalingClient_OnRead(SC);
  
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
                                      &(pj_ssize_t)len, 0);
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


pj_bool_t SignalingClient_OnHangingGetConnectAndRecv(signaling_client *SC) 
{
  pj_status_t rc = 0;
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "GET /wait?peer_id=%i HTTP/1.0\r\n\r\n",
           SC->my_id);
  int len = strlen(buffer);

  pj_status_t status = pj_sock_send(SC->sock_get, buffer, &(pj_ssize_t)len, 0);
  if (status != PJ_SUCCESS) {
    printf("%s send to peer failed, error code %d\n", __FUNCTION__, status);
    return PJ_FALSE;
  }
  if (app.key != NULL)
    printf("%s key not unregister! socket %d\n", __FUNCTION__, SC->sock_get);
  

  rc = pj_ioqueue_register_sock(app.pool, app.ioqueue, SC->sock_get, SC, &hanging_cb, &app.key);
  if (rc != PJ_SUCCESS) {
    printf("%s register sock failed !\n", __FUNCTION__);
    return PJ_FALSE;
  }
          
  SC->if_register = PJ_TRUE;

  SC->notification_data_len = sizeof(SC->notification_data);

  rc = pj_ioqueue_recv( app.key, &app.op_key, 
                                    SC->notification_data, &SC->notification_data_len,
                                    PJ_IOQUEUE_ALWAYS_ASYNC);

  if (rc != PJ_EPENDING && rc != PJ_ECANCELLED) {
    char errmsg[PJ_ERR_MSG_SIZE];
    printf("%s ioqueue read error: %s\n", __FUNCTION__, errmsg);
    pj_assert(!"Unhandled error");
  }

  return PJ_TRUE;
}
extern void icedemo_init_session(unsigned rolechar);
extern int encode_session_sdp(char *buffer, unsigned maxlen);
extern void icedemo_input_remote_sdp(char* buffer_in, int buffer_len);
extern void icedemo_start_nego(); 
static void ice_start(const char* message,
                      int msg_len,
                      int peer_id)
{
  unsigned role = 'c';
  icedemo.in_nego = PJ_TRUE;
  icedemo_init_session(role);
  encode_session_sdp(g_sdp_buffer, sizeof(g_sdp_buffer));

  memset(g_sdp_remote_buffer, 0x0, sizeof(g_sdp_remote_buffer));
  memcpy(g_sdp_remote_buffer, message + sizeof(SDP_FLAG) , msg_len - sizeof(SDP_FLAG)); 
  icedemo_input_remote_sdp(g_sdp_remote_buffer, strlen(g_sdp_remote_buffer));

  SignalingClient_SendToPeer(icedemo.SC, peer_id, g_sdp_buffer, strlen(g_sdp_buffer));
  icedemo_start_nego();
}

static void PrintNodeCount(list_node* head)
{
  list_node* next = head->next;
  int i = 0;
  while(head != next)
  {
    i++;
    next = next->next;
  }
  printf("%s node number %d\n", __func__, i);
}

static void SignalingClient_PushBackNewMsg(const char* message, 
                                    int msg_len,
                                    int peer_id)
{
  //new message push back
  list_node* head = &icedemo.list;
  list_node* node = (list_node*)malloc(sizeof(list_node));
  if (node == NULL) {
    printf("malloc list node failed, exit!!!\n");
    exit(-1);
  }
  memset(node, 0x0, sizeof(list_node));
  memcpy(node->buffer, message, msg_len);
  node->peer_id = peer_id;

  pj_list_push_back(head->prev, node);
  PrintNodeCount(head);
}

void SignalingClient_PopMsgAndStartIce()
{
  //pop message if state == idle
  list_node* head = &icedemo.list;
  if (icedemo.in_nego == PJ_FALSE) {//if nego not complete, and new msg coming, do not pop msg now
    if (pj_list_empty(head) == PJ_FALSE) {
      list_node* node_pop = head->next;

      pj_mutex_lock(icedemo.socket_mutex);
      ice_start(node_pop->buffer, strlen(node_pop->buffer), node_pop->peer_id);
      pj_mutex_unlock(icedemo.socket_mutex);

      pj_list_erase(node_pop);
	    free(node_pop);
      node_pop = NULL;
    }
  }
}


void SignalingClient_OnMessageFromPeer(signaling_client *SC,
                                       int peer_id,
                                       const char* message,
                                       int msg_len) 
{

  

  if (msg_len == (sizeof(kByeMessage) - 1) &&
      strncmp(message, kByeMessage, sizeof(kByeMessage) - 1) == 0) {
    SignalingClient_DestroyPeer(SC, peer_id);
  } else {
    assert(SC->my_id != peer_id );
    assert(message && msg_len);

    if (-1 != SignalingClient_FindPeer(SC, peer_id)) {
      int pos = FindSubStr(message, SDP_FLAG);
      if (pos == -1) {
        printf("recv msg, but not sdp information. peer_id %d return\n", peer_id);
        return;
      } else {
        //fifo, store msg into queue, wait state to idle and start init session and nego
        pj_mutex_lock(app.pop_mutex);
        SignalingClient_PushBackNewMsg(message, msg_len, peer_id);
        SignalingClient_PopMsgAndStartIce();
        pj_mutex_unlock(app.pop_mutex);
      }
    } else {
      printf("(WARNING) << Received a message from unknown peer while already in a "
            "conversation with a different peer.\n");
      return;
    }
  }
  if (SC->state == CONNECTED) {
    SignalingClient_SocketCreate(SC, PJ_TRUE);
    SignalingClient_Connect(SC, &SC->saddr, SC->sock_get);
    SignalingClient_OnHangingGetConnectAndRecv(SC);
  }
  return;
}

pj_bool_t SignalingClient_GetHeaderValue( signaling_client *SC,
                                          const char* data,
                                          int data_len,
                                          int eoh,
                                          const char* header_pattern,
                                          int* value) 
{
  int found = FindSubStr(data, header_pattern);
  if (found != -1 && found < eoh) {
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
  if (found != -1 && found < eoh) {
    int begin = found + strlen(header_pattern);
    int end = FindSubStr(data + begin, "\r\n") + begin;
    if (end == -1)
      end = eoh;
    memcpy(value, data + begin, end - begin);
    return PJ_TRUE;
  }
  return PJ_FALSE;
}

pj_bool_t SignalingClient_ReadIntoBuffer(signaling_client *SC,
                                         pj_sock_t socket,
                                         char* data,
                                         int data_len,
                                         int* content_length)
 
{
  //if (!data || !SC || !content_length)
    //return;
  pj_status_t status;

  pj_bool_t ret = PJ_FALSE;
  int i = FindSubStr(data, "\r\n\r\n");
  if (i != -1) {   
    if (SignalingClient_GetHeaderValue(SC, data, data_len, i, "\r\nContent-Length: ", content_length)) {
      int total_response_size = (i + 4) + *content_length;
      if ((int)strlen(data) >= total_response_size) {
        ret = PJ_TRUE;
        char should_close[10] = {0};
        const char kConnection[] = "\r\nConnection: ";
        if (SignalingClient_GetHeaderValueStr(SC, data, data_len, i, kConnection, should_close) &&
            strncmp(should_close, "close", 5) == 0) {
          SignalingClient_SocketClose(SC, socket);
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
  if (pos != -1)
    status = atoi(&response[pos + 1]);
  return status;
}

pj_bool_t SignalingClient_ParseServerResponse(signaling_client *SC,
                                              pj_sock_t socket,
                                              const char* response,
                                              int content_length,
                                              int* peer_id,
                                              int* eoh)

{
  int status = SignalingClient_GetResponseStatus(response);
  if (status != 200) {
    printf("(LS_ERROR) << Received error from server socket %d peer_id %d\n", socket, peer_id);
	if (socket != -1)
	  SignalingClient_SocketClose(SC, socket);
    return PJ_FALSE;
  }

  *eoh = FindSubStr(response, "\r\n\r\n");
  assert(*eoh != -1);
  if (*eoh == -1)
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
  pj_status_t status = 0;

  
  do {
	SC->control_data_len = sizeof(SC->control_data);
    status = pj_sock_recv(SC->sock, SC->control_data, &SC->control_data_len, 0);
    if (status == PJ_SUCCESS)
      break;
  } while (PJ_TRUE);

  if (SignalingClient_ReadIntoBuffer(SC, SC->sock, SC->control_data, sizeof(SC->control_data), &content_length)) {
    int peer_id = 0, eoh = 0;
    pj_bool_t ok =
		SignalingClient_ParseServerResponse(SC, SC->sock, SC->control_data, content_length, &peer_id, &eoh);
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
            if (eol == -1)
              break;
            int id = 0;
            char name[MAX_CLIENT_NAME_LEN];
            pj_bool_t connected;
            char sub_control_data[1024] = {0};
            memcpy(sub_control_data, (char*)SC->control_data + pos, eol - pos);
			memset(name, 0x0, sizeof(name));
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
	  SC->control_data_len = 0;
    if (SC->state == SIGNING_IN) {
	    SC->state = CONNECTED;
      SignalingClient_SocketCreate(SC, PJ_TRUE);
      SignalingClient_Connect(SC, &SC->saddr, SC->sock_get);
      SignalingClient_OnHangingGetConnectAndRecv(SC);
    } 
  }
}

void SignalingClient_OnHangingGetRead(signaling_client *SC) 
{
  //printf("%s (INFO)", __func__);
  int content_length = 0;
  if (SignalingClient_ReadIntoBuffer(SC, SC->sock_get, SC->notification_data, 
                                      SC->notification_data_len, &content_length)) {
    int peer_id = 0, eoh = 0;
    pj_bool_t ok =
		SignalingClient_ParseServerResponse(SC, SC->sock_get, SC->notification_data, content_length, &peer_id, &eoh);

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
          } else {
            SignalingClient_DestroyPeer(SC, id);
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

  if (SC->is_connected_get == PJ_FALSE) {
	  SignalingClient_SocketCreate(SC, PJ_TRUE);
	  SignalingClient_Connect(SC, &SC->saddr, SC->sock_get);
    SignalingClient_OnHangingGetConnectAndRecv(SC);
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
  if (separator != -1) {
    *id = atoi(&entry[separator + 1]);
    memcpy(name, entry, separator);
    separator = FindSubStr(entry + separator + 1, ",") + separator + 1;
    if (separator != -1) {
      *connected = atoi(&entry[separator + 1]) ? PJ_TRUE : PJ_FALSE;
    }
  }
  return strlen(name) ? PJ_TRUE : PJ_FALSE;
}

void SignalingClient_OnClose(signaling_client *SC, int err) {
  printf("(INFO) %s\n", __FUNCTION__);
}
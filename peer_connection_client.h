/*
 *  Copyright 2011 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SIGNALING_CLIENT_PEER_CONNECTION_CLIENT_H_
#define SIGNALING_CLIENT_PEER_CONNECTION_CLIENT_H_

#include <pjlib.h>
#include <pj/sock.h>


#define MAX_CLIENT_NAME_LEN 512
#define MAX_ONCONNECT_DATA_LEN  2048
#define MAX_CONTROL_DATA_LEN 2048
#define MAX_NOTIFY_DATA_LEN 2048
#define MAX_PEERS_NUMBER  5

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

struct peers {
  int id;
  char name[MAX_CLIENT_NAME_LEN];
};
typedef struct signaling_client {
  State state;
  pj_sockaddr_in saddr; //server address info
  pj_sock_t sock;
  int my_id;
  char onconnect_data[MAX_ONCONNECT_DATA_LEN];
  char control_data[MAX_CONTROL_DATA_LEN];
  sc_observer_callback *callback;
  struct peers peers[MAX_PEERS_NUMBER];
  char notification_data[MAX_NOTIFY_DATA_LEN];
  char client_name[MAX_CLIENT_NAME_LEN];
} signaling_client;

signaling_client* SignalingClient_Create();
void SignalingClient_Destroy(signaling_client* SC);
void SignalingClient_Close(signaling_client* SC);

pj_sock_t SignalingClient_SocketCreate(signaling_client *SC); 
void SignalingClient_SocketClose(signaling_client* SC);

int SignalingClient_AllocPeer(signaling_client* SC);

pj_status_t SignalingClient_DestroyPeer(signaling_client* SC, int peer_id);

int SignalingClient_Connect(signaling_client *SC,
                              pj_sockaddr_in* server);

void SignalingClient_DoConnect(signaling_client *SC);

void SignalingClient_OnConnect(signaling_client *SC);

pj_bool_t SignalingClient_is_connected(signaling_client *SC);

void SignalingClient_RegisterObserver(signaling_client *SC,
                                      sc_observer_callback* callback);

pj_bool_t SignalingClient_SendToPeer( signaling_client *SC,
                                      int peer_id, 
                                      const char* message,
                                      int message_len);

void SignalingClient_OnMessageFromPeer(signaling_client *SC,
                                       int peer_id,
                                       const char* message,
                                       int msg_len);                                      

pj_bool_t SignalingClient_SendHangUp(signaling_client *SC, int peer_id);

pj_bool_t SignalingClient_SignOut(signaling_client *SC);

pj_bool_t SignalingClient_OnHangingGetConnect(signaling_client *SC);

pj_bool_t SignalingClient_GetHeaderValue( signaling_client *SC,
                                          const char* data,
                                          int data_len,
                                          int eoh,
                                          const char* header_pattern,
                                          int* value); 

pj_bool_t SignalingClient_GetHeaderValueStr( signaling_client *SC,
                                          const char* data,
                                          int data_len,
                                          int eoh,
                                          const char* header_pattern,
                                          char* value);


pj_bool_t SignalingClient_ReadIntoBuffer(signaling_client *SC,
                                         char* data,
                                         int data_len,
                                         int* content_length) ;

int SignalingClient_GetResponseStatus(const char* response);

pj_bool_t SignalingClient_ParseServerResponse(signaling_client *SC, 
                                              const char* response,
                                              int content_length,
                                              int* peer_id,
                                              int* eoh);

void SignalingClient_OnRead(signaling_client *SC);                                              
void SignalingClient_OnHangingGetRead(signaling_client *SC,
                                      pj_sock_t socket);

pj_bool_t SignalingClient_ParseEntry( const char* entry,
                                      char* name,
                                      int* id,
                                      pj_bool_t* connected);

void SignalingClient_OnClose(signaling_client *SC, int err);

#endif  // SIGNALING_CLIENT_PEER_CONNECTION_CLIENT_H_

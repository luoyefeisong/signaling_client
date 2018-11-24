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
#include <pjlib-util.h>
#include <pjnath.h>

#define MAX_CLIENT_NAME_LEN 128
#define MAX_BUFFER_LEN 2048
#define MAX_PEERS_NUMBER  500

#define SDP_FLAG "sdp info"

typedef struct list_node
{
    PJ_DECL_LIST_MEMBER(struct list_node);
    char buffer[MAX_BUFFER_LEN];
    int peer_id;
} list_node;


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
  pj_bool_t is_connected;
  pj_bool_t is_connected_get;  //used for http get wait
  pj_sockaddr_in saddr; //server address info
  pj_sock_t sock;
  pj_sock_t sock_get;   //used for http get wait
  int if_register;
  int my_id;
  char onconnect_data[MAX_BUFFER_LEN];
  char control_data[MAX_BUFFER_LEN];
  pj_ssize_t control_data_len;
  char notification_data[MAX_BUFFER_LEN];
  pj_ssize_t notification_data_len;
  char buffer[MAX_BUFFER_LEN];
  sc_observer_callback *callback;
  struct peers peers[MAX_PEERS_NUMBER];
  char client_name[MAX_CLIENT_NAME_LEN];
} signaling_client;

typedef struct app_t
{
	/* Command line options are stored here */
	struct options
	{
		unsigned comp_cnt;
		pj_str_t ns;
		int max_host;
		pj_bool_t regular;
		pj_str_t stun_srv;
		pj_str_t turn_srv;
		pj_bool_t turn_tcp;
		pj_str_t turn_username;
		pj_str_t turn_password;
		pj_bool_t turn_fingerprint;
		const char *log_file;
	} opt;

	/* Our global variables */
	pj_caching_pool cp;
	pj_pool_t *pool;
	pj_thread_t *thread;
	pj_bool_t thread_quit_flag;
	pj_ice_strans_cfg ice_cfg;
	pj_ice_strans *icest;
	FILE *log_fhnd;

	/* Variables to store parsed remote ICE info */
	struct rem_info
	{
		char ufrag[80];
		char pwd[80];
		unsigned comp_cnt;
		pj_sockaddr def_addr[PJ_ICE_MAX_COMP];
		unsigned cand_cnt;
		pj_ice_sess_cand cand[PJ_ICE_ST_MAX_CAND];
	} rem;
	signaling_client* SC;
	pj_bool_t in_nego;
	list_node list;
	list_node *tail;
  pj_bool_t need_pop_msg;
  pj_mutex_t* socket_mutex;
  void* active_sock_listen;
} icedemo_t;


extern char g_sdp_remote_buffer[4096];
extern icedemo_t icedemo;
extern char g_sdp_buffer[4096];

signaling_client* SignalingClient_Create();
void SignalingClient_Destroy(signaling_client* SC);
void SignalingClient_Close(signaling_client* SC);

pj_sock_t SignalingClient_SocketCreate(signaling_client *SC, pj_bool_t is_get); 

void SignalingClient_SocketClose(signaling_client* SC, pj_sock_t socket) ;

int SignalingClient_AllocPeer(signaling_client* SC);

pj_status_t SignalingClient_DestroyPeer(signaling_client* SC, int peer_id);

int SignalingClient_FindPeer(signaling_client* SC, int peer_id);

int SignalingClient_GetPeerByName(signaling_client *SC, pj_str_t* name);

int SignalingClient_Connect(signaling_client *SC,
                            pj_sockaddr_in* server,
                            pj_sock_t socket);

void SignalingClient_PopMsgAndStartIce();

void SignalingClient_DoConnect(signaling_client *SC);

void SignalingClient_OnConnect(signaling_client *SC, pj_sock_t socket);

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

pj_bool_t SignalingClient_OnHangingGetConnectAndRecv(signaling_client *SC);

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
                                         pj_sock_t socket,
                                         char* data,
                                         int data_len,
                                         int* content_length); 

int SignalingClient_GetResponseStatus(const char* response);

pj_bool_t SignalingClient_ParseServerResponse(signaling_client *SC, 
                                              pj_sock_t socket,
                                              const char* response,
                                              int content_length,
                                              int* peer_id,
                                              int* eoh);

void SignalingClient_OnRead(signaling_client *SC);                                              
void SignalingClient_OnHangingGetRead(signaling_client *SC);

pj_bool_t SignalingClient_ParseEntry( const char* entry,
                                      char* name,
                                      int* id,
                                      pj_bool_t* connected);

void SignalingClient_OnClose(signaling_client *SC, int err);

#endif  // SIGNALING_CLIENT_PEER_CONNECTION_CLIENT_H_

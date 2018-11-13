#include "conductor.h"
#include "defaults.h"

void Conductor_StartLogin(signaling_client* SC, 
                          pj_sockaddr_in* server)
{
  pj_sock_t socket;
  socket = SignalingClient_SocketCreate(SC);
  if (PJ_INVALID_SOCKET == socket )
  {
    return;
  }
  if (SignalingClient_is_connected(SC))
    return;
  SignalingClient_Connect(SC, server);
  SignalingClient_DoConnect(SC);
  SignalingClient_OnConnect(SC);

}
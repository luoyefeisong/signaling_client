#include "peer_connection_client.h"

#define SERVER_ADDR "132.232.3.109"
#define SERVER_PORT 8888

#define POOL_SIZE 10240

extern void Conductor_StartLogin(signaling_client* SC, 
						  pj_sockaddr_in* server);

extern void async_handler();

void start_signaling_client(signaling_client* SC);
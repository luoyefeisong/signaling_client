#include "peer_connection_client.h"
#include "conductor.h"
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>

#define SERVER_ADDR "132.232.3.109"
#define SERVER_PORT 8888

#define POOL_SIZE 10240

static struct app_t
{
	/* Our global variables */
	pj_caching_pool cp;
	pj_pool_t *pool;
	pj_thread_t *thread;
}app;

int main()
{
  /* Initialize the libraries before anything else */
  pj_init();
  pjlib_util_init();
  pjnath_init();

  signaling_client* SC = NULL;
  SC = SignalingClient_Create();
  if (!SC)
    printf("create client failed!!! \n");

  pj_sockaddr_in server = {0};
  pj_str_t str = { 0 };
  str.ptr = SERVER_ADDR;
  str.slen = strlen(SERVER_ADDR);
  server.sin_addr = pj_inet_addr(&str);
  server.sin_family = pj_AF_INET();
  server.sin_port = SERVER_PORT; 

  Conductor_StartLogin(SC, &server);

  SignalingClient_Destroy(SC);
}

void main_loop()
{
	pj_caching_pool cp
	pj_caching_pool_init(&icedemo.cp, NULL, 0);
	// Create pool.
	pool = pj_pool_create(mem, NULL, POOL_SIZE, 4000, NULL);
}
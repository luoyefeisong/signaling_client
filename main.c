#include "peer_connection_client.h"
#include "conductor.h"
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>
#include "defaults.h"

#define SERVER_ADDR "132.232.3.109"
#define SERVER_PORT 8888

#define POOL_SIZE 10240

const int max_msec = 500;

extern void async_handler();

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
  server.sin_port = pj_htons(SERVER_PORT); 

  async_handler();
  Conductor_StartLogin(SC, &server);
  pj_thread_join(app.thread);
  SignalingClient_Destroy(SC);
}

#if 1

static int worker_thread(void *unused)
{
	PJ_UNUSED_ARG(unused);
  int c = 0;

  do
	{
		c = pj_ioqueue_poll(app.ioqueue, NULL);
		if (c < 0)
		{
			pj_status_t err = pj_get_netos_error();
		    printf("poll error %d\n", err);
			return err;
		}

	} while(PJ_TRUE); //(c > 0 && net_event_count < MAX_NET_EVENTS);
/*
	count += net_event_count;
	if (p_count)
		*p_count = count;
*/
	return PJ_SUCCESS;
}
	
void async_handler()
{
  pj_status_t rc = 0;

	pj_caching_pool_init(&app.cp, NULL, 0);
 	// Create pool.
	app.pool = pj_pool_create(&app.cp.factory, "signaling client", 
                            POOL_SIZE, 4000, NULL);

  rc = pj_ioqueue_create(app.pool, 64, &app.ioqueue);
  if (rc != PJ_SUCCESS) {
    goto on_error;
  }
  rc = pj_thread_create(app.pool, "signaling client", &worker_thread,
						   NULL, 0, 0, &app.thread);
  if (rc != PJ_SUCCESS) {
	  goto on_error;
  }


  return;

on_error:
/*
  if (skey != NULL)
   	pj_ioqueue_unregister(skey);
  else if (ssock != PJ_INVALID_SOCKET)
	  pj_sock_close(ssock);
    
  if (ckey1 != NULL)
   	pj_ioqueue_unregister(ckey1);
  else if (csock1 != PJ_INVALID_SOCKET)
	  pj_sock_close(csock1);
    
  if (ckey0 != NULL)
   	pj_ioqueue_unregister(ckey0);
  else if (csock0 != PJ_INVALID_SOCKET)
	  pj_sock_close(csock0);
*/    
  if (app.ioqueue != NULL)
	  pj_ioqueue_destroy(app.ioqueue);

  pj_pool_release(app.pool);
  pj_caching_pool_destroy(&app.cp);
  return;  
}
#endif
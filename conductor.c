#include "conductor.h"
#include "defaults.h"

void Conductor_StartLogin(signaling_client* SC, 
                          pj_sockaddr_in* server)
{
  pj_sock_t socket;
  socket = SignalingClient_SocketCreate(SC, PJ_FALSE);
  if (PJ_INVALID_SOCKET == socket )
  {
    return;
  }
  if (SignalingClient_is_connected(SC))
    return;
  SignalingClient_Connect(SC, server, socket);
  SignalingClient_DoConnect(SC);
  SignalingClient_OnConnect(SC, socket);

}



void start_signaling_client(signaling_client* SC)
{
  pj_sockaddr_in server = {0};
  pj_str_t str = { 0 };
  str.ptr = SERVER_ADDR;
  str.slen = strlen(SERVER_ADDR);
  server.sin_addr = pj_inet_addr(&str);
  server.sin_family = pj_AF_INET();
  server.sin_port = pj_htons(SERVER_PORT); 
    
  

  async_handler();
  pj_mutex_create_recursive(app.pool, "mutex_pop_msg", &app.pop_mutex);
  Conductor_StartLogin(SC, &server);
  //pj_thread_join(app.thread);
  //SignalingClient_Destroy(SC);
}

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
                            POOL_SIZE, 1024, NULL);

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
  if (app.key != NULL) {
   	pj_ioqueue_unregister(app.key);
    app.key = NULL; 
  }

  if (app.ioqueue != NULL) {
	  pj_ioqueue_destroy(app.ioqueue);
    app.ioqueue = NULL;
  }
  pj_pool_release(app.pool);
  app.pool = NULL;

  pj_caching_pool_destroy(&app.cp);
  return;  
}

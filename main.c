#include "peer_connection_client.h"
#include "conductor.h"
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>
#include "defaults.h"




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

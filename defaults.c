/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "defaults.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma warning(disable : 4996)
#else
#include <unistd.h>
#endif

app_t app = {0};

const char kAudioLabel[] = "audio_label";
const char kVideoLabel[] = "video_label";
const char kStreamId[] = "stream_id";
const unsigned short kDefaultServerPort = 8888;


void GetEnvVarOrDefault( char* value,
                         const char* env_var_name,
                         const char* default_value) {
  const char* env_var = getenv(env_var_name);
  if (env_var)
    strcpy(value, env_var);

  if (*value == 0)
    strcpy(value, default_value);

  return;
}
/*
std::string GetPeerConnectionString() {
  return GetEnvVarOrDefault("WEBRTC_CONNECT", "stun:stun.l.google.com:19302");
}

std::string GetDefaultServerName() {
  return GetEnvVarOrDefault("SIGNALING_SERVER", "localhost");
}
*/

void GetPeerName(char* name) {
  char computer_name[256] = {0};
  char value[256] = {0};
  GetEnvVarOrDefault(value, "USERNAME", "user");
  strncpy(name, value, strlen(value));

  name[strlen(name)] = '@';
  
  if (gethostname(computer_name, ARRAYSIZE(computer_name)) == 0) {
    strcat(name, computer_name);
  } else {
    strcat(name, "host");
  }
  return;
}


int FindSubStr(const char *haystack, const char *needle)
{
	char *subStr = strstr(haystack, needle);
	if (subStr == NULL)
		return 0;
	else
		return subStr - haystack;

}
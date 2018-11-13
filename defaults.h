/*
 *  Copyright 2011 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXAMPLES_PEERCONNECTION_CLIENT_DEFAULTS_H_
#define EXAMPLES_PEERCONNECTION_CLIENT_DEFAULTS_H_

#include <stdint.h>
#include <pjlib.h>
typedef struct app_t
{
	/* Our global variables */
	pj_caching_pool cp;
	pj_pool_t *pool;
	pj_thread_t *thread;
  pj_ioqueue_t *ioqueue;
  pj_ioqueue_key_t *key;
}app_t;

extern app_t app;

extern const char kAudioLabel[];
extern const char kVideoLabel[];
extern const char kStreamId[];
extern const unsigned short kDefaultServerPort;

void GetEnvVarOrDefault(char* value,
                        const char* env_var_name,
                        const char* default_value);
void GetPeerName(char*);

int FindSubStr(const char *haystack, const char *needle);

#endif  // EXAMPLES_PEERCONNECTION_CLIENT_DEFAULTS_H_

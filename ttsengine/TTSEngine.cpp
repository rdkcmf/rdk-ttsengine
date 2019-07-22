/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2019 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include "TTSSpeaker.h"
#include "TTSSession.h"
#include "TTSManager.h"

#include "logger.h"
#include "glib_utils.h"
#include "rt_msg_dispatcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/audio.h>

#ifdef USE_BREAKPAD
#include "breakpad_wrapper.h"
#endif

#include <rtRemote.h>
#include <iostream>
#include <map>

namespace TTS {

#define TTS_MANAGER_RT_OBJECT_NAME "RT_TTS_MGR"

#define NULL_CHECK(a) ((a)?(a):"")
#define PRINT_CONFIG(a) TTSLOG_WARNING("%s = %s", a, NULL_CHECK(getenv(a)))

GMainLoop* gLoop;

} // namespace TTS

using namespace TTS;

int main() {
#ifdef USE_BREAKPAD
    breakpad_ExceptionHandler();
#endif

    int pipefd[2];
    GSource *source = NULL;

    /* start the gmain loop */
    gLoop = g_main_loop_new(g_main_context_default(), FALSE);

    PRINT_CONFIG("TTS_DEFAULT_LOG_LEVEL");
    PRINT_CONFIG("TTS_ENGINE_RT_LOG_LEVEL");
    PRINT_CONFIG("TTS_ENGINE_TEST_CLEANUP");
    PRINT_CONFIG("MAX_PIPELINE_FAILURE_THRESHOLD");

    // Initialization
    logger_init();
    rtLogSetLevel(getenv("TTS_ENGINE_RT_LOG_LEVEL") ? (rtLogLevel)atoi(getenv("TTS_ENGINE_RT_LOG_LEVEL")) : RT_LOG_INFO);

    // Install rt message dispatcher
    if(pipe(pipefd) == -1) {
        TTSLOG_ERROR("Can't create pipe, exiting app");
        return 1;
    }
    source = installRtRemoteMessageHandler(pipefd, g_main_loop_get_context(gLoop));

    rtError e = rtRemoteInit();
    if (e != RT_OK)
    {
        TTSLOG_ERROR("failed to initialize rtRemoteInit: %d", e);
        return 1;
    }

    gst_init(NULL, NULL);

    // Register Manager Remote Object
    rtObjectRef rtObj(new TTSManager);
    if (rtRemoteRegisterObject(TTS_MANAGER_RT_OBJECT_NAME, rtObj) != RT_OK) {
        TTSLOG_ERROR("failed to register remote object : %s", TTS_MANAGER_RT_OBJECT_NAME);
        abort();
    } else {
        TTSLOG_VERBOSE("Successfully registed TTS_Manager object \"%s\"", TTS_MANAGER_RT_OBJECT_NAME);
    }

    // Setup cleanup test trigger
    if(getenv("TTS_ENGINE_TEST_CLEANUP")) {
        int to = atoi(getenv("TTS_ENGINE_TEST_CLEANUP"));
        g_timeout_add_seconds(to, [](void *) -> int {
                g_main_loop_quit(gLoop); return G_SOURCE_REMOVE;
                }, NULL);
    }

    g_main_loop_run(gLoop);
    rtRemoteShutdown();
    gst_deinit();

    if(source)
        g_source_unref(source);
    close(pipefd[0]);
    close(pipefd[1]);
}


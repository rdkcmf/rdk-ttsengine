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
#ifndef _RT_REMOTE_MESSAGE_DISPATCHER_H_
#define _RT_REMOTE_MESSAGE_DISPATCHER_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <rtRemote.h>

#include "logger.h"
#include "glib_utils.h"
#include "rt_msg_dispatcher.h"

namespace TTS {

void processMessageInMainLoop(void*, void *data) {
    static char discard[1];
    if(data) {
        int ret = HANDLE_EINTR_EAGAIN(read(*(int*)data, discard, 1));
        if (-1 == ret) {
            TTSLOG_ERROR("unable to read from pipe");
        }

        rtError err = rtRemoteProcessSingleItem();
        if (err == RT_ERROR_QUEUE_EMPTY) {
            TTSLOG_TRACE("queue was empty upon processing event");
        }
    }
}

void rtRemoteQueueReadyHandler(void *data)
{
    static char temp[1];
    if(data) {
        int ret = HANDLE_EINTR_EAGAIN(write(*(int*)data, temp, 1));
        if (ret == -1)
            TTSLOG_ERROR("can't write to pipe");
    }
}

GSource *installRtRemoteMessageHandler(int pipefd[2], GMainContext* context) {
    GSource *source = create_and_setup_source(pipefd[PIPE_LISTEN], processMessageInMainLoop, NullCB, &pipefd[PIPE_LISTEN]);
    g_source_attach(source, context);
    rtRemoteRegisterQueueReadyHandler(rtEnvironmentGetGlobal(), rtRemoteQueueReadyHandler, &pipefd[PIPE_WRITE]);
    return source;
}

} // namespace TTS

#endif

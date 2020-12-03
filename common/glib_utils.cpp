/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
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
#include "glib_utils.h"

#include <unistd.h>
#include <errno.h>
#include <stdio.h>

namespace TTS {

void NullCB(void*, void*) {return NULL;}

GSourceFuncs EventSource::sourceFuncs =
{
    /* prepare */
    [](GSource*, gint *timeout) -> gboolean
    {
        *timeout = -1;
        return FALSE;
    },
    /* check */
    [](GSource *base) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        return !!source->pfd.revents;
    },
    /* dispatch */
    [](GSource *base, GSourceFunc, gpointer) -> gboolean
    {
        auto* source = reinterpret_cast<EventSource*>(base);
        if (source->pfd.revents & G_IO_IN)
        {
            if(source->ioCB)
                source->ioCB(source, source->ctx);
        }

        if (source->pfd.revents & (G_IO_ERR | G_IO_HUP))
        {
            if(source->dCB)
                source->dCB(source, source->ctx);
            return FALSE;
        }

        source->pfd.revents = 0;
        return TRUE;
    },
    NULL,
    NULL,
    NULL
};

GSource* create_and_setup_source(int fd, SourceIOCallback IOCallBack, SourceDestroyCallback DestroyCallBack, void* ctx)
{
    // create source
    GSource* source = g_source_new(&EventSource::sourceFuncs, sizeof(EventSource));
    EventSource* Esource = (EventSource*) source;

    // attach pipe out FD to source
    Esource->pfd.fd = fd;
    Esource->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
    Esource->pfd.revents = 0;
    Esource->ioCB = IOCallBack;
    Esource->dCB = DestroyCallBack;
    Esource->ctx = ctx;
    Esource->sessionId = 0;
    g_source_add_poll(source, &Esource->pfd);

    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_can_recurse(source, TRUE);

    return source;
}

std::vector<std::string> split(const std::string &str, const char delim) {
    std::string::size_type prevPos = 0, pos = 0;
    std::vector<std::string> tokens;

    while(!str.empty() && (pos = str.find(delim, pos)) != std::string::npos) {
        tokens.push_back(str.substr(prevPos, pos - prevPos));
        prevPos = ++pos;
    }

    if(!str.empty() && prevPos < str.length())
        tokens.push_back(str.substr(prevPos, pos - prevPos));

    return tokens;
}

} // namespace TTS

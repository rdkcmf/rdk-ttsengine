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

#include "TTSEventSource.h"
#include "TTSCommon.h"
#include "logger.h"

namespace TTS {

rtDefineObject(TTSEventSource, rtObject);
rtDefineMethod(TTSEventSource, setListener);
rtDefineMethod(TTSEventSource, delListener);

rtError Emit::addListenerOrQueue(rtString eventName, const rtFunctionRef &f)
{
    if(!m_sendingEvents)
        addListener(eventName, f);
    else
        m_addListenerQueue.push(EventHandler{eventName, f});

    return RT_OK;
}

rtError Emit::delListenerOrQueue(rtString eventName, const rtFunctionRef &f)
{
    if(!m_sendingEvents)
        delListener(eventName, f);
    else
        m_delListenerQueue.push(EventHandler{eventName, f});

    return RT_OK;
}

rtError Emit::Send(int numArgs,const rtValue* args,rtValue* result)
{
    (void)result;
    rtError error = RT_OK;
    if (numArgs > 0) {
        m_sendingEvents = true;
        rtString eventName = args[0].toString();
        std::vector<_rtEmitEntry>::iterator itr = mEntries.begin();

        while(itr != mEntries.end()) {
            _rtEmitEntry& entry = (*itr);
            if(entry.n == eventName) {
                rtValue discard;
                // SYNC EVENTS
                error = entry.f->Send(numArgs-1, args+1, &discard);
                if (error != RT_OK)
                    TTSLOG_INFO("failed to send. %s", rtStrError(error));

                // EPIPE means it's disconnected
                if(error == rtErrorFromErrno(EPIPE) || error == RT_ERROR_STREAM_CLOSED || discard == TTS_OBJECT_DESTROYED) {
                    if(discard == TTS_OBJECT_DESTROYED)
                        TTSLOG_INFO("Client Destroyed, removing handler");
                    else
                        TTSLOG_INFO("Broken entry in mEntries, removing handler");
                    itr = mEntries.erase(itr);
                } else {
                    ++itr;
                }
            } else {
                ++itr;
            }
        }

        m_sendingEvents = false;
        EventHandler evtHandler;

        while(m_addListenerQueue.size() > 0) {
            evtHandler = m_addListenerQueue.front();
            addListener(evtHandler.eventName, evtHandler.function);
            m_addListenerQueue.pop();
        }

        while(m_delListenerQueue.size() > 0) {
            evtHandler = m_delListenerQueue.front();
            delListener(evtHandler.eventName, evtHandler.function);
            m_delListenerQueue.pop();
        }
    }

    return error;
}

rtError TTSEventSource::setListener(rtString eventName, const rtFunctionRef& f) {
    TTSLOG_WARNING("Add Listener for %s", eventName.cString());

    // This is a workaround for the bug in rtRemote (regression caused by https://github.com/pxscene/pxCore/pull/1111)
    // TODO: This must be removed once (https://github.com/pxscene/pxCore/pull/1415) is merged
    f->setHash(-1);

    return ((Emit*)m_emit.ptr())->addListenerOrQueue(eventName, f);
}

rtError TTSEventSource::delListener(rtString eventName, const rtFunctionRef& f) {
    TTSLOG_WARNING("Delete Listener for %s", eventName.cString());
    return ((Emit*)m_emit.ptr())->delListenerOrQueue(eventName, f);
}

rtError TTSEventSource::sendEvent(Event& event) {
    auto handleEvent = [](gpointer data) -> gboolean {
        TTSEventSource& self = *static_cast<TTSEventSource*>(data);
        rtObjectRef obj;

        if (!self.m_eventQueue.empty()) {
            {
                std::lock_guard<std::mutex> lock(self.m_mutex);
                obj = self.m_eventQueue.front();
                self.m_eventQueue.pop();
            }

            TTSLOG_WARNING("Sending event{name=%s}...", obj.get<rtString>("name").cString());
            rtError rc = self.m_emit.send(obj.get<rtString>("name"), obj);
            if (RT_OK != rc) {
                TTSLOG_ERROR("Can't send event{name=%s} to all listeners, error code: %d", obj.get<rtString>("name").cString(), rc);
            }

            // if timeout occurs do not increment hang detector or stream is closed disable hang detection.
            if (RT_ERROR_TIMEOUT == rc || rc == rtErrorFromErrno(EPIPE) || rc == RT_ERROR_STREAM_CLOSED) {
                if (!self.m_isRemoteClientHanging) {
                    self.m_isRemoteClientHanging = true;
                    TTSLOG_WARNING("Remote client is entered to a hanging state");
                }
                if (rc == rtErrorFromErrno(EPIPE) || rc == RT_ERROR_STREAM_CLOSED) {
                    TTSLOG_WARNING("Remote client connection seems to be closed/broken");
                    // Clear the listeners here
                    self.m_emit->clearListeners();
                }
            } else if (RT_OK == rc) {
                if (self.m_isRemoteClientHanging) {
                    self.m_isRemoteClientHanging = false;
                    TTSLOG_WARNING("Remote client is recovered after the hanging state");
                }
            }

            {
                if (!self.m_eventQueue.empty()) {
                    std::lock_guard<std::mutex> lock(self.m_mutex);
                    return G_SOURCE_CONTINUE;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(self.m_mutex);
            self.m_timeoutId = 0;
        }
        return G_SOURCE_REMOVE;
    };

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_eventQueue.push(event.object());
        if (m_timeoutId == 0) {
            m_timeoutId = g_timeout_add(0, handleEvent, (void*) this);
        }
    }

    return RT_OK;
}

void TTSEventSource::clear()
{
    if (m_timeoutId != 0) {
        g_source_remove(m_timeoutId);
        m_timeoutId = 0;
    }

    m_eventQueue = std::queue<rtObjectRef>();
    m_emit->clearListeners();
}

} // namespace TTS


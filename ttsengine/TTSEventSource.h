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
#ifndef _TTS_EVENT_EMITTER_H_
#define _TTS_EVENT_EMITTER_H_

#include <glib.h>
#include <rtRemote.h>

#include <queue>
#include <mutex>

namespace TTS {

class Event
{
public:
    Event(const char* eventName) : m_object(new rtMapObject) {
        m_object.set("name", eventName);
    }

    rtObjectRef object() const { return m_object; }
    void set(rtString p, rtValue v) { m_object.set(p, v); }
    rtString name() const { return m_object.get<rtString>("name"); }

private:
    rtObjectRef m_object;
};

class Emit : public rtEmit {
public:
    Emit() : m_sendingEvents(false) {}

    rtError addListenerOrQueue(rtString eventName, const rtFunctionRef &f);
    rtError delListenerOrQueue(rtString eventName, const rtFunctionRef &f);

    virtual rtError Send(int numArgs,const rtValue* args,rtValue* result) override;

private:
    bool m_sendingEvents;

    struct EventHandler {
        rtString eventName;
        rtFunctionRef function;
    };
    std::queue<EventHandler> m_addListenerQueue;
    std::queue<EventHandler> m_delListenerQueue;
};

class TTSEventSource : public rtObject {
public:
    rtDeclareObject(TTSEventSource, rtObject);
    rtMethod2ArgAndNoReturn("on", setListener, rtString, rtFunctionRef);
    rtMethod2ArgAndNoReturn("del", delListener, rtString, rtFunctionRef);

    rtError setListener(rtString eventName, const rtFunctionRef& f);
    rtError delListener(rtString eventName, const rtFunctionRef& f);

    TTSEventSource() :
        m_emit(new Emit()),
        m_timeoutId(0),
        m_isRemoteClientHanging(false) {
    }

    ~TTSEventSource() {
        if (m_timeoutId != 0)
            g_source_remove(m_timeoutId);
    }

    rtError sendEvent(Event& event);
    bool isRemoteClientHanging() const { return m_isRemoteClientHanging; }
    void clear();

private:
    rtEmitRef m_emit;
    std::mutex m_mutex;
    std::queue<rtObjectRef> m_eventQueue;
    int m_timeoutId;
    bool m_isRemoteClientHanging;
};

} // namespace TTS

#endif

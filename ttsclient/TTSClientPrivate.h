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
#ifndef _TTS_CLIENT_PRIVATE_H_
#define _TTS_CLIENT_PRIVATE_H_

#include <rtRemote.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>

#include "TTSClient.h"
#include "TTSErrors.h"
#include "glib_utils.h"

namespace TTS {

#define TTS_ENGINE_PROGRAM_NAME "TTSEngine"

bool isProgramRunning(const char* name);

struct CallbackDataWrapper {
    CallbackDataWrapper(void *data, bool connection) : m_data(data), m_isConnectionData(connection) {}

    void *data() { return m_data; };
    void clear() { m_data = NULL; }
    bool isConnectionCBData() { return m_isConnectionData; }

private:
    void *m_data;
    bool m_isConnectionData;
};

struct SessionInfo {
    SessionInfo() :
        m_appId(0),
        m_sessionId(0),
        m_gotResource(false),
        m_callback(NULL),
        m_callbackWrapper(new CallbackDataWrapper(this, false)) {}

    ~SessionInfo() {
        m_callbackWrapper->clear();
        m_rtEventCallback = NULL;
        m_callback = NULL;
        m_gotResource = 0;
        m_sessionId = 0;
        m_appId = 0;
    }

    uint32_t m_appId;
    uint32_t m_sessionId;
    std::string m_appName;
    bool m_gotResource;
    rtObjectRef m_session;
    TTSSessionCallback *m_callback;
    CallbackDataWrapper *m_callbackWrapper;
    rtRefT<rtFunctionCallback> m_rtEventCallback;
};

class TTSClientPrivate {
public:
    TTSClientPrivate(TTSConnectionCallback *client, bool discardRtDispatching=false);
    ~TTSClientPrivate();
    void cleanupConnection(bool serverCrash=false);
    // TTS Global APIs
    TTS_Error enableTTS(bool enable);
    TTS_Error setTTSConfiguration(Configuration &config);
    bool isTTSEnabled(bool forcefetch=false);
    bool isSessionActiveForApp(uint32_t appId);

    // Resource management APIs
    TTS_Error acquireResource(uint32_t appId);
    TTS_Error claimResource(uint32_t appId);
    TTS_Error releaseResource(uint32_t appId);

    // Session management APIs
    uint32_t /*sessionId*/ createSession(uint32_t sessionId, std::string appName, TTSSessionCallback *callback);
    TTS_Error destroySession(uint32_t sessionId);
    bool isActiveSession(uint32_t sessionId, bool forcefetch=false);

    // Speak APIs
    TTS_Error speak(uint32_t sessionId, SpeechData& data);
    TTS_Error abort(uint32_t sessionId);
    bool isSpeaking(uint32_t sessionId);

private:
    TTSClientPrivate(TTSClientPrivate&) = delete;

    rtObjectRef m_manager;
    bool m_connected;
    bool m_serverCrashed;
    bool m_ttsEnabled;
    bool *m_cachedEnableTTS;
    bool m_discardDispatchThread;
    bool m_quitStartupThread;
    Configuration *m_cachedConfig;
    std::thread *m_startupThread;
    std::mutex m_mutex;
    std::mutex m_startupThreadMutex;
    std::condition_variable m_startupThreadCondition;
    TTSConnectionCallback *m_callback;
    CallbackDataWrapper *m_callbackWrapper;
    rtRefT<rtFunctionCallback> m_rtEventCallback;
    std::map<uint32_t, SessionInfo*> m_sessionMap;
    ResourceAllocationPolicy m_policy;

    void echoSessionID(char *sessionId);
    bool findRemoteObject(std::string obj_name, uint32_t timeout_ms);
    void connectToTTSManager();

    static void InitializeRtRemote();
    static void rtServerCrashCB(void *data);
    static void StartDispatcherThread();
    static rtError onEventCB(int numArgs, const rtValue* args, rtValue* result, void* context);
    static rtError onConnectionEvent(const rtObjectRef &event, TTSClientPrivate *client);
    static rtError onSessionEvent(const rtObjectRef &event, SessionInfo *sessionInfo);

    static std::once_flag m_rtRemoteInit;
    static std::once_flag m_dispatchThreadCreated;
    static std::thread *m_dispatchThread;
    static GMainLoop *m_dispatcherMainLoop;
    static unsigned long m_count;
};

} // namespace TTS

#endif //_TTS_CLIENT_PRIVATE_H_

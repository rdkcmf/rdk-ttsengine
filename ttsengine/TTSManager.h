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
#ifndef _TTS_ENGINE_H_
#define _TTS_ENGINE_H_

#include "TTSCommon.h"
#include "TTSSession.h"
#include "glib_utils.h"

#include <map>
#include <mutex>
#include <thread>
#include <atomic>

namespace TTS {

#define CLIENT_MONITOR_SOCKET_PATH "/tmp/TTS_MANAGER_CLIENT_MONITOR"

class TTSManager : public TTSEventSource {
public:
    rtDeclareObject(TTSManager, TTSEventEmiter);

    TTSManager();
    virtual ~TTSManager();

    // TTS Global APIs
    rtMethod1ArgAndNoReturn("enableTTS", enableTTS, bool);
    rtMethodNoArgAndReturn("isTTSEnabled", isTTSEnabled, bool);
    rtMethod1ArgAndReturn("listVoices", listVoices, rtValue, rtObjectRef);
    rtMethod1ArgAndNoReturn("setConfiguration", setConfiguration, rtObjectRef);
    rtMethodNoArgAndReturn("getConfiguration", getConfiguration, rtObjectRef);
    rtMethod1ArgAndReturn("isSessionActiveForApp", isSessionActiveForApp, uint32_t, bool);

    rtError enableTTS(bool enable);
    rtError isTTSEnabled(bool &enabled);
    rtError listVoices(rtValue language, rtObjectRef &voices);
    rtError setConfiguration(rtObjectRef configuration);
    rtError getConfiguration(rtObjectRef &configuration);
    rtError isSessionActiveForApp(uint32_t appid, bool &active);

    // Resource management APIs
    rtMethodNoArgAndReturn("getResourceAllocationPolicy", getResourceAllocationPolicy, rtValue);
    rtMethod1ArgAndReturn("reservePlayerResource", reservePlayerResource, uint32_t, rtValue);
    rtMethod1ArgAndReturn("releasePlayerResource", releasePlayerResource, uint32_t, rtValue);

    rtError getResourceAllocationPolicy(rtValue &policy);
    rtError reservePlayerResource(uint32_t appId, rtValue &result, bool internalReq=false);
    rtError releasePlayerResource(uint32_t appId, rtValue &result, bool internalReq=false);

    /***************** For Override Control *****************/
    rtMethod1ArgAndReturn("claimPlayerResource", claimPlayerResource, uint32_t, rtValue);
    rtError claimPlayerResource(uint32_t appId, rtValue &result);
    /***************** For Override Control *****************/

    // Session control functions
    rtMethod3ArgAndReturn("createSession", createSession, uint32_t, rtString, rtObjectRef, rtObjectRef);
    rtMethod1ArgAndReturn("destroySession", destroySession, uint32_t, rtValue);

    rtError createSession(uint32_t appId, rtString appName, rtObjectRef eventCallbacks, rtObjectRef &sessionObject);
    rtError destroySession(uint32_t sessionId, rtValue &result);

private:
    using ID_Session_Map=std::map<uint32_t, TTSSession*>;
    ID_Session_Map m_appMap;
    ID_Session_Map m_sessionMap;

    using ConnectionMap=std::map<int, EventSource*>;
    ConnectionMap m_connectionMap;

    TTSConfiguration m_defaultConfiguration;
    ResourceAllocationPolicy m_policy;
    uint32_t m_reservationForApp;
    uint32_t m_reservedApp;
    uint32_t m_claimedApp;
    TTSSession *m_activeSession;
    TTSSpeaker *m_speaker;
    std::thread *m_thread;
    bool m_monitorClients;
    bool m_claimedSession;
    bool m_ttsEnabled;
    std::mutex m_mutex;

    TTSConfiguration& loadConfigurationsFromFile(rtString configFile);
    void setResourceAllocationPolicy(ResourceAllocationPolicy policy);
    void makeSessionActive(TTSSession *session);
    void makeSessionInActive(TTSSession *session);
    void makeReservedOrClaimedSessionActive();

    static void MonitorClients(void *ctx);
    static void MonitorClientsSourceIOCB(void *source, void *ctx);
    static void MonitorClientsSourceDestroyedCB(void *source, void *ctx);
};

} // namespace TTS

#endif

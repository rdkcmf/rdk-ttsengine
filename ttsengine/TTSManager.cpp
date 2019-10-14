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

#include "TTSManager.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <iostream>
#include <fstream>
#include <regex>
#include <map>
#include <set>

namespace TTS {

extern GMainLoop* gLoop;

#define TTS_CONFIGURATION_FILE "/opt/tts/tts.ini"
#define RESERVATION_POLICY_STRING "Reservation"

#define _return(tts_code) result.setUInt8(tts_code); return RT_OK;

#define CHECK_RETURN_IF_FAIL(condition, errString) do {\
    if(!(condition)) { \
        TTSLOG_ERROR("%s", (errString)); \
        return; \
    } } while(0)

#define CHECK_RT_INPUT_AND_UPDATE(rtObj, prop, tS, evaluate) do {\
    if(!rtObj || rtObj.get(prop, tS) != RT_OK) { \
        TTSLOG_ERROR("%s property is not found in object", prop); \
    }  else { evaluate; } } while(0)

// Find the session information
#define FIND_SESSION_OR_RETURN(sessionId) \
    ID_Session_Map::iterator it; \
    TTSSession* session = NULL; \
    if((it = m_sessionMap.find(sessionId)) == m_sessionMap.end()) { \
        TTSLOG_ERROR("Session \"%u\" not found in session map", sessionId); \
        _return(TTS_NO_SESSION_FOUND); \
    } \
    session = (TTSSession*)it->second; (void)session;
// -------------------------

//Define TTSManager object
rtDefineObject(TTSManager, TTSEventSource);

//Define TTSManager object methods
rtDefineMethod(TTSManager, enableTTS);
rtDefineMethod(TTSManager, isTTSEnabled);
rtDefineMethod(TTSManager, listVoices);
rtDefineMethod(TTSManager, setConfiguration);
rtDefineMethod(TTSManager, getConfiguration);
rtDefineMethod(TTSManager, isSessionActiveForApp);

rtDefineMethod(TTSManager, createSession);
rtDefineMethod(TTSManager, destroySession);
rtDefineMethod(TTSManager, getResourceAllocationPolicy);
rtDefineMethod(TTSManager, reservePlayerResource);
rtDefineMethod(TTSManager, releasePlayerResource);
rtDefineMethod(TTSManager, claimPlayerResource);

// -------------------------

uint32_t nextSessionId() {
    static uint32_t counter = 0;
    return ++counter;
}

void TTSManager::MonitorClientsSourceIOCB(void *source, void *ctx) {
    TTSLOG_TRACE("TTSManager::MonitorClientsSourceIOCB");

    char buf[256];
    EventSource *s = (EventSource*)source;
    TTSManager *manager = (TTSManager*)ctx;

    if(!s || !manager) {
        TTSLOG_WARNING("Null Source | Null Manager in %s", __FUNCTION__);
        return;
    }

    int rc = read(s->pfd.fd,buf,sizeof(buf));
    if(rc > 0) {
        buf[rc] = '\0';
        TTSLOG_VERBOSE("Read %d bytes from fd=%d, data=%s", rc, s->pfd.fd, buf);

        // Update TTSManager's connection map
        s->sessionId = std::atol(buf);
        manager->m_connectionMap[s->pfd.fd] = s;
    }
}

void TTSManager::MonitorClientsSourceDestroyedCB(void *source, void *ctx) {
    TTSLOG_TRACE("TTSManager::MonitorClientsSourceDestroyedCB");

    EventSource *s = (EventSource*)source;
    TTSManager *manager = (TTSManager*)ctx;

    if(!s || !manager) {
        TTSLOG_WARNING("Null Source | Null Manager in %s", __FUNCTION__);
        return;
    }

    TTSLOG_WARNING("Source with fd=%d is closed, its session \"%u\" will be destroyed",
            s->pfd.fd, s->sessionId);

    // Remove the session from TTSManager
    rtValue result;
    manager->destroySession(s->sessionId, result);
}

void TTSManager::MonitorClients(void *ctx) {
    TTSLOG_TRACE("TTSManager::MonitorClients");

    TTSManager* manager = (TTSManager*)ctx;
    struct sockaddr_un addr;
    int serverFd, connectedFd;

    TTSLOG_INFO("Starting thread...");
    if ( (serverFd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        TTSLOG_ERROR("socket error");
        exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLIENT_MONITOR_SOCKET_PATH, sizeof(addr.sun_path)-1);
    unlink(CLIENT_MONITOR_SOCKET_PATH);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        TTSLOG_ERROR("bind error");
        exit(-1);
    }

    if (listen(serverFd, 10) == -1) {
        TTSLOG_ERROR("listen error");
        exit(-1);
    }

    while (manager && manager->m_monitorClients) {
        if ( (connectedFd = accept(serverFd, NULL, NULL)) == -1) {
            TTSLOG_ERROR("accept error : %s", strerror(errno));
            continue;
        }

        TTSLOG_INFO("New session connected with fd=%d", connectedFd);
        GSource *gs = create_and_setup_source(connectedFd, MonitorClientsSourceIOCB, MonitorClientsSourceDestroyedCB, manager);
        g_source_attach(gs, g_main_loop_get_context(gLoop));
    }

    TTSLOG_INFO("Exiting thread...");
    close(serverFd);
    unlink(CLIENT_MONITOR_SOCKET_PATH);
}

// -------------------------

TTSConfiguration& TTSManager::loadConfigurationsFromFile(rtString configFileName) {
    TTSLOG_TRACE("Reading configuration file");

    // Read configuration file and update m_defaultConfiguration
    std::ifstream configFile(configFileName, std::ios::in);

    if(configFile.is_open()) {
        std::cmatch m;
        std::string line;
        std::map<std::string, std::string> configSet;
        std::regex re("\\s*([a-zA-Z0-9_-]+)\\s*=\\s*([^ ]+).*$");

        while(1) {
            if(std::getline(configFile, line)) {
                if(!line.empty() && std::regex_match(line.c_str(), m, re) && m.size() >= 3)
                    configSet[m[1].str()] = m[2].str();
            } else
                break;
        }

        std::map<std::string, std::string>::iterator it;
        if((it = configSet.find("TTSEndPoint")) != configSet.end()) {
            m_defaultConfiguration.setEndPoint(it->second.c_str());
            configSet.erase(it);
        }

        if((it = configSet.find("SecureTTSEndPoint")) != configSet.end()) {
            m_defaultConfiguration.setSecureEndPoint(it->second.c_str());
            configSet.erase(it);
        }

        if((it = configSet.find("Language")) != configSet.end()) {
            m_defaultConfiguration.setLanguage(it->second.c_str());
            configSet.erase(it);
        }

        if((it = configSet.find("Voice")) != configSet.end()) {
            m_defaultConfiguration.setVoice(it->second.c_str());
            configSet.erase(it);
        }

        if((it = configSet.find("Volume")) != configSet.end()) {
            m_defaultConfiguration.setVolume(std::stod(it->second));
            configSet.erase(it);
        }

        if((it = configSet.find("Rate")) != configSet.end()) {
            m_defaultConfiguration.setRate(std::stoi(it->second));
            configSet.erase(it);
        }

        ResourceAllocationPolicy policy = OPEN;
        if((it = configSet.find("ResourceAccessPolicy")) != configSet.end()) {
            std::string &policyStr = it->second;
            if(!policyStr.empty() && policyStr == RESERVATION_POLICY_STRING)
                policy = RESERVATION;
            configSet.erase(it);
        }
        setResourceAllocationPolicy(policy);

        m_defaultConfiguration.m_others = std::move(configSet);

        configFile.close();
    } else {
        TTSLOG_ERROR("Configuration file \"%s\" is not found, using defaults", configFileName.cString());
    }

    TTSLOG_WARNING("TTSEndPoint : %s", m_defaultConfiguration.endPoint().cString());
    TTSLOG_WARNING("SecureTTSEndPoint : %s", m_defaultConfiguration.secureEndPoint().cString());
    TTSLOG_WARNING("Language : %s", m_defaultConfiguration.language().cString());
    TTSLOG_WARNING("Voice : %s", m_defaultConfiguration.voice().cString());
    TTSLOG_WARNING("Volume : %lf", m_defaultConfiguration.volume());
    TTSLOG_WARNING("Rate : %u", m_defaultConfiguration.rate());

    auto it = m_defaultConfiguration.m_others.begin();
    while( it != m_defaultConfiguration.m_others.end()) {
        TTSLOG_WARNING("%s : %s", it->first.c_str(), it->second.c_str());
        ++it;
    }

    return m_defaultConfiguration;
}

TTSManager::TTSManager() :
    m_policy(INVALID_POLICY),
    m_reservationForApp(0),
    m_reservedApp(0),
    m_claimedApp(0),
    m_activeSession(NULL),
    m_speaker(NULL),
    m_thread(NULL),
    m_monitorClients(true),
    m_claimedSession(false),
    m_ttsEnabled(false) {
    TTSLOG_TRACE("TTSManager::TTSManager");

    // Load configuration from file
    loadConfigurationsFromFile(TTS_CONFIGURATION_FILE);

    // Setup Speaker passing the read configuration
    m_speaker = new TTSSpeaker(m_defaultConfiguration);

    // Start client monitor thread
    m_thread = new std::thread(MonitorClients, this);
}

TTSManager::~TTSManager() {
    TTSLOG_ERROR("TTSManager::~TTSManager");

    // Clear active session
    if(m_activeSession) {
        m_claimedSession = false;
        m_activeSession->setInactive();
        m_activeSession = NULL;
    }

    // Clear All Sessions
    m_sessionMap.clear();
    m_appMap.clear();

    // Clear Speaker Instance
    if(m_speaker) {
        delete m_speaker;
        m_speaker = NULL;
    }

    // Attempt to stop the MonitorClients thread
    if(m_monitorClients && m_thread) {
        m_monitorClients = false;
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if(fd>0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));

            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, CLIENT_MONITOR_SOCKET_PATH, sizeof(addr.sun_path)-1);
            connect(fd, (struct sockaddr*)&addr, sizeof(addr));
            m_thread->join();
            close(fd);

            delete m_thread;
            m_thread = NULL;
        }
    }
}

rtError TTSManager::enableTTS(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if(m_ttsEnabled != enable) {
        m_ttsEnabled = enable;
        TTSLOG_INFO("TTS is %s", enable ? "Enabled" : "Disabled");

        Event d("tts_state_changed");
        d.set("enabled", m_ttsEnabled);
        sendEvent(d);

        if(m_ttsEnabled) {
            if(m_policy == RESERVATION) {
                makeReservedOrClaimedSessionActive();
            } else {
                if(!m_sessionMap.empty())
                    m_speaker->ensurePipeline(true);

                TTSLOG_INFO("Making all the sessions active");
                for(ID_Session_Map::iterator it = m_sessionMap.begin(); it != m_sessionMap.end(); ++it)
                    it->second->setActive(m_speaker, false);
            }
        } else {
            if(m_policy == RESERVATION) {
                makeSessionInActive(m_activeSession);
            } else {
                m_speaker->ensurePipeline(false);
                TTSLOG_INFO("Making all the sessions inactive");
                for(ID_Session_Map::iterator it = m_sessionMap.begin(); it != m_sessionMap.end(); ++it)
                    it->second->setInactive(false);
            }
        }
    }

    return RT_OK;
}

rtError TTSManager::isTTSEnabled(bool &enabled) {
    enabled = m_ttsEnabled;
    return RT_OK;
}

rtError TTSManager::listVoices(rtValue language, rtObjectRef &voices) {
    bool returnCurrentConfiguration = false;
    std::string key = std::string("voice_for_"); // return all voices

    if(language.isEmpty() || language.toString().isEmpty()) {
        returnCurrentConfiguration = true; // return voice for the configured language
        key = m_defaultConfiguration.language();
    } else if(language != "*") {
        key += language.toString().cString(); // return voices for only the passed language
    }

    rtArrayObject *voicearray = new rtArrayObject;
    if(returnCurrentConfiguration) {
        TTSLOG_INFO("Retrieving voice configured for language=%s", key.c_str());
        voicearray->pushBack(m_defaultConfiguration.voice());
    } else {
        TTSLOG_INFO("Retrieving voice list for language key=%s", key.c_str());
        auto it = m_defaultConfiguration.m_others.begin();
        while(it != m_defaultConfiguration.m_others.end()) {
            if(it->first.find(key.c_str()) == 0)
                voicearray->pushBack(rtString(it->second.c_str()));
            ++it;
        }
    }

    voices = voicearray;
    return RT_OK;
}

rtError TTSManager::setConfiguration(rtObjectRef configuration) {
    TTSLOG_VERBOSE("Setting Default Configuration");

    double d;
    uint8_t ui;
    rtString s;
    rtString v = m_defaultConfiguration.voice();

    m_mutex.lock();
    CHECK_RT_INPUT_AND_UPDATE(configuration, "ttsEndPoint", s, m_defaultConfiguration.setEndPoint(s));
    CHECK_RT_INPUT_AND_UPDATE(configuration, "ttsEndPointSecured", s, m_defaultConfiguration.setSecureEndPoint(s));
    CHECK_RT_INPUT_AND_UPDATE(configuration, "language", s, m_defaultConfiguration.setLanguage(s));
    CHECK_RT_INPUT_AND_UPDATE(configuration, "voice", s, m_defaultConfiguration.setVoice(s));
    CHECK_RT_INPUT_AND_UPDATE(configuration, "volume", d, m_defaultConfiguration.setVolume(d));
    CHECK_RT_INPUT_AND_UPDATE(configuration, "rate", ui, m_defaultConfiguration.setRate(ui));

    if(m_defaultConfiguration.endPoint().isEmpty() && !m_defaultConfiguration.secureEndPoint().isEmpty())
        m_defaultConfiguration.setEndPoint(m_defaultConfiguration.secureEndPoint());
    else if(m_defaultConfiguration.secureEndPoint().isEmpty() && !m_defaultConfiguration.endPoint().isEmpty())
        m_defaultConfiguration.setSecureEndPoint(m_defaultConfiguration.endPoint());
    else if(m_defaultConfiguration.endPoint().isEmpty() && m_defaultConfiguration.secureEndPoint().isEmpty())
        TTSLOG_WARNING("TTSEndPoint & SecureTTSEndPoints are empty!!!");

    // Pass newly set configuration to all the sessions
    // WARN : This might over write the session specific configurations (TBD)
    ID_Session_Map::iterator it = m_sessionMap.begin();
    while(it != m_sessionMap.end()) {
        it->second->setConfiguration(m_defaultConfiguration);;
        ++it;
    }
    m_mutex.unlock();

    TTSLOG_INFO("Default config updated, endPoint=%s, secureEndPoint=%s, lang=%s, voice=%s, vol=%lf, rate=%u",
            m_defaultConfiguration.endPoint().cString(),
            m_defaultConfiguration.secureEndPoint().cString(),
            m_defaultConfiguration.language().cString(),
            m_defaultConfiguration.voice().cString(),
            m_defaultConfiguration.volume(),
            m_defaultConfiguration.rate());

    if(v != m_defaultConfiguration.voice()) {
        Event d("voice_changed");
        d.set("voice", m_defaultConfiguration.voice());
        sendEvent(d);
    }

    return RT_OK;
}

rtError TTSManager::getConfiguration(rtObjectRef &configuration) {
    TTSLOG_VERBOSE("Getting Default Configuration");

    configuration = new rtMapObject;
    configuration.set("ttsEndPoint", m_defaultConfiguration.endPoint());
    configuration.set("ttsEndPointSecured", m_defaultConfiguration.secureEndPoint());
    configuration.set("language", m_defaultConfiguration.language());
    configuration.set("voice", m_defaultConfiguration.voice());
    configuration.set("volume", m_defaultConfiguration.volume());
    configuration.set("rate", (uint8_t)m_defaultConfiguration.rate());

    return RT_OK;
}

rtError TTSManager::isSessionActiveForApp(uint32_t appid, bool &active) {
    if(m_policy == RESERVATION) {
        active = (m_activeSession && m_activeSession->appId() == appid);
    } else {
        active = (m_appMap.find(appid) != m_appMap.end());
    }
    return RT_OK;
}

rtError TTSManager::createSession(uint32_t appId, rtString appName, rtObjectRef eventCallbacks, rtObjectRef &sessionObject) {
    TTSSession *session = NULL;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        sessionObject = new rtMapObject;

        // Check for duplicate App IDs / Sessions
        ID_Session_Map::iterator it = m_sessionMap.begin();
        while(it != m_sessionMap.end()) {
            session = it->second;
            if(session->appId() == appId)
                break;
            ++it;
        }

        if(it != m_sessionMap.end() && session) {
            TTSLOG_ERROR("Application \"%s\" already has a session \"%u\"", appName.cString(), session->sessionId());
            sessionObject.set("result", TTS_CREATE_SESSION_DUPLICATE);
            return RT_OK;
        }

        // Create a session
        uint32_t sessionId = nextSessionId();
        session = new TTSSession(appId, appName, sessionId, m_defaultConfiguration);

        // Update return values
        sessionObject.set("session", session);
        sessionObject.set("id", sessionId);
        sessionObject.set("ttsEnabled", m_ttsEnabled);

        // Update session map
        m_appMap[appId] = session;
        m_sessionMap[sessionId] = session;
        TTSLOG_INFO("New session \"%u\" created for app (%u, %s, %p)...",
                sessionId, appId, appName.cString(), session);

        if(m_policy == RESERVATION) {
            // Allocate resource when it is reserved for this session
            makeReservedOrClaimedSessionActive();
        } else {
            // Make all the new sessions active to enable them speak any time
            if(m_ttsEnabled) {
                m_speaker->ensurePipeline(true);
                session->setActive(m_speaker, false);
            }
        }

        sessionObject.set("result", TTS_OK);
    }

    if(eventCallbacks) {
        rtString key;
        uint32_t length;
        rtFunctionRef ref;
        rtObjectRef allKeys;

        if(eventCallbacks.get("allKeys", allKeys) == RT_OK) {
            if(allKeys.get("length", length) == RT_OK) {
                for(uint32_t i = 0; i < length; ++i) {
                    if(allKeys.get(i, key) == RT_OK && eventCallbacks.get(key.cString(), ref) == RT_OK) {
                        session->send("on", key, ref);
                    }
                }
            }
        }
    }

    return RT_OK;
}

rtError TTSManager::destroySession(uint32_t sessionId, rtValue &result) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Find the session information
    FIND_SESSION_OR_RETURN(sessionId);

    // Deactivate session
    if(m_policy == RESERVATION)
        releasePlayerResource(session->appId(), result, true);
    else
        session->setInactive(false);

    // Remove from the map
    m_sessionMap.erase(it);

    ID_Session_Map::iterator ait = m_appMap.find(session->appId());
    if(ait != m_appMap.end())
        m_appMap.erase(ait);

    TTSLOG_WARNING("Session \"%u\" with AppID \"%u\" is destroyed, map_size=%d", sessionId, session->appId(), m_sessionMap.size());

    // Remove ConnectionMap Entry
    EventSource *es = NULL;
    ConnectionMap::iterator citr = m_connectionMap.begin();
    while(citr != m_connectionMap.end()) {
        es = (EventSource*)citr->second;
        if(es->sessionId == sessionId) {
            int fd = es->pfd.fd;
            g_source_remove_poll((GSource*)es, &es->pfd);
            g_source_destroy((GSource*)es);
            g_source_unref((GSource*)es);
            m_connectionMap.erase(citr);
            close(fd);
            break;
        }
        ++citr;
    }

    if(m_sessionMap.size() == 0) {
        TTSLOG_WARNING("All sessions were destroyed, destroy pipeline");
        m_speaker->ensurePipeline(false);
    }

    _return(TTS_OK);
}

void TTSManager::setResourceAllocationPolicy(ResourceAllocationPolicy policy) {
    if(m_policy != policy) {
        if(policy == PRIORITY) {
            TTSLOG_WARNING("Priority based resource allocation is not implemented now, falling back to Open policy");
            policy = OPEN;
        }

        m_policy = policy;
        TTSLOG_INFO("%s Policy is in effect", (policy == RESERVATION) ? "Reservation" : ((policy == OPEN) ? "Open" : "Priority"));
    }
}

rtError TTSManager::getResourceAllocationPolicy(rtValue &policy) {
    policy = m_policy;
    return RT_OK;
}

void TTSManager::makeSessionActive(TTSSession *session) {
    if(session && m_activeSession != session) {
        m_speaker->ensurePipeline(true);
        session->setActive(m_speaker);
        m_activeSession = session;
        TTSLOG_INFO("Reserved Resource, RequestingAppName = \"%s\", AppId = \"%u\" is made active",
                session->appName().cString() ? session->appName().cString() : "Null", session->appId());
    }
}

void TTSManager::makeSessionInActive(TTSSession *session) {
    if(session && m_activeSession == session) {
        session->setInactive();
        m_speaker->ensurePipeline(false);
        m_activeSession = NULL;
        TTSLOG_INFO("Released Resource, RequestingAppName = \"%s\", AppId = \"%u\" is made in-active",
                session->appName().cString() ?session->appName().cString() : "Null", session->appId());
    }
}

void TTSManager::makeReservedOrClaimedSessionActive() {
    if(m_ttsEnabled) {
        uint32_t appid = m_claimedApp ? m_claimedApp : m_reservedApp;
        if(appid) {
            // Find the App information
            ID_Session_Map::iterator ait;
            if((ait = m_appMap.find(appid)) == m_appMap.end()) {
                // Session is not found, resource will be alloted to the App when it is created
                TTSLOG_WARNING("No App is live with ID \"%u\", will be granted resource on creation", appid);
            } else {
                // Make the requested App active
                makeSessionActive((TTSSession*)ait->second);
            }
        }
    }
}

rtError TTSManager::reservePlayerResource(uint32_t appId, rtValue &result, bool internalReq) {
    if(m_policy != RESERVATION) {
        TTSLOG_WARNING("%s policy is in effect, skipping reservation request", (m_policy == PRIORITY) ? "Priority based" : "Open");
        _return(TTS_OK);
    }

    // Lock would be already held by claimPlayerResource()
    if(!internalReq)
        std::lock_guard<std::mutex> lock(m_mutex);

    TTSLOG_INFO("Request to reserve the Player for %u, reservedApp=%u, claimedApp=%u, activeApp=%u",
            appId, m_reservedApp, m_claimedApp, m_activeSession ? m_activeSession->appId() : 0);

    if (!appId) {
        TTSLOG_ERROR("Empty input");
        _return(TTS_EMPTY_APPID_INPUT);
    }

    if(m_reservedApp != 0) {
        if(m_reservedApp == appId) {
            TTSLOG_WARNING("App \"%u\" already holds the Player", appId);
            _return(TTS_OK);
        } else {
            TTSLOG_ERROR("Resource is already reserved for app \"%u\", should be released first", m_reservedApp);
            _return(TTS_RESOURCE_BUSY);
        }
    }

    m_reservedApp = appId;

    makeReservedOrClaimedSessionActive();

    _return(TTS_OK);
}

rtError TTSManager::releasePlayerResource(uint32_t appId, rtValue &result, bool internalReq) {
    if(m_policy != RESERVATION) {
        TTSLOG_WARNING("%s policy is in effect, skipping release request", (m_policy == PRIORITY) ? "Priority based" : "Open");
        _return(TTS_OK);
    }

    if(!internalReq)
        std::lock_guard<std::mutex> lock(m_mutex);

    TTSLOG_INFO("Request to release the Player from %u, reservedApp=%u, claimedApp=%u, activeApp=%u",
            appId, m_reservedApp, m_claimedApp, m_activeSession ? m_activeSession->appId() : 0);

    if (!appId) {
        TTSLOG_ERROR("Empty input");
        _return(TTS_EMPTY_APPID_INPUT);
    }

    // Handle releasing of claimed resource first
    if(m_claimedApp != 0 && m_claimedApp == appId) {
        m_claimedApp = 0;
        // Releasing of resource should happen irrespective of TTS Enabled / not
        if(m_activeSession && m_activeSession->appId() == appId && appId != m_reservedApp) {
            makeSessionInActive(m_activeSession);
            makeReservedOrClaimedSessionActive();
        }
        _return(TTS_OK);
    }

    // Handle releasing of normal reserved app
    if(m_reservedApp != 0) {
        if(m_reservedApp != appId) {
            TTSLOG_ERROR("App \"%u\" doesn't own the resource", appId);
            _return(TTS_FAIL);
        } else {
            // Releasing of resource should happen irrespective of TTS Enabled / not
            if(m_activeSession && m_activeSession->appId() == appId)
                makeSessionInActive(m_activeSession);
            m_reservedApp = 0;
        }
    }

    _return(TTS_OK);
}

rtError TTSManager::claimPlayerResource(uint32_t appId, rtValue &result) {
    if(m_policy != RESERVATION) {
        TTSLOG_WARNING("%s policy is in effect, skipping claim request", (m_policy == PRIORITY) ? "Priority based" : "Open");
        _return(TTS_OK);
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    TTSLOG_INFO("Request to claim the Player for %u, reservedApp=%u, claimedApp=%u, activeApp=%u",
            appId, m_reservedApp, m_claimedApp, m_activeSession ? m_activeSession->appId() : 0);

    if (!appId) {
        TTSLOG_ERROR("Empty input");
        _return(TTS_EMPTY_APPID_INPUT);
    } else if (m_appMap.find(appId) == m_appMap.end()) {
        TTSLOG_ERROR("App \"%u\" not found in the map", appId);
        _return(TTS_APP_NOT_FOUND);
    }

    if(m_claimedApp) {
        if(m_claimedApp == appId) {
            // Reject recursive claim requests
            TTSLOG_WARNING("App is still holding claimed the resource");
            _return(TTS_OK);
        } else {
            // Reject nested claim requests
            TTSLOG_ERROR("Resource had already been claimed by another App, couldn't satisfy request");
            _return(TTS_RESOURCE_BUSY);
        }
    }

    m_claimedApp = appId;

    if(m_reservedApp) {
        if(m_reservedApp == m_claimedApp) {
            // Active App request "claim"
            m_claimedApp = 0;
            TTSLOG_WARNING("App already holds the resource");
            _return(TTS_OK);
        } else {
            rtValue v;
            uint32_t tmp = m_reservedApp;
            releasePlayerResource(m_reservedApp, v, true);

            // Keep previously reserved app, so that once claimed app is done with the resource
            // it can be assigned back to the reserved app
            m_reservedApp = tmp;
        }
    }

    makeReservedOrClaimedSessionActive();

    _return(!m_ttsEnabled ? TTS_OK : ((m_activeSession && m_activeSession->appId() == m_claimedApp) ? TTS_OK : TTS_FAIL));
}

} // namespace TTS

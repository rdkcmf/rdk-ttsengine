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

#include "TTSClientPrivate.h"

#include "logger.h"
#include "glib_utils.h"
#include "rt_msg_dispatcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>

// --- //

namespace TTS {

#define TTS_MANAGER_RT_OBJECT_NAME "RT_TTS_MGR"
#define CLIENT_MONITOR_SOCKET_PATH "/tmp/TTS_MANAGER_CLIENT_MONITOR"

#define CHECK_CONNECTION_RETURN_ON_FAIL(ret) do {\
    if(!m_connected) { \
        TTSLOG_ERROR("Connection to TTS manager is not establised"); \
        return ret; \
    } } while(0)

#define CHECK_SESSION_RETURN_ON_FAIL(id, sessionitr, sessioninfo, ret) do { \
    sessionitr = m_sessionMap.find(id); \
    if(sessionitr == m_sessionMap.end()) { \
        TTSLOG_ERROR("TTS Session is not created"); \
        return ret; \
    } \
    sessioninfo = sessionitr->second; \
    } while(0)

#define INSTALL_HANDLER_CHECK_RESULT(obj, event, callback) do {\
    if(obj.send("on", event, callback) != RT_OK) { \
        TTSLOG_ERROR("Installing \"%s\" event handler failed", event); \
    } else { \
        TTSLOG_INFO("Installed handler for event \"%s\"", event); \
    } } while(0)

#define UNINSTALL_HANDLER_CHECK_RESULT(obj, event, callback) do {\
    if(obj.send("del", event, callback) != RT_OK) { \
        TTSLOG_ERROR("Uninstalling \"%s\" event handler failed", event); \
    } else { \
        TTSLOG_INFO("Uninstalled handler for event \"%s\"", event); \
    } } while(0)

#define SET_UNSET_EXTENDED_EVENT(sessionInfo, input_event_list, event_flag, event_name) do { \
    uint32_t event = (uint32_t)(event_flag); \
    if((input_event_list & event) && !(sessionInfo->m_extendedEvents & event)) { \
        INSTALL_HANDLER_CHECK_RESULT(sessionInfo->m_session, event_name, sessionInfo->m_rtEventCallback.ptr()); \
        sessionInfo->m_extendedEvents |= event; \
    } else if(!(input_event_list & event) && (sessionInfo->m_extendedEvents & event)) { \
        UNINSTALL_HANDLER_CHECK_RESULT(sessionInfo->m_session, event_name, sessionInfo->m_rtEventCallback.ptr()); \
        sessionInfo->m_extendedEvents &= ~event; \
    } } while(0)

#define UNUSED(x) (void)(x)

// --- //

static bool doesMatchPidWithName(long int pid, const char* name)
{
    bool matched = false;
    char readBuf[32];
    char cmdlinePath[64];

    snprintf(cmdlinePath, sizeof(cmdlinePath), "/proc/%ld/cmdline", pid);
    int fd = open(cmdlinePath, O_NONBLOCK | O_RDONLY);
    if (fd > 0) {
        if (read(fd, &readBuf, sizeof(readBuf)) > 0)
            matched = (strncmp(readBuf, name, sizeof(readBuf)) == 0);
        close(fd);
    }

    return matched;
}

bool isProgramRunning(const char* name)
{
    DIR* dir;
    struct dirent* ent;
    bool found = false;
    static long int lastPid = 0;
    long int pid = 0;

    if(lastPid && doesMatchPidWithName(lastPid, name))
        return true;
    lastPid = 0;

    if (!(dir = opendir("/proc"))) {
        perror("can't open /proc");
        return false;
    }

    while((ent = readdir(dir)) != NULL) {
        pid = atol(ent->d_name);
        if(pid <= 0)
            continue;

        if(doesMatchPidWithName(pid, name)) {
            lastPid = pid;
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

// Static Member Definition & Initialization
std::once_flag TTSClientPrivate::m_rtRemoteInit;
std::once_flag TTSClientPrivate::m_dispatchThreadCreated;
std::thread *TTSClientPrivate::m_dispatchThread = NULL;
GMainLoop *TTSClientPrivate::m_dispatcherMainLoop = NULL;
unsigned long TTSClientPrivate::m_count = 0;

// --- //

static inline const char *policyStr(ResourceAllocationPolicy policy) {
    switch(policy) {
        case RESERVATION: return "Reservation";
        case PRIORITY: return "Priority";
        case OPEN: return "Open";
        default: return "*Invalid*";
    }
}

// --- //

void TTSClientPrivate::InitializeRtRemote() {
    // Log level setting
    rtLogSetLevel(getenv("TTS_CLIENT_RT_LOG_LEVEL") ? (rtLogLevel)atoi(getenv("TTS_CLIENT_RT_LOG_LEVEL")) : RT_LOG_INFO);

    // Initialize rtRemote
    if (rtRemoteInit() != RT_OK) {
        rtLogError("failed to initialize rtRemoteInit");
    }
}

void TTSClientPrivate::rtServerCrashCB(void *data) {
    TTSClientPrivate *self = (TTSClientPrivate *)data;
    if(self->m_callback) {
        TTSLOG_ERROR("Connection to TTSManager got closed, i.e TTSManager crashed");
        self->m_callback->onTTSServerClosed();
        self->cleanupConnection(true);
    }
}

bool TTSClientPrivate::findRemoteObject(std::string obj_name, uint32_t timeout_ms) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    TTSLOG_VERBOSE("Attempting to find remote object %s\n", obj_name.c_str());

    rtObjectRef obj;
    rtError rc = rtRemoteLocateObject(obj_name.c_str(), obj, timeout_ms /*milliSecs*/, rtServerCrashCB, this);
    if (rc != RT_OK) {
        TTSLOG_ERROR("TTSClientPrivate failed to find object: %s rc %d\n", obj_name.c_str(), rc);
    } else {
        TTSLOG_INFO("Remote object found.\n");
        m_manager = obj;
        return true;
    }

    return false;
}

void TTSClientPrivate::connectToTTSManager() {
    static std::mutex sMutex;
    static unsigned char sTimeOut = 30;

    while(!m_quitStartupThread) {
        {
            // Serialize finding TTS Manager rtObject
            std::lock_guard<std::mutex> lock(sMutex);

            // Find TTS Manager object
            if(!m_manager && !findRemoteObject("RT_TTS_MGR", 10000 /*milli seconds*/)) {
                TTSLOG_ERROR("Couldn't connect to TTS Manager");
                return;
            } else {
                rtValue value = -1;
                if(m_manager.sendReturns("getResourceAllocationPolicy", value) == RT_OK) {
                    m_policy = (ResourceAllocationPolicy)value.toInt8();
                    TTSLOG_INFO("%d(%s) policy is enforced by TTSEngine", m_policy, policyStr(m_policy));
                }

                m_rtEventCallback = new rtFunctionCallback(onEventCB, m_callbackWrapper);
                INSTALL_HANDLER_CHECK_RESULT(m_manager, "tts_state_changed", m_rtEventCallback.ptr());
                INSTALL_HANDLER_CHECK_RESULT(m_manager, "voice_changed", m_rtEventCallback.ptr());
                m_connected = true;

                if(m_cachedEnableTTS) {
                    TTSLOG_INFO("Set cached enableTTS=%d", *m_cachedEnableTTS);
                    enableTTS(*m_cachedEnableTTS);
                    delete m_cachedEnableTTS;
                    m_cachedEnableTTS = NULL;
                }

                if(m_cachedConfig) {
                    TTSLOG_INFO("Set cached TTS Configuration (\"%s\", \"%s\", \"%s\", \"%s\", %lf, %d)",
                            m_cachedConfig->ttsEndPoint.c_str(),
                            m_cachedConfig->ttsEndPointSecured.c_str(),
                            m_cachedConfig->language.c_str(),
                            m_cachedConfig->voice.c_str(),
                            m_cachedConfig->volume,
                            m_cachedConfig->rate);
                    setTTSConfiguration(*m_cachedConfig);
                    delete m_cachedConfig;
                    m_cachedConfig = NULL;
                }

                // Check if this needs to be done on main thread
                if(m_callback)
                    m_callback->onTTSServerConnected();

                TTSLOG_INFO("Connected to TTS Manager");
            }
        }

        // Wait till the connection breaks
        TTSLOG_WARNING("Will be monitoring TTSEngine crash!!!");
        std::unique_lock<std::mutex> mlock(m_startupThreadMutex);
        m_startupThreadCondition.wait(mlock, [this] () { return m_serverCrashed || m_quitStartupThread; });
        if(m_serverCrashed) {
            TTSLOG_WARNING("TTSEngine crashed!!!");
        } else {
            TTSLOG_WARNING("Startup thread is forced to exit!!!");
            return;
        }

        // Wait 30 seconds for TTSEngine to start
        TTSLOG_WARNING("Waiting for TTSEngine to be started!!!");
        std::lock_guard<std::mutex> lock(sMutex);
        TTSLOG_WARNING("Checking TTSEngine process, Time Out = %d!!!", sTimeOut);
        while(!m_quitStartupThread && !isProgramRunning(TTS_ENGINE_PROGRAM_NAME) && sTimeOut) {
            sleep(1);
            sTimeOut--;
        }

        if(m_quitStartupThread) {
            TTSLOG_WARNING("Startup thread is forced to exit!!!");
        } else {
            if(sTimeOut) {
                m_serverCrashed = false;
                TTSLOG_WARNING("TTSEngine started!!!");

                // Give TTSEngine time to setup the remote object and initialize
                // First thread which detects TTSEngine restart should wait for
                // this extra 3 seconds, other threads need not since this code part is
                // serialized.
                if(sTimeOut < 30) {
                    sleep(3);
                    sTimeOut = 30;
                }
            } else {
                TTSLOG_WARNING("TTSEngine is not started!!!");
                m_quitStartupThread = true;
            }
        }
    }
}

void TTSClientPrivate::StartDispatcherThread() {
    // Install rt message dispatcher
    m_dispatchThread = new std::thread([]() {
        TTSLOG_INFO("Starting Dispatch thread %ld", syscall(__NR_gettid));

        int pipefd[2];
        GMainContext *context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GMainLoop *loop = g_main_loop_new(context, FALSE);
        m_dispatcherMainLoop = loop;

        if(pipe(pipefd) == -1) {
            TTSLOG_ERROR("Can't create pipe, can't install rtRemoteQueueReadyHandler");
            return;
        }

        // Set GSource to call rtRemoteProcessSingleItem() on rt message arrival
        GSource *source = installRtRemoteMessageHandler(pipefd, context);

        // Run main loop
        g_main_loop_run(loop);

        // Cleanup
        TTSLOG_INFO("Cleaning up dispatcher thread");
        g_source_unref(source);
        g_main_loop_unref(loop);
        g_main_context_unref(context);
        close(pipefd[0]);
        close(pipefd[1]);
        TTSLOG_INFO("Dispatcher thread exit");
    });
}

TTSClientPrivate::TTSClientPrivate(TTSConnectionCallback *callback, bool discardRtDispatching) :
    m_connected(false),
    m_serverCrashed(false),
    m_ttsEnabled(false),
    m_cachedEnableTTS(NULL),
    m_discardDispatchThread(discardRtDispatching),
    m_quitStartupThread(false),
    m_cachedConfig(NULL),
    m_startupThread(NULL),
    m_callback(callback),
    m_callbackWrapper(new CallbackDataWrapper(this, true)),
    m_policy(INVALID_POLICY) {
    TTSLOG_INFO("Constructing TTSClientPrivate");
    ++m_count;

    // Initialize rtRemote
    std::call_once(m_rtRemoteInit, InitializeRtRemote);

    // Install rt message dispatcher
    if(!discardRtDispatching)
        std::call_once(m_dispatchThreadCreated, StartDispatcherThread);

    // Attempt to find TTS Engine's remote  object on caller's thread
    findRemoteObject(TTS_MANAGER_RT_OBJECT_NAME, 100);

    // Find TTS Engine's remote object (if not already) & do post connect initialization
    m_startupThread = new std::thread(&TTSClientPrivate::connectToTTSManager, this);
}

TTSClientPrivate::~TTSClientPrivate() {
    TTSLOG_INFO("Destroying TTS Client");
    cleanupConnection();

    // Stop rtObject detection thread
    if(m_startupThread) {
        m_quitStartupThread = true;
        m_startupThreadCondition.notify_one();
        m_startupThread->join();
        delete m_startupThread;
        m_startupThread = NULL;
        TTSLOG_INFO("Startup thread is stopped");
    }

    // Stop dispatcher thread when no more needed
    if(--m_count == 0 && m_dispatchThread) {
        if(m_dispatcherMainLoop) {
            g_main_loop_quit(m_dispatcherMainLoop);
            m_dispatcherMainLoop = NULL;
        }

        m_dispatchThread->join();
        delete m_dispatchThread;
        m_dispatchThread = NULL;
        TTSLOG_INFO("Dispatcher thread is stopped");
    }
}

void TTSClientPrivate::cleanupConnection(bool serverCrash) {
    TTSLOG_WARNING("Cleaning up TTS Connection");
    bool tconnected = m_connected;
    m_connected = false;

    rtValue result;
    if(tconnected) {
        if(serverCrash) {
            m_serverCrashed = true;
            TTSLOG_ERROR("Notify the thread to monitor if TTSEngine is starting in 30 seconds");
            m_startupThreadCondition.notify_one();
        } else {
            // Destroy sessions
            auto it = m_sessionMap.begin();
            while(it != m_sessionMap.end()) {
                m_manager.sendReturns("destroySession", it->first, result);
                delete (SessionInfo*)it->second;
                it = m_sessionMap.erase(it);
            }

            // Cleanup TTSClient callbacks
            m_callbackWrapper->clear();
            rtRemoteUnregisterDisconnectedCallback(rtServerCrashCB, this);
        }
    }
    m_manager = NULL;

    m_rtEventCallback = NULL;
}

TTS_Error TTSClientPrivate::enableTTS(bool enable) {
    if(!m_connected) {
        TTSLOG_WARNING("Connection to TTS manager is not establised, caching input");
        if(!m_cachedEnableTTS)
            m_cachedEnableTTS = new bool;
        *m_cachedEnableTTS = enable;
        return TTS_OK;
    }

    if(m_manager.send("enableTTS", enable) != RT_OK) {
        TTSLOG_ERROR("Couldn't %s TTS", enable ? "enable" : "disable");
        return TTS_FAIL;
    }

    TTSLOG_VERBOSE("TTS is set to %s", enable ? "enabled" : "disabled");
    return TTS_OK;
}

TTS_Error TTSClientPrivate::listVoices(std::string &language, std::vector<std::string> &voices) {
    if(m_connected) {
        rtObjectRef voicearray;
        if(m_manager.sendReturns("listVoices", rtString(language.c_str()), voicearray) != RT_OK) {
            TTSLOG_ERROR("Couldn't retrieve voice list");
            return TTS_FAIL;
        }

        rtValue v;
        rtArrayObject* array = (rtArrayObject*) voicearray.getPtr();
        uint32_t len = voicearray.get<uint32_t>("length");
        for(uint32_t i = 0; i < len; i++) {
            if(array->Get(i, &v) == RT_OK)
                voices.push_back(v.toString().cString());
        }
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::setTTSConfiguration(Configuration &config) {
    if(!m_connected) {
        TTSLOG_WARNING("Connection to TTS manager is not establised, caching input");
        if(!m_cachedConfig)
            m_cachedConfig = new Configuration();
        if(!config.ttsEndPoint.empty())
            m_cachedConfig->ttsEndPoint = config.ttsEndPoint;
        if(!config.ttsEndPointSecured.empty())
            m_cachedConfig->ttsEndPointSecured = config.ttsEndPointSecured;
        if(!config.language.empty())
            m_cachedConfig->language = config.language;
        if(!config.voice.empty())
            m_cachedConfig->voice = config.voice;
        if(config.volume >= 1 && config.volume <= 100)
            m_cachedConfig->volume = config.volume;
        if(config.rate >= 1 && config.rate <= 100)
            m_cachedConfig->rate = config.rate;
        return TTS_OK;
    }

    rtObjectRef map = new rtMapObject;
    map.set("ttsEndPoint", config.ttsEndPoint.c_str());
    map.set("ttsEndPointSecured", config.ttsEndPointSecured.c_str());
    map.set("language", config.language.c_str());
    map.set("voice", config.voice.c_str());
    map.set("volume", config.volume);
    map.set("rate", config.rate);

    rtError rc = m_manager.send("setConfiguration", map);
    if(rc != RT_OK) {
        TTSLOG_ERROR("Couldn't set default configuration");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::getTTSConfiguration(Configuration &config) {
    if(!m_connected) {
        TTSLOG_WARNING("Connection to TTS manager is not establised");
        return TTS_FAIL;
    }

    rtObjectRef map;
    rtError rc = m_manager.sendReturns("getConfiguration", map);
    if(rc != RT_OK) {
        TTSLOG_ERROR("Couldn't get configuration");
        return TTS_FAIL;
    }

    config.ttsEndPoint = map.get<rtString>("ttsEndPoint").cString();
    config.ttsEndPointSecured = map.get<rtString>("ttsEndPointSecured").cString();
    config.language = map.get<rtString>("language").cString();
    config.voice = map.get<rtString>("voice").cString();
    config.volume = map.get<double>("volume");
    config.rate = map.get<uint8_t>("rate");

    return TTS_OK;
}

bool TTSClientPrivate::isTTSEnabled(bool force) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);

    if(!force) {
        return m_ttsEnabled;
    } else {
        bool enabled = false;
        if(m_manager.sendReturns("isTTSEnabled", enabled) != RT_OK) {
            TTSLOG_ERROR("Couldn't retrieve TTS enabled/disabled detail");
            return false;
        }

        m_ttsEnabled = enabled;
        TTSLOG_VERBOSE("TTS is %s", enabled ? "enabled" : "disabled");
        return enabled;
    }
}

bool TTSClientPrivate::isSessionActiveForApp(uint32_t appId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);
    UNUSED(appId);

    bool result = false;
    rtError rc = m_manager.sendReturns("isSessionActiveForApp", appId, result);
    if(rc != RT_OK) {
        TTSLOG_ERROR("Couldn't enquire session active state");
        return false;
    }

    return result;
}

TTS_Error TTSClientPrivate::acquireResource(uint32_t appId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);

    TTSLOG_VERBOSE("acquireResource");
    if(m_policy != RESERVATION) {
        TTSLOG_ERROR("Non-Reservation policy (i.e %s) is in effect, declining request", policyStr(m_policy));
        return TTS_POLICY_VIOLATION;
    }

    rtValue result;
    rtError rc = m_manager.sendReturns("reservePlayerResource", appId, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Couldn't request reservation of resource, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::claimResource(uint32_t appId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);

    TTSLOG_VERBOSE("claimResource");
    if(m_policy != RESERVATION) {
        TTSLOG_ERROR("Non-Reservation policy (i.e %s) is in effect, declining request", policyStr(m_policy));
        return TTS_POLICY_VIOLATION;
    }

    rtValue result;
    rtError rc = m_manager.sendReturns("claimPlayerResource", appId, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Couldn't claim reservation of resource, TTS Code = %d", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::releaseResource(uint32_t appId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);

    TTSLOG_VERBOSE("releaseResource");
    if(m_policy != RESERVATION) {
        TTSLOG_ERROR("Non-Reservation policy (i.e %s) is in effect, declining request", policyStr(m_policy));
        return TTS_POLICY_VIOLATION;
    }

    rtValue result;
    rtError rc = m_manager.sendReturns("releasePlayerResource", appId, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Resource release didn't succeed, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

void TTSClientPrivate::echoSessionID(char *sessionId) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd>0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, CLIENT_MONITOR_SOCKET_PATH, sizeof(addr.sun_path)-1);
        if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != -1) {
            if(write(fd, sessionId, strlen(sessionId)) <= 0) {
                TTSLOG_VERBOSE("write failed on fd = %d", fd);
            }
        }
    }
}

uint32_t TTSClientPrivate::createSession(uint32_t appId, std::string appName, TTSSessionCallback *callback) {
    CHECK_CONNECTION_RETURN_ON_FAIL(0);

    std::lock_guard<std::mutex> lock(m_mutex);

    SessionInfo *sessionInfo = new SessionInfo();
    sessionInfo->m_appId = appId;
    sessionInfo->m_appName = appName;
    sessionInfo->m_callback = callback;
    sessionInfo->m_rtEventCallback = new rtFunctionCallback(onEventCB, sessionInfo->m_callbackWrapper);

    rtObjectRef eventCallbacks = new rtMapObject;
    if(m_policy == RESERVATION) {
        eventCallbacks.set("resource_acquired", sessionInfo->m_rtEventCallback.ptr());
        eventCallbacks.set("resource_released", sessionInfo->m_rtEventCallback.ptr());
    } else {
        sessionInfo->m_gotResource = true;
    }

    rtObjectRef obj;
    rtError rc = m_manager.sendReturns("createSession", appId, rtString(appName.c_str()), eventCallbacks, obj);
    if(rc != RT_OK || obj.get<rtValue>("result").toUInt32() != TTS_OK) {
        TTSLOG_ERROR("Session couldn't be created for App (\"%u\", \"%s\")", appId, appName.c_str());
    } else {
        TTSLOG_VERBOSE("Created Session");

        sessionInfo->m_session = obj.get<rtObjectRef>("session");
        if(obj.get("id", sessionInfo->m_sessionId) == RT_OK) {
            m_sessionMap[sessionInfo->m_sessionId] = sessionInfo;
            TTSLOG_INFO("Session ID : %u", sessionInfo->m_sessionId);
            echoSessionID((char *)std::to_string(sessionInfo->m_sessionId).c_str());
            if(m_callback) {
                m_ttsEnabled = obj.get<bool>("ttsEnabled");
                m_callback->onTTSStateChanged(m_ttsEnabled);
            }

            INSTALL_HANDLER_CHECK_RESULT(sessionInfo->m_session, "started", sessionInfo->m_rtEventCallback.ptr());
            INSTALL_HANDLER_CHECK_RESULT(sessionInfo->m_session, "spoke", sessionInfo->m_rtEventCallback.ptr());
        } else {
            sessionInfo->m_sessionId = 0;
            TTSLOG_ERROR("Session ID couldn't be retrieved");
        }

        if(sessionInfo->m_callback)
            sessionInfo->m_callback->onTTSSessionCreated(sessionInfo->m_appId, sessionInfo->m_sessionId);
    }

    if(sessionInfo->m_sessionId) {
        TTSLOG_VERBOSE("Session Info for SessionId-%u is %p", sessionInfo->m_sessionId, sessionInfo);
        return sessionInfo->m_sessionId;
    } else {
        delete sessionInfo;
        return 0;
    }
}

TTS_Error TTSClientPrivate::destroySession(uint32_t sessionId) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);
    UNUSED(sessionId);

    rtValue result;
    rtError rc = m_manager.sendReturns("destroySession", sessionId, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Session couldn't be destroyed, Reasons, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }
    delete sessionInfo;
    m_sessionMap.erase(sessionItr);

    return TTS_OK;
}

bool TTSClientPrivate::isActiveSession(uint32_t sessionId, bool forcefetch) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(false);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, false);
    UNUSED(sessionId);

    if(forcefetch) {
        rtValue v;
        rtError rc = sessionInfo->m_session.get("isActive", v);
        if(rc != RT_OK) {
            TTSLOG_ERROR("Couldn't enquire session active state");
            return false;
        }
        sessionInfo->m_gotResource = v.toBool();
    }

    return sessionInfo->m_gotResource;
}

TTS_Error TTSClientPrivate::setPreemptiveSpeak(uint32_t sessionId, bool preemptive) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);

    rtValue result;
    rtError rc = sessionInfo->m_session.sendReturns("setPreemptiveSpeak", preemptive, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Coudn't set preemptive speak configuration, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::requestExtendedEvents(uint32_t sessionId, uint32_t extendedEvents) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);
    UNUSED(sessionId);

    SET_UNSET_EXTENDED_EVENT(sessionInfo, extendedEvents, EXT_EVENT_WILL_SPEAK, "willSpeak");
    SET_UNSET_EXTENDED_EVENT(sessionInfo, extendedEvents, EXT_EVENT_PAUSED, "paused");
    SET_UNSET_EXTENDED_EVENT(sessionInfo, extendedEvents, EXT_EVENT_RESUMED, "resumed");
    SET_UNSET_EXTENDED_EVENT(sessionInfo, extendedEvents, EXT_EVENT_CANCELLED, "cancelled");
    SET_UNSET_EXTENDED_EVENT(sessionInfo, extendedEvents, EXT_EVENT_INTERRUPTED, "interrupted");
    SET_UNSET_EXTENDED_EVENT(sessionInfo, extendedEvents, EXT_EVENT_NETWORK_ERROR, "networkerror");
    SET_UNSET_EXTENDED_EVENT(sessionInfo, extendedEvents, EXT_EVENT_PLAYBACK_ERROR, "playbackerror");

    rtError rc = sessionInfo->m_session.send("requestExtendedEvents", sessionInfo->m_extendedEvents);
    if(rc != RT_OK) {
        TTSLOG_ERROR("Couldn't request extended event notification");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::speak(uint32_t sessionId, SpeechData& data) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_ERROR("TTS is disabled, can't speak");
        return TTS_NOT_ENABLED;
    }

    if(!sessionInfo->m_gotResource) {
        TTSLOG_WARNING("Session is not active, can't speak");
        return TTS_SESSION_NOT_ACTIVE;
    }

    rtValue result;
    rtError rc = sessionInfo->m_session.sendReturns("speak", data.id, data.text.c_str(), data.secure, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Coudn't speak, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::abort(uint32_t sessionId, bool clearPending) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to abort");
        return TTS_OK;
    }

    rtValue result;
    rtError rc = RT_OK;
    if(clearPending)
        rc = sessionInfo->m_session.send("abortAndClearPending");
    else
        rc = sessionInfo->m_session.sendReturns("shut", result);
    if(rc != RT_OK || (!clearPending && result.toUInt8() != TTS_OK)) {
        TTSLOG_ERROR("Coudn't abort, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::pause(uint32_t sessionId, uint32_t speechId) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to pause");
        return TTS_OK;
    }

    rtValue result;
    rtError rc = sessionInfo->m_session.sendReturns("pause", speechId, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Coudn't pause, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivate::resume(uint32_t sessionId, uint32_t speechId) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to resume");
        return TTS_OK;
    }

    rtValue result;
    rtError rc = sessionInfo->m_session.sendReturns("resume", speechId, result);
    if(rc != RT_OK || result.toUInt8() != TTS_OK) {
        TTSLOG_ERROR("Coudn't resume, TTS Code = %u", result.toUInt8());
        return (TTS_Error)result.toUInt8();
    }

    return TTS_OK;
}

bool TTSClientPrivate::isSpeaking(uint32_t sessionId) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(false);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, false);
    UNUSED(sessionId);

    if(sessionInfo->m_gotResource) {
        rtValue v;
        rtError rc = sessionInfo->m_session.get("isSpeaking", v);
        if(rc != RT_OK) {
            TTSLOG_ERROR("Resource release didn't succeed");
            return false;
        }
        return v.toBool();
    }

    return false;
}

TTS_Error TTSClientPrivate::getSpeechState(uint32_t sessionId, uint32_t speechId, SpeechState &state) {
    SessionInfo *sessionInfo;
    std::map<uint32_t, SessionInfo*>::iterator sessionItr;

    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    CHECK_SESSION_RETURN_ON_FAIL(sessionId, sessionItr, sessionInfo, TTS_NO_SESSION_FOUND);
    UNUSED(sessionId);

    rtValue result;
    rtError rc = sessionInfo->m_session.sendReturns("getSpeechState", speechId, result);
    if(rc != RT_OK) {
        TTSLOG_ERROR("Couldn't retrieve speech state");
        return TTS_FAIL;
    }
    state = (SpeechState)result.toUInt8();

    return TTS_OK;
}

rtError TTSClientPrivate::onEventCB(int numArgs, const rtValue* args, rtValue* result, void* context) {
    rtError rc = RT_OK;

    if(context && numArgs == 1) {
        // Check if the CallbackData is valid (i.e TTSClientPrivate / SessionInfo is a valid & alive)
        CallbackDataWrapper *cbwrapper = (CallbackDataWrapper*)context;
        if(!cbwrapper->data()) {
            if(result) {
                // Getting here means, events are received for a client which is no more
                // Return custom return code, so that the EventSource removes the handler
                *result = rtValue(TTS_OBJECT_DESTROYED);
                delete cbwrapper;
            }
            return rc;
        }

        // Handle Client / Session Events
        if(cbwrapper->isConnectionCBData())
            rc = onConnectionEvent(args[0].toObject(), (TTSClientPrivate*)cbwrapper->data());
        else
            rc = onSessionEvent(args[0].toObject(), (SessionInfo*)cbwrapper->data());
    }

    if (result)
        *result = rtValue(RT_OK == rc);

    return rc;
}

rtError TTSClientPrivate::onConnectionEvent(const rtObjectRef &event, TTSClientPrivate *client) {
    rtValue val;
    if(event.get("name", val) == RT_OK) {
        if (val.toString() == "tts_state_changed") {
            TTSLOG_INFO("Got tts_state_changed event from TTS Manager for %p", client);
            client->m_ttsEnabled = event.get<bool>("enabled");
            if(client->m_callback)
                client->m_callback->onTTSStateChanged(client->m_ttsEnabled);
        } else if(val.toString() == "voice_changed") {
            std::string voice = event.get<rtString>("voice").cString();
            if(client->m_callback)
                client->m_callback->onVoiceChanged(voice);
            TTSLOG_INFO("Got voice_changed event from TTS Manager %p, new voice = %s", client, voice.c_str());
        }
    }

    return RT_OK;
}

rtError TTSClientPrivate::onSessionEvent(const rtObjectRef &event, SessionInfo *sessionInfo) {
    rtValue val;
    rtString eventName;
    if(event.get("name", val) == RT_OK) {
        eventName = val.toString();
        SpeechData d;
        if (eventName == "resource_acquired") {
            uint32_t session = event.get<rtValue>("session").toUInt32();
            TTSLOG_INFO("Got resource_acquired event from session %u", session);
            sessionInfo->m_gotResource = true;
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onResourceAcquired(sessionInfo->m_appId, session);
        } else if (eventName == "resource_released") {
            uint32_t session = event.get<rtValue>("session").toUInt32();
            TTSLOG_INFO("Got resource_released event from session %u", session);
            sessionInfo->m_gotResource = false;
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onResourceReleased(sessionInfo->m_appId, session);
        } else if(eventName == "willSpeak") {
            TTSLOG_INFO("Got willSpeak event from session %u", sessionInfo->m_sessionId);
            d.id = event.get<rtValue>("id").toUInt32();
            d.text = event.get<rtString>("text").cString();
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onWillSpeak(sessionInfo->m_appId, sessionInfo->m_sessionId, d);
        } else if(eventName == "started") {
            TTSLOG_INFO("Got started event from session %u", sessionInfo->m_sessionId);
            d.id = event.get<rtValue>("id").toUInt32();
            d.text = event.get<rtString>("text").cString();
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onSpeechStart(sessionInfo->m_appId, sessionInfo->m_sessionId, d);
        } else if(eventName == "paused") {
            TTSLOG_INFO("Got paused event from session %u", sessionInfo->m_sessionId);
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onSpeechPause(sessionInfo->m_appId, sessionInfo->m_sessionId, event.get<rtValue>("id").toUInt32());
        } else if(eventName == "resumed") {
            TTSLOG_INFO("Got resumed event from session %u", sessionInfo->m_sessionId);
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onSpeechResume(sessionInfo->m_appId, sessionInfo->m_sessionId, event.get<rtValue>("id").toUInt32());
        } else if(eventName == "cancelled") {
            if(sessionInfo->m_callback) {
                std::string ids = event.get<rtString>("ids").cString();
                char *token = strtok((char*)ids.c_str(), ",");
                uint32_t speechid = 0;
                while(token) {
                    speechid = atol(token);
                    TTSLOG_INFO("Got cancelled event from session %u, speech id %u", sessionInfo->m_sessionId, speechid);
                    sessionInfo->m_callback->onSpeechCancelled(sessionInfo->m_appId, sessionInfo->m_sessionId, speechid);
                    token = strtok(NULL, ",");
                }
            }
        } else if(eventName == "interrupted") {
            TTSLOG_INFO("Got interrupted event from session %u", sessionInfo->m_sessionId);
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onSpeechInterrupted(sessionInfo->m_appId, sessionInfo->m_sessionId, event.get<rtValue>("id").toUInt32());
        } else if(eventName == "networkerror") {
            TTSLOG_INFO("Got networkerror event from session %u", sessionInfo->m_sessionId);
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onNetworkError(sessionInfo->m_appId, sessionInfo->m_sessionId, event.get<rtValue>("id").toUInt32());
        } else if(eventName == "playbackerror") {
            TTSLOG_INFO("Got playbackerror event from session %u", sessionInfo->m_sessionId);
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onPlaybackError(sessionInfo->m_appId, sessionInfo->m_sessionId, event.get<rtValue>("id").toUInt32());
        } else if (eventName == "spoke") {
            TTSLOG_INFO("Got spoke event from session %u", sessionInfo->m_sessionId);
            d.id = event.get<rtValue>("id").toUInt32();
            d.text = event.get<rtString>("text").cString();
            if(sessionInfo->m_callback)
                sessionInfo->m_callback->onSpeechComplete(sessionInfo->m_appId, sessionInfo->m_sessionId, d);
        }
    }

    return RT_OK;
}

} // namespace TTS

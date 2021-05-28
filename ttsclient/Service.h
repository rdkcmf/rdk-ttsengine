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
#ifndef _SERVICES_H_
#define _SERVICES_H_

#ifndef MODULE_NAME
#define MODULE_NAME ttsengine
#endif

#include <WPEFramework/core/core.h>
#include <WPEFramework/plugins/Service.h>
#undef LOG
#include <thread>
#include <mutex>
#include <list>

#include <unistd.h>
#include <sys/syscall.h>

#ifndef _LOG_INFO
#define _LOG_INFO(fmt, ...) do { \
    printf("000000-00:00:00.000 [INFO] [tid=%ld] %s:%s:%d " fmt "\n", \
    syscall(__NR_gettid), \
    __FUNCTION__, basename(__FILE__), __LINE__, \
    __VA_ARGS__); \
} while(0)
#endif

namespace TTSThunderClient {

#define THUNDER_RPC_TIMEOUT 1000 /* milliseconds */

using namespace WPEFramework;

using StringList = std::list<std::string>;
using TimePoint = std::chrono::time_point<std::chrono::system_clock>;
using WPEFrameworkPlugin = WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>;
using WPEFrameworkPluginPtr = WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>*;

class Service
{
public:
    struct Client {
        virtual void onActivation() {}
        virtual void onDeactivation() {}
    };
    using ClientList = std::list<Service::Client*>;

    // To activate & initialize on service crash
    // Those should be done on a separate thread other than
    // the callback thread
    struct AsyncWorker {
        using Task = std::function<void (Service *service)>;
        using TaskList = std::list<Task>;

        AsyncWorker(Service *service) : m_service(service), m_thread(nullptr) {}
        ~AsyncWorker() { cleanup(); }

        void post(Task task);
        void cleanup();

        private:
        Service *m_service;
        std::thread *m_thread;

        bool m_running;
        TaskList m_tasklist;
        std::mutex m_mutex;

        friend class std::thread;
    };

public:
    Service(const char *callsign);
    virtual ~Service();

    const std::string &callsign() { return m_callSign; }
    virtual bool isActive(bool force=false);
    virtual void activate();

    void setSecurityTokenPayload(const char *payload);
    virtual void initialize(bool activateIfRequired = true);
    virtual void uninitialize();
    virtual bool initialized();

    void registerClient(Client *client);
    void unregisterClient(Client *client);
    bool get(std::string method, Core::JSON::String &response);
    bool invoke(std::string method, JsonObject &request, JsonObject &response);

    template<typename handler_t, typename object_t>
    bool subscribe(std::string event, handler_t handler, object_t object);

    // service crash handling
    virtual bool shouldActivateOnCrash() { return false; }
    virtual uint16_t healthThreshold() { return 5 * 60; }
    virtual uint8_t maxRestartsInMonitoringPeriod() { return 1; }
    virtual bool shouldExcludeRequestedDeactivations() { return true; }

protected:
    virtual void onActivation(bool requested);
    virtual void onDeactivation(bool requested);

    const std::string m_callSign;
    std::shared_ptr<WPEFrameworkPlugin> m_remoteObject;
    StringList m_eventsRegistered;
    ClientList m_clients;
    std::mutex m_mutex;
    bool m_active;
    bool m_activeQuerySuccess;
    std::string m_tokenPayload;
    std::string m_token;
    bool m_envOverride;

    static std::string getSecurityToken(const std::string &payload);
    static WPEFrameworkPluginPtr controller(const std::string &payload);

    // Reactivating crashed services
    bool lastSessionWasHealthy();
    bool isServiceUnstable();
    std::list<TimePoint> m_crashTimeStamps;

    void notifyClientsOfActivation();
    void notifyClientsOfDeactivation();

    // Services
    void installStateChangeHandler();
    static std::once_flag m_installStateChangeHandler;
    static void OnPluginStateChange(const JsonObject &params);

    using ServiceList = std::list<Service*>;
    static std::mutex m_serviceListMutex;
    static ServiceList m_services;
    AsyncWorker m_worker;
};

template<typename handler_t, typename object_t>
bool Service::subscribe(std::string event, handler_t handler, object_t object)
{
    // This protects the WPEFrameworkPlugin instance untill the function is complete
    auto remote = m_remoteObject;

    if(!m_remoteObject)
        return false;

    auto result = m_remoteObject->Subscribe<JsonObject>(THUNDER_RPC_TIMEOUT, _T(event), handler, object);
    _LOG_INFO("%s to \"%s\" event from \"%s\"", (result == Core::ERROR_NONE) ? "Subscribed" : "Couldn't subscribe", event.c_str(), m_callSign.c_str());
    if(result == Core::ERROR_NONE) {
        m_eventsRegistered.push_back(event);
        return true;
    }

    return false;
}

} // namespace TTSThunderClient

#undef _LOG_INFO

#endif  // _SERVICES_H_


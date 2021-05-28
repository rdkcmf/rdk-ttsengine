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
#include "Service.h"
#include "logger.h"

MODULE_NAME_DECLARATION(BUILD_REFERENCE);

#if defined(SECURITY_TOKEN_ENABLED) && ((SECURITY_TOKEN_ENABLED == 0) || (SECURITY_TOKEN_ENABLED == false))
#define GetSecurityToken(a, b) 0
#define GetToken(a, b, c) 0
#else
#include <WPEFramework/securityagent/securityagent.h>
#include <WPEFramework/securityagent/SecurityTokenUtil.h>
#endif

#define INITIAL_TOKEN_STRING "token="
#define INITIAL_TOKEN_STRING_SIZE (sizeof(INITIAL_TOKEN_STRING)-1)
#define MAX_SECURITY_TOKEN_SIZE 1024
#define PLUGIN_ACTIVATION_TIMEOUT 2000
#define STATE_CHANGE_HANDLER_INSTALLATION_FAILURE_THRESHOLD 3

namespace TTSThunderClient {

Service::ServiceList Service::m_services;
std::mutex Service::m_serviceListMutex;
std::once_flag Service::m_installStateChangeHandler;

void Service::AsyncWorker::post(Task task) {
    m_mutex.lock();
    m_tasklist.push_back(task);
    bool running = m_running;
    m_mutex.unlock();

    if(running)
        return;

    cleanup();
    m_thread = new std::thread([](void *data) {
        AsyncWorker *worker = (AsyncWorker*)data;

        worker->m_mutex.lock();
        worker->m_running = true;

        while(1) {
            if(worker->m_tasklist.size() == 0)
                break;

            Task task = worker->m_tasklist.front();
            worker->m_tasklist.pop_front();
            worker->m_mutex.unlock();
            task(worker->m_service);
            worker->m_mutex.lock();
        }

        worker->m_running = false;
        worker->m_mutex.unlock();
    }, this);
}

void Service::AsyncWorker::cleanup() {
    if(!m_thread)
        return;

    m_thread->join();
    delete m_thread;
    m_thread = nullptr;
}

std::string Service::getSecurityToken(const std::string &payload)
{
    std::string token = "token=";
    int tokenLength = 0;
    unsigned char buffer[MAX_SECURITY_TOKEN_SIZE] = {0};

    static std::string endpoint;
    if(endpoint.empty()) {
        Core::SystemInfo::GetEnvironment(_T("THUNDER_ACCESS"), endpoint);
        TTSLOG_INFO("Thunder RPC Endpoint read from env - %s", endpoint.c_str());
    }

    if(endpoint.empty()) {
        Core::File file("/etc/WPEFramework/config.json", false);

        if(file.Open(true)) {
            JsonObject config;
            if(config.IElement::FromFile(file)) {
                Core::JSON::String port = config.Get("port");
                Core::JSON::String binding = config.Get("binding");
                if(!binding.Value().empty() && !port.Value().empty())
                    endpoint = binding.Value() + ":" + port.Value();
            }
            file.Close();
        }

        if(endpoint.empty())
            endpoint = _T("127.0.0.1:9998");

        TTSLOG_INFO("Thunder RPC Endpoint read from config file - %s", endpoint.c_str());
        Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), endpoint);
    }

    if(payload.empty()) {
        tokenLength = GetSecurityToken(sizeof(buffer), buffer);
    } else {
        int buffLength = std::min(sizeof(buffer), payload.length());
        ::memcpy(buffer, payload.c_str(), buffLength);
        tokenLength = GetToken(sizeof(buffer), buffLength, buffer);
    }

    if(tokenLength > 0)
        token.append((char*)buffer);
    else
        token.clear();

    return token;
}

WPEFrameworkPluginPtr Service::controller(const std::string &payload)
{
    static auto &controller = *(new WPEFrameworkPlugin("", "", false, Service::getSecurityToken(payload)));
    return &controller;
}

void Service::notifyClientsOfActivation()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
        ((Service::Client*)(*it))->onActivation();
    }
}

void Service::notifyClientsOfDeactivation()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
        ((Service::Client*)(*it))->onDeactivation();
    }
}

void Service::onActivation(bool /*requested*/)
{
    m_worker.post([](Service *service) {
        uint8_t timeout_counter = (THUNDER_RPC_TIMEOUT / 250);
        bool shouldForceQueryController = !service->shouldActivateOnCrash() || service->isServiceUnstable();
        while(timeout_counter-- && !service->isActive(shouldForceQueryController))
            usleep(250 * 1000);

        if(service->isActive(!shouldForceQueryController)) {
            service->initialize(false);
            service->notifyClientsOfActivation();
        } else {
            TTSLOG_ERROR("Service couldn't be activated");
        }
    });
}

bool Service::lastSessionWasHealthy()
{
    // If the last session survived beyond healthThreshold() seconds consider it healthy
    if(m_crashTimeStamps.size() > 0) {
        auto session_duration = std::chrono::system_clock::now() - m_crashTimeStamps.back();
        auto in_seconds = std::chrono::duration_cast<std::chrono::seconds>(session_duration).count();
        return (in_seconds >= healthThreshold());
    }
    return true;
}

bool Service::isServiceUnstable()
{
    return m_crashTimeStamps.size() > maxRestartsInMonitoringPeriod();
}

void Service::onDeactivation(bool requested)
{
    m_eventsRegistered.clear(); // To avoid attempts to unregistering event handlers
    uninitialize();
    notifyClientsOfDeactivation();

    if(shouldActivateOnCrash()) {
        if(requested && shouldExcludeRequestedDeactivations())
            return;

        if(lastSessionWasHealthy()) {
            TTSLOG_ERROR("Last session was healthy(or the first session), reinstating max restart attempts counter");
            m_crashTimeStamps.clear();
        }

        m_crashTimeStamps.push_back(std::chrono::system_clock::now());

        if(isServiceUnstable()) {
            TTSLOG_ERROR("Service \"%s\" was identified as unstable, not attempting to restart it", m_callSign.c_str());
            return;
        }

        TTSLOG_WARNING("Detected crash on \"%s\" service, attempting to revive it, attempt - %d", m_callSign.c_str(), m_crashTimeStamps.size());
        m_worker.post([](Service *service) {
            service->activate();
            TTSLOG_INFO("Reactivation job done");
        });
    }
}

void Service::OnPluginStateChange(const JsonObject &params)
{
    string callsign = params["callsign"].String();
    string state = params["state"].String();
    string reason = params["reason"].String();

    bool activated =  (state == "Activated");
    bool deactivated = !activated ? (state == "Deactivated") : false;
    bool requested = (reason == "Requested");

    TTSLOG_WARNING("Service::OnPluginStateChange [%s - %s, %s]", callsign.c_str(), state.c_str(), reason.c_str());

    std::unique_lock<std::mutex> lock(m_serviceListMutex);
    for(ServiceList::iterator it = m_services.begin(); it != m_services.end(); ++it) {
        Service *service = *it;
        if(service->callsign().compare(0, callsign.length(), callsign) == 0) {
            if(activated)
                service->onActivation(requested);
            else if(deactivated)
                service->onDeactivation(requested);
        }
    }
}

void Service::installStateChangeHandler()
{
    std::call_once(m_installStateChangeHandler, [this]() {
        m_worker.post([](Service *service) {
            int attempt = 0;
            while(++attempt <= STATE_CHANGE_HANDLER_INSTALLATION_FAILURE_THRESHOLD) {
                auto result = controller(service->m_tokenPayload)->Subscribe<JsonObject>(THUNDER_RPC_TIMEOUT, _T("statechange"), OnPluginStateChange);
                TTSLOG_INFO("%s to \"statechange\" event from \"controller\"", (result == Core::ERROR_NONE) ? "Subscribed" : "Couldn't subscribe");
                if(result == Core::ERROR_NONE)
                    break;
                sleep(1);
            }
        });
    });
}

Service::Service(const char *callsign) : m_callSign(callsign ? callsign : ""), m_remoteObject(nullptr), m_active(false), m_activeQuerySuccess(false), m_envOverride(false), m_worker(this)
{
    m_serviceListMutex.lock();
    m_services.push_back(this);
    m_serviceListMutex.unlock();

    installStateChangeHandler();

    std::string tokenURLEnv = "_security_token_url";
    if(!std::isdigit(m_callSign.back()))
        tokenURLEnv = m_callSign + tokenURLEnv;
    else
        tokenURLEnv = m_callSign.substr(0, m_callSign.rfind('.')) + tokenURLEnv;

    std::replace(tokenURLEnv.begin(), tokenURLEnv.end(), '.', '_');
    Core::SystemInfo::GetEnvironment(tokenURLEnv, m_tokenPayload);

    if(!m_tokenPayload.empty()) {
        m_envOverride = true;
        TTSLOG_INFO("URL from env for %s is %s", tokenURLEnv.c_str(), m_tokenPayload.empty() ? "NULL" : m_tokenPayload.c_str());
    }
}

Service::~Service()
{
    m_serviceListMutex.lock();
    ServiceList::iterator it = std::find(m_services.begin(), m_services.end(), this);
    if(it != m_services.end())
        m_services.erase(it);
    m_serviceListMutex.unlock();

    m_clients.clear();

    uninitialize();
}

bool Service::isActive(bool force)
{
    if(m_callSign.empty()) {
        TTSLOG_ERROR("Empty callsign");
        return false;
    }

    force |= !m_activeQuerySuccess;
    if(!force)
        return m_active;

    std::string method = "status@" + m_callSign;
    Core::JSON::ArrayType<PluginHost::MetaData::Service> response;
    uint32_t ret  = Service::controller(m_tokenPayload)->Get(THUNDER_RPC_TIMEOUT, method, response);

    m_activeQuerySuccess = (ret == Core::ERROR_NONE);
    m_active = (m_activeQuerySuccess && response.Length() > 0 && response[0].JSONState == PluginHost::IShell::ACTIVATED);
    TTSLOG_INFO("Plugin \"%s\" is %s, error=%d", m_callSign.c_str(), m_active ? "active" : "not active", ret);

    return m_active;
}

void Service::activate()
{
    if(m_callSign.empty() || m_active)
        return;

    JsonObject request, response;
    request["callsign"] = m_callSign;
    uint32_t ret = Service::controller(m_tokenPayload)->Invoke<JsonObject, JsonObject>(PLUGIN_ACTIVATION_TIMEOUT, "activate", request, response);
    m_active = (ret == Core::ERROR_NONE);
    TTSLOG_INFO("Activating plugin \"%s\" was %s, error=%d", m_callSign.c_str(), m_active ? "successful" : "failure", ret);
}

void Service::setSecurityTokenPayload(const char *payload)
{
    if(payload && !m_envOverride) {
        TTSLOG_VERBOSE("Setting security token payload as %s", payload);
        m_tokenPayload = std::string(payload);
    }
}

void Service::initialize(bool activateIfRequired)
{
    if(initialized())
        return;

    if(!isActive()) {
        if(activateIfRequired)
            activate();
        isActive(true);
    }

    if(m_active && !m_remoteObject) {
        if(m_token.empty())
            m_token = Service::getSecurityToken(m_tokenPayload);

        if(m_token.empty())
            m_remoteObject = std::make_shared<WPEFrameworkPlugin>(m_callSign, _T(""));
        else
            m_remoteObject = std::make_shared<WPEFrameworkPlugin>(m_callSign, _T(""), false, m_token);

        if(m_remoteObject)
            TTSLOG_INFO("Successfully connected to remote object \"%s\"", m_callSign.c_str());
        else
            TTSLOG_ERROR("Couldn't connect to remote object \"%s\"", m_callSign.c_str());
    }
}

void Service::uninitialize()
{
    m_active = false;

    while(m_eventsRegistered.size()) {
        m_remoteObject->Unsubscribe(THUNDER_RPC_TIMEOUT, _T(m_eventsRegistered.front()));
        m_eventsRegistered.pop_front();
    }

    m_remoteObject = nullptr;
}

bool Service::initialized()
{
    return m_remoteObject != nullptr;
}

void Service::registerClient(Service::Client *client)
{
    if(!client)
        return;

    std::unique_lock<std::mutex> lock(m_mutex);
    if(std::find(m_clients.begin(), m_clients.end(), client) == m_clients.end())
        m_clients.push_back(client);
}

void Service::unregisterClient(Service::Client *client)
{
    if(!client)
        return;

    std::unique_lock<std::mutex> lock(m_mutex);
    ClientList::iterator it = std::find(m_clients.begin(), m_clients.end(), client);
    if(it != m_clients.end())
        m_clients.erase(it);
}

bool Service::get(std::string method, Core::JSON::String &response)
{
    auto remote = m_remoteObject;
    if(!m_remoteObject)
        return false;

    auto ret = m_remoteObject->Get<Core::JSON::String>(THUNDER_RPC_TIMEOUT, method, response);
    if(ret == Core::ERROR_NONE)
        return true;

    TTSLOG_ERROR("Getting \"%s\" property on \"%s\" failed, error=%d", method.c_str(), m_callSign.c_str(), ret);
    return false;
}

bool Service::invoke(std::string method, JsonObject &request, JsonObject &response)
{
    auto remote = m_remoteObject;
    if(!m_remoteObject)
        return false;

    auto ret = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_RPC_TIMEOUT, method, request, response);
    if(ret == Core::ERROR_NONE && response["success"].Boolean() == true)
        return true;

    if(response.HasLabel("error")) {
        JsonObject error;
        error.FromString(response["error"].String());
        TTSLOG_ERROR("Calling \"%s\" method on \"%s\" failed - code:%d, msg:%s",
                method.c_str(), m_callSign.c_str(), error["code"].Number(), error["message"].String().c_str());
    } else {
        TTSLOG_ERROR("Calling \"%s\" method on \"%s\" failed, error=%d", method.c_str(), m_callSign.c_str(), ret);
    }

    return false;
}

} // namespace TTSThunderClient

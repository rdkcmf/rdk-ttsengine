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

#include "TTSClientPrivateJsonRPC.h"

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

#define CHECK_CONNECTION_RETURN_ON_FAIL(ret) do {\
    if(!TextToSpeechService::Instance()->isActive()) { \
        TTSLOG_ERROR("Connection to TTS manager is not establised"); \
        return ret; \
    } } while(0)

#define UNUSED(x) (void)(x)

#define DEFAULT_SESSION_ID 1

// --- //

TTSClientPrivateJsonRPC::TTSClientPrivateJsonRPC(TTSConnectionCallback *callback, bool) :
    m_ttsEnabled(false),
    m_connectionCallback(callback),
    m_sessionCallback(nullptr),
    m_lastSpeechId(0),
    m_appId(0),
    m_firstQuery(true) {
    TextToSpeechService::Instance()->initialize();
    TextToSpeechService::Instance()->registerClient(this);
    TextToSpeechService::Instance()->restartServiceOnCrash(false);

    if (Core::SystemInfo::GetEnvironment(_T("CLIENT_IDENTIFIER"), m_callsign) == true) {
        std::string::size_type pos =  m_callsign.find(',');
        if (pos != std::string::npos)
        {
            m_callsign.erase(pos,std::string::npos);
        }
    }

    if(TextToSpeechService::Instance()->isActive() && m_connectionCallback)
        m_connectionCallback->onTTSServerConnected();
}

TTSClientPrivateJsonRPC::~TTSClientPrivateJsonRPC() {
    TextToSpeechService::Instance()->unregisterClient(this);
    abort(DEFAULT_SESSION_ID, false);
    destroySession(DEFAULT_SESSION_ID);
}

TTS_Error TTSClientPrivateJsonRPC::enableTTS(bool enable) {
    JsonObject request, response;
    request["enabletts"] = enable;
    if(!TextToSpeechService::Instance()->invoke("enabletts", request, response)) {
        TTSLOG_ERROR("Couldn't %s TTS", enable ? "enable" : "disable");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateJsonRPC::listVoices(std::string &language, std::vector<std::string> &voices) {
    JsonObject request, response;
    request["language"] = language;
    if(!TextToSpeechService::Instance()->invoke("listvoices", request, response)) {
        TTSLOG_ERROR("Couldn't retrieve voice list");
        return TTS_FAIL;
    }

    if(response.HasLabel("voices")) {
        auto voicearray = response["voices"].Array();
        uint32_t len = voicearray.Length();
        for(uint32_t i = 0; i < len; i++)
            voices.push_back(voicearray[i].String());
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateJsonRPC::setTTSConfiguration(Configuration &config) {
    JsonObject request, response;
    request["ttsEndPoint"] = config.ttsEndPoint;
    request["ttsEndPointSecured"] = config.ttsEndPointSecured;
    request["language"] = config.language;
    request["voice"] = config.voice;
    request["volume"] = std::to_string(config.volume);
    request["rate"] = (int)config.rate;

    if(!TextToSpeechService::Instance()->invoke("setttsconfiguration", request, response)) {
        TTSLOG_ERROR("Couldn't set default configuration");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateJsonRPC::getTTSConfiguration(Configuration &config) {
    JsonObject request, response;
    if(!TextToSpeechService::Instance()->invoke("getttsconfiguration", request, response)) {
        TTSLOG_ERROR("Couldn't get configuration");
        return TTS_FAIL;
    }

    config.ttsEndPoint = response["ttsendpoint"].String();
    config.ttsEndPointSecured = response["ttsendpointsecured"].String();
    config.language = response["language"].String();
    config.voice = response["voice"].String();
    config.volume = std::stod(response["volume"].String());
    config.rate = response["rate"].Number();

    return TTS_OK;
}

bool TTSClientPrivateJsonRPC::isTTSEnabled(bool force) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);

    force |= m_firstQuery;
    m_firstQuery = false;

    if(!force) {
        return m_ttsEnabled;
    } else {
        JsonObject request, response;
        if(!TextToSpeechService::Instance()->invoke("isttsenabled", request, response)) {
            TTSLOG_ERROR("Couldn't retrieve TTS enabled/disabled detail");
            return false;
        }

        m_ttsEnabled = response.HasLabel("isenabled") && response["isenabled"].Boolean();
        TTSLOG_VERBOSE("TTS is %s", m_ttsEnabled ? "enabled" : "disabled");
        return m_ttsEnabled;
    }
}

uint32_t TTSClientPrivateJsonRPC::createSession(uint32_t appId, std::string appName, TTSSessionCallback *callback) {
    CHECK_CONNECTION_RETURN_ON_FAIL(0);
    UNUSED(appName);

    m_appId = appId;
    if(m_connectionCallback) {
        isTTSEnabled(true);
        m_connectionCallback->onTTSStateChanged(m_ttsEnabled);
    }

    m_sessionCallback = callback;
    if(m_sessionCallback)
        m_sessionCallback->onTTSSessionCreated(m_appId, DEFAULT_SESSION_ID);

    return DEFAULT_SESSION_ID;
}

TTS_Error TTSClientPrivateJsonRPC::destroySession(uint32_t sessionId) {
    UNUSED(sessionId);
    m_sessionCallback  = nullptr;
    return TTS_OK;
}

TTS_Error TTSClientPrivateJsonRPC::speak(uint32_t sessionId, SpeechData& data) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_ERROR("TTS is disabled, can't speak");
        return TTS_NOT_ENABLED;
    }

    TextToSpeechService::Instance()->registerSpeechEventHandlers();

    m_lastSpeechId = 0;
    JsonObject request, response;
    request["text"] = data.text;
    request["callsign"] = m_callsign;
    if(!TextToSpeechService::Instance()->invoke("speak", request, response)) {
        TTSLOG_ERROR("Coudn't speak, %d..Error code: %d", m_ttsEnabled,response["TTS_Status"].Number());
        return TTS_FAIL;
    }

    if(response.HasLabel("speechid")) {
        m_lastSpeechId = response["speechid"].Number();
        bool success = m_requestedSpeeches.add(data.id, m_lastSpeechId);
        TTSLOG_INFO("Requested speech with clientid-%d, serviceid-%d, is_duplicate_client_id=%d", data.id, m_lastSpeechId, !success);
    } else {
        TTSLOG_ERROR("Requested speech with clientid-%d, text-%s doesn't return valid serviceid", data.id, data.text.c_str());
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateJsonRPC::abort(uint32_t sessionId, bool clearPending) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);
    UNUSED(clearPending);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to abort");
        return TTS_OK;
    }

    if(m_requestedSpeeches.empty() || !m_lastSpeechId) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    JsonObject request, response;
    request["speechid"] = m_lastSpeechId;
    if(!TextToSpeechService::Instance()->invoke("cancel", request, response)) {
        TTSLOG_ERROR("Coudn't abort");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateJsonRPC::pause(uint32_t sessionId, uint32_t speechId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to pause");
        return TTS_OK;
    }

    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    JsonObject request, response;
    request["speechid"] = serviceid;
    if(!TextToSpeechService::Instance()->invoke("pause", request, response)) {
        TTSLOG_ERROR("Coudn't pause");
        return TTS_FAIL;
    }

    return TTS_OK;
}

TTS_Error TTSClientPrivateJsonRPC::resume(uint32_t sessionId, uint32_t speechId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    if(!m_ttsEnabled) {
        TTSLOG_WARNING("TTS is disabled, nothing to resume");
        return TTS_OK;
    }

    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    JsonObject request, response;
    request["speechid"] = serviceid;
    if(!TextToSpeechService::Instance()->invoke("resume", request, response)) {
        TTSLOG_ERROR("Coudn't resume");
        return TTS_FAIL;
    }

    return TTS_OK;
}

bool TTSClientPrivateJsonRPC::isSpeaking(uint32_t sessionId) {
    CHECK_CONNECTION_RETURN_ON_FAIL(false);
    UNUSED(sessionId);

    if(m_requestedSpeeches.empty() || !m_lastSpeechId) {
        TTSLOG_WARNING("No speech in progress");
        return false;
    }

    JsonObject request, response;
    request["speechid"] = m_lastSpeechId;
    if(!TextToSpeechService::Instance()->invoke("isspeaking", request, response)) {
        TTSLOG_ERROR("isspeaking query failed");
        return false;
    }

    return response.HasLabel("speaking") ? response["speaking"].Boolean() : false;
}

TTS_Error TTSClientPrivateJsonRPC::getSpeechState(uint32_t sessionId, uint32_t speechId, SpeechState &state) {
    CHECK_CONNECTION_RETURN_ON_FAIL(TTS_FAIL);
    UNUSED(sessionId);

    uint32_t serviceid = m_requestedSpeeches.getServiceId(speechId);
    if(!serviceid) {
        TTSLOG_WARNING("No speech in progress");
        return TTS_OK;
    }

    JsonObject request, response;
    request["speechid"] = serviceid;
    if(!TextToSpeechService::Instance()->invoke("getspeechstate", request, response)) {
        TTSLOG_ERROR("Couldn't retrieve speech state");
        return TTS_FAIL;
    }
    state = response.HasLabel("speechstate") ? (SpeechState)response["speechstate"].Number() : SPEECH_NOT_FOUND;

    return TTS_OK;
}

void TTSClientPrivateJsonRPC::onActivation()
{
    if(m_connectionCallback) {
        TTSLOG_INFO("Got service connected event from TTS Manager for %p", this);
        m_connectionCallback->onTTSServerConnected();
    }
}

void TTSClientPrivateJsonRPC::onDeactivation()
{
    m_lastSpeechId = 0;
    m_ttsEnabled = false;
    if(m_connectionCallback) {
        TTSLOG_INFO("Got service disconnected event from TTS Manager for %p", this);
        m_connectionCallback->onTTSServerClosed();
    }
}

void TTSClientPrivateJsonRPC::onTTSStateChange(bool enabled)
{
    m_lastSpeechId = 0;
    m_ttsEnabled = enabled;
    if(m_connectionCallback) {
        TTSLOG_INFO("Got tts_state_changed event from TTS Manager for %p", this);
        m_connectionCallback->onTTSStateChanged(enabled);
    }
}

void TTSClientPrivateJsonRPC::onVoiceChange(std::string voice)
{
    if(m_connectionCallback) {
        TTSLOG_INFO("Got voice_changed event from TTS Manager %p, new voice = %s", this, voice.c_str());
        m_connectionCallback->onVoiceChanged(voice);
    }
}
void TTSClientPrivateJsonRPC::onSpeechStart(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        SpeechData data(clientSpeechId);
        TTSLOG_INFO("Got started event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechStart(m_appId, DEFAULT_SESSION_ID, data);
    }
}

void TTSClientPrivateJsonRPC::onSpeechPause(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got paused event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechPause(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateJsonRPC::onSpeechResume(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.getClientId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got resumed event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechResume(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateJsonRPC::onSpeechCancel(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got cancelled event from session %u, speech id %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechCancelled(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateJsonRPC::onSpeechInterrupt(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got interrupted event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechInterrupted(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateJsonRPC::onNetworkError(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got networkerror event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onNetworkError(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateJsonRPC::onPlaybackError(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        TTSLOG_INFO("Got playbackerror event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onPlaybackError(m_appId, DEFAULT_SESSION_ID, clientSpeechId);
    }
}

void TTSClientPrivateJsonRPC::onSpeechComplete(uint32_t serviceSpeechId)
{
    uint32_t clientSpeechId = m_requestedSpeeches.removeServiceId(serviceSpeechId);
    if(clientSpeechId && m_sessionCallback) {
        SpeechData data(clientSpeechId);
        TTSLOG_INFO("Got spoke event from session %u", DEFAULT_SESSION_ID);
        m_sessionCallback->onSpeechComplete(m_appId, DEFAULT_SESSION_ID, data);
    }
}

} // namespace TTS

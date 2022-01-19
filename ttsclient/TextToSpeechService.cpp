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

#include "TextToSpeechService.h"
#include "logger.h"

namespace TTSThunderClient {

#define TEXTTOSPEECH_CALLSIGN "org.rdk.TextToSpeech.1"

TextToSpeechService *TextToSpeechService::Instance()
{
    static TextToSpeechService instance;
    return &instance;
}

TextToSpeechService::TextToSpeechService() :
    Service(TEXTTOSPEECH_CALLSIGN),
    m_initialized(false),
    m_registeredSpeechEventHandlers(false),
    m_restartOnCrash(false),
    m_maxRestartAttempts(3),
    m_duration(60)
{
    setSecurityTokenPayload("http://texttospeechclient");
}

void TextToSpeechService::initialize(bool activateIfRequired)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if(initialized())
        return;

    Service::initialize(activateIfRequired);

    if(isActive() && m_remoteObject) {
        subscribe("onttsstatechanged", onTTSStateChange, this);
        subscribe("onvoicechanged", onVoiceChange, this);
    }

    m_initialized = true;

    for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it)
        ((TextToSpeechService::Client*)(*it))->onTTSStateChange(false);
}

void TextToSpeechService::uninitialize()
{
    Service::uninitialize();
    m_initialized = false;
    m_registeredSpeechEventHandlers = false;
}

bool TextToSpeechService::initialized()
{
    return Service::initialized() && m_initialized;
}

void TextToSpeechService::registerSpeechEventHandlers()
{
    if(isActive() && !m_registeredSpeechEventHandlers && m_remoteObject) {
        m_registeredSpeechEventHandlers = true;
        subscribe("onspeechstart", onSpeechStart, this);
        subscribe("onspeechpause", onSpeechPause, this);
        subscribe("onspeechresume", onSpeechResume, this);
        subscribe("onspeechcancelled", onSpeechCancel, this);
        subscribe("onspeechinterrupted", onSpeechInterrupt, this);
        subscribe("onnetworkerror", onNetworkError, this);
        subscribe("onplaybackerror", onPlaybackError, this);
        subscribe("onspeechcomplete", onSpeechComplete, this);
    }
}

void TextToSpeechService::restartServiceOnCrash(bool flag, uint8_t maxattempts, uint16_t duration, bool ignoreManualDeactivation)
{
    m_restartOnCrash = flag;
    m_maxRestartAttempts = maxattempts;
    m_duration = duration;
    m_ignoreManualDeactivation = ignoreManualDeactivation;
    TTSLOG_INFO("restart on crash = %d, attemptx = %d, duration = %d, ignoreManualDeactivation = %d", m_restartOnCrash, maxattempts, duration, ignoreManualDeactivation);
}

void TextToSpeechService::dispatchEvent(EventType event, const JsonObject &params)
{
    int speechid = 0;
    bool enabled = false;
    std::string voice;
    bool dispatch = true;;

    if(event == StateChange) {
        dispatch = params.HasLabel("state");
        enabled = dispatch ? params["state"].Boolean() : false;
        TTSLOG_INFO("%s(StateChange), state=%s", __FUNCTION__, enabled ? "enabled" : "disabled");
    } else if (event == VoiceChange) {
        dispatch = params.HasLabel("voice");
        voice = dispatch ? params["voice"].String() : "";
        TTSLOG_INFO("%s(VoiceChange), voice=%s", __FUNCTION__, voice.c_str());
    } else {
        dispatch = params.HasLabel("speechid");
        speechid = dispatch ? params["speechid"].Number() : 0;
        TTSLOG_INFO("%s(SpeechEvent-%d), servicespeecid=%d", __FUNCTION__, (int)event, speechid);
    }

    if(dispatch && initialized()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        for(ClientList::iterator it = m_clients.begin(); it != m_clients.end(); ++it) {
            switch(event) {
                case StateChange: ((TextToSpeechService::Client*)(*it))->onTTSStateChange(enabled); break;
                case VoiceChange: ((TextToSpeechService::Client*)(*it))->onVoiceChange(voice); break;
                case SpeechStart: ((TextToSpeechService::Client*)(*it))->onSpeechStart(speechid); break;
                case SpeechPause: ((TextToSpeechService::Client*)(*it))->onSpeechPause(speechid); break;
                case SpeechResume: ((TextToSpeechService::Client*)(*it))->onSpeechResume(speechid); break;
                case SpeechCancel: ((TextToSpeechService::Client*)(*it))->onSpeechCancel(speechid); break;
                case SpeechInterrupt: ((TextToSpeechService::Client*)(*it))->onSpeechInterrupt(speechid); break;
                case NetworkError: ((TextToSpeechService::Client*)(*it))->onNetworkError(speechid); break;
                case PlaybackError: ((TextToSpeechService::Client*)(*it))->onPlaybackError(speechid); break;
                case SpeechComplete: ((TextToSpeechService::Client*)(*it))->onSpeechComplete(speechid); break;
            }
        }
    }
}

void TextToSpeechService::onTTSStateChange(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(StateChange, params);
}

void TextToSpeechService::onVoiceChange(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(VoiceChange, params);
}

void TextToSpeechService::onSpeechStart(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(SpeechStart, params);
}

void TextToSpeechService::onSpeechPause(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(SpeechPause, params);
}

void TextToSpeechService::onSpeechResume(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(SpeechResume, params);
}

void TextToSpeechService::onSpeechCancel(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(SpeechCancel, params);
}

void TextToSpeechService::onSpeechInterrupt(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(SpeechInterrupt, params);
}

void TextToSpeechService::onNetworkError(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(NetworkError, params);
}

void TextToSpeechService::onPlaybackError(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(PlaybackError, params);
}

void TextToSpeechService::onSpeechComplete(TextToSpeechService *service, const JsonObject &params)
{
    if(service) service->dispatchEvent(SpeechComplete, params);
}

} // namespace TTSThunderClient

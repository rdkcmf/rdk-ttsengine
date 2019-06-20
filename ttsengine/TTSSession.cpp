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

#include "TTSSession.h"
#include "TTSErrors.h"
#include "logger.h"

#include <sstream>

namespace TTS {

// --- //

#define _return(tts_code) result.setUInt8(tts_code); return RT_OK;

#define CHECK_ACTIVENESS() do {\
    if(!m_speaker) { \
        TTSLOG_ERROR("Session \"%u\" is not active to start a speech", m_sessionId); \
        _return(TTS_SESSION_NOT_ACTIVE); \
    } } while(0)

// --- //

//Define TTSSession object
rtDefineObject(TTSSession, TTSEventSource);

//Define TTSSession object properties
rtDefineProperty(TTSSession, isActive);
rtDefineProperty(TTSSession, sessionID);
rtDefineProperty(TTSSession, isSpeaking);

//Define TTSSession object methods
rtDefineMethod(TTSSession, getConfiguration);
rtDefineMethod(TTSSession, setPreemptiveSpeak);
rtDefineMethod(TTSSession, getSpeechState);
rtDefineMethod(TTSSession, speak);
rtDefineMethod(TTSSession, pause);
rtDefineMethod(TTSSession, resume);
rtDefineMethod(TTSSession, shut);
rtDefineMethod(TTSSession, clearAllPendingSpeeches);
rtDefineMethod(TTSSession, requestExtendedEvents);

// --- //

TTSSession::TTSSession(uint32_t appId, rtString appName, uint32_t sessionId, TTSConfiguration configuration) :
    m_speaker(NULL), m_havingConfigToUpdate(false), m_extendedEvents(0) {
    m_appId = appId;
    m_name = appName;
    m_sessionId = sessionId;
    m_configuration = configuration;
}

TTSSession::~TTSSession() {
}

rtError TTSSession::setPreemptiveSpeak(bool preemptive, rtValue &result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_configuration.setPreemptiveSpeak(preemptive);
    TTSLOG_INFO("Preemptive Speech has been %s", preemptive ? "enabled" : "disabled");
    _return(TTS_OK);
}

rtError TTSSession::getSpeechState(rtValue id, rtValue &result) {
    TTSLOG_TRACE("Speak");

    // Check if it is active session
    CHECK_ACTIVENESS();

    _return(m_speaker->getSpeechState(this, id.toUInt32()));
}

rtError TTSSession::speak(rtValue id, rtString text, bool secure, rtValue &result) {
    TTSLOG_TRACE("Speak");

    // Check if it is active session
    CHECK_ACTIVENESS();

    if(!m_configuration.isValid()) {
        TTSLOG_ERROR("Configuration is not set, can't speak");
        _return(TTS_INVALID_CONFIGURATION);
    }

    m_speaker->speak(this, id.toUInt32(), text, secure);

    _return(TTS_OK);
}

rtError TTSSession::pause(rtValue id, rtValue &result) {
    TTSLOG_TRACE("Pause");

    // Check if it is active session
    CHECK_ACTIVENESS();

    if(m_speaker->isSpeaking(this)) {
        m_speaker->pause(id.toUInt32());
    }

    _return(TTS_OK);
}

rtError TTSSession::resume(rtValue id, rtValue &result) {
    TTSLOG_TRACE("Resume");

    // Check if it is active session
    CHECK_ACTIVENESS();

    if(m_speaker->isSpeaking(this)) {
        m_speaker->resume(id.toUInt32());
    }

    _return(TTS_OK);
}

rtError TTSSession::shut(rtValue &result) {
    TTSLOG_TRACE("Shut");

    // Check if it is active session
    CHECK_ACTIVENESS();

    if(m_speaker->isSpeaking(this)) {
        m_speaker->cancelCurrentSpeech();
    }

    _return(TTS_OK);
}

rtError TTSSession::clearAllPendingSpeeches() {
    TTSLOG_INFO("Clearing all speeches from session");
    if(m_speaker) {
        std::vector<uint32_t> speechesCancelled;
        m_speaker->clearAllSpeechesFrom(this, speechesCancelled);
        cancelled(speechesCancelled);
    }
    return RT_OK;
}

rtError TTSSession::requestExtendedEvents(rtValue eventflags) {
    m_extendedEvents = eventflags.toUInt32();
    return RT_OK;
}

rtError TTSSession::isActive(rtValue &active) const {
    active = (m_speaker != NULL);
    return RT_OK;
}

rtError TTSSession::isSpeaking(rtValue &speaking) const {
    speaking = (m_speaker && m_speaker->isSpeaking(this));
    return RT_OK;
}

rtError TTSSession::sessionID(uint32_t &sessionid) const {
    sessionid = sessionId();
    return RT_OK;
}

rtError TTSSession::getConfiguration(rtObjectRef &configuration) {
    TTSLOG_TRACE("Getting configuration");

    configuration.set("ttsEndPoint", m_configuration.endPoint());
    configuration.set("ttsEndPointSecured", m_configuration.secureEndPoint());
    configuration.set("language", m_configuration.language());
    configuration.set("volume", m_configuration.volume());
    configuration.set("voice", m_configuration.voice());
    configuration.set("rate", m_configuration.rate());

    return RT_OK;
}

void TTSSession::setConfiguration(TTSConfiguration &config) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if(m_speaker && m_speaker->isSpeaking(this)) {
        TTSLOG_WARNING("Session \"%u\" is speaking now, will update configuration once speaking is done", m_sessionId);
        m_tmpConfiguration = config;
        TTSLOG_INFO("tmpConfiguration, endPoint=%s, secureEndPoint=%s, lang=%s, voice=%s, vol=%lf, rate=%u",
                m_tmpConfiguration.endPoint().cString(),
                m_tmpConfiguration.secureEndPoint().cString(),
                m_tmpConfiguration.language().cString(),
                m_tmpConfiguration.voice().cString(),
                m_tmpConfiguration.volume(),
                m_tmpConfiguration.rate());
        m_havingConfigToUpdate = true;
    } else {
        m_configuration.updateWith(config);
        TTSLOG_INFO("configuration, endPoint=%s, secureEndPoint=%s, lang=%s, voice=%s, vol=%lf, rate=%u",
                m_configuration.endPoint().cString(),
                m_configuration.secureEndPoint().cString(),
                m_configuration.language().cString(),
                m_configuration.voice().cString(),
                m_configuration.volume(),
                m_configuration.rate());
    }
}

void TTSSession::setActive(TTSSpeaker *speaker, bool notifyClient) {
    TTSLOG_TRACE("Activating Session");

    if(m_speaker) {
        TTSLOG_ERROR("Session \"%u\" is already active", sessionId());
        return;
    }

    m_speaker = speaker;

    if(notifyClient) {
        Event d("resource_acquired");
        d.set("session", sessionId());
        sendEvent(d);
    }
}

void TTSSession::setInactive(bool notifyClient) {
    TTSLOG_TRACE("Deactivating Session");

    // If active session, reset speaker
    if(m_speaker) {
        clearAllPendingSpeeches();
        m_speaker = NULL;

        if(notifyClient) {
            Event d("resource_released");
            d.set("session", sessionId());
            sendEvent(d);
        }
    }
}

TTSConfiguration *TTSSession::configuration() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return &m_configuration;
}

void TTSSession::willSpeak(uint32_t speech_id, rtString text) {
    TTSLOG_VERBOSE(" [%d, %s]", speech_id, text.cString());

    Event d("willSpeak");
    d.set("id", speech_id);
    d.set("text", text);
    sendEvent(d);
}

void TTSSession::spoke(uint32_t speech_id, rtString text) {
    TTSLOG_VERBOSE(" [%d, %s]", speech_id, text.cString());

    if(m_havingConfigToUpdate) {
        m_configuration.updateWith(m_tmpConfiguration);
        m_havingConfigToUpdate = false;
    }

    Event d("spoke");
    d.set("id", speech_id);
    d.set("text", text);
    sendEvent(d);
}

void TTSSession::paused(uint32_t speech_id) {
    if(!(m_extendedEvents & EXT_EVENT_PAUSED))
        return;

    TTSLOG_WARNING(" [id=%d]", speech_id);

    Event d("paused");
    d.set("id", speech_id);
    sendEvent(d);
}

void TTSSession::resumed(uint32_t speech_id) {
    if(!(m_extendedEvents & EXT_EVENT_RESUMED))
        return;

    TTSLOG_WARNING(" [id=%d]", speech_id);

    Event d("resumed");
    d.set("id", speech_id);
    sendEvent(d);
}

void TTSSession::cancelled(std::vector<uint32_t> &speeches) {
    if(!(m_extendedEvents & EXT_EVENT_CANCELLED))
        return;

    if(speeches.size() <= 0)
        return;

    std::stringstream ss;
    for(auto it = speeches.begin(); it != speeches.end(); ++it) {
        if(it != speeches.begin())
            ss << ",";
        ss << *it;
    }
    TTSLOG_WARNING(" [ids=%s]", ss.str().c_str());

    Event d("cancelled");
    d.set("ids", rtString(ss.str().c_str()));
    sendEvent(d);
}

void TTSSession::interrupted(uint32_t speech_id) {
    if(!(m_extendedEvents & EXT_EVENT_INTERRUPTED))
        return;

    TTSLOG_WARNING(" [id=%d]", speech_id);

    Event d("interrupted");
    d.set("id", speech_id);
    sendEvent(d);
}

void TTSSession::networkerror(uint32_t speech_id){
    if(!(m_extendedEvents & EXT_EVENT_NETWORK_ERROR))
        return;

    TTSLOG_WARNING(" [id=%d]", speech_id);

    Event d("networkerror");
    d.set("id", speech_id);
    sendEvent(d);
}

void TTSSession::playbackerror(uint32_t speech_id){
    if(!(m_extendedEvents & EXT_EVENT_PLAYBACK_ERROR))
        return;

    TTSLOG_WARNING(" [id=%d]", speech_id);

    Event d("playbackerror");
    d.set("id", speech_id);
    sendEvent(d);
}

} // namespace TTS


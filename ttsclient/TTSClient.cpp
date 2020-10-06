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
#include "TTSClientPrivateRtRemote.h"
#include "logger.h"
#include <mutex>

// --- //

namespace TTS {

#define CHECK_PRIV() do {\
    if(!m_priv) {\
        TTSLOG_ERROR("TTSClient is not intialized"); \
        return TTS_FAIL; \
    } } while(0)

// --- //


TTSClient *TTSClient::create(TTSConnectionCallback *callback, bool discardRtDispatching)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    // Check if TTSEngine is running
    if(getenv("TTS_USE_THUNDER_CLIENT") ? true : isProgramRunning(TTS_ENGINE_PROGRAM_NAME)) {
        TTSLOG_INFO("TTSEngine is running");
        return new TTSClient(callback, discardRtDispatching);
    } else {
        TTSLOG_ERROR("TTSEngine is not running, TTSClient can't be instantiated");
        return NULL;
    }
}

TTSClient::TTSClient(TTSConnectionCallback *callback, bool discardRtDispatching) {
    if(getenv("TTS_USE_THUNDER_CLIENT")) {
        m_priv = new TTSClientPrivateJsonRPC(callback, discardRtDispatching);
    } else {
        m_priv = new TTSClientPrivateRtRemote(callback, discardRtDispatching);
    }
}

TTSClient::~TTSClient() {
    if(m_priv) {
        delete m_priv;
        m_priv = NULL;
    }
}

TTS_Error TTSClient::enableTTS(bool enable) {
    CHECK_PRIV();
    return m_priv->enableTTS(enable);
}

TTS_Error TTSClient::listVoices(std::string language, std::vector<std::string> &voices) {
    CHECK_PRIV();
    return m_priv->listVoices(language, voices);
}

TTS_Error TTSClient::setTTSConfiguration(Configuration &config) {
    CHECK_PRIV();
    return m_priv->setTTSConfiguration(config);
}

TTS_Error TTSClient::getTTSConfiguration(Configuration &config) {
    CHECK_PRIV();
    return m_priv->getTTSConfiguration(config);
}

bool TTSClient::isTTSEnabled(bool forcefetch) {
    CHECK_PRIV();
    return m_priv->isTTSEnabled(forcefetch);
}

bool TTSClient::isSessionActiveForApp(uint32_t appid) {
    CHECK_PRIV();
    return m_priv->isSessionActiveForApp(appid);
}

TTS_Error TTSClient::acquireResource(uint32_t appid) {
    CHECK_PRIV();
    return m_priv->acquireResource(appid);
}

TTS_Error TTSClient::claimResource(uint32_t appid) {
    CHECK_PRIV();
    return m_priv->claimResource(appid);
}

TTS_Error TTSClient::releaseResource(uint32_t appid) {
    CHECK_PRIV();
    return m_priv->releaseResource(appid);
}

uint32_t TTSClient::createSession(uint32_t sessionid, std::string appname, TTSSessionCallback *callback) {
    CHECK_PRIV();
    return m_priv->createSession(sessionid, appname, callback);
}

TTS_Error TTSClient::destroySession(uint32_t sessionid) {
    CHECK_PRIV();
    return m_priv->destroySession(sessionid);
}

bool TTSClient::isActiveSession(uint32_t sessionid, bool forcefetch) {
    CHECK_PRIV();
    return m_priv->isActiveSession(sessionid, forcefetch);
}

TTS_Error TTSClient::setPreemptiveSpeak(uint32_t sessionid, bool preemptive) {
    CHECK_PRIV();
    return m_priv->setPreemptiveSpeak(sessionid, preemptive);
}

TTS_Error TTSClient::requestExtendedEvents(uint32_t sessionid, uint32_t extendedEvents) {
    CHECK_PRIV();
    return m_priv->requestExtendedEvents(sessionid, extendedEvents);
}

TTS_Error TTSClient::speak(uint32_t sessionid, SpeechData& data) {
    CHECK_PRIV();
    return m_priv->speak(sessionid, data);
}

TTS_Error TTSClient::pause(uint32_t sessionid, uint32_t speechid) {
    CHECK_PRIV();
    return m_priv->pause(sessionid, speechid);
}

TTS_Error TTSClient::resume(uint32_t sessionid, uint32_t speechid) {
    CHECK_PRIV();
    return m_priv->resume(sessionid, speechid);
}

TTS_Error TTSClient::abort(uint32_t sessionid, bool clearPending) {
    CHECK_PRIV();
    return m_priv->abort(sessionid, clearPending);
}

bool TTSClient::isSpeaking(uint32_t sessionid) {
    CHECK_PRIV();
    return m_priv->isSpeaking(sessionid);
}

TTS_Error TTSClient::getSpeechState(uint32_t sessionid, uint32_t speechid, SpeechState &state) {
    CHECK_PRIV();
    return m_priv->getSpeechState(sessionid, speechid, state);
}

} // namespace TTS

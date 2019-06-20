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
#ifndef _TTS_CLIENT_H_
#define _TTS_CLIENT_H_

#include "TTSErrors.h"

#include <iostream>
#include <vector>

namespace TTS {

// Caller can update any of the fields if interested.
// The empty / default fields will be ignored during config
struct Configuration {
    Configuration() : volume(0), rate(0) {};
    ~Configuration() {}

    std::string ttsEndPoint;
    std::string ttsEndPointSecured;
    std::string language;
    std::string voice;
    double volume;
    uint8_t rate;
};

struct SpeechData {
    SpeechData() : secure(true), id(0) {}
    ~SpeechData() {}

    bool secure;
    uint32_t id;
    std::string text;
};

class TTSConnectionCallback {
public:
    TTSConnectionCallback() {}
    virtual ~TTSConnectionCallback() {}

    virtual void onTTSServerConnected() = 0;
    virtual void onTTSServerClosed() = 0;
    virtual void onTTSStateChanged(bool enabled) = 0;
    virtual void onVoiceChanged(std::string voice) { (void)voice; }
};

class TTSSessionCallback {
public:
    TTSSessionCallback() {}
    virtual ~TTSSessionCallback() {}

    virtual void onTTSSessionCreated(uint32_t appId, uint32_t sessionId) = 0;
    virtual void onResourceAcquired(uint32_t appId, uint32_t sessionId) = 0;
    virtual void onResourceReleased(uint32_t appId, uint32_t sessionId) = 0;
    virtual void onSpeechStart(uint32_t appId, uint32_t sessionId, SpeechData &data) { (void)appId; (void)sessionId; (void)data; }
    virtual void onSpeechPause(uint32_t appId, uint32_t sessionId, uint32_t speechId) { (void)appId; (void)sessionId; (void)speechId; }
    virtual void onSpeechResume(uint32_t appId, uint32_t sessionId, uint32_t speechId) { (void)appId; (void)sessionId; (void)speechId; }
    virtual void onSpeechCancelled(uint32_t appId, uint32_t sessionId, uint32_t speechId) { (void)appId; (void)sessionId; (void)speechId; }
    virtual void onSpeechInterrupted(uint32_t appId, uint32_t sessionId, uint32_t speechId) { (void)appId; (void)sessionId; (void)speechId; }
    virtual void onNetworkError(uint32_t appId, uint32_t sessionId, uint32_t speechId) { (void)appId; (void)sessionId; (void)speechId; }
    virtual void onPlaybackError(uint32_t appId, uint32_t sessionId, uint32_t speechId) { (void)appId; (void)sessionId; (void)speechId; }
    virtual void onSpeechComplete(uint32_t appId, uint32_t sessionId, SpeechData &data) { (void)appId; (void)sessionId; (void)data; }
};

//
// Note :
// The Session APIs are designed to have multiple sessions for a client
// But at present the implementation supports single session. So the sessionid input for
// all the APIs, except createSession, will be omitted, the internaly maintained ID will be used.
//
class TTSClientPrivate;
class TTSClient {
public:
    static TTSClient *create(TTSConnectionCallback *connCallback, bool discardRtDispatching=false);
    virtual ~TTSClient();

    // TTS Global APIs
    TTS_Error enableTTS(bool enable=true);
    TTS_Error listVoices(std::string language, std::vector<std::string> &voices);
    TTS_Error setTTSConfiguration(Configuration &config);
    bool isTTSEnabled(bool forcefetch=false);
    bool isSessionActiveForApp(uint32_t appid);

    // Resource management APIs
    TTS_Error acquireResource(uint32_t appid);
    TTS_Error claimResource(uint32_t appid);
    TTS_Error releaseResource(uint32_t appid);

    // Session management APIs
    uint32_t /*sessionid*/ createSession(uint32_t appid, std::string appname, TTSSessionCallback *sessCallback);
    TTS_Error destroySession(uint32_t sessionid);
    bool isActiveSession(uint32_t sessionid, bool forcefetch=false);
    TTS_Error setPreemptiveSpeak(uint32_t sessionid, bool preemptive);
    TTS_Error requestExtendedEvents(uint32_t sessionid, uint32_t extendedEvents);

    // Speak APIs
    TTS_Error speak(uint32_t sessionid, SpeechData& data);
    TTS_Error pause(uint32_t sessionid, uint32_t speechid);
    TTS_Error resume(uint32_t sessionid, uint32_t speechid);
    TTS_Error abort(uint32_t sessionid);
    bool isSpeaking(uint32_t sessionid);
    TTS_Error getSpeechState(uint32_t sessionid, uint32_t speechid, SpeechState &state);
    TTS_Error clearAllPendingSpeeches(uint32_t sessionid);

private:
    TTSClient(TTSConnectionCallback *client, bool discardRtDispatching=false);
    TTSClient(TTSClient&) = delete;

    TTSClientPrivate *m_priv;
};

} // namespace TTS

#endif //_TTS_CLIENT_H_

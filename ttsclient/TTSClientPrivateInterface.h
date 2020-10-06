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
#ifndef _TTS_CLIENT_PRIVATE_INTERFACE_H_
#define _TTS_CLIENT_PRIVATE_INTERFACE_H_

namespace TTS {

bool isProgramRunning(const char* name);

class TTSClientPrivateInterface {
public:
    virtual ~TTSClientPrivateInterface() {}

    // TTS Global APIs
    virtual TTS_Error enableTTS(bool enable) = 0;
    virtual TTS_Error listVoices(std::string &language, std::vector<std::string> &voices) = 0;
    virtual TTS_Error setTTSConfiguration(Configuration &config) = 0;
    virtual TTS_Error getTTSConfiguration(Configuration &config) = 0;
    virtual bool isTTSEnabled(bool forcefetch=false) = 0;
    virtual bool isSessionActiveForApp(uint32_t appId) = 0;

    // Resource management APIs
    virtual TTS_Error acquireResource(uint32_t appId) = 0;
    virtual TTS_Error claimResource(uint32_t appId) = 0;
    virtual TTS_Error releaseResource(uint32_t appId) = 0;

    // Session management APIs
    virtual uint32_t /*sessionId*/ createSession(uint32_t sessionId, std::string appName, TTSSessionCallback *callback) = 0;
    virtual TTS_Error destroySession(uint32_t sessionId) = 0;
    virtual bool isActiveSession(uint32_t sessionId, bool forcefetch=false) = 0;
    virtual TTS_Error setPreemptiveSpeak(uint32_t sessionId, bool preemptive=true) = 0;
    virtual TTS_Error requestExtendedEvents(uint32_t sessionId, uint32_t extendedEvents) = 0;

    // Speak APIs
    virtual TTS_Error speak(uint32_t sessionId, SpeechData& data) = 0;
    virtual TTS_Error pause(uint32_t sessionId, uint32_t speechId = 0) = 0;
    virtual TTS_Error resume(uint32_t sessionId, uint32_t speechId = 0) = 0;
    virtual TTS_Error abort(uint32_t sessionId, bool clearPending) = 0;
    virtual bool isSpeaking(uint32_t sessionId) = 0;
    virtual TTS_Error getSpeechState(uint32_t sessionId, uint32_t speechId, SpeechState &state) = 0;
};

} // namespace TTS

#endif //_TTS_CLIENT_PRIVATE_INTERFACE_H_

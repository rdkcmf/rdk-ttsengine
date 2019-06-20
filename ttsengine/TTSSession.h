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
#ifndef _TTS_SESSION_H_
#define _TTS_SESSION_H_

#include "TTSSpeaker.h"
#include "TTSEventSource.h"
#include "TTSErrors.h"

#include <mutex>

namespace TTS {

class TTSSession : public TTSEventSource, public TTSSpeakerClient {
public:
    rtDeclareObject(TTSSession, TTSEventSource);

    TTSSession(uint32_t appId, rtString appName, uint32_t sessionId, TTSConfiguration configuration);
    virtual ~TTSSession();

    // Declare object functions
    rtMethodNoArgAndReturn("getConfiguration", getConfiguration, rtObjectRef);
    rtMethod1ArgAndReturn("setPreemptiveSpeak", setPreemptiveSpeak, bool, rtValue);
    rtMethod3ArgAndReturn("speak", speak, rtValue, rtString, bool, rtValue);
    rtMethod1ArgAndReturn("pause", pause, rtValue, rtValue);
    rtMethod1ArgAndReturn("resume", resume, rtValue, rtValue);
    rtMethodNoArgAndReturn("shut", shut, rtValue);
    rtMethod1ArgAndReturn("getSpeechState", getSpeechState, rtValue, rtValue);
    rtMethodNoArgAndNoReturn("clearAllPendingSpeeches", clearAllPendingSpeeches);
    rtMethod1ArgAndNoReturn("requestExtendedEvents", requestExtendedEvents, rtValue);

    rtError getConfiguration(rtObjectRef &configuration);
    rtError setPreemptiveSpeak(bool preemptive, rtValue &result);
    rtError getSpeechState(rtValue id, rtValue &result);
    rtError speak(rtValue id, rtString text, bool secure, rtValue &result);
    rtError pause(rtValue id, rtValue &result);
    rtError resume(rtValue id, rtValue &result);
    rtError shut(rtValue &result);
    rtError clearAllPendingSpeeches();
    rtError requestExtendedEvents(rtValue eventflags);

    // Declare object properties
    rtReadOnlyProperty(isActive, isActive, rtValue);
    rtReadOnlyProperty(sessionID, sessionID, uint32_t);
    rtReadOnlyProperty(isSpeaking, isSpeaking, rtValue);

    rtError isActive(rtValue &active) const;
    rtError sessionID(uint32_t &sessionid) const;
    rtError isSpeaking(rtValue &speaking) const;

    // Non-rt public functions
    void setConfiguration(TTSConfiguration &config);
    void setActive(TTSSpeaker *speaker, bool notifyClient=true);
    void setInactive(bool notifyClient=true);

    uint32_t appId() const { return m_appId; }
    rtString appName() const { return m_name; }
    uint32_t sessionId() const { return m_sessionId; }

protected:
    // Speaker Client Callbacks
    virtual TTSConfiguration *configuration();
    virtual void willSpeak(uint32_t speech_id, rtString text);
    virtual void spoke(uint32_t speech_id, rtString text);
    virtual void paused(uint32_t speech_id);
    virtual void resumed(uint32_t speech_id);
    virtual void cancelled(std::vector<uint32_t> &speeches);
    virtual void interrupted(uint32_t speech_id);
    virtual void networkerror(uint32_t speech_id);
    virtual void playbackerror(uint32_t speech_id);

    TTSSpeaker *m_speaker;
    std::mutex m_mutex;

private:
    TTSConfiguration m_tmpConfiguration;
    TTSConfiguration m_configuration;
    bool m_havingConfigToUpdate;

    rtString m_name;
    uint32_t m_appId;
    uint32_t m_sessionId;
    uint32_t m_extendedEvents;
};

} // namespace TTS

#endif

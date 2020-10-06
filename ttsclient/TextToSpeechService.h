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
#ifndef _TEXTTOSPEECH_SERVICES_H_
#define _TEXTTOSPEECH_SERVICES_H_

#include "Service.h"

namespace TTSThunderClient {

class TextToSpeechService : public Service
{
public:
    enum EventType {
        StateChange,
        VoiceChange,
        SpeechStart,
        SpeechPause,
        SpeechResume,
        SpeechCancel,
        SpeechInterrupt,
        NetworkError,
        PlaybackError,
        SpeechComplete
    };

    struct Client : public Service::Client {
        virtual void onTTSStateChange(bool /*enabled*/) {};
        virtual void onVoiceChange(std::string /*voice*/) {};
        virtual void onSpeechStart(uint32_t /*speeechId*/) {};
        virtual void onSpeechPause(uint32_t /*speeechId*/) {};
        virtual void onSpeechResume(uint32_t /*speeechId*/) {};
        virtual void onSpeechCancel(uint32_t /*speeechId*/) {};
        virtual void onSpeechInterrupt(uint32_t /*speeechId*/) {};
        virtual void onNetworkError(uint32_t /*speeechId*/) {};
        virtual void onPlaybackError(uint32_t /*speeechId*/) {};
        virtual void onSpeechComplete(uint32_t /*speeechId*/) {};
    };

public:
    static TextToSpeechService* Instance();
    TextToSpeechService(const TextToSpeechService&) = delete;
    TextToSpeechService& operator=(const TextToSpeechService&) = delete;
    virtual ~TextToSpeechService() {}

    void initialize(bool activateIfRequired = false) override;
    void uninitialize() override;
    bool initialized() override;
    void registerSpeechEventHandlers();
    void restartServiceOnCrash(bool flag, uint8_t maxAttempts = 3, uint16_t duration = 60, bool ignoreManualDeactivation = true);

private:
    TextToSpeechService();

    virtual bool shouldActivateOnCrash() { return m_restartOnCrash; }
    virtual uint8_t maxRestartsInMonitoringPeriod() { return m_maxRestartAttempts; }
    virtual uint16_t healthThreshold() { return m_duration; }
    virtual bool shouldExcludeRequestedDeactivations() { return m_ignoreManualDeactivation; }

    void dispatchEvent(EventType event, const JsonObject &params);
    static void onTTSStateChange(TextToSpeechService *service, const JsonObject &params);
    static void onVoiceChange(TextToSpeechService *service, const JsonObject &params);
    static void onSpeechStart(TextToSpeechService *service, const JsonObject &params);
    static void onSpeechPause(TextToSpeechService *service, const JsonObject &params);
    static void onSpeechResume(TextToSpeechService *service, const JsonObject &params);
    static void onSpeechCancel(TextToSpeechService *service, const JsonObject &params);
    static void onSpeechInterrupt(TextToSpeechService *service, const JsonObject &params);
    static void onNetworkError(TextToSpeechService *service, const JsonObject &params);
    static void onPlaybackError(TextToSpeechService *service, const JsonObject &params);
    static void onSpeechComplete(TextToSpeechService *service, const JsonObject &params);

    bool m_initialized;
    bool m_registeredSpeechEventHandlers;
    bool m_restartOnCrash;
    bool m_ignoreManualDeactivation;
    uint8_t m_maxRestartAttempts;
    uint16_t m_duration;
};

} // namespace TTSThunderClient

#endif  // _TEXTTOSPEECH_SERVICES_H_


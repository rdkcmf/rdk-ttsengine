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
#ifndef _TTS_SPEAKER_H_
#define _TTS_SPEAKER_H_

#include <rtRemote.h>
#include <gst/audio/audio.h>
#include <gst/app/gstappsink.h>

#include <map>
#include <list>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "TTSErrors.h"

// --- //

namespace TTS {

#define DEFAULT_RATE  50
#define DEFAULT_WPM 200
#define MAX_VOLUME 100

// --- //

class TTSConfiguration {
public:
    TTSConfiguration();
    ~TTSConfiguration();

    void setEndPoint(const rtString endpoint);
    void setSecureEndPoint(const rtString endpoint);
    void setLanguage(const rtString language);
    void setVoice(const rtString voice);
    void setVolume(const double volume);
    void setRate(const uint8_t rate);
    void setPreemptiveSpeak(const bool preemptive);

    const rtString &endPoint() { return m_ttsEndPoint; }
    const rtString &secureEndPoint() { return m_ttsEndPointSecured; }
    const rtString &language() { return m_language; }
    const double &volume() { return m_volume; }
    const uint8_t &rate() { return m_rate; }
    bool isPreemptive() { return m_preemptiveSpeaking; }
    const rtString &voice();

    void updateWith(TTSConfiguration &config);
    bool isValid();

    static std::map<std::string, std::string> m_others;

private:
    rtString m_ttsEndPoint;
    rtString m_ttsEndPointSecured;
    rtString m_language;
    rtString m_voice;
    double m_volume;
    uint8_t m_rate;
    bool m_preemptiveSpeaking;
};

class TTSSpeakerClient {
public:
    virtual TTSConfiguration* configuration() = 0;
    virtual void willSpeak(uint32_t speech_id, rtString text) = 0;
    virtual void spoke(uint32_t speech_id, rtString text) = 0;
    virtual void paused(uint32_t speech_id) = 0;
    virtual void resumed(uint32_t speech_id) = 0;
    virtual void cancelled(std::vector<uint32_t> &speeches) = 0;
    virtual void interrupted(uint32_t speech_id) = 0;
    virtual void networkerror(uint32_t speech_id) = 0;
    virtual void playbackerror(uint32_t speech_id) = 0;
};

struct SpeechData {
    public:
        SpeechData() : client(NULL), secure(false), id(0), text() {}
        SpeechData(TTSSpeakerClient *c, uint32_t i, rtString t, bool s=false) : client(c), secure(s), id(i), text(t) {}
        SpeechData(const SpeechData &n) {
            client = n.client;
            id = n.id;
            text = n.text;
            secure = n.secure;
        }
        ~SpeechData() {}

        TTSSpeakerClient *client;
        bool secure;
        uint32_t id;
        rtString text;
};

class TTSSpeaker {
public:
    TTSSpeaker(TTSConfiguration &config);
    ~TTSSpeaker();

    void ensurePipeline(bool flag=true);

    // Speak Functions
    int speak(TTSSpeakerClient* client, uint32_t id, rtString text, bool secure); // Formalize data to speak API
    bool isSpeaking(const TTSSpeakerClient *client = NULL);
    SpeechState getSpeechState(const TTSSpeakerClient *client, uint32_t id);
    void clearAllSpeechesFrom(const TTSSpeakerClient *client, std::vector<uint32_t> &speechesCancelled);
    void cancelCurrentSpeech();
    bool reset();

    void pause(uint32_t id = 0);
    void resume(uint32_t id = 0);

private:

    // Private Data
    TTSConfiguration &m_defaultConfig;
    TTSSpeakerClient *m_clientSpeaking;
    uint32_t m_currentSpeechId;
    bool m_isSpeaking;
    bool m_isPaused;

    std::mutex m_stateMutex;
    std::condition_variable m_condition;

    std::list<SpeechData> m_queue;
    std::mutex m_queueMutex;
    void queueData(SpeechData);
    void flushQueue();
    SpeechData dequeueData();

    // Private functions
    inline void setSpeakingState(bool state, TTSSpeakerClient *client=NULL);

    // GStreamer Releated members
    GstElement *m_pipeline;
    GstElement *m_source;
    GstElement *m_audioSink;
    bool        m_pipelineError;
    bool        m_networkError;
    bool        m_runThread;
    bool        m_flushed;
    bool        m_isEOS;
    bool        m_ensurePipeline;
    std::thread *m_gstThread;

    static void GStreamerThreadFunc(void *ctx);
    void createPipeline();
    void resetPipeline();
    void destroyPipeline();

    // GStreamer Helper functions
    bool needsPipelineUpdate();
    std::string constructURL(TTSConfiguration &config, SpeechData &d);
    bool isSilentPunctuation(const char c);
    void replaceSuccesivePunctuation(std::string& subject);
    void replaceIfIsolated(std::string& subject, const std::string& search, const std::string& replace);
    void curlSanitize(std::string &url);
    void sanitizeString(rtString &input, std::string &sanitizedString);
    void speakText(TTSConfiguration config, SpeechData &data);
    bool waitForStatus(GstState expected_state, uint32_t timeout_ms);
    void waitForAudioToFinishTimeout(float timeout_s);
    bool handleMessage(GstMessage*);
    static int GstBusCallback(GstBus *bus, GstMessage *message, gpointer data);
};

} // namespace TTS

#endif

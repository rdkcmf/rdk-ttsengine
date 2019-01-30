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

#include "TTSSpeaker.h"
#include "logger.h"

#include <curl/curl.h>
#include <unistd.h>
#include <regex>

namespace TTS {

std::map<std::string, std::string> TTSConfiguration::m_others;

TTSConfiguration::TTSConfiguration() :
    m_ttsEndPoint(""),
    m_ttsEndPointSecured(""),
    m_language(""),
    m_voice(""),
    m_volume(MAX_VOLUME),
    m_rate(DEFAULT_RATE),
    m_preemptiveSpeaking(true) {
        if(getenv("TTS_ENGINE_PREEMPTIVE_SPEAKING"))
            m_preemptiveSpeaking = atoi(getenv("TTS_ENGINE_PREEMPTIVE_SPEAKING"));
}

TTSConfiguration::~TTSConfiguration() {}

void TTSConfiguration::setEndPoint(const rtString endpoint) {
    if(!endpoint.isEmpty())
        m_ttsEndPoint = endpoint;
    else
        TTSLOG_WARNING("Invalid TTSEndPoint input \"%s\"", endpoint.cString());
}

void TTSConfiguration::setSecureEndPoint(const rtString endpoint) {
    if(!endpoint.isEmpty())
        m_ttsEndPointSecured = endpoint;
    else
        TTSLOG_WARNING("Invalid Secured TTSEndPoint input \"%s\"", endpoint.cString());
}

void TTSConfiguration::setLanguage(const rtString language) {
    if(!language.isEmpty())
        m_language = language;
    else
        TTSLOG_WARNING("Empty Language input");
}

void TTSConfiguration::setVoice(const rtString voice) {
    if(!voice.isEmpty())
        m_voice = voice;
    else
        TTSLOG_WARNING("Empty Voice input");
}

void TTSConfiguration::setVolume(const double volume) {
    if(volume >= 1 && volume <= 100)
        m_volume = volume;
    else
        TTSLOG_WARNING("Invalid Volume input \"%lf\"", volume);
}

void TTSConfiguration::setRate(const uint8_t rate) {
    if(rate >= 1 && rate <= 100)
        m_rate = rate;
    else
        TTSLOG_WARNING("Invalid Rate input \"%u\"", rate);
}

const rtString &TTSConfiguration::voice() {
    static rtString str;

    str = "";
    if(!m_voice.isEmpty())
        return m_voice;
    else {
        std::string key = std::string("voice_for_") + m_language.cString();
        auto it = m_others.find(key);
        if(it != m_others.end())
            str = it->second.c_str();
        return str;
    }
}

void TTSConfiguration::updateWith(TTSConfiguration &nConfig) {
    setEndPoint(nConfig.m_ttsEndPoint);
    setSecureEndPoint(nConfig.m_ttsEndPointSecured);
    setLanguage(nConfig.m_language);
    setVoice(nConfig.m_voice);
    setVolume(nConfig.m_volume);
    setRate(nConfig.m_rate);
}

bool TTSConfiguration::isValid() {
    if((m_ttsEndPoint.isEmpty() && m_ttsEndPointSecured.isEmpty())) {
        TTSLOG_ERROR("TTSEndPointEmpty=%d, TTSSecuredEndPointEmpty=%d",
                m_ttsEndPoint.isEmpty(), m_ttsEndPointSecured.isEmpty());
        return false;
    }
    return true;
}

// --- //

TTSSpeaker::TTSSpeaker(TTSConfiguration &config) :
    m_defaultConfig(config),
    m_isSpeaking(false),
    m_pipeline(NULL),
    m_source(NULL),
    m_audioSink(NULL),
    m_pipelineError(NULL),
    m_runThread(true),
    m_flushed(false),
    m_isEOS(false),
    m_gstThread(new std::thread(GStreamerThreadFunc, this)) {
        setenv("GST_DEBUG", "2", 0);
}

TTSSpeaker::~TTSSpeaker() {
    if(m_isSpeaking)
        m_flushed = true;
    m_runThread = false;
    m_condition.notify_one();

    if(m_gstThread) {
        m_gstThread->join();
        m_gstThread = NULL;
    }
}

int TTSSpeaker::speak(TTSSpeakerClient *client, uint32_t id, rtString text, bool secure) {
    TTSLOG_TRACE("id=%d, text=\"%s\"", id, text.cString());

    // If force speak is set, clear old queued data & stop speaking
    if(client->configuration()->isPreemptive())
        reset();

    SpeechData data(client, id, text, secure);
    queueData(data);

    return 0;
}

bool TTSSpeaker::isSpeaking(const TTSSpeakerClient *client) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    if(client)
        return (client == m_clientSpeaking);

    return m_isSpeaking;
}

bool TTSSpeaker::reset() {
    TTSLOG_VERBOSE("Resetting Speaker");
    if(m_isSpeaking) {
        m_flushed = true;
        m_condition.notify_one();
    }
    flushQueue();

    return true;
}

void TTSSpeaker::setSpeakingState(bool state, TTSSpeakerClient *client) {
    std::lock_guard<std::mutex> lock(m_stateMutex);

    m_isSpeaking = state;
    m_clientSpeaking = client;
    
    // If thread just completes speaking (called only from GStreamerThreadFunc),
    // it will take the next text from queue, no need to keep
    // m_flushed (as nothing is being spoken, which needs bail out)
    if(state == false)
        m_flushed = false;
}

void TTSSpeaker::queueData(SpeechData data) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.push_back(data);
    m_condition.notify_one();
}

void TTSSpeaker::flushQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queue.clear();
}

SpeechData TTSSpeaker::dequeueData() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    SpeechData d;
    d = m_queue.front();
    m_queue.pop_front();
    m_flushed = false;
    return d;
}

bool TTSSpeaker::waitForStatus(GstState expected_state, uint32_t timeout_ms) {
    // wait for the pipeline to get to pause so we know we have the audio device
    if(m_pipeline) {
        GstState state;
        GstState pending;

        auto timeout = std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms);

        do {
            std::unique_lock<std::mutex> mlock(m_queueMutex);
            m_condition.wait_until(mlock, timeout, [this, &state, &pending, expected_state] () {
                    // Speaker has flushed the data, no need wait for the completion
                    // must break and reset the pipeline
                    if(m_flushed) {
                        TTSLOG_VERBOSE("Bailing out because of forced text queue (m_flushed=true)");
                        return true;
                    }

                    gst_element_get_state(m_pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
                    if(state == expected_state)
                        return true;

                    return false;
                });
        } while(!m_flushed && state != expected_state && timeout > std::chrono::system_clock::now());

        if(state == expected_state) {
            TTSLOG_VERBOSE("Got Status : expected_state = %d, new_state = %d", expected_state, state);
            return true;
        }

        TTSLOG_WARNING("Timed Out waiting for state %s, currentState %s",
                gst_element_state_get_name(expected_state), gst_element_state_get_name(state));
        return false;
    }

    return true;
}

#ifdef INTELCE
static GstElement* findElement(GstElement *element, const char* targetName)
{
    GstElement *re = NULL;
    if (GST_IS_BIN(element)) {
        GstIterator* it = gst_bin_iterate_elements(GST_BIN(element));
        GValue item = G_VALUE_INIT;
        bool done = false;
        while(!done) {
            switch (gst_iterator_next(it, &item)) {
                case GST_ITERATOR_OK:
                    {
                        GstElement *next = GST_ELEMENT(g_value_get_object(&item));
                        done = (re = findElement(next, targetName)) != NULL;
                        g_value_reset (&item);
                        break;
                    }
                case GST_ITERATOR_RESYNC:
                    gst_iterator_resync (it);
                    break;
                case GST_ITERATOR_ERROR:
                case GST_ITERATOR_DONE:
                    done = true;
                    break;
            }
        }
        g_value_unset (&item);
        gst_iterator_free(it);
    } else {
        if (strstr(gst_element_get_name(element), targetName)) {
            re = element;
        }
    }
    return re;
}

static void onHaveType(GstElement *typefind, guint /*probability*/, GstCaps *srcPadCaps, gpointer user_data)
{
    GstElement* pipeline = static_cast<GstElement*>(user_data);

    if ((srcPadCaps == NULL) || (pipeline == NULL)) {
        TTSLOG_ERROR( "Typefind SRC Pad Caps NULL");
        return;
    }

    GstStructure *s = gst_caps_get_structure(srcPadCaps, 0);
    TTSLOG_WARNING("onHaveType %s", gst_structure_get_name(s));

    if (strncmp (gst_structure_get_name(s), "audio/", 6) == 0) {
        // link typefind directly to mpegaudioparse to complete pipeline
        GstElement *sink = findElement(pipeline,"mpegaudioparse");
        GstPad *sinkpad = gst_element_get_static_pad (sink, "sink");
        GstPad *srcpad  = gst_element_get_static_pad (typefind, "src");

        if(!gst_pad_is_linked(sinkpad) && !gst_pad_is_linked(srcpad)) {
            bool linked = GST_PAD_LINK_SUCCESSFUL(gst_pad_link (srcpad, sinkpad));
            if(!linked)
                TTSLOG_WARNING("Failed to link typefind and audio");
        }

        gst_object_unref (sinkpad);
        gst_object_unref (srcpad);
    } else if (strncmp (gst_structure_get_name(s), "application/x-id3", 17) == 0) {
        // link typefind to id3demux then id3demux to mpegaudioparse to complete pipeline
        GstElement *sink = findElement(pipeline,"mpegaudioparse");
        GstElement *id3demux = findElement(pipeline,"id3demux");
        GstPad *sinkpad = gst_element_get_static_pad (sink, "sink");
        GstPad *srcpad  = gst_element_get_static_pad (typefind, "src");
        GstPad *id3Sinkpad = gst_element_get_static_pad (id3demux, "sink");
        GstPad *id3Srcpad  = gst_element_get_static_pad (id3demux, "src");

        if(!gst_pad_is_linked(sinkpad) && !gst_pad_is_linked(srcpad)
                && !gst_pad_is_linked(id3Srcpad) && !gst_pad_is_linked(id3Sinkpad)) {
            bool linkedid3Sink = GST_PAD_LINK_SUCCESSFUL(gst_pad_link (srcpad, id3Sinkpad));
            bool linkedid3Src  = GST_PAD_LINK_SUCCESSFUL(gst_pad_link (id3Srcpad, sinkpad));
            if (!linkedid3Sink || !linkedid3Src)
                TTSLOG_WARNING("Failed to link typefind and audio");
        }

        gst_object_unref (id3Sinkpad);
        gst_object_unref (id3Srcpad);
        gst_object_unref (sinkpad);
        gst_object_unref (srcpad);
    }
}
#endif

// GStreamer Releated members
void TTSSpeaker::createPipeline() {
    TTSLOG_TRACE("Creating Pipeline...");

    bool result = TRUE;

    m_isEOS = false;

    m_pipeline = gst_pipeline_new(NULL);
    if (!m_pipeline) {
        TTSLOG_ERROR("Failed to create gstreamer pipeline");
        return;
    }

    m_source = gst_element_factory_make("souphttpsrc", NULL);

    // create soc specific elements
#if defined(BCM_NEXUS)
    GstElement *decodebin = NULL;
    decodebin = gst_element_factory_make("brcmmp3decoder", NULL);
    m_audioSink = gst_element_factory_make("brcmpcmsink", NULL);
#elif defined(INTELCE)
    GstElement *typefind = NULL;
    GstElement *id3demux = NULL;
    GstElement *parse = NULL;
    typefind = gst_element_factory_make("typefind", NULL);
    id3demux = gst_element_factory_make("id3demux", NULL);
    parse = gst_element_factory_make("mpegaudioparse", NULL);
    m_audioSink = gst_element_factory_make("ismd_audio_sink", NULL);
    // Need these properties so two gstreamer pipelines can play back audio at same time on ismd...
    g_object_set(G_OBJECT(m_audioSink), "sync", FALSE, NULL);
    g_object_set(G_OBJECT(m_audioSink), "audio-input-set-as-primary", FALSE, NULL);
#endif

    std::string tts_url =
        !m_defaultConfig.secureEndPoint().isEmpty() ? m_defaultConfig.secureEndPoint().cString() : m_defaultConfig.endPoint().cString();
    if(!tts_url.empty()) {
        if(!m_defaultConfig.voice().isEmpty()) {
            tts_url.append("voice=");
            tts_url.append(m_defaultConfig.voice());
        }

        if(!m_defaultConfig.language().isEmpty()) {
            tts_url.append("&language=");
            tts_url.append(m_defaultConfig.language());
        }

        tts_url.append("&text=init");
        curlSanitize(tts_url);

        g_object_set(G_OBJECT(m_source), "location", tts_url.c_str(), NULL);
    }

    // set the TTS volume to max.
    g_object_set(G_OBJECT(m_audioSink), "volume", (double) ((m_defaultConfig.volume() / MAX_VOLUME) * GST_PCM_VOLUME_MAX), NULL);

    // Add elements to pipeline and link
#if defined(BCM_NEXUS)
    gst_bin_add_many(GST_BIN(m_pipeline), m_source, decodebin, m_audioSink, NULL);
    result &= gst_element_link (m_source, decodebin);
    result &= gst_element_link (decodebin, m_audioSink);
#elif defined(INTELCE)
    gst_bin_add_many(GST_BIN(m_pipeline), m_source, typefind, id3demux, parse, m_audioSink, NULL);
    result &= gst_element_link (m_source, typefind);
    result &= gst_element_link (parse, m_audioSink);
    // used to link rest of elements based on typefind results
    g_signal_connect (typefind, "have-type", G_CALLBACK (onHaveType), m_pipeline);
#endif

    if(!result) {
        TTSLOG_ERROR("failed to link elements!");
        gst_object_unref(m_pipeline);
        m_pipeline = NULL;
        return;
    }

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_add_watch(bus, GstBusCallback, (gpointer)(this));
    gst_object_unref(bus);

    // wait until pipeline is set to NULL state
    resetPipeline();
}

void TTSSpeaker::resetPipeline() {
    TTSLOG_TRACE("Resetting Pipeline...");

    // Detect pipe line error and destroy the pipeline if any
    if(m_pipelineError) {
        TTSLOG_WARNING("Pipeline error occured, attempting to recover by re-creating pipeline");

        // Try to recover from errors by destroying the pipeline
        destroyPipeline();
        m_pipelineError = false;
    }

    if(!m_pipeline) {
        // If pipe line is NULL, create one
        createPipeline();
    } else {
        // If pipeline is present, bring it to NULL state
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        while(!waitForStatus(GST_STATE_NULL, 60*1000));
    }
}

void TTSSpeaker::destroyPipeline() {
    TTSLOG_TRACE("Destroying Pipeline...");

    if(m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        waitForStatus(GST_STATE_NULL, 1*1000);
        gst_object_unref(m_pipeline);
    }

    m_pipeline = NULL;
    m_condition.notify_one();
}

void TTSSpeaker::waitForAudioToFinishTimeout(float timeout_s) {
    TTSLOG_TRACE("timeout_s=%f", timeout_s);

    auto timeout = std::chrono::system_clock::now() + std::chrono::seconds((unsigned long)timeout_s);

    while(m_pipeline && !m_pipelineError && !m_isEOS && !m_flushed && timeout > std::chrono::system_clock::now()) {
        std::unique_lock<std::mutex> mlock(m_queueMutex);
        m_condition.wait_until(mlock, timeout, [this] () {
                if(!m_pipeline || m_pipelineError)
                    return true;

                // EOS enquiry
                if(m_isEOS)
                    return true;

                // Speaker has flushed the data, no need wait for the completion
                // must break and reset the pipeline
                if(m_flushed) {
                    TTSLOG_VERBOSE("Bailing out because of forced text queue (m_flushed=true)");
                    return true;
                }

                return false;
        });
    }
    TTSLOG_INFO("m_isEOS=%d, m_pipeline=%p, m_pipelineError=%d, m_flushed=%d",
            m_isEOS, m_pipeline, m_pipelineError, m_flushed);

    // Irrespective of EOS / Timeout reset pipeline
    if(m_pipeline)
        gst_element_set_state(m_pipeline, GST_STATE_NULL);

    if(!m_isEOS)
        TTSLOG_ERROR("Stopped waiting for audio to finish without hitting EOS!");
    m_isEOS = false;
}

void TTSSpeaker::replaceIfIsolated(std::string& text, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = text.find(search, pos)) != std::string::npos) {
        bool punctBefore = (pos == 0 || std::ispunct(text[pos-1]) || std::isspace(text[pos-1]));
        bool punctAfter = (pos+1 == text.length() || std::ispunct(text[pos+1]) || std::isspace(text[pos+1]));

        if(punctBefore && punctAfter) {
            text.replace(pos, search.length(), replace);
            pos += replace.length();
        } else {
            pos += search.length();
        }
    }
}

bool TTSSpeaker::isSilentPunctuation(const char c) {
    static std::string SilentPunctuation = "?!:;-()";
    return (SilentPunctuation.find(c) != std::string::npos);
}

void TTSSpeaker::replaceSuccesivePunctuation(std::string& text) {
    size_t pos = 0;
    while(pos < text.length()) {
        // Remove unwanted characters
        static std::string stray = "\"";
        if(stray.find(text[pos]) != std::string::npos) {
            text.erase(pos,1);
            if(++pos == text.length())
                break;
        }

        if(ispunct(text[pos])) {
            ++pos;
            while(pos < text.length() && (isSilentPunctuation(text[pos]) || isspace(text[pos]))) {
                if(isSilentPunctuation(text[pos]))
                    text.erase(pos,1);
                else
                    ++pos;
            }
        } else {
            ++pos;
        }
    }
}

void TTSSpeaker::curlSanitize(std::string &sanitizedString) {
    CURL *curl = curl_easy_init();
    if(curl) {
      char *output = curl_easy_escape(curl, sanitizedString.c_str(), sanitizedString.size());
      if(output) {
          sanitizedString = output;
          curl_free(output);
      }
    }
    curl_easy_cleanup(curl);
}

void TTSSpeaker::sanitizeString(rtString &input, std::string &sanitizedString) {
    sanitizedString = input.cString();

    replaceIfIsolated(sanitizedString, "$", "dollar");
    replaceIfIsolated(sanitizedString, "#", "pound");
    replaceIfIsolated(sanitizedString, "&", "and");
    replaceIfIsolated(sanitizedString, "|", "bar");
    replaceIfIsolated(sanitizedString, "/", "or");

    replaceSuccesivePunctuation(sanitizedString);

    curlSanitize(sanitizedString);

    TTSLOG_VERBOSE("In:%s, Out:%s", input.cString(), sanitizedString.c_str());
}

std::string TTSSpeaker::constructURL(TTSConfiguration &config, SpeechData &d) {
    if(!config.isValid()) {
        TTSLOG_ERROR("Invalid configuration");
        return "";
    }

    // EndPoint URL
    std::string tts_request;
    if(d.secure)
        tts_request.append(config.secureEndPoint());
    else
        tts_request.append(config.endPoint());

    // Voice
    if(!config.voice().isEmpty()) {
        tts_request.append("voice=");
        tts_request.append(config.voice().cString());
    }

    // Language
    if(!config.language().isEmpty()) {
        tts_request.append("&language=");
        tts_request.append(config.language());
    }

    // Rate / speed
    tts_request.append("&rate=");
    tts_request.append(std::to_string(config.rate() > 100 ? 100 : config.rate()));

    // Sanitize String
    std::string sanitizedString;
    sanitizeString(d.text, sanitizedString);

    tts_request.append("&text=");
    tts_request.append(sanitizedString);

    TTSLOG_WARNING("Constructured final URL is %s", tts_request.c_str());
    return tts_request;
}

void TTSSpeaker::speakText(TTSConfiguration config, SpeechData &data) {
    m_isEOS = false;

    if(m_pipeline && !m_pipelineError && !m_flushed) {
        g_object_set(G_OBJECT(m_source), "location", constructURL(config, data).c_str(), NULL);
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        TTSLOG_VERBOSE("Speaking.... (%d, \"%s\")", data.id, data.text.cString());

        //Wait for EOS with a timeout incase EOS never comes
        waitForAudioToFinishTimeout(60);
    } else {
        TTSLOG_WARNING("m_pipeline=%p, m_pipelineError=%d", m_pipeline, m_pipelineError);
    }
}

void TTSSpeaker::GStreamerThreadFunc(void *ctx) {
    TTSSpeaker *speaker = (TTSSpeaker*) ctx;

    TTSLOG_INFO("Starting GStreamerThread");
    speaker->createPipeline();

    while(speaker && speaker->m_runThread) {
        // Take an item from the queue
        TTSLOG_INFO("Waiting for text input");
        while(speaker->m_runThread && speaker->m_queue.empty()) {
            std::unique_lock<std::mutex> mlock(speaker->m_queueMutex);
            speaker->m_condition.wait(mlock, [speaker] () { return (!speaker->m_queue.empty() || !speaker->m_runThread); });
        }

        // Stop thread on Speaker's cue
        if(!speaker->m_runThread) {
            if(speaker->m_pipeline) {
                gst_element_set_state(speaker->m_pipeline, GST_STATE_NULL);
                speaker->waitForStatus(GST_STATE_NULL, 1*1000);
            }
            TTSLOG_INFO("Stopping GStreamerThread");
            return;
        }

        TTSLOG_INFO("Got text input, list size=%d", speaker->m_queue.size());
        SpeechData data = speaker->dequeueData();

        speaker->setSpeakingState(true, data.client);
        // Inform the client before speaking
        if(!speaker->m_flushed)
            data.client->willSpeak(data.id, data.text);

        // Push it to gstreamer for speaking
        if(!speaker->m_flushed) {
            speaker->speakText(*data.client->configuration(), data);
        }

        // Inform the client after speaking
        if(!speaker->m_flushed)
            data.client->spoke(data.id, data.text);
        speaker->setSpeakingState(false);

        // stop the pipeline until the next tts string...
        speaker->resetPipeline();
    }
}

int TTSSpeaker::GstBusCallback(GstBus *, GstMessage *message, gpointer data) {
    TTSSpeaker *speaker = (TTSSpeaker*)data;
    return speaker->handleMessage(message);
}

bool TTSSpeaker::handleMessage(GstMessage *message) {
    GError* error = NULL;
    gchar* debug = NULL;

    if(!m_pipeline) {
        TTSLOG_WARNING("NULL Pipeline");
        return false;
    }

    switch (GST_MESSAGE_TYPE(message)){
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(message, &error, &debug);
            TTSLOG_ERROR("error! code: %d, %s, Debug: %s", error->code, error->message, debug);
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "error-pipeline");
#ifdef BCM_NEXUS
            m_pipelineError = true;
            m_condition.notify_one();
#endif
            break;

        case GST_MESSAGE_WARNING:
            gst_message_parse_warning(message, &error, &debug);
            TTSLOG_WARNING("warning! code: %d, %s, Debug: %s", error->code, error->message, debug);
            break;

        case GST_MESSAGE_EOS:
            TTSLOG_INFO("Audio EOS message received");
            m_isEOS = true;
            m_condition.notify_one();
            break;

        case GST_MESSAGE_STATE_CHANGED:
            gchar* filename;
            GstState oldstate, newstate, pending;
            gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);

            // Ignore messages not coming directly from the pipeline.
            if (GST_ELEMENT(GST_MESSAGE_SRC(message)) != m_pipeline)
                break;

            filename = g_strdup_printf("%s-%s", gst_element_state_get_name(oldstate), gst_element_state_get_name(newstate));
            GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, filename);
            g_free(filename);

            // get the name and state
            TTSLOG_VERBOSE("%s old_state %s, new_state %s, pending %s",
                    GST_MESSAGE_SRC_NAME(message) ? GST_MESSAGE_SRC_NAME(message) : "",
                    gst_element_state_get_name (oldstate), gst_element_state_get_name (newstate), gst_element_state_get_name (pending));

            if (oldstate == GST_STATE_NULL && newstate == GST_STATE_READY) {
            } else if (oldstate == GST_STATE_READY && newstate == GST_STATE_PAUSED) {
                GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "paused-pipeline");
            } else if (oldstate == GST_STATE_PAUSED && newstate == GST_STATE_PAUSED) {
            } else if (oldstate == GST_STATE_PAUSED && newstate == GST_STATE_PLAYING) {
                GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "playing-pipeline");
            } else if (oldstate == GST_STATE_PLAYING && newstate == GST_STATE_PAUSED) {
            } else if (oldstate == GST_STATE_PAUSED && newstate == GST_STATE_READY) {
            } else if (oldstate == GST_STATE_READY && newstate == GST_STATE_NULL) {
            }
            break;

        default:
            break;
    }

    if(error)
        g_error_free(error);

    if(debug)
        g_free(debug);

    return true;
}

} // namespace TTS

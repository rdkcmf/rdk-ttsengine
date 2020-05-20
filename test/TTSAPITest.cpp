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

#include "TTSClient.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sstream>
#include <iostream>
#include <fstream>
#include <map>

using namespace std;

// --- //

#define NULL_CHECK(a) ((a)?(a):"")
#define PRINT_CONFIG(a) TTSLOG_WARNING("%s = %s", a, NULL_CHECK(getenv(a)))

// --- //

using namespace TTS;

volatile unsigned short g_connectedToTTSEventCount = 0;
volatile bool g_connectedToTTS = false;

class MyConnectionCallback : public TTSConnectionCallback {
public:
    virtual void onTTSServerConnected() {
        TTSLOG_INFO("Connection to TTSManager got established");
        g_connectedToTTS = true;
        ++g_connectedToTTSEventCount;
    }

    virtual void onTTSServerClosed() {
        TTSLOG_ERROR("Connection to TTSManager got closed, shutting app in 3 seconds!!!");
        --g_connectedToTTSEventCount;
        g_connectedToTTS = false;

        if(g_connectedToTTS <= 0)
            exit(0);
    }

    virtual void onTTSStateChanged(bool enabled) {
        TTSLOG_INFO("TTS is %s", enabled ? "enabled" : "disabled");
    }

    virtual void onVoiceChanged(std::string voice) {
        TTSLOG_INFO("TTS voice got changed to %s", voice.c_str());
    }
};

class MySessionCallback : public TTSSessionCallback {
public:
    virtual void onTTSSessionCreated(uint32_t appId, uint32_t sessionId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
    }

    virtual void onResourceAcquired(uint32_t appId, uint32_t sessionId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
    }

    virtual void onResourceReleased(uint32_t appId, uint32_t sessionId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
    }

    virtual void onSpeechStart(uint32_t appId, uint32_t sessionId, SpeechData &sd) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, sd.id);
    };

    virtual void onSpeechPause(uint32_t appId, uint32_t sessionId, uint32_t speechId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, speechId);
    };

    virtual void onSpeechResume(uint32_t appId, uint32_t sessionId, uint32_t speechId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, speechId);
    };

    virtual void onSpeechCancelled(uint32_t appId, uint32_t sessionId, uint32_t speechId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, speechId);
    };

    virtual void onSpeechInterrupted(uint32_t appId, uint32_t sessionId, uint32_t speechId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, speechId);
    };

    virtual void onNetworkError(uint32_t appId, uint32_t sessionId, uint32_t speechId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, speechId);
    };

    virtual void onPlaybackError(uint32_t appId, uint32_t sessionId, uint32_t speechId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, speechId);
    };

    virtual void onSpeechComplete(uint32_t appId, uint32_t sessionId, SpeechData &sd) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d, SpeechId=%d", appId, sessionId, sd.id);
    };
};

struct AppInfo {
    AppInfo(uint32_t sid) : m_sessionId(sid) {}
    uint32_t m_sessionId;
};

void validateReturn(TTS_Error error, uint32_t delay_us) {
    if(error != TTS_OK) {
        cout << "Operation Failed with code (" << error << ")" << endl;
    } else {
        cout << "Operation succeeded" << endl;
        usleep(1000 * delay_us);
    }
}

struct MyStream {
    MyStream() : myfile(NULL), in(&cin) {
    }

    MyStream(string fname) : myfile(new ifstream(fname)) {
        if(myfile->is_open()) {
            cout << "Reading from file" << endl;
            in = myfile;
        } else {
            cout << "Reading from std::cin" << endl;
            in = &cin;
        }
    }

    template<class T>
    bool getInput(T &var, const char *prompt = NULL, bool console = false) {
        stringstream ss;
        static char cstr[5 * 1024];
        string str;

        istream *tin = in;
        if(console)
            in = &cin;

        do {
            if(prompt)
                cout << prompt;
            try {
                if(in == myfile && in->eof())
                    in = &cin;
                in->getline(cstr, sizeof(cstr)-1);
            } catch(...) {
                if(in == myfile)
                    in = &cin;
            }

            str = cstr;
            if(in == myfile)
                cout << cstr << endl;

            if((str.find('#') == string::npos || !str.erase(str.find('#')).empty()) && !str.empty()) {
                ss.str(str);
                ss >> var;
                break;
            }
        } while(1);
        in = tin;

        return true;
    }

private:
    ifstream *myfile;
    istream *in;
};

#define OPT_ENABLE_TTS          1
#define OPT_VOICE_LIST          2
#define OPT_SET_CONFIG          3
#define OPT_GET_CONFIG          4
#define OPT_TTS_ENABLED         5
#define OPT_SESSION_ACTIVE      6
#define OPT_ACQUIRE_RESOURCE    7
#define OPT_CLAIM_RESOURCE      8
#define OPT_RELEASE_RESOURCE    9
#define OPT_CREATE_SESSION      10
#define OPT_IS_ACTIVE_SESSION   11
#define OPT_SET_PREEMPTIVE      12
#define OPT_REQ_EXT_EVENTS      13
#define OPT_SPEAK               14
#define OPT_PAUSE               15
#define OPT_RESUME              16
#define OPT_ABORT               17
#define OPT_IS_SPEAKING         18
#define OPT_SPEECH_STATE        19
#define OPT_DESTROY_SESSION     20
#define OPT_EXIT                21
#define OPT_BLOCK_TILL_INPUT    22
#define OPT_SLEEP               23

int main(int argc, char *argv[]) {
    std::map<uint32_t, AppInfo*> appInfoMap;

    TTSClient *client = TTSClient::create(new MyConnectionCallback);
    if(!client) {
        cout << "Couldn't create client connection" << endl;
        return -1;
    }

    sleep(1);

    int ival;
    int choice;
    TTS_Error error;
    MyStream stream((argc > 1 ? argv[1] : "example.txt"));
    while(g_connectedToTTS) {
        error = TTS_OK;
        int counter = 0;
        do {
            choice = 0;
            static bool flag = true;
            if(argc == 1 || flag) {
                flag = false;
                if(counter == 0) {
                    cout << endl;
                    cout << "------------------------" << endl;
                    cout << OPT_ENABLE_TTS          << ".enableTTS" << endl;
                    cout << OPT_VOICE_LIST          << ".listVoices" << endl;
                    cout << OPT_SET_CONFIG          << ".setTTSConfiguration" << endl;
                    cout << OPT_GET_CONFIG          << ".getTTSConfiguration" << endl;
                    cout << OPT_TTS_ENABLED         << ".isTTSEnabled" << endl;
                    cout << OPT_SESSION_ACTIVE      << ".isSessionActiveForApp" << endl;
                    cout << "-" << endl;
                    cout << OPT_ACQUIRE_RESOURCE    << ".acquireResource" << endl;
                    cout << OPT_CLAIM_RESOURCE      << ".claimResource" << endl;
                    cout << OPT_RELEASE_RESOURCE    << ".releaseResource" << endl;
                    cout << "-" << endl;
                    cout << OPT_CREATE_SESSION      << ".createSession" << endl;
                    cout << OPT_IS_ACTIVE_SESSION   << ".isActiveSession" << endl;
                    cout << OPT_SET_PREEMPTIVE      << ".setPreemptiveSpeech" << endl;
                    cout << OPT_REQ_EXT_EVENTS      << ".requestExtendedEvents" << endl;
                    cout << OPT_SPEAK               << ".speak" << endl;
                    cout << OPT_PAUSE               << ".pause" << endl;
                    cout << OPT_RESUME              << ".resume" << endl;
                    cout << OPT_ABORT               << ".abort" << endl;
                    cout << OPT_IS_SPEAKING         << ".isSpeaking" << endl;
                    cout << OPT_SPEECH_STATE        << ".getSpeechState" << endl;
                    cout << OPT_DESTROY_SESSION     << ".destroySession" << endl;
                    cout << OPT_EXIT                << ".exit" << endl;
                    cout << OPT_BLOCK_TILL_INPUT    << ".dummyInput" << endl;
                    cout << OPT_SLEEP               << ".sleep" << endl;
                    cout << "------------------------" << endl;
                } else {
                    cout << endl;
                }
            } else {
                cout << "------------------------" << endl;
            }
            stream.getInput(choice, "Enter your choice : ");

            // This is to handle bad input
            if(++counter == 2) {
                cin.clear();
                cin.ignore();
                counter = 1;
            }
        } while(g_connectedToTTS && !(choice >= OPT_ENABLE_TTS && choice <= OPT_SLEEP));

        bool res = 0;
        int sid = 0;
        int appid = 0;
        int secure = false;
        int sessionid = 0;
        char clearall = 'n';
        string stext;
        string appname;
        std::string language;
        std::vector<string> voices;
        Configuration config;
        SpeechData sdata;
        SpeechState sstate;
        switch(choice) {
            case OPT_ENABLE_TTS:
                int enable;
                stream.getInput(enable, "1.Enable/0.Disable TTS : ");
                error = client->enableTTS(enable);
                validateReturn(error, 100);
                break;

            case OPT_VOICE_LIST:
                stream.getInput(language, "Enter the language [\"*\" - all voices, \".\" - current voice]: ");
                error = client->listVoices(language == "." ? string() : language, voices);
                validateReturn(error, 0);

                cout << "Supported voices for language [" + language + "]" << endl;
                for(uint32_t i = 0; i < voices.size(); i++) {
                    if(i > 0)
                        cout << ", ";
                    cout << voices[i];
                }
                cout << endl;
                break;

            case OPT_SET_CONFIG:
                stream.getInput(config.language, "Enter language [en-US/es-MX] : ");
                stream.getInput(config.voice, "Enter voice [carol/Angelica] : ");
                stream.getInput(config.volume, "Enter volume [0.0-100.0] : ");
                stream.getInput(ival, "Enter speed [0-100] : ");
                config.rate = ival;
                error = client->setTTSConfiguration(config);
                validateReturn(error, 0);
                break;

            case OPT_GET_CONFIG:
                error = client->getTTSConfiguration(config);
                validateReturn(error, 0);
                cout << "ttsEndPoint : " << config.ttsEndPoint << endl;
                cout << "ttsEndPointSecured : " << config.ttsEndPointSecured << endl;
                cout << "language : " << config.language << endl;
                cout << "voice : " << config.voice << endl;
                cout << "volume : " << config.volume << endl;
                cout << "rate : " << (long)config.rate << endl;
                break;

            case OPT_TTS_ENABLED:
                res = client->isTTSEnabled(true);
                cout << "TTS is " << (res ? "Enabled" : "Disabled") << endl;
                break;

            case OPT_SESSION_ACTIVE:
                stream.getInput(appid, "Enter app id : ");
                res = client->isSessionActiveForApp(appid);
                cout << "App " << appid << (res ? " has session & active" : " has no session / inactive") << endl;
                break;

            case OPT_ACQUIRE_RESOURCE:
                stream.getInput(appid, "Enter app id : ");
                error = client->acquireResource(appid);
                validateReturn(error, 150);
                break;

            case OPT_CLAIM_RESOURCE:
                stream.getInput(appid, "Enter app id : ");
                error = client->claimResource(appid);
                validateReturn(error, 150);
                break;

            case OPT_RELEASE_RESOURCE:
                stream.getInput(appid, "Enter app id : ");
                error = client->releaseResource(appid);
                validateReturn(error, 150);
                break;

            case OPT_CREATE_SESSION:
                stream.getInput(appid, "Enter app id : ");
                stream.getInput(appname, "Enter app name : ");
                sessionid = client->createSession(appid, appname, new MySessionCallback());
                if(sessionid) {
                    appInfoMap[appid] = new AppInfo(sessionid);
                    cout << "Session (" << sessionid << ") created for app (" << appid << ")" << endl;
                } else {
                    cout << "Session couldn't be created for app (" << appid << ")" << endl;
                }
                break;

            case OPT_IS_ACTIVE_SESSION:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    res = client->isActiveSession(sessionid);
                    cout << "Session (" << sessionid << ") of app (" << appid << ") is " << (res ? "active" : "inactive") << endl;
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_SET_PREEMPTIVE:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    bool preemptive = true;
                    stream.getInput(preemptive, "Enter preemptive speech [0/1] : ");
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    error = client->setPreemptiveSpeak(sessionid, preemptive);
                    validateReturn(error, 0);
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_REQ_EXT_EVENTS:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    uint32_t events = 0;
                    stream.getInput(events,
                        "Enter required events flag \n[LSB6-Playback Error, LSB5-Network Error, LSB4-Interrupted, LSB3-Cancelled, LSB2-Resumed, LSB1-Paused]\nEnter events flag [0-63] : ");
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    error = client->requestExtendedEvents(sessionid, events);
                    validateReturn(error, 0);
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_SPEAK:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    stream.getInput(secure, "Secure/Plain Transfer [0/1] : ");
                    stream.getInput(sid, "Speech Id (int) : ");
                    stream.getInput(stext, "Enter text to be spoken : ");
                    sdata.secure = secure;
                    sdata.id = sid;
                    sdata.text = stext;
                    error = client->speak(sessionid, sdata);
                    validateReturn(error, 100);
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_PAUSE:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    stream.getInput(sid, "Speech Id (int) : ");
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    error = client->pause(sessionid, sid);
                    validateReturn(error, 0);
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_RESUME:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    stream.getInput(sid, "Speech Id (int) : ");
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    error = client->resume(sessionid, sid);
                    validateReturn(error, 0);
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_ABORT:
                stream.getInput(appid, "Enter app id : ");
                stream.getInput(clearall, "Should clear pending speeches [y/n]: ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    error = client->abort(sessionid, (clearall == 'y' || clearall == 'Y'));
                    validateReturn(error, 0);
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_IS_SPEAKING:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    res = client->isSpeaking(sessionid);
                    cout << "Session (" << sessionid << ") of app (" << appid << ") is " << (res ? "speaking" : "not speaking") << endl;
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_SPEECH_STATE:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    stream.getInput(sid, "Speech Id (int) : ");
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    error = client->getSpeechState(sessionid, sid, sstate);
                    validateReturn(error, 0);
                    string state;
                    switch(sstate) {
                        case SPEECH_PENDING: state = "Pending"; break;
                        case SPEECH_IN_PROGRESS: state = "In Progress/Speaking"; break;
                        case SPEECH_PAUSED: state = "Paused"; break;
                        default: state = "Not found";
                    }
                    cout << "Speech Status : " << state << endl;
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_DESTROY_SESSION:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    error = client->destroySession(appInfoMap.find(appid)->second->m_sessionId);
                    validateReturn(error, 100);
                    AppInfo *ai = appInfoMap.find(appid)->second;
                    appInfoMap.erase(appInfoMap.find(appid));
                    delete ai;
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case OPT_EXIT:
            exit(0);

            case OPT_BLOCK_TILL_INPUT: {
                std::string in;
                stream.getInput(in, "Enter any value to continue : ", true);
                }
            break;

            case OPT_SLEEP:
            stream.getInput(appid, "Enter delay (in secs) : ");
            sleep(appid);
            break;
        }
    }

    return 0;
}

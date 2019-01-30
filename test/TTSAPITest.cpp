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

    virtual void onSpeechStart(uint32_t appId, uint32_t sessionId, SpeechData &) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
    };

    virtual void onSpeechComplete(uint32_t appId, uint32_t sessionId, SpeechData &) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
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
    bool getInput(T &var, const char *prompt = NULL) {
        if(prompt)
            cout << prompt;
        *in >> var;
        if(in == myfile)
            cout << var << endl;
        return true;
    }

private:
    ifstream *myfile;
    istream *in;
};

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
                cout << "1.enableTTS" << endl;
                cout << "2.setTTSConfiguration" << endl;
                cout << "3.isTTSEnabled" << endl;
                cout << "4.isSessionActiveForApp" << endl;
                cout << "-" << endl;
                cout << "5.acquireResource" << endl;
                cout << "6.claimResource" << endl;
                cout << "7.releaseResource" << endl;
                cout << "-" << endl;
                cout << "8.createSession" << endl;
                cout << "9.isActiveSession" << endl;
                cout << "10.speak" << endl;
                cout << "11.abort" << endl;
                cout << "12.isSpeaking" << endl;
                cout << "13.destroySession" << endl;
                cout << "14.exit" << endl;
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
        } while(g_connectedToTTS && !(choice >= 1 && choice <= 15));

        bool res = 0;
        int sid = 0;
        int appid = 0;
        int secure = false;
        string stext;
        string appname;
        int sessionid = 0;
        Configuration config;
        SpeechData sdata;
        switch(choice) {
            case 1:
                int enable;
                stream.getInput(enable, "1.Enable/0.Disable TTS : ");
                error = client->enableTTS(enable);
                validateReturn(error, 100);
                break;

            case 2:
                stream.getInput(config.language, "Enter language [en-US/es-MX] : ");
                stream.getInput(config.voice, "Enter voice [carol/Angelica] : ");
                stream.getInput(config.volume, "Enter volume [0.0-100.0] : ");
                stream.getInput(ival, "Enter speed [0-100] : ");
                config.rate = ival;
                error = client->setTTSConfiguration(config);
                validateReturn(error, 0);
                break;

            case 3:
                res = client->isTTSEnabled(true);
                cout << "TTS is " << (res ? "Enabled" : "Disabled") << endl;
                break;

            case 4:
                stream.getInput(appid, "Enter app id : ");
                res = client->isSessionActiveForApp(appid);
                cout << "App " << appid << (res ? " has session & active" : " has no session / inactive") << endl;
                break;

            case 5:
                stream.getInput(appid, "Enter app id : ");
                error = client->acquireResource(appid);
                validateReturn(error, 150);
                break;

            case 6:
                stream.getInput(appid, "Enter app id : ");
                error = client->claimResource(appid);
                validateReturn(error, 150);
                break;

            case 7:
                stream.getInput(appid, "Enter app id : ");
                error = client->releaseResource(appid);
                validateReturn(error, 150);
                break;

            case 8:
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

            case 9:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    res = client->isActiveSession(sessionid);
                    cout << "Session (" << sessionid << ") of app (" << appid << ") is " << (res ? "active" : "inactive") << endl;
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case 10:
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

            case 11:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    error = client->abort(sessionid);
                    validateReturn(error, 0);
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case 12:
                stream.getInput(appid, "Enter app id : ");
                if(appInfoMap.find(appid) != appInfoMap.end()) {
                    sessionid = appInfoMap.find(appid)->second->m_sessionId;
                    res = client->isSpeaking(sessionid);
                    cout << "Session (" << sessionid << ") of app (" << appid << ") is " << (res ? "speaking" : "not speaking") << endl;
                } else {
                    cout << "Session hasn't been created for app(" << appid << ")" << endl;
                }
                break;

            case 13:
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

            case 14:
            exit(0);

            case 15:
            stream.getInput(appid, "Enter delay (in secs) : ");
            sleep(appid);
            break;
        }
    }

    return 0;
}

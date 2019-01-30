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

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <condition_variable>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <regex>
#include <list>
#include <map>

// --- //

#define NULL_CHECK(a) ((a)?(a):"")
#define PRINT_CONFIG(a) TTSLOG_WARNING("%s = %s", a, NULL_CHECK(getenv(a)))

// --- //

using namespace TTS;

GMainLoop* g_loop;
volatile bool g_connectedToTTS = false;
volatile bool g_sessionActive = false;
std::condition_variable g_condition;
std::mutex g_mutex;

class MyConnectionCallback : public TTSConnectionCallback {
public:
    virtual void onTTSServerConnected() {
        TTSLOG_INFO("Connection to TTSManager got established");
        g_connectedToTTS = true;
        g_condition.notify_one();
    }

    virtual void onTTSServerClosed() {
        TTSLOG_ERROR("Connection to TTSManager got closed, shutting app in 3 seconds!!!");
        g_timeout_add_seconds(3, [](gpointer) -> gboolean { exit(1); return false; }, NULL);
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
        g_sessionActive = true;
        g_condition.notify_one();
    }

    virtual void onResourceReleased(uint32_t appId, uint32_t sessionId) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
        g_sessionActive = false;
    }

    virtual void onSpeechStart(uint32_t appId, uint32_t sessionId, SpeechData &) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
    };

    virtual void onSpeechComplete(uint32_t appId, uint32_t sessionId, SpeechData &) {
        TTSLOG_WARNING("AppId=%d, SessionId=%d", appId, sessionId);
    };
};

struct ThreadData {
public:
    ThreadData() :
        isReservationMgr(false),
        console(false),
        noReservation(false),
        preReservation(false),
        flipReqType(false),
        claimResource(false),
        appId(1),
        loopCount(1),
        activationCount(1),
        speakCount(1),
        mainloop(NULL) {
            sd.id = 100;
            sd.text = "Hi, How are you?";
            appName = (char*)"WPE";
        }

    int isReservationMgr;
    std::string configFile;
    int console;
    int noReservation;
    int preReservation;
    int flipReqType;
    int claimResource;
    unsigned long appId;
    std::string  appName;
    int loopCount;
    int activationCount;
    int speakCount;
    SpeechData sd;
    GMainLoop *mainloop;
};

void ThreadFunc(void *data) {
    ThreadData &td = *(ThreadData*)data;
    uint32_t app_id = td.appId;
    uint32_t session_id = 0;

    TTSClient *session = TTSClient::create(new MyConnectionCallback);

    auto c_timeout = std::chrono::system_clock::now() + std::chrono::seconds(10);
    while(!g_connectedToTTS && std::chrono::system_clock::now() < c_timeout) {
        TTSLOG_WARNING("Waiting for TTS Manager connection");
        std::unique_lock<std::mutex> lock(g_mutex);
        g_condition.wait_until(lock, c_timeout, []() { return g_connectedToTTS; });
    }

    if(!g_connectedToTTS) {
        TTSLOG_ERROR("Cound't connect to TTS Manager, exiting...");
        g_main_loop_quit(g_loop);
        return;
    }

    if(!session->isTTSEnabled()) {
        session->enableTTS();
    }

    char *v = getenv("timeout");
    int interval = 3;
    if(v) {
        interval = atoi(v);
    }

    if(td.isReservationMgr) {
        struct Res {
            unsigned long id;
            unsigned long duration;
        };

        std::list<Res> rsrvConfig;
        if(td.console) {
            char op = 0;
            unsigned long appId = 0;
            while(1) {
                op = 0;
                appId = 0;
                while(1) {
                    op = 0;
                    std::cout << "Op [a-Acquire, c-Claim, r-Release, q-Quit] : ";
                    std::cin >> op;
                    if(op == 'a' || op == 'c' || op == 'r' || op == 'q')
                        break;
                }
                if(op == 'q')
                    break;

                std::string in;
                while(appId == 0 || appId > 10) {
                    appId = 0;
                    std::cout << "App ID [0-10]: ";
                    std::cin >> in;
                    appId = std::atol(in.c_str());
                }
                switch(op) {
                    case 'a':
                        session->acquireResource(appId);
                        break;

                    case 'c':
                        session->claimResource(appId);
                        break;

                    case 'r':
                        session->releaseResource(appId);
                        break;
                }
            }
        } else if(!td.configFile.empty()) {
            // Read configuration file and update m_defaultConfiguration
            std::ifstream configFile(td.configFile, std::ios::in);

            if(configFile.is_open()) {
                std::cmatch m;
                std::string line;
                std::map<std::string, std::string> configSet;
                std::regex re_duration("\\s*([0-9]{1,})\\s*$");
                std::regex re_reservation("\\s*([acr])\\s*[,:-]\\s*([0-9]{1,})\\s*$");

                while(1) {
                    if(std::getline(configFile, line) && !line.empty()) {
                        if(std::regex_match(line.c_str(), m, re_duration) && m.size() == 2) {
                            std::cout << "Sleep " << m[1].str() << std::endl;
                            sleep(std::atol(m[1].str().c_str()));
                        } else if(std::regex_match(line.c_str(), m, re_reservation) && m.size() >= 3) {
                            std::cout << "Type : " << m[1].str() << ", App : " << m[2].str() << std::endl;

                            switch(m[1].str().c_str()[0]) {
                                case 'a':
                                    session->acquireResource(std::atol(m[2].str().c_str()));
                                    break;

                                case 'c':
                                    session->claimResource(std::atol(m[2].str().c_str()));
                                    break;

                                case 'r':
                                    session->releaseResource(std::atol(m[2].str().c_str()));
                                    break;
                            }
                        }
                    } else
                        break;
                }
            }
        }
    } else {
        int lc = td.loopCount;
        while(lc--) {
            if(!td.noReservation && td.preReservation) {
                TTSLOG_WARNING("-Acquire Resource");
                session->acquireResource(app_id);
            }

            TTSLOG_WARNING("-Create Session");
            session_id = session->createSession(app_id, td.appName.c_str(), new MySessionCallback);

            int ac = td.activationCount;
            while(ac--) {

                if(!td.noReservation && !td.preReservation) {
                    if(td.claimResource) {
                        TTSLOG_WARNING("-Claim resources");
                        session->claimResource(app_id);
                    } else {
                        TTSLOG_WARNING("-Acquire resources");
                        session->acquireResource(app_id);
                    }
                }

                auto a_timeout = std::chrono::system_clock::now() + std::chrono::seconds(10);
                while(!g_sessionActive && std::chrono::system_clock::now() < a_timeout) {
                    TTSLOG_WARNING("Waiting for Session to become active");
                    std::unique_lock<std::mutex> lock(g_mutex);
                    g_condition.wait_until(lock, a_timeout, [session, session_id]() { return g_sessionActive; });
                }

                if(!g_sessionActive) {
                    TTSLOG_ERROR("Timed out waiting for Session to become active");
                    continue;
                }

                if(getenv("ENABLE_STRESS_TEST")) {
                    new std::thread([session, session_id]() {
                            while(1) {
                            TTSLOG_WARNING("TTS Session is %s", session->isActiveSession(session_id, true) ? "Active" : "Inactive");
                            usleep(500);
                            }
                            });

                    new std::thread([session]() {
                            while(1) {
                            TTSLOG_WARNING("TTS Manager is %s", session->isTTSEnabled(true) ? "Active" : "Inactive");
                            usleep(250);
                            }
                            });

                    if(!td.noReservation) {
                        new std::thread([session, app_id]() {
                                while(1) {
                                sleep(1);
                                session->releaseResource(app_id);
                                sleep(1);
                                session->acquireResource(app_id);
                                }
                                });
                    }
                }

                int sc = td.speakCount;
                while(sc--) {
                    if(sc % 2 == 0)
                        td.sd.secure = false;
                    else
                        td.sd.secure = true;
                    td.sd.id = 100 + (td.speakCount - sc);
                    TTSLOG_WARNING("-Speak - %d", td.sd.id);
                    if(!getenv("SKIP_SPEAK"))
                        session->speak(session_id, td.sd);
                    sleep(interval >= 0 ? interval : 3);
                }

                if(!td.noReservation && !td.preReservation) {
                    if(!getenv("SKIP_RESOURCE_RELEASE")) {
                        TTSLOG_WARNING("-Release resources");
                        session->releaseResource(app_id);
                    }
                    sleep(1);
                }

                if(td.flipReqType)
                    td.claimResource = !td.claimResource;
            }

            if(!td.noReservation && td.preReservation) {
                if(!getenv("SKIP_RESOURCE_RELEASE")) {
                    TTSLOG_WARNING("-Release resources");
                    session->releaseResource(app_id);
                }
                sleep(1);
            }

            TTSLOG_WARNING("-Destroy Session");
            session->destroySession(session_id);
        }
    }

    g_main_loop_quit(g_loop);
}

int main(int argc, char *argv[]) {
    /* start the gmain loop */
    g_loop = g_main_loop_new(g_main_context_default(), FALSE);

    PRINT_CONFIG("timeout");

    if(argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf(\
        " \n\
        The test loop is designed like this \n\
        while [1-lC] \n\
        { \n\
            // Reserve Resource \n\
            // Create Session \n\
            while [1-aC] \n\
            { \n\
                // Request / Claim Resource \n\
                while [1-sC] \n\
                { \n\
                    // Speak \n\
                } \n\
                // Release Requested / Claimed Resource \n\
            } \n\
            // Release Reserved Resource \n\
            // Destroy Session \n\
        } \n\
            \n\
        Usage : \n\
        * simple client which creates a session, reserves resource, speaks, releases the resource and destroys the session \n\
            %s [-i <appid>] [-a <appname>] [-t <text-to-speak>] [-sC <x>] [-aC <y>] [-lC <z>] \n\
            \n\
        * simple client which reserves resource, creates a session, speaks, releases the resource and destroys the session \n\
            %s --preReservation [-i <appid>] [-a <appname>] [-t <text-to-speak>] [-sC <x>] [-aC <y>] [-lC <z>] \n\
            \n\
            Here the resource request will be made by the client even before the session is created, resource release happens as usual \n\
            \n\
        * simple client which does the above except requesting resource by itself (requires resource to be assigned from outside) \n\
            %s --noReservation [-i <appid>] [-a <appname>] [-t <text-to-speak>] [-sC <x>] [-aC <y>] [-lC <z>] \n\
            \n\
            In this case the resource allocation happens from outside through the exposed reserveResource(appId), and so. \n\
            Refer next type of test invocation \n\
            \n\
        * a reservation manager test app, with console input \n\
            %s --reservationMgr --console \n\
            \n\
        * a reservation manager test app, which reads the reservation sequence from a file \n\
            %s --reservationMgr -f <config_file_path> \n\
        \n", argv[0], argv[0], argv[0], argv[0], argv[0]);

        return 0;
    }

    ThreadData td;
    static struct option long_options[] =
    {
        {"noReservation",   no_argument,       &td.noReservation, 1},
        {"preReservation",  no_argument,       &td.preReservation, 1},
        {"flipReq",         no_argument,       &td.flipReqType, 1},
        {"claim",           no_argument,       &td.claimResource, 1},
        {"id",              required_argument, 0, 'i'},
        {"app",             required_argument, 0, 'a'},
        {"text",            required_argument, 0, 't'},
        {"lC",              required_argument, 0, 'l'},
        {"aC",              required_argument, 0, 'r'},
        {"sC",              required_argument, 0, 's'},

        {"reservationMgr",  no_argument,       &td.isReservationMgr, 1},
        {"console",         no_argument,       &td.console, 1},
        {"file",            required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    while (1)
    {
        /* getopt_long stores the option index here. */
        int option_index = 0;

        int c = getopt_long (argc, argv, "ci:a:t:l:r:s:f:",
                long_options, &option_index);

        /* Check if none of the given options selected to break the loop */
        if (c == -1)
            break;

        switch (c)
        {
            case 0:
                /* Option 0 is selected. Don't do anything and break the loop */
                if (long_options[option_index].flag != 0)
                    break;
                printf ("option %s", long_options[option_index].name);
                if (optarg)
                    printf (" with arg %s", optarg);
                printf ("\n");
                break;

            case 'i':
                td.appId = atoi(optarg);
                break;

            case 'a':
                td.appName = strdup(optarg);
                break;

            case 't':
                td.sd.text = strdup(optarg);
                break;

            case 'l':
                td.loopCount = atoi(optarg);
                break;

            case 'r':
                td.activationCount = atoi(optarg);
                break;

            case 's':
                td.speakCount = atoi(optarg);
                break;

            case 'f':
                td.configFile = strdup(optarg);
                break;

            case '?':
                /* getopt_long already printed an error message. */
                break;

            default:
                abort ();
        }
    }

    printf("RsrvMgr=%d, console=%d, configFile=%s, flipReq=%d, claimReq=%d, appId=%lu, appName=%s, loopCount=%d, activationCount=%d, speakCount=%d, text=%s\n",
            td.isReservationMgr,
            td.console,
            td.configFile.c_str(),
            td.flipReqType,
            td.claimResource,
            td.appId,
            td.appName.c_str(),
            td.loopCount,
            td.activationCount,
            td.speakCount,
            td.sd.text.c_str());

    new std::thread(ThreadFunc, &td);

    g_main_loop_run(g_loop);

    return 0;
}

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
#define MAX_CLIENT_COUNT 10
#define MAX_SESSION_COUNT 50

// --- //

using namespace TTS;

volatile unsigned short g_connectedToTTSEventCount = 0;
volatile bool g_connectedToTTS = false;

std::condition_variable g_condition;
std::mutex g_mutex;
GMainLoop* g_loop;

class MyConnectionCallback : public TTSConnectionCallback {
public:
    virtual void onTTSServerConnected() {
        TTSLOG_INFO("Connection to TTSManager got established");
        g_connectedToTTS = true;
        ++g_connectedToTTSEventCount;
        g_condition.notify_one();
    }

    virtual void onTTSServerClosed() {
        TTSLOG_ERROR("Connection to TTSManager got closed, shutting app in 3 seconds!!!");
        --g_connectedToTTSEventCount;
        // g_timeout_add_seconds(3, [](gpointer) -> gboolean { exit(1); return false; }, NULL);
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

struct ThreadData {
public:
    ThreadData() :
        appSeed(1),
        clientCount(1),
        sessionCount(1),
        loopCount(1),
        abortTest(false),
        mainloop(NULL) {
        }

    int appSeed;
    int clientCount;
    int sessionCount;
    int loopCount;
    int abortTest;
    GMainLoop *mainloop;
};

void ThreadFunc(void *data) {
    ThreadData &td = *(ThreadData*)data;
    unsigned long appid = 0;

    TTSClient *client_list[MAX_CLIENT_COUNT];
    int session_list[MAX_CLIENT_COUNT][MAX_SESSION_COUNT] = {0};

    // Create n clients
    for(int i = 0; i < td.clientCount; i++) {
        client_list[i] = TTSClient::create(new MyConnectionCallback);
    }

    // Wait for all clients to be connected to TTSEngine
    auto c_timeout = std::chrono::system_clock::now() + std::chrono::seconds(10);
    while(!g_connectedToTTS && std::chrono::system_clock::now() < c_timeout) {
        TTSLOG_WARNING("Waiting for TTS Manager connection");
        std::unique_lock<std::mutex> lock(g_mutex);
        g_condition.wait_until(lock, c_timeout, [td]() { return g_connectedToTTS && (g_connectedToTTSEventCount == td.clientCount); });
    }
    sleep(1);

    if(!g_connectedToTTS) {
        TTSLOG_ERROR("Cound't connect to TTS Manager, exiting...");
        g_main_loop_quit(g_loop);
        return;
    }

#if 0
    // Multi-client callback reception test
    for(int i = 0; i < td.clientCount; i++) {
        static bool flag = true;
        client_list[i]->enableTTS(flag);
        sleep(3);
        flag = !flag;
        delete client_list[i];
    }
    g_main_loop_quit(g_loop);
    return;
#else
    // Enable TTS
    client_list[0]->enableTTS();
#endif

    char *v = getenv("timeout");
    int interval = 3;
    if(v) {
        interval = atoi(v);
    }

    // Create sessions
    for(int c = 0; c < td.clientCount; c++) {
        if(c == 1)
            client_list[0]->enableTTS(false);

        TTSLOG_WARNING("-Create Sessions");
        for(int s = 0; s < td.sessionCount; s++) {
            appid = td.appSeed + (c * td.sessionCount) + s;
            session_list[c][s] = client_list[c]->createSession(appid, "WPE", new MySessionCallback);
        }
    }

    SpeechData sd;
    sd.text = "Hi, How do you do?";
    for(int c = 0; c < td.clientCount; c++) {
        for(int s = 0; s < td.sessionCount; s++) {

                // Simulate TTS state change
                if(s % 2 == 0)
                    client_list[c]->enableTTS(true);
                else
                    client_list[c]->enableTTS(false);

                // Acquire Resource
                appid = td.appSeed + (c * td.sessionCount) + s;
                TTSLOG_WARNING("-Acquire resources, %d", appid);
                client_list[c]->acquireResource(appid);
                sleep(1);

                // Speak
                for(int l = 0; l < td.loopCount; l++) {
                    sd.id = c + s + l;
                    TTSLOG_WARNING("-Speak - %d", sd.id);
                    if(client_list[c]->speak(session_list[c][s], sd) == TTS_OK) {
                        // If Speech request is successful, wait for its completion
                        if(td.abortTest) {
                            sleep(1);
                            TTSLOG_WARNING("Aborting Speech...");
                            client_list[c]->abort(session_list[c][s]);
                        } else {
                            sleep(interval >= 0 ? interval : 3);
                        }
                    }
                }

                // Release Resource
                TTSLOG_WARNING("-Release resources");
                client_list[c]->releaseResource(appid);
                sleep(1);
        }

        // Delete client
        delete client_list[c];
        client_list[c] = NULL;
    }

    // Wait to observe any misbehavior (crash / dump) post cleanup
    sleep(10);

    g_main_loop_quit(g_loop);
}

int main(int argc, char *argv[]) {
    /* start the gmain loop */
    g_loop = g_main_loop_new(g_main_context_default(), FALSE);

    PRINT_CONFIG("timeout");

    if(argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf(\
        " \n\
        Usage : \n\
        * %s [--appSeed <seed_value_for_appid>] [--cC <x>] [--sC <y>] [--lC <z>] [--testAbort]\n\
        \n", argv[0]);

        return 0;
    }

    ThreadData td;
    static struct option long_options[] =
    {
        {"appSeed",         required_argument, 0, 'a'},
        {"cC",              required_argument, 0, 'c'},
        {"sC",              required_argument, 0, 's'},
        {"lC",              required_argument, 0, 'l'},

        {"testAbort",       no_argument, &td.abortTest, 1},

        {0, 0, 0, 0}
    };

    while (1)
    {
        /* getopt_long stores the option index here. */
        int option_index = 0;

        int c = getopt_long (argc, argv, "a:c:s:l:",
                long_options, &option_index);

	/* Check if none of the given options selected to break the loop */
        if (c == -1)
            break;

        switch (c)
        {
            case 0:
		/* Don't do anything and continue to check other input options */
                if (long_options[option_index].flag != 0)
                    break;
                printf ("option %s", long_options[option_index].name);
                if (optarg)
                    printf (" with arg %s", optarg);
                printf ("\n");
                break;

            case 'a':
                td.appSeed = atoi(optarg);
                if(td.appSeed <= 0 )
                    td.appSeed = 1;
                break;

            case 'c':
                td.clientCount = atoi(optarg);
                if(td.clientCount > MAX_CLIENT_COUNT)
                    td.clientCount = MAX_CLIENT_COUNT;
                break;

            case 's':
                td.sessionCount = atoi(optarg);
                if(td.sessionCount > MAX_SESSION_COUNT)
                    td.sessionCount = MAX_SESSION_COUNT;
                break;

            case 'l':
                td.loopCount = atoi(optarg);
                break;

            case '?':
                /* getopt_long already printed an error message. */
                break;

            default:
                abort ();
        }
    }

    printf("AppSeed=%d, Client Count=%d, Session Count=%d, Loop Count=%d, Test Abort=%d\n",
            td.appSeed,
            td.clientCount,
            td.sessionCount,
            td.loopCount,
            td.abortTest);

    new std::thread(ThreadFunc, &td);

    g_main_loop_run(g_loop);

    return 0;
}

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
#ifndef _TTS_ERRORS_H_
#define _TTS_ERRORS_H_

namespace TTS {

enum ResourceAllocationPolicy {
    INVALID_POLICY = -1,
    RESERVATION, // Resource must be reserved before usage
    PRIORITY,    // NOT IMPLEMENTED
    OPEN         // Any client can use the resource without any prior reservation
};

enum TTS_Error {
    TTS_OK = 0,
    TTS_FAIL,
    TTS_NOT_ENABLED,
    TTS_CREATE_SESSION_DUPLICATE,
    TTS_EMPTY_APPID_INPUT,
    TTS_RESOURCE_BUSY,
    TTS_NO_SESSION_FOUND,
    TTS_NESTED_CLAIM_REQUEST,
    TTS_INVALID_CONFIGURATION,
    TTS_SESSION_NOT_ACTIVE,
    TTS_APP_NOT_FOUND,
    TTS_POLICY_VIOLATION,
    TTS_OBJECT_DESTROYED = 1010,
};

}

#endif

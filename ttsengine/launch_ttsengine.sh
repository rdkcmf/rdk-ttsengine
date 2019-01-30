#!/bin/sh
##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

killall TTSEngine

if [ -f /opt/SetEnv.sh ] && [ "$BUILD_TYPE" != "prod" ]; then
    . /opt/SetEnv.sh
fi

if [ -z "$GST_REGISTRY" ]; then
    export GST_REGISTRY="/opt/.gstreamer/registry.bin"
fi

if [ -z "$GST_REGISTRY_UPDATE" ]; then
    export GST_REGISTRY_UPDATE="no"
fi

if [ -z "$GST_REGISTRY_FORK" ]; then
    export GST_REGISTRY_FORK="no"
fi

LAUNCH_COUNT=1
while :; do
    if [ ! -e '/usr/bin/TTSEngine' ]; then
        echo "\"/usr/bin/TTSEngine\" binary is not found"
        exit 0;
    fi

    LOG_FILE_DIR=/opt/logs
    LOG_FILE_NAME=tts_engine.log
    LOG_FILE=$LOG_FILE_DIR/$LOG_FILE_NAME
    LOG_FILE_TIMESTAMP=`date "+%b %d, %H:%M:%S"`

    echo >> $LOG_FILE
    echo >> $LOG_FILE
    echo "**********************************************************************"             >> $LOG_FILE
    echo "Start of new log :- Time Stamp - $LOG_FILE_TIMESTAMP, Launch Count - $LAUNCH_COUNT" >> $LOG_FILE
    echo "**********************************************************************"             >> $LOG_FILE

    echo "Starting TTSEngine..."
    TTSEngine >> $LOG_FILE 2>&1
    echo "TTSEngine is terminated / stopped..."
    LAUNCH_COUNT=$((LAUNCH_COUNT+1))
done


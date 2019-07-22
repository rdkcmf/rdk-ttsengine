##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2016 RDK Management
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
CURRENTPATH = `pwd`
all: TTSEngine libTTSClient.so TTSMultiClientTest TTSAPITest

VPATH=linux

SEARCH=\
  -I./common/ \
  -I$(SYSROOT_INCLUDES_DIR)/ \
  -I$(SYSROOT_INCLUDES_DIR)/pxcore \
  -I$(SYSROOT_INCLUDES_DIR)/glib-2.0 \
  -I$(SYSROOT_INCLUDES_DIR)/libsoup-2.4 \
  -I$(SYSROOT_INCLUDES_DIR)/gstreamer-1.0 \
  -I$(SYSROOT_LIBS_DIR)/zlib \
  -I$(SYSROOT_LIBS_DIR)/openssl \
  -I$(SYSROOT_LIBS_DIR)/glib-2.0/include

COMMON_CXXFLAGS += $(CXXFLAGS) -Wno-attributes -Wall -Wextra -Werror -g -fpermissive $(SEARCH) -DRT_PLATFORM_LINUX -std=c++1y -fPIC

COMMON_LDFLAGS = -lglib-2.0 -lpthread -lgobject-2.0 -lcurl
ifdef ENABLE_RDK_LOGGER
COMMON_CXXFLAGS += -DUSE_RDK_LOGGER
COMMON_LDFLAGS += -lrdkloggers -llog4c
endif

ifdef ENABLE_BREAKPAD
COMMON_CXXFLAGS += -DUSE_BREAKPAD
COMMON_LDFLAGS += -lbreakpadwrapper
endif

TTSENGINE_LDFLAGS = $(COMMON_LDFLAGS) -lgstreamer-1.0
TTSCLIENT_LDFLAGS = $(COMMON_LDFLAGS)
TTSTEST_LDFLAGS = $(COMMON_LDFLAGS)

ifeq ($(USE_PXCORE_STATIC_LIBS),TRUE)
TTSENGINE_LDFLAGS += -lrtRemote_s -lrtCore_s
TTSCLIENT_LDFLAGS += -lrtRemote_s -lrtCore_s
else
TTSENGINE_LDFLAGS += -lrtRemote -lrtCore
TTSCLIENT_LDFLAGS += -lrtRemote -lrtCore
TTSTEST_LDFLAGS += -lrtRemote -lrtCore
endif

Common_includes = $(wildcard ./common/*.h ./common/*.cpp)
TTSEngine_includes = $(wildcard ./ttsengine/*.h ./ttsengine/*.cpp)
TTSClient_includes = $(wildcard ./ttsclient/*.h ./ttsclient/*.cpp)
TTSTestClient_includes = $(wildcard ./test/*.h ./test/*.cpp)
TTSMultiClientTest_includes = $(wildcard ./test/*.h ./test/*.cpp)
TTSAPITest_includes = $(wildcard ./test/*.h ./test/*.cpp)

# Build Common
Common_OBJDIR=common/obj
$(Common_OBJDIR)/%.o : common/%.cpp $(Common_includes)
	@[ -d $(Common_OBJDIR) ] || mkdir -p $(Common_OBJDIR)
	$(CXX) -c $(COMMON_CXXFLAGS) $< -o $@
Common_SRCS=\
           rt_msg_dispatcher.cpp \
           glib_utils.cpp \
           logger.cpp
Common_OBJS=$(patsubst %.cpp, $(Common_OBJDIR)/%.o, $(notdir $(Common_SRCS)))
Common_OBJS: $(Common_SRCS)
Common_OBJS_ALL=$(Common_OBJS)

# Build TTSEngine
TTSEngine_OBJDIR=ttsengine/obj
$(TTSEngine_OBJDIR)/%.o : ttsengine/%.cpp $(TTSEngine_includes) $(Common_includes)
	@[ -d $(TTSEngine_OBJDIR) ] || mkdir -p $(TTSEngine_OBJDIR)
	$(CXX) -c $(COMMON_CXXFLAGS) $< -o $@

TTSEngine_SRCS=\
           TTSEngine.cpp \
           TTSManager.cpp \
           TTSSession.cpp \
           TTSEventSource.cpp \
           TTSSpeaker.cpp
TTSEngine_OBJS=$(patsubst %.cpp, $(TTSEngine_OBJDIR)/%.o, $(notdir $(TTSEngine_SRCS)))
TTSEngine_OBJS: $(TTSEngine_SRCS)
TTSEngine_OBJS_ALL=$(TTSEngine_OBJS)
TTSEngine: $(TTSEngine_OBJS_ALL) $(Common_OBJS_ALL)
	$(CXX) $(TTSEngine_OBJS_ALL) $(Common_OBJS_ALL) $(TTSENGINE_LDFLAGS) -o TTSEngine

# Build TTSClient
TTSClient_OBJDIR=ttsclient/obj
$(TTSClient_OBJDIR)/%.o : ttsclient/%.cpp $(TTSClient_includes) $(Common_includes)
	@[ -d $(TTSClient_OBJDIR) ] || mkdir -p $(TTSClient_OBJDIR)
	$(CXX) -c $(COMMON_CXXFLAGS) $< -o $@

TTSClient_SRCS= TTSClient.cpp TTSClientPrivate.cpp
TTSClient_OBJS=$(patsubst %.cpp, $(TTSClient_OBJDIR)/%.o, $(notdir $(TTSClient_SRCS)))
TTSClient_OBJS: $(TTSClient_SRCS)
TTSClient_OBJS_ALL=$(TTSClient_OBJS)
libTTSClient.so: $(TTSClient_OBJS_ALL) $(Common_OBJS_ALL)
	$(CXX) $(TTSClient_OBJS_ALL) $(Common_OBJS_ALL) $(TTSCLIENT_LDFLAGS) -shared -fPIC -o libTTSClient.so

# Build TTSTestClient
SEARCH+= -I./ttsclient/
TTSTestClient_OBJDIR=test/obj
$(TTSTestClient_OBJDIR)/%.o : test/%.cpp $(TTSTestClient_includes) $(Common_includes)
	@[ -d $(TTSTestClient_OBJDIR) ] || mkdir -p $(TTSTestClient_OBJDIR)
	$(CXX) -c $(COMMON_CXXFLAGS) $< -o $@

TTSTestClient_SRCS= TTSTestClient.cpp libTTSClient.so
TTSTestClient_OBJS=$(patsubst %.cpp, $(TTSTestClient_OBJDIR)/%.o, $(notdir $(TTSTestClient_SRCS)))
TTSTestClient_OBJS: $(TTSTestClient_SRCS)
TTSTestClient_OBJS_ALL=$(TTSTestClient_OBJS)
TTSTestClient: $(TTSTestClient_OBJS_ALL) $(Common_OBJS_ALL)
	$(CXX) $(TTSTestClient_OBJS_ALL) $(Common_OBJS_ALL) $(TTSTEST_LDFLAGS) -L. -lTTSClient -o TTSTestClient

# Build TTSMultiClientTest
SEARCH+= -I./ttsclient/
TTSMultiClientTest_OBJDIR=test/obj
$(TTSMultiClientTest_OBJDIR)/%.o : test/%.cpp $(TTSMultiClientTest_includes) $(Common_includes)
	@[ -d $(TTSMultiClientTest_OBJDIR) ] || mkdir -p $(TTSMultiClientTest_OBJDIR)
	$(CXX) -c $(COMMON_CXXFLAGS) $< -o $@

TTSMultiClientTest_SRCS= TTSMultiClientTest.cpp libTTSClient.so
TTSMultiClientTest_OBJS=$(patsubst %.cpp, $(TTSMultiClientTest_OBJDIR)/%.o, $(notdir $(TTSMultiClientTest_SRCS)))
TTSMultiClientTest_OBJS: $(TTSMultiClientTest_SRCS)
TTSMultiClientTest_OBJS_ALL=$(TTSMultiClientTest_OBJS)
TTSMultiClientTest: $(TTSMultiClientTest_OBJS_ALL) $(Common_OBJS_ALL)
	$(CXX) $(TTSMultiClientTest_OBJS_ALL) $(Common_OBJS_ALL) $(TTSTEST_LDFLAGS) -L. -lTTSClient -o TTSMultiClientTest

# Build TTSAPITest
SEARCH+= -I./ttsclient/
TTSAPITest_OBJDIR=test/obj
$(TTSAPITest_OBJDIR)/%.o : test/%.cpp $(TTSAPITest_includes) $(Common_includes)
	@[ -d $(TTSAPITest_OBJDIR) ] || mkdir -p $(TTSAPITest_OBJDIR)
	$(CXX) -c $(COMMON_CXXFLAGS) $< -o $@

TTSAPITest_SRCS= TTSAPITest.cpp libTTSClient.so
TTSAPITest_OBJS=$(patsubst %.cpp, $(TTSAPITest_OBJDIR)/%.o, $(notdir $(TTSAPITest_SRCS)))
TTSAPITest_OBJS: $(TTSAPITest_SRCS)
TTSAPITest_OBJS_ALL=$(TTSAPITest_OBJS)
TTSAPITest: $(TTSAPITest_OBJS_ALL) $(Common_OBJS_ALL)
	$(CXX) $(TTSAPITest_OBJS_ALL) $(Common_OBJS_ALL) $(TTSTEST_LDFLAGS) -L. -lTTSClient -o TTSAPITest

install:
	@mkdir -p ${INSTALL_PATH}/lib/rdk/
	@cp -f ttsengine/launch_ttsengine.sh ${INSTALL_PATH}/lib/rdk/
	@chmod +x ${INSTALL_PATH}/lib/rdk/launch_ttsengine.sh
	
	@mkdir -p ${INSTALL_PATH}/usr/bin/
	@cp -f TTSEngine TTSMultiClientTest TTSAPITest ${INSTALL_PATH}/usr/bin/
	
	@mkdir -p ${INSTALL_PATH}/usr/lib/
	@cp -f libTTSClient.so ${INSTALL_PATH}/usr/lib/
	-ln -s libTTSClient.so ${INSTALL_PATH}/usr/lib/libTTSClient.so.0
	-ln -s libTTSClient.so ${INSTALL_PATH}/usr/lib/libTTSClient.so.0.0
	
	@mkdir -p ${INSTALL_PATH}/usr/include/
	@cp -f ttsclient/TTSClient.h ${INSTALL_PATH}/usr/include/
	@cp -f common/TTSCommon.h ${INSTALL_PATH}/usr/include/

clean:
	@rm -rf $(Common_OBJDIR)/*.o $(TTSEngine_OBJDIR)/*.o $(TTSClient_OBJDIR)/*.o
	@rm -rf $(TTSTestClient_OBJDIR)/*.o $(TTSMultiClientTest_OBJDIR)/*.o $(TTSAPITest_OBJDIR)/*.o
	@rm -rf TTSEngine libTTSClient.so TTSTestClient TTSMultiClientTest TTSAPITest


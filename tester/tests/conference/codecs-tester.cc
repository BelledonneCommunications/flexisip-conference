/*
    Flexisip, a flexible SIP proxy server with media capabilities.
    Copyright (C) 2010-2026 Belledonne Communications SARL, All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>

#include "bc-utils.hh"
#include "call-assert.hh"
#include "call-builder.hh"
#include "client-builder.hh"
#include "client-call.hh"
#include "client-core.hh"
#include "core-assert.hh"
#include "exceptions/bad-configuration.hh"
#include "test-patterns/test.hh"
#include "test-suite.hh"
#include "utils/conference-builder.hh"
#include "utils/server/test-conference-server.hh"

using namespace std;
using namespace flexisip;
using namespace flexisip::tester;
using namespace linphone;

namespace {

void startConferenceServerWithConfig(std::map<std::string, std::string>&& customConfig) {
	const string confFactoryUri = "sip:conference-factory@sip.example.org";
	const string confFocusUri = "sip:conference-focus@sip.example.org";

	customConfig.emplace("global/transports", "sip:127.0.0.1:0;transport=tcp");
	customConfig.emplace("conference-server/database-backend", "sqlite");
	customConfig.emplace("conference-server/database-connection-string", "/dev/null");
	customConfig.emplace("conference-server/conference-factory-uris", confFactoryUri);
	customConfig.emplace("conference-server/conference-focus-uris", confFocusUri);
	customConfig.emplace("conference-server/state-directory", bcTesterWriteDir().append("var/lib/flexisip"));

	Server proxy{customConfig};
	proxy.start();

	TestConferenceServer conferenceServer{proxy};
	conferenceServer.start();
}

/**
 * Test that the conference server does not start when a supported codec is misconfigured.
 */
void wrongSupportedCodecsConfigured() {
	// Misconfigured audio
	try {
		// vp8 should not be in supported-audio-codecs.
		startConferenceServerWithConfig({
		    {"conference-server/supported-audio-codecs", "opus vp8"},
		    {"conference-server/supported-video-codec", "vp8"},
		});

		BC_HARD_FAIL("Conference server should have thrown an exception.");
	} catch (const BadConfigurationValue& e) {
		BC_HARD_ASSERT_TRUE(string{e.what()}.find("unsupported codecs in the list: vp8") != string::npos);
	}

	try {
		// When sfu is enabled, only the first codec is used, so this should pass.
		startConferenceServerWithConfig({
		    {"conference-server/audio-engine-mode", "sfu"},
		    {"conference-server/supported-audio-codecs", "opus vp8"},
		    {"conference-server/supported-video-codec", "vp8"},
		});
	} catch (const BadConfigurationValue& e) {
		BC_HARD_FAIL("Conference server should not have thrown an exception.");
	}

	try {
		// Enable sfu and add a wrong codec in first place.
		startConferenceServerWithConfig({
		    {"conference-server/audio-engine-mode", "sfu"},
		    {"conference-server/supported-audio-codecs", "vp8 opus"},
		    {"conference-server/supported-video-codec", "vp8"},
		});

		BC_HARD_FAIL("Conference server should have thrown an exception.");
	} catch (const BadConfigurationValue& e) {
		BC_HARD_ASSERT_TRUE(string{e.what()}.find("unsupported codecs in the list: vp8") != string::npos);
	}

	// Misconfigured video
	try {
		startConferenceServerWithConfig({
		    {"conference-server/supported-audio-codecs", "opus speex pcmu pcma"},
		    {"conference-server/supported-video-codec", "AV1"},
		});

		BC_HARD_FAIL("Conference server should have thrown an exception.");
	} catch (const BadConfigurationValue& e) {
		BC_HARD_ASSERT_TRUE(string{e.what()}.find("only VP8 is supported as of today for video conferences") !=
		                    string::npos);
	}
}

void clientJoinConferenceCodecs(bool disableOpus,
                                linphone::Call::State expectedCallState,
                                const CallAssertionInfo::MediaStateList& expectedMediaState) {
	const string confFactoryUri = "sip:conference-factory@sip.example.org";
	const string confFocusUri = "sip:conference-focus@sip.example.org";

	Server proxy{{
	    {"global/transports", "sip:127.0.0.1:0;transport=tcp"},
	    {"conference-server/supported-audio-codecs", "opus"},
	    {"conference-server/database-backend", "sqlite"},
	    {"conference-server/database-connection-string", "/dev/null"},
	    {"conference-server/conference-factory-uris", confFactoryUri},
	    {"conference-server/conference-focus-uris", confFocusUri},
	    {"conference-server/state-directory", bcTesterWriteDir().append("var/lib/flexisip")},
	}};
	proxy.start();

	TestConferenceServer conferenceServer{proxy};
	conferenceServer.start();

	const auto confFactoryAddr = Factory::get()->createAddress(confFactoryUri);

	ClientBuilder clientBuilder{proxy.getAgent()};
	clientBuilder.setConferenceFactoryAddress(confFactoryAddr);

	CoreClient coreClient = clientBuilder.build("sip:toto@sip.example.org", "");

	// Disable opus
	if (disableOpus) {
		auto audioPayloads = coreClient.getCore()->getAudioPayloadTypes();
		for (const auto& payload : audioPayloads) {
			if (payload->getMimeType() == "opus") payload->enable(false);
		}
	}

	CoreAssert asserter{proxy, coreClient};

	ConferenceBuilder conferenceBuilder{coreClient};
	const auto confUri = conferenceBuilder.setCapability(StreamType::Audio, true)
	                         .setCapability(StreamType::Video, false)
	                         .setCapability(StreamType::Text, false)
	                         .setSubject("codecs tests")
	                         .build(asserter);

	BC_HARD_ASSERT_NOT_NULL(confUri);

	auto call = coreClient.callBuilder().setVideo(OnOff::Off).call(confUri->asString());
	BC_HARD_ASSERT(call.has_value());

	CallAssert{asserter}.waitUntil({{*call, expectedCallState, expectedMediaState}}).hard_assert_passed();
}

/**
 * Test that a client with incompatible codecs cannot join a conference.
 */
void clientJoinConferenceWithWrongCodecs() {
	const CallAssertionInfo::MediaStateList noAudio{
	    {linphone::StreamType::Audio, CallAssertionInfo::MediaState::NotSent},
	    {linphone::StreamType::Audio, CallAssertionInfo::MediaState::NotReceived}};

	clientJoinConferenceCodecs(true, linphone::Call::State::Error, noAudio);
}

/**
 * Test that a client with compatible codecs can join a conference.
 */
void clientJoinConferenceWithGoodCodecs() {
	const CallAssertionInfo::MediaStateList onlyAudioSent{
	    {linphone::StreamType::Audio, CallAssertionInfo::MediaState::Sent}};

	clientJoinConferenceCodecs(false, linphone::Call::State::StreamsRunning, onlyAudioSent);
}

TestSuite _("conference::codecs",
            {
                CLASSY_TEST(wrongSupportedCodecsConfigured),
                CLASSY_TEST(clientJoinConferenceWithWrongCodecs),
                CLASSY_TEST(clientJoinConferenceWithGoodCodecs),
            });
} // namespace
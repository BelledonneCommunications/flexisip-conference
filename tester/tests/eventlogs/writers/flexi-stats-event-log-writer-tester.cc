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

#include "eventlogs/writers/flexi-stats-event-log-writer.hh"

#include <atomic>
#include <memory>
#include <regex>
#include <unordered_map>

#include "bctoolbox/tester.h"
#include "linphone++/enums.hh"

#include "asserts.hh"
#include "bc-utils.hh"
#include "chat-room-builder.hh"
#include "client-builder.hh"
#include "core-assert.hh"
#include "eventlogs/event-logs.hh"
#include "flexiapi/schemas/iso-8601-date.hh"
#include "flexisip/configmanager.hh"
#include "flexisip/module-router.hh"
#include "http-mock/http-mock.hh"
#include "server/mysql/mysql-server.hh"
#include "test-patterns/test.hh"
#include "test-suite.hh"
#include "utils/server/test-conference-server.hh"

using namespace std;
using namespace nlohmann;

namespace flexisip::tester::eventlogs {
using namespace flexisip::tester::http_mock;

optional<MysqlServer> sDbServer{nullopt};

void callToConference() {
	const auto proxy = makeAndStartProxy();
	const auto& agent = proxy->getAgent();
	std::atomic_int eventLogRequestsReceivedCount{0};
	HttpMock flexiapiServer{{"/"}, &eventLogRequestsReceivedCount};
	int port = flexiapiServer.serveAsync();
	BC_HARD_ASSERT_TRUE(port > -1);
	agent->setEventLogWriter(std::make_unique<FlexiStatsEventLogWriter>(*agent->getRoot(), "127.0.0.1", to_string(port),
	                                                                    "", "aRandomApiToken"));
	const ClientBuilder builder{proxy->getAgent()};
	const auto johan = builder.build("sip:johan@sip.example.org");
	const string expectedConferenceId = "expected-conf-id";
	const auto chatroom = "sip:chatroom-" + expectedConferenceId + "@sip.example.org";
	const auto fakeConfServer = builder.build(chatroom);
	CoreAssert asserter{johan, fakeConfServer, agent};

	johan.invite(chatroom);
	BC_HARD_ASSERT_TRUE(
	    asserter.iterateUpTo(4, [&eventLogRequestsReceivedCount] { return 0 < eventLogRequestsReceivedCount; }, 1s));

	const auto startedEvent = flexiapiServer.popRequestReceived();
	json actualJson;
	try {
		actualJson = json::parse(startedEvent->body);
	} catch (const exception&) {
		BC_FAIL("json::parse exception with received body");
	}
	BC_ASSERT_CPP_EQUAL(actualJson.at("conference_id"), expectedConferenceId);
}

void messageToChatroomClearText() {
	const string confFactoryUri = "sip:conference-factory@sip.example.org";
	const string confFocusUri = "sip:conference-focus@sip.example.org";
	const auto proxy = makeAndStartProxy({
	    {"conference-server/conference-factory-uris", confFactoryUri},
	    {"conference-server/conference-focus-uris", confFocusUri},
	    // `mysql` to be as close to real-world deployments as possible
	    {"conference-server/database-backend", "mysql"},
	    {"conference-server/database-connection-string", sDbServer->connectionString()},
	    {"conference-server/state-directory", bcTesterWriteDir().append("var/lib/flexisip")},
	});
	const auto& agent = proxy->getAgent();
	ClientBuilder builder{agent};
	builder.setConferenceFactoryAddress(linphone::Factory::get()->createAddress(confFactoryUri));
	builder.setLimeX3DH(OnOff::Off);
	const string expectedFrom = "clemence@sip.example.org";
	const string expectedTos[] = {"pauline@sip.example.org", "tony@sip.example.org", "mike@sip.example.org"};
	constexpr int recipientCount = sizeof(expectedTos) / sizeof(expectedTos[0]);
	const auto clemence = builder.build(expectedFrom);
	const auto pauline = builder.build(expectedTos[0]);
	const auto tony = builder.build(expectedTos[1]);
	const auto mike = builder.build(expectedTos[2]);
	CoreAssert asserter{clemence, pauline, tony, mike, agent};
	const auto clemChat = clemence.chatroomBuilder()
	                          .setBackend(linphone::ChatRoom::Backend::FlexisipChat)
	                          .setSubject("GYM")
	                          .build({pauline.getMe(), tony.getMe(), mike.getMe()});
	BC_HARD_ASSERT_TRUE(clemChat != nullptr);
	std::atomic_int eventLogRequestsReceivedCount{0};
	HttpMock flexiapiServer{{"/"}, &eventLogRequestsReceivedCount};
	int port = flexiapiServer.serveAsync();
	agent->setEventLogWriter(
	    std::make_unique<FlexiStatsEventLogWriter>(*agent->getRoot(), "127.0.0.1", to_string(port), "/", "toktok"));
	const TestConferenceServer confServer(*proxy);
	const auto before = chrono::system_clock::now();

	clemChat->createMessageFromUtf8("💃🏼")->send();
	asserter
	    .iterateUpTo(0x10,
	                 [&eventLogRequestsReceivedCount]() {
		                 FAIL_IF(eventLogRequestsReceivedCount < 1 /* sent */ + recipientCount);
		                 return ASSERTION_PASSED();
	                 })
	    .assert_passed();

	const auto sentEvent = flexiapiServer.popRequestReceived();
	BC_HARD_ASSERT(sentEvent != nullptr);
	BC_ASSERT_CPP_EQUAL(sentEvent->method, "POST");
	BC_ASSERT_CPP_EQUAL(sentEvent->path, "/messages");
	json actualJson;
	try {
		actualJson = json::parse(sentEvent->body);
	} catch (const exception&) {
		BC_FAIL("json::parse exception with received body");
	}
	const auto logId = actualJson["id"].get<string>();
	actualJson.erase("id");
	const auto sentAt = actualJson["sent_at"].get<flexiapi::ISO8601Date>();
	actualJson.erase("sent_at");
	json expectedJson{
	    {"from", expectedFrom},
	    {"to", unordered_map<string, nullptr_t>{}},
	    {"conference_id", clemChat->getConferenceAddress()->getUriParam("conf-id")},
	    {"encrypted", false},
	};
	BC_ASSERT_CPP_EQUAL(actualJson, expectedJson);
	BC_ASSERT_TRUE(before <= sentAt);
	unordered_map<string, shared_ptr<Request>> deliveredEvents{};
	const regex extractEventIdAndDeviceFromPath{R"regex(/messages/(.+)/to/(.+)/devices/.+)regex"};
	for (auto _ = 0; _ < recipientCount; ++_) {
		auto event = flexiapiServer.popRequestReceived();
		BC_ASSERT(event != nullptr);
		std::smatch match{};
		BC_ASSERT_TRUE(std::regex_match(event->path, match, extractEventIdAndDeviceFromPath));
		BC_HARD_ASSERT_CPP_EQUAL(match.size(), 3);
		BC_ASSERT_CPP_EQUAL(match[1], logId);
		deliveredEvents.emplace(match[2], std::move(event));
	}
	for (const auto& expectedTo : expectedTos) {
		try {
			const auto& deliveredEvent = deliveredEvents.at(expectedTo);
			BC_ASSERT_CPP_EQUAL(deliveredEvent->method, "PATCH");
			try {
				actualJson = json::parse(deliveredEvent->body);
			} catch (const exception&) {
				BC_FAIL("json::parse exception with received body");
			}
			const auto receivedAt = actualJson["received_at"].get<flexiapi::ISO8601Date>();
			actualJson.erase("received_at");
			expectedJson = R"(
			{
			  "last_status": 200
			})"_json;
			BC_ASSERT_CPP_EQUAL(actualJson, expectedJson);
			BC_ASSERT_TRUE(sentAt <= receivedAt);
		} catch (const std::out_of_range&) {
			SLOGD << "Unable to find key " << expectedTo << " in delivered event map";
			BC_FAIL("Unable to find key in delivered event map");
		}
	}
}

namespace {

TestSuite _{
    "FlexiStatsEventLogWriter",
    {
        CLASSY_TEST(callToConference),
        CLASSY_TEST(messageToChatroomClearText),
    },
    Hooks{}
        .beforeSuite([] {
	        sDbServer.emplace();
	        sDbServer->waitReady();
	        return 0;
        })
        .beforeEach([] { sDbServer->clear(); })
        .afterSuite([] {
	        sDbServer.reset();
	        return 0;
        }),
};

} // namespace
} // namespace flexisip::tester::eventlogs
/*
    Flexisip, a flexible SIP proxy server with media capabilities.
    Copyright (C) 2010-2026 Belledonne Communications SARL, All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "registration-events/client.hh"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "linphone++/linphone.hh"

#include "asserts.hh"
#include "client-builder.hh"
#include "contact-inserter.hh"
#include "core-assert.hh"
#include "flexisip/configmanager.hh"
#include "flexisip/sofia-wrapper/su-root.hh"
#include "flexisip/utils/sip-uri.hh"
#include "registrar/record.hh"
#include "registrar/registrar-db.hh"
#include "server/regevent-server.hh"
#include "test-patterns/test.hh"
#include "test-suite.hh"
#include "utils/uri-utils.hh"

using namespace std;
using namespace std::chrono_literals;
using namespace flexisip;
using namespace flexisip::tester;
using namespace linphone;

namespace flexisip::tester {
namespace {

const string participantAor{"sip:participant@127.0.0.1"};
const vector<string_view> participantContactParams{"+org.linphone.specs=\"conference/2.0\""};

class RecordingListener : public registration_event::ClientListener {
public:
	virtual ~RecordingListener() = default;

	void onNotifyReceived(const list<shared_ptr<ParticipantDeviceIdentity>>& participantDevices) override {
		mNotifications.push_back(participantDevices);
	}

	void onRefreshed(const shared_ptr<ParticipantDeviceIdentity>& participantDevice) override {
		mRefreshed.push_back(participantDevice);
	}

	vector<list<shared_ptr<ParticipantDeviceIdentity>>> mNotifications{};
	vector<shared_ptr<ParticipantDeviceIdentity>> mRefreshed{};
};

class CoreEventRecorder : public CoreListener {
public:
	void onSubscriptionStateChanged(const shared_ptr<Core>&,
	                                const shared_ptr<Event>& event,
	                                SubscriptionState state) override {
		mStates.emplace_back(event, state);
	}

	void onNotifyReceived(const shared_ptr<Core>&,
	                      const shared_ptr<Event>&,
	                      const string&,
	                      const shared_ptr<const Content>& body) override {
		mNotifyBodies.push_back(body);
	}

	vector<pair<shared_ptr<Event>, SubscriptionState>> mStates{};
	vector<shared_ptr<const Content>> mNotifyBodies{};
};

struct ClientFixture {
	ClientFixture()
	    : mRoot(make_shared<sofiasip::SuRoot>()),
	      mRegistrarDb(make_shared<RegistrarDb>(mRoot, make_shared<ConfigManager>())),
	      mRegEventServer(make_unique<RegEventServer>(mRegistrarDb)),
	      mClientCore(
	          ClientBuilder(mRegEventServer->getTransport().str()).setRegistration(OnOff::Off).make(participantAor)),
	      mCore(mClientCore->getCore()), mCoreEvents(make_shared<CoreEventRecorder>()),
	      mFactory(make_shared<registration_event::ClientFactory>(mCore, 30s)),
	      mTarget(Factory::get()->createAddress(participantAor)), mClient(mFactory->create(mTarget)) {
		mCore->addListener(mCoreEvents);
	}

	~ClientFixture() {
		mCore->removeListener(mCoreEvents);
	}

	void insertContact(const string& contact,
	                   const string& uniqueId,
	                   const string& aor = participantAor,
	                   const vector<string_view>& contactParams = {}) {
		ContactInserter inserter{*mRegistrarDb, make_shared<AcceptUpdatesListener>()};
		inserter.withGruu(true)
		    .setExpire(60s)
		    .setAor(aor)
		    .setContactParams(vector<string_view>(contactParams))
		    .insert({.contact = contact, .uniqueId = uniqueId});

		CoreAssert{*mRoot}.wait([&inserter] { return LOOP_ASSERTION(inserter.finished()); }).hard_assert_passed();
	}

	void publish(const string& aor = participantAor, const string& uniqueId = {}) {
		mRegistrarDb->publish(Record::Key{SipUri{aor}, mRegistrarDb->useGlobalDomain()}, uniqueId);
	}

	void waitFor(const function<bool()>& predicate) {
		CoreAssert{mRegEventServer->getCore(), mCore, *mRoot}
		    .wait([&predicate] { return LOOP_ASSERTION(predicate()); })
		    .hard_assert_passed();
	}

	void waitThenAssert(const function<bool()>& predicate) {
		CoreAssert{mRegEventServer->getCore(), mCore, *mRoot}
		    .forceIterateThenAssert(64, 0ms, [&predicate] { return LOOP_ASSERTION(predicate()); })
		    .hard_assert_passed();
	}

	shared_ptr<sofiasip::SuRoot> mRoot{};
	shared_ptr<RegistrarDb> mRegistrarDb{};
	unique_ptr<RegEventServer> mRegEventServer{};
	shared_ptr<CoreClient> mClientCore{};
	shared_ptr<linphone::Core> mCore{};
	shared_ptr<CoreEventRecorder> mCoreEvents{};
	shared_ptr<registration_event::ClientFactory> mFactory{};
	shared_ptr<linphone::Address> mTarget{};
	shared_ptr<registration_event::Client> mClient{};
};

/**
 * Verify repeated subscribe and unsubscribe operations, including state replay.
 */
void clientCanSubscribeAndUnsubscribeRepeatedly() {
	ClientFixture fixture{};
	fixture.insertContact("sip:device@127.0.0.1", "device", participantAor, participantContactParams);

	auto listener = make_shared<RecordingListener>();
	fixture.mClient->addObserver(listener);
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return !fixture.mCoreEvents->mStates.empty() && !listener->mNotifications.empty(); });

	const auto firstEvent = fixture.mCoreEvents->mStates.back().first;
	BC_ASSERT_CPP_EQUAL(firstEvent->getName(), "reg");
	BC_ASSERT_CPP_EQUAL(firstEvent->getToAddress()->asStringUriOnly(), fixture.mTarget->asStringUriOnly());

	auto replayListener = make_shared<RecordingListener>();
	fixture.mClient->subscribe(replayListener);
	fixture.waitFor([&] { return replayListener->mNotifications.size() == 1; });
	BC_ASSERT_CPP_EQUAL(replayListener->mNotifications.back().size(), 1);

	fixture.mClient->unsubscribe();
	fixture.waitFor([&] { return fixture.mCoreEvents->mStates.back().second == SubscriptionState::Terminated; });

	const auto stateCount = fixture.mCoreEvents->mStates.size();
	fixture.mClient->unsubscribe();
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return fixture.mCoreEvents->mStates.size() > stateCount; });
}

/**
 * Verify registration NOTIFY contacts become participant device identities.
 */
void notifyParsingBuildsParticipantIdentities() {
	ClientFixture fixture{};
	fixture.insertContact("sip:device@127.0.0.1", "device", participantAor, participantContactParams);
	fixture.insertContact("sip:ignored@127.0.0.1", "ignored", participantAor);

	auto listener = make_shared<RecordingListener>();
	fixture.mClient->addObserver(listener);
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return !listener->mNotifications.empty(); });

	BC_ASSERT_CPP_EQUAL(listener->mNotifications.back().size(), 1);
	const auto& identity = listener->mNotifications.back().front();
	BC_ASSERT_CPP_EQUAL(identity->getAddress()->asStringUriOnly(), "sip:participant@127.0.0.1;gr=device");
	const auto capabilities = identity->getCapabilityDescriptorList();
	BC_ASSERT_CPP_EQUAL(capabilities.size(), 1);
	if (capabilities.size() == 1) BC_ASSERT_CPP_EQUAL(capabilities.front(), "conference/2.0");
}

/**
 * Verify notifications for unrelated AORs are ignored.
 */
void notifyIgnoresOtherAors() {
	ClientFixture fixture{};
	fixture.insertContact("sip:device@127.0.0.1", "device", participantAor, participantContactParams);

	auto listener = make_shared<RecordingListener>();
	fixture.mClient->addObserver(listener);
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return !listener->mNotifications.empty(); });
	const auto notificationCount = listener->mNotifications.size();

	const string otherAor{"sip:other@127.0.0.1"};
	fixture.insertContact("sip:other-device@127.0.0.1", "other-device", otherAor, participantContactParams);
	fixture.publish(otherAor);
	fixture.waitThenAssert([&] { return listener->mNotifications.size() == notificationCount; });
}

/**
 * Verify a newly registered contact emits a participant snapshot.
 */
void newContactsEmitSnapshots() {
	ClientFixture fixture{};
	fixture.insertContact("sip:device@127.0.0.1", "device", participantAor, participantContactParams);

	auto listener = make_shared<RecordingListener>();
	fixture.mClient->addObserver(listener);
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return !listener->mNotifications.empty(); });
	const auto notificationCount = listener->mNotifications.size();
	BC_ASSERT_CPP_EQUAL(listener->mNotifications.back().size(), 1);

	fixture.insertContact("sip:new@127.0.0.1", "new", participantAor, participantContactParams);
	fixture.publish();
	fixture.waitFor([&] { return listener->mNotifications.size() > notificationCount; });
	BC_ASSERT_CPP_EQUAL(listener->mNotifications.back().size(), 2);
}

/**
 * Verify multiple registered contacts are emitted in one snapshot.
 */
void multipleContactsEmitSnapshot() {
	ClientFixture fixture{};
	fixture.insertContact("sip:registered_0@127.0.0.1", "registered_0", participantAor, participantContactParams);

	auto listener = make_shared<RecordingListener>();
	fixture.mClient->addObserver(listener);
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return !listener->mNotifications.empty(); });
	const auto notificationCount = listener->mNotifications.size();
	BC_ASSERT_CPP_EQUAL(listener->mNotifications.back().size(), 1);

	fixture.insertContact("sip:registered_1@127.0.0.1", "registered_1", participantAor, participantContactParams);
	fixture.insertContact("sip:registered_2@127.0.0.1", "registered_2", participantAor, participantContactParams);
	fixture.publish();
	fixture.waitFor([&] { return listener->mNotifications.size() > notificationCount; });

	BC_ASSERT_CPP_EQUAL(listener->mNotifications.back().size(), 3);
}

/**
 * Verify a refreshed contact emits an onRefreshed callback without a snapshot.
 */
void refreshedContactsNotifyListener() {
	ClientFixture fixture{};
	fixture.insertContact("sip:device@127.0.0.1", "device", participantAor, participantContactParams);

	auto listener = make_shared<RecordingListener>();
	fixture.mClient->addObserver(listener);
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return !listener->mNotifications.empty(); });
	const auto notificationCount = listener->mNotifications.size();

	fixture.publish(participantAor, uri_utils::grToUniqueId("device"));
	fixture.waitFor([&] { return listener->mRefreshed.size() == 1; });

	BC_ASSERT_CPP_EQUAL(listener->mNotifications.size(), notificationCount);
	BC_ASSERT_CPP_EQUAL(listener->mRefreshed.front()->getAddress()->asStringUriOnly(),
	                    "sip:participant@127.0.0.1;gr=device");
	const auto capabilities = listener->mRefreshed.front()->getCapabilityDescriptorList();
	BC_ASSERT_CPP_EQUAL(capabilities.size(), 1);
	if (capabilities.size() == 1) BC_ASSERT_CPP_EQUAL(capabilities.front(), "conference/2.0");
}

/**
 * Verify unsubscribe stops callbacks and allows a later resubscription.
 */
void unsubscribeStopsCallbacksAndAllowsResubscription() {
	ClientFixture fixture{};
	fixture.insertContact("sip:device@127.0.0.1", "device", participantAor, participantContactParams);

	auto listener = make_shared<RecordingListener>();
	fixture.mClient->addObserver(listener);
	fixture.mClient->subscribe(listener);
	fixture.waitFor([&] { return !listener->mNotifications.empty(); });
	const auto notificationCount = listener->mNotifications.size();
	const auto stateCount = fixture.mCoreEvents->mStates.size();
	fixture.mClient->unsubscribe();
	fixture.waitThenAssert([&] { return listener->mNotifications.size() == notificationCount; });

	auto replayListener = make_shared<RecordingListener>();
	fixture.mClient->subscribe(replayListener);
	fixture.waitFor([&] { return fixture.mCoreEvents->mStates.size() > stateCount; });
}

} // namespace

TestSuite _{
    "registration_event::Client",
    {
        CLASSY_TEST(clientCanSubscribeAndUnsubscribeRepeatedly),
        CLASSY_TEST(notifyParsingBuildsParticipantIdentities),
        CLASSY_TEST(notifyIgnoresOtherAors),
        CLASSY_TEST(newContactsEmitSnapshots),
        CLASSY_TEST(multipleContactsEmitSnapshot),
        CLASSY_TEST(refreshedContactsNotifyListener),
        CLASSY_TEST(unsubscribeStopsCallbacksAndAllowsResubscription),
    },
};

} // namespace flexisip::tester
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

#include "conference/conference-server.hh"

#include <chrono>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

#include "asserts.hh"
#include "bc-utils.hh"
#include "chat-room-builder.hh"
#include "client-builder.hh"
#include "client-core.hh"
#include "core-assert.hh"
#include "flexisip/registrar/registar-listeners.hh"
#include "modules/module-forward.hh"
#include "registrar/binding-parameters.hh"
#include "registrar/extended-contact.hh"
#include "registrar/record.hh"
#include "registrar/registrar-db.hh"
#include "registrardb-internal.hh"
#include "registrardb-redis.hh"
#include "server/mysql/mysql-server.hh"
#include "server/proxy-server.hh"
#include "server/redis-server.hh"
#include "server/regevent-server.hh"
#include "test-patterns/test.hh"
#include "test-suite.hh"
#include "utils/server/test-conference-server.hh"
#include "utils/uri-utils.hh"

using namespace std;
using namespace std::chrono_literals;
using namespace std::chrono;
using namespace flexisip;
using namespace flexisip::tester;

namespace {

optional<MysqlServer> sDbServer{nullopt};

class AllJoinedWaiter : public linphone::ChatRoomListener, public std::enable_shared_from_this<AllJoinedWaiter> {
public:
	void onConferenceJoined(const std::shared_ptr<linphone::ChatRoom>& chatRoom,
	                        const std::shared_ptr<const linphone::EventLog>&) override {
		for (auto it = mChatrooms.begin(); it != mChatrooms.end(); ++it) {
			if (*it == chatRoom->cPtr()) {
				mChatrooms.erase(it);
				break;
			}
		}
	}

	void setChatrooms(std::initializer_list<shared_ptr<linphone::ChatRoom>>&& chatrooms) {
		mChatrooms.reserve(chatrooms.size());
		auto self = shared_from_this();
		for (const auto& chatroom : chatrooms) {
			chatroom->addListener(self);
			mChatrooms.emplace_back(chatroom->cPtr());
		}
	}

	const auto& getChatrooms() {
		return mChatrooms;
	}

private:
	std::vector<const void*> mChatrooms{};
};

/**
 * Test that the conference-server correctly binds the chat rooms from the chat rooms DB into the registrar DB
 * during its initialization.
 */
void conferenceServerBindsChatroomsFromDBOnInit() {
	const string confFactoryUri = "sip:conference-factory@sip.example.org";
	const string confFocusUri = "sip:conference-focus@sip.example.org";
	Server proxy{{// Requesting bind on port 0 to let the kernel find any available port
	              {"global/transports", "sip:127.0.0.1:0;transport=tcp"},
	              {"module::Registrar/enabled", "true"},
	              {"module::Registrar/reg-domains", "sip.example.org"},

	              // `mysql` to be as close to real-world deployments as possible
	              {"conference-server/database-backend", "mysql"},
	              {"conference-server/database-connection-string", sDbServer->connectionString()},
	              {"conference-server/conference-factory-uris", confFactoryUri},
	              {"conference-server/conference-focus-uris", confFocusUri},
	              {"conference-server/empty-chat-room-deletion", "false"},
	              {"conference-server/state-directory", bcTesterWriteDir().append("var/lib/flexisip")}}};
	proxy.start();
	const auto& regDb = proxy.getRegistrarDb();
	const auto* registrarBackend = dynamic_cast<const RegistrarDbInternal*>(&regDb->getRegistrarBackend());
	BC_HARD_ASSERT_TRUE(registrarBackend != nullptr);
	const auto& records = registrarBackend->getAllRecords();
	BC_HARD_ASSERT_CPP_EQUAL(records.size(), 0);
	const auto& agent = proxy.getAgent();
	ClientBuilder clientBuilder{agent};
	clientBuilder.setConferenceFactoryAddress(linphone::Factory::get()->createAddress(confFactoryUri))
	    .setLimeX3DH(OnOff::Off);
	const auto me = clientBuilder.build("I@sip.example.org");
	const auto you = clientBuilder.build("you@sip.example.org");
	BC_HARD_ASSERT_CPP_EQUAL(records.size(), 2);
	CoreAssert asserter{proxy, you, me};
	auto chatroomBuilder = me.chatroomBuilder();
	chatroomBuilder.setBackend(linphone::ChatRoom::Backend::FlexisipChat).setGroup(OnOff::On);
	const auto listener = make_shared<AllJoinedWaiter>();
	const auto& confMan = proxy.getConfigManager();
	const auto conferenceServerUri = [confServerCfg = confMan->getRoot()->get<GenericStruct>("conference-server")] {
		return confServerCfg->get<ConfigString>("transport")->read();
	};
	{ // Populate conference server's DB
		TestConferenceServer conferenceServer(*agent, confMan, regDb);
		conferenceServer.start();
		BC_HARD_ASSERT_CPP_EQUAL(records.size(), 2 /* users */ + 1 /* factory */ + 1 /* focus */);
		const auto& inMyRoom = you.getMe();
		listener->setChatrooms({
		    chatroomBuilder.setSubject("Boom0").build({inMyRoom}),
		    chatroomBuilder.setSubject("Boom1").build({inMyRoom}),
		    chatroomBuilder.setSubject("Boom2").build({inMyRoom}),
		    chatroomBuilder.setSubject("Boom3").build({inMyRoom}),
		});

		asserter
		    .iterateUpTo(8,
		                 [&chatrooms = listener->getChatrooms()] {
			                 FAIL_IF(0 < chatrooms.size());
			                 return ASSERTION_PASSED();
		                 })
		    .assert_passed();

		BC_ASSERT_CPP_EQUAL(listener->getChatrooms().size(), 0);
		// Chat rooms are now only identified by the parameter conf-id therefore the registrarDb doesn't grow anymore
		BC_ASSERT_CPP_EQUAL(records.size(), 2 /* users */ + 1 /* factory */ + 1 /* focus */);
	} // Shutdown conference server
	(const_cast<RegistrarDbInternal*>(registrarBackend))->clearAll();

	// Spin it up again
	TestConferenceServer conferenceServer(*agent, confMan, regDb);
	conferenceServer.start();

	// The conference server restored its chatrooms from DB and bound them back on the Registrar
	// Chat rooms are now only identified by the parameter conf-id therefore the registrarDb doesn't grow anymore
	BC_ASSERT_CPP_EQUAL(records.size(), 1 /* factory */ + 1 /* focus */);
	for (const auto& record : records) {
		const auto& contacts = record.second->getExtendedContacts();
		BC_ASSERT_CPP_EQUAL(contacts.size(), 1);
		BC_ASSERT_CPP_EQUAL(contacts.latest()->get()->urlAsString(), conferenceServerUri());
	}
}

// Anchor CNFFACREGKEYMIG
// Flexisip 2.2 used CallIDs as keys in the Registrar in the absence of a +sip.instance field. The Conference server
// relied on this to update its contact in the registrar, and now relies on a +sip.instance to achieve the same result.
// Unfortunately, the transition from 2.2 to 2.3 leaves an entry with the "CONFERENCE" CallID as key that the conference
// server has to clean up manually.
void conferenceServerClearsOldBindingsOnInit() {
	const string confFactoryUri = "sip:conference-factory@sip.example.org";
	const string confFocusUri = "sip:conference-focus@sip.example.org";
	Server proxy{{
	    // Requesting bind on port 0 to let the kernel find any available port
	    {"global/transports", "sip:127.0.0.1:0;transport=tcp"},

	    {"conference-server/database-backend", "sqlite"},
	    {"conference-server/database-connection-string", "/dev/null"},
	    {"conference-server/conference-factory-uris", confFactoryUri},
	    {"conference-server/conference-focus-uris", confFocusUri},
	    {"conference-server/state-directory", bcTesterWriteDir().append("var/lib/flexisip")},
	}};
	proxy.start();
	auto& registrar = *proxy.getRegistrarDb();
	const auto* registrarBackend = dynamic_cast<const RegistrarDbInternal*>(&registrar.getRegistrarBackend());
	BC_HARD_ASSERT_TRUE(registrarBackend != nullptr);
	const auto& records = registrarBackend->getAllRecords();
	BC_HARD_ASSERT_CPP_EQUAL(records.size(), 0);
	sofiasip::Home home{};
	const SipUri aor(confFactoryUri);
	BindingParameters params{};
	params.globalExpire = chrono::seconds{0xdead};
	params.callId = "CONFERENCE";
	const auto unexpectedContact = "sip:unexpected@127.0.0.1";
	const auto contact =
	    sip_contact_create(home.home(), reinterpret_cast<const url_string_t*>(unexpectedContact), nullptr);
	// Fake an existing contact as if left over from a previous version
	registrar.bind(aor, contact, params, nullptr);
	BC_HARD_ASSERT_CPP_EQUAL(records.size(), 1);
	{
		const auto& contacts = records.begin()->second->getExtendedContacts();
		BC_HARD_ASSERT_CPP_EQUAL(contacts.size(), 1);
		BC_ASSERT_CPP_EQUAL(contacts.latest()->get()->urlAsString(), unexpectedContact);
	}

	TestConferenceServer conferenceServer(proxy);
	conferenceServer.start();

	BC_ASSERT_CPP_EQUAL(records.size(), 1 /* factory */ + 1 /* focus */);
	const auto& contacts = records.begin()->second->getExtendedContacts();
	BC_ASSERT_CPP_EQUAL(contacts.size(), 1);
	for (const auto& contact : contacts) {
		// Left over contact has been cleaned up
		BC_ASSERT_CPP_NOT_EQUAL(contact->urlAsString(), unexpectedContact);
	}
}

/** Assert the conference server re-sends the INVITE when a participant device comes back online.
 *
 *  1. Set up two participants with one device each, a proxy and a conference server.
 *  2. Simulate a device going offline long enough for its REGISTER to expire (but still being within its message-expire
 * time, such that it is still in the RegistrarDB).
 *  3. Invite it to a chatroom. The conference server will get a 404 from the proxy for this device.
 *  4. Simulate device going back online. The proxy will notify the conference server and the latter will re-send the
 * INVITE to the participant device.
 */
void inviteResentOnReconnect() {
	static const auto confFactoryUri = "sip:conference-factory@sip.example.org"s;
	const auto testDir = TmpDir(__FUNCTION__ + "."s);
	auto proxy = Server({
	    // Requesting bind on port 0 to let the kernel find any available port
	    {"global/transports", "sip:127.0.0.1:0;transport=tcp"},

	    {"conference-server/database-backend", "sqlite"},
	    {"conference-server/database-connection-string", "/dev/null"},
	    {"conference-server/conference-factory-uris", confFactoryUri},
	    {"conference-server/conference-focus-uris", "sip:conference-focus@sip.example.org"},
	    {"conference-server/state-directory", testDir.path() / "conf-server"},
	});
	proxy.start();
	auto& agent = proxy.getAgent();
	const auto& regDb = proxy.getRegistrarDb();
	auto conferenceServer = TestConferenceServer(*agent, proxy.getConfigManager(), regDb);
	conferenceServer.start();
	auto clientBuilder = ClientBuilder(agent);
	clientBuilder.setConferenceFactoryAddress(linphone::Factory::get()->createAddress(confFactoryUri))
	    .setLimeX3DH(OnOff::Off);
	const auto simon = clientBuilder.build("simon@sip.example.org");
	auto julien = clientBuilder.setMessageExpires(0xbah).build("julien@sip.example.org");
	julien.disconnect(); // Client goes offline
	const auto julienAddress = julien.getMe();
	const auto& registrarBackend = dynamic_cast<const RegistrarDbInternal&>(regDb->getRegistrarBackend());
	auto& records = registrarBackend.getAllRecords();
	BC_HARD_ASSERT(!records.empty());
	const auto& julienKey = Record::Key(SipUri(julienAddress->asStringUriOnly()), false);
	const auto& julienDevices = records.at(julienKey.asString())->getExtendedContacts();
	auto& julienDeviceContact = (**julienDevices.latest());
	constexpr auto margin = 10s;
	const auto inviteExpirationTime = julienDeviceContact.getSipExpires() + margin;
	julienDeviceContact.setRegisterTime(julienDeviceContact.getRegisterTime() - inviteExpirationTime.count());
	// Registration expires, but the contact is still in the Registrar (because of message-expires)
	BC_ASSERT(!julienDeviceContact.isExpired());
	auto asserter = CoreAssert(proxy, julien, simon);
	const auto listener = make_shared<AllJoinedWaiter>();
	const auto simonChatroom = simon.chatroomBuilder()
	                               .setBackend(linphone::ChatRoom::Backend::FlexisipChat)
	                               .setGroup(OnOff::On)
	                               .setSubject("Liblinphone Team")
	                               .build({julienAddress});
	listener->setChatrooms({simonChatroom});
	asserter
	    .iterateUpTo(
	        8, [&chatRoomsToCreate = listener->getChatrooms()] { return LOOP_ASSERTION(chatRoomsToCreate.empty()); })
	    .assert_passed();

	const auto confServerChatrooms = conferenceServer.getChatrooms();
	BC_HARD_ASSERT_CPP_EQUAL(confServerChatrooms.size(), 1);
	auto julienParticipant = shared_ptr<linphone::Participant>();
	for (auto& participant : confServerChatrooms.front()->getParticipants()) {
		if (participant->getAddress()->equal(julienAddress)) {
			julienParticipant = participant;
			break;
		}
	}
	BC_HARD_ASSERT_NOT_NULL(julienParticipant);
	const auto devices = julienParticipant->getDevices();
	BC_HARD_ASSERT_CPP_EQUAL(devices.size(), 1);
	const auto& offlineDevice = devices.front();
	BC_ASSERT_ENUM_EQUAL(offlineDevice->getState(), linphone::ParticipantDevice::State::ScheduledForJoining);

	SLOGD << "TEST " << __FUNCTION__ << " Client reconnects";
	julien.reconnect();
	julien.refreshRegisters();
	asserter
	    .iterateUpTo(
	        8, [&] { return LOOP_ASSERTION(offlineDevice->getState() == linphone::ParticipantDevice::State::Present); })
	    .assert_passed();
	BC_ASSERT_ENUM_EQUAL(offlineDevice->getState(), linphone::ParticipantDevice::State::Present);
}

class SubscriptionsCounter {
public:
	static constexpr string_view mLogPrefix{"SubscriptionsCounter"};

	void countSubscription(const shared_ptr<MsgSip>& msg) {
		const auto callId = msg->getCallID();
		const auto* sip = msg->getSip();
		const auto* to = sip->sip_to ? url_as_string(msg->getHome(), sip->sip_to->a_url) : "<unknown>";

		if (sip->sip_expires && sip->sip_expires->ex_delta == 0) {
			LOGD << "New unsubscription to " << to << " (" << callId << ")";
			mUnsubscriptionCallIds[to].emplace(callId);
		} else {
			LOGD << "New subscription to " << to << " (" << callId << ")";
			mSubscriptionCallIds[to].emplace_back(callId);
		}
	};

	void countNotify(const shared_ptr<MsgSip>& msg) {
		const auto* cseq = msg->getSip()->sip_cseq;
		const auto key = msg->getCallID() + ":" + (cseq ? to_string(cseq->cs_seq) : "<unknown>");
		mNotifyCallIds.emplace(key);

		LOGD << "New NOTIFY (" << key << "): now has received " << mNotifyCallIds.size() << " NOTIFY requests so far";
	}

	size_t getCount(const string& to) {
		const auto it = mSubscriptionCallIds.find(to);
		if (it == mSubscriptionCallIds.end()) return 0;
		return it->second.size();
	};

	size_t getNotifyCount() const {
		return mNotifyCallIds.size();
	}

	size_t getUnsubscriptionCount(const string& to) const {
		const auto it = mUnsubscriptionCallIds.find(to);
		if (it == mUnsubscriptionCallIds.end()) return 0;
		return it->second.size();
	}

	set<string> mNotifyCallIds{};
	map<string, list<string>> mSubscriptionCallIds{};
	map<string, set<string>> mUnsubscriptionCallIds{};
};

/*
 * Specification:
 * - Each participant has at most one external registration subscription, regardless of the number of chatrooms
 *   in which they participate.
 * - Each participant is subscribed independently of the other participants.
 * - Registering an additional device for a participant updates every chatroom in which that participant is present.
 * - After leaving one chatroom, registering an additional device updates only the remaining chatrooms.
 * - After leaving all chatrooms, registering an additional device updates no chatroom and tears down the external
 *   registration subscription.
 * - Rejoining a chatroom restores registration updates for that chatroom.
 */
void singleExternalSubscriptionPerParticipant() {
	SubscriptionsCounter counter{};

	// External proxy and RegEvent server setup.
	auto hookExternalProxy = InjectedHooks{
	    .onRequest =
	        [&counter](unique_ptr<RequestSipEvent>&& ev) {
		        const auto& msg = ev->getMsgSip();
		        const auto method = msg->getSipMethod();
		        if (method == sip_method_subscribe) {
			        counter.countSubscription(msg);
		        } else if (method == sip_method_notify) {
			        const auto* sip = msg->getSip();
			        if (sip->sip_event && sip->sip_event->o_type && string_view{sip->sip_event->o_type} == "reg") {
				        counter.countNotify(msg);
			        }
		        }

		        return std::move(ev);
	        },
	};
	Server externalProxy{
	    {
	        {"global/transports", "sip:127.0.0.2:0;transport=tcp"},
	        {"global/aliases", "external.example.org"},
	        {"module::Registrar/enabled", "true"},
	        {"module::Registrar/reg-domains", "external.example.org"},
	        {"module::RegEvent/enabled", "true"},
	    },
	    &hookExternalProxy,
	};

	const auto& externalProxyRegistrarDb = externalProxy.getRegistrarDb();
	RegEventServer externalRegEvent{externalProxyRegistrarDb};

	externalProxy.setConfigParameter({
	    "module::RegEvent/regevent-server",
	    "sip:127.0.0.2:" + to_string(externalRegEvent.getCore()->getTransportsUsed()->getTcpPort()) + ";transport=tcp",
	});

	externalProxy.start();

	// Internal proxy and conference server setup.
	const string confFocusUri{"sip:conference-focus@local.example.org"};
	const string confFactoryUri{"sip:conference-factory@local.example.org"};
	const auto externalProxyUri = "sip:127.0.0.2:"s + externalProxy.getFirstPort() + ";transport=tcp";

	Server proxy{{
	    {"global/transports", "sip:127.0.0.1:0;transport=tcp"},
	    {"global/aliases", "local.example.org"},
	    {"module::Registrar/enabled", "true"},
	    {"module::Registrar/reg-domains", "local.example.org"},
	    {"conference-server/database-backend", "sqlite"},
	    {"conference-server/database-connection-string", "/dev/null"},
	    {"conference-server/conference-factory-uris", confFactoryUri},
	    {"conference-server/conference-focus-uris", confFocusUri},
	    {"conference-server/empty-chat-room-deletion", "false"},
	    {"conference-server/state-directory", bcTesterWriteDir().append("var/lib/flexisip")},
	}};
	proxy.start();

	// This is to route requests (received on the external proxy) intended for the conference-focus to the local proxy.
	TmpDir directory{__func__};
	const auto routesConfigFilePath = directory.path() / "routes.conf";
	{
		ofstream file{routesConfigFilePath};
		file << "<sip:127.0.0.1:" << proxy.getFirstPort() << ";transport=tcp> to.uri.user == 'conference-focus'";
	}
	externalProxy.setConfigParameter({"module::Forward/routes-config-path", routesConfigFilePath.string()});
	dynamic_pointer_cast<ForwardModule>(externalProxy.getAgent()->findModuleByRole("Forward"))->reload();

	// Check no one is registered on the external proxy yet.
	const auto* externalProxyRegistrarBackend =
	    dynamic_cast<const RegistrarDbInternal*>(&externalProxyRegistrarDb->getRegistrarBackend());
	BC_HARD_ASSERT_TRUE(externalProxyRegistrarBackend != nullptr);
	const auto& externalRegistrarDbRecords = externalProxyRegistrarBackend->getAllRecords();
	BC_HARD_ASSERT_CPP_EQUAL(externalRegistrarDbRecords.size(), 0);

	// Setup local clients.
	ClientBuilder clientsBuilder{proxy.getAgent()};
	clientsBuilder.setConferenceFactoryAddress(linphone::Factory::get()->createAddress(confFactoryUri));

	const std::string whaleAddress{"sip:whale@local.example.org"};
	const auto whale = clientsBuilder.build(whaleAddress);

	// Setup external clients.
	ClientBuilder externalClientsBuilder{externalProxy.getAgent()};
	externalClientsBuilder.setConferenceFactoryAddress(linphone::Factory::get()->createAddress(confFactoryUri));

	const std::string sealAddress{"sip:seal@external.example.org"};
	const auto seal = externalClientsBuilder.build(sealAddress);
	const std::string wombatAddress{"sip:wombat@external.example.org"};
	const auto wombat = externalClientsBuilder.build(wombatAddress);
	const std::string shrimpAddress{"sip:shrimp@external.example.org"};
	const auto shrimp = externalClientsBuilder.build(shrimpAddress);

	// Check all external clients are registered on the external proxy.
	BC_HARD_ASSERT_CPP_EQUAL(externalRegistrarDbRecords.size(), 3); // seal, wombat, shrimp

	TestConferenceServer conferenceServer{proxy};
	conferenceServer.setOutboundProxy(externalProxyUri);
	conferenceServer.start();

	// Check the local registrar DB has the conference factory and focus URIs bound.
	const auto& registrarDb = proxy.getRegistrarDb();
	const auto* registrarDbBackend = dynamic_cast<const RegistrarDbInternal*>(&registrarDb->getRegistrarBackend());
	BC_HARD_ASSERT_TRUE(registrarDbBackend != nullptr);
	const auto& records = registrarDbBackend->getAllRecords();
	BC_HARD_ASSERT_CPP_EQUAL(records.size(), 1 /* users */ + 1 /* factory */ + 1 /* focus */);

	CoreAssert asserter{proxy, externalProxy, externalRegEvent.getCore(), whale, seal, wombat, shrimp};

	const auto mammalsChatroom = whale.chatroomBuilder()
	                                 .setBackend(linphone::ChatRoom::Backend::FlexisipChat)
	                                 .setGroup(OnOff::On)
	                                 .setSubject("Mammals Rocks!")
	                                 .build({seal.getMe(), wombat.getMe()});

	{
		const auto listener = make_shared<AllJoinedWaiter>();
		listener->setChatrooms({mammalsChatroom});
		asserter
		    .waitUntil(5s, [&chatRoomsToCreate =
		                        listener->getChatrooms()] { return LOOP_ASSERTION(chatRoomsToCreate.empty()); })
		    .assert_passed();

		BC_ASSERT_CPP_EQUAL(counter.getCount("sip:seal@external.example.org"), 1);
		BC_ASSERT_CPP_EQUAL(counter.getCount("sip:wombat@external.example.org"), 1);
		BC_ASSERT_CPP_EQUAL(counter.getCount("sip:shrimp@external.example.org"), 0);
	}

	const auto swimmersChatroom = whale.chatroomBuilder()
	                                  .setBackend(linphone::ChatRoom::Backend::FlexisipChat)
	                                  .setGroup(OnOff::On)
	                                  .setSubject("Can Swim")
	                                  .build({seal.getMe(), shrimp.getMe()});

	{
		const auto listener = make_shared<AllJoinedWaiter>();
		listener->setChatrooms({swimmersChatroom});
		asserter
		    .waitUntil(5s, [&chatRoomsToCreate =
		                        listener->getChatrooms()] { return LOOP_ASSERTION(chatRoomsToCreate.empty()); })
		    .assert_passed();

		BC_ASSERT_CPP_EQUAL(counter.getCount("sip:seal@external.example.org"), 1);
		BC_ASSERT_CPP_EQUAL(counter.getCount("sip:wombat@external.example.org"), 1);
		BC_ASSERT_CPP_EQUAL(counter.getCount("sip:shrimp@external.example.org"), 1);
	}

	const auto confServerChatrooms = conferenceServer.getChatrooms();
	BC_HARD_ASSERT_CPP_EQUAL(confServerChatrooms.size(), 2);

	const auto mammalsServerChatroom = conferenceServer.findChatroom(wombat.getMe());
	BC_HARD_ASSERT_NOT_NULL(mammalsServerChatroom);
	const auto swimmersServerChatroom = conferenceServer.findChatroom(shrimp.getMe());
	BC_HARD_ASSERT_NOT_NULL(swimmersServerChatroom);

	const auto sealMutableAddress = linphone::Factory::get()->createAddress(sealAddress);

	const auto waitForSealDeviceCount = [&](const shared_ptr<linphone::ChatRoom>& chatroom, size_t count) {
		asserter
		    .waitUntil(5s,
		               [&] {
			               // Check that the seal participant has the expected number of devices in the chatroom.
			               return LOOP_ASSERTION(std::ranges::any_of(
			                   chatroom->getParticipants(), [&sealAddress, count](const auto& participant) {
				                   return participant->getAddress()->asStringUriOnly() == sealAddress &&
				                          participant->getDevices().size() == count;
			                   }));
		               })
		    .assert_passed();
	};
	const auto hasSeal = [&sealAddress](const shared_ptr<linphone::ChatRoom>& chatroom) {
		return std::ranges::any_of(chatroom->getParticipants(), [&sealAddress](const auto& participant) {
			return participant->getAddress()->asStringUriOnly() == sealAddress;
		});
	};
	const auto waitForSealPresence = [&](const shared_ptr<linphone::ChatRoom>& chatroom, bool present) {
		asserter.waitUntil(5s, [&] { return LOOP_ASSERTION(hasSeal(chatroom) == present); }).assert_passed();
	};

	const auto notifyCountAfterSetup = counter.getNotifyCount();

	// Case: chatrooms get updated when a new device is registered for a participant already in the chatroom.
	{
		const auto newDevice = externalClientsBuilder.build(sealAddress);
		waitForSealDeviceCount(mammalsServerChatroom, 2);
		waitForSealDeviceCount(swimmersServerChatroom, 2);

		// Check only one NOTIFY received for the new device, not one per chatroom.
		BC_ASSERT_CPP_EQUAL(counter.getNotifyCount(), notifyCountAfterSetup + 1);
	}

	// Case: user leaves one chatroom then change one device, make sure only the other chatroom gets updated, not the
	// one the user left.
	{
		// Seal leaves the mammals chatroom.
		mammalsChatroom->removeParticipant(mammalsChatroom->findParticipant(sealMutableAddress));
		waitForSealPresence(mammalsServerChatroom, false);

		// Seal has a new device (which gets destroyed by the end of this scope).
		const auto notifyCountAfterMammalsLeave = counter.getNotifyCount();
		const auto newDevice = externalClientsBuilder.build(sealAddress);

		waitForSealDeviceCount(swimmersServerChatroom, 2);
		BC_ASSERT_CPP_EQUAL(counter.getNotifyCount(), notifyCountAfterMammalsLeave + 1);

		// Check seal is still not present in the mammals chatroom, even though it has a new device.
		BC_ASSERT_FALSE(hasSeal(mammalsServerChatroom));
		// Check no unsubscription was received.
		BC_ASSERT_CPP_EQUAL(counter.getUnsubscriptionCount(sealAddress), 0);
	}

	// Case: user leaves all chatrooms then change one device, make sure no chatroom gets updated.
	{
		const auto unsubscriptionCount = counter.getUnsubscriptionCount(sealAddress);

		// Seal leaves the swimmers chatroom.
		swimmersChatroom->removeParticipant(swimmersChatroom->findParticipant(sealMutableAddress));
		waitForSealPresence(swimmersServerChatroom, false);

		BC_ASSERT_FALSE(hasSeal(swimmersServerChatroom));
		BC_ASSERT_FALSE(hasSeal(mammalsServerChatroom));

		// Wait until the RegistrationEvent::Client gets destroyed (because seal is not in any chatroom anymore).
		asserter
		    .waitUntil(
		        5s,
		        [&] { return LOOP_ASSERTION(counter.getUnsubscriptionCount(sealAddress) == unsubscriptionCount + 1); })
		    .assert_passed();

		// Seal has a new device (which gets destroyed by the end of this scope).
		// This device addition should not trigger any NOTIFY request since seal is not in any chatroom.
		const auto newDevice = externalClientsBuilder.build(sealAddress);
	}

	// Case: user rejoins a chatroom then changes one device, make sure the chatroom gets updated.
	{
		// Seal rejoins the mammals chatroom.
		mammalsChatroom->addParticipant(sealMutableAddress);
		waitForSealPresence(mammalsServerChatroom, true);

		const auto notifyCountAfterReturn = counter.getNotifyCount();
		// Seal has a new device (which gets destroyed by the end of this scope).
		const auto newDevice = externalClientsBuilder.build(sealAddress);
		waitForSealDeviceCount(mammalsServerChatroom, 2);

		BC_ASSERT_CPP_EQUAL(counter.getNotifyCount(), notifyCountAfterReturn + 1);
	}
}

/**
 * Test that the conference-server correctly binds the "old" chatroom (chatroom-xyz) even if the uuid has changed.
 */
void oldChatroomSupport() {
	RedisServer redis{};
	const auto testDir = TmpDir(__FUNCTION__ + "."s);
	Server proxy{
	    {
	        {"global/transports", "sip:127.0.0.1:0;transport=tcp"},
	        {"module::Registrar/db-implementation", "redis"},
	        {"module::Registrar/enable-gruu", "true"},
	        {"module::Registrar/redis-server-domain", "localhost"},
	        {"module::Registrar/redis-server-port", std::to_string(redis.port())},
	        {"module::Registrar/redis-slave-check-period", "1" /* second */},
	        {"conference-server/database-backend", "sqlite"},
	        {"conference-server/database-connection-string", "/dev/null"},
	        {"conference-server/conference-factory-uris", "sip:conference-factory@sip.example.org"},
	        {"conference-server/conference-focus-uris", "sip:conference-focus@sip.example.org"},
	        {"conference-server/state-directory", testDir.path() / "conf-server"},
	    },
	};
	proxy.start();
	CoreAssert asserter{proxy};
	auto& registrar = proxy.getRegistrarDb();

	auto backend = dynamic_cast<const RegistrarDbRedisAsync*>(&registrar->getRegistrarBackend());
	BC_HARD_ASSERT(backend != nullptr);
	auto& registrarBackend = const_cast<RegistrarDbRedisAsync&>(*backend); // we want to force a behavior
	BC_ASSERT(registrarBackend.connect() != std::nullopt);

	class FakeListener : public ContactUpdateListener {
		void onRecordFound(const std::shared_ptr<Record>&) override {
			recorded = true;
		}
		void onError(const SipStatus&) override {}
		void onInvalid(const SipStatus&) override {}
		void onContactUpdated(const std::shared_ptr<ExtendedContact>&) override {
			updated = true;
		}

	public:
		bool recorded{};
		bool updated{};
	};
	std::shared_ptr<FakeListener> listener = std::make_shared<FakeListener>();
	const auto bindingUrl = "sip:chatroom-xyz@sip.example.org;gr=aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
	flexisip::SipUri uri(bindingUrl);

	BindingParameters parameter;
	parameter.callId = "dummy";
	parameter.globalExpire = chrono::seconds{100};
	parameter.alias = false;
	parameter.version = 0;
	parameter.withGruu = true;

	// Simulate an old chatroom creation with a uuid 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa'.
	sofiasip::Home home{};
	const auto gruuA = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
	sip_contact_t* sipContactOnA = sip_contact_create(
	    home.home(), reinterpret_cast<const url_string_t*>(url_make(home.home(), "sip:127.0.0.1:6064;transport=tcp")),
	    su_strdup(home.home(), ("+sip.instance=" + UriUtils::grToUniqueId(gruuA)).c_str()), nullptr);
	registrar->bind(uri, sipContactOnA, parameter, listener);
	BC_ASSERT(asserter.iterateUpTo(10, [&listener] { return listener->recorded; }));

	TestConferenceServer conf{proxy};
	conf.start();
	BC_ASSERT_CPP_EQUAL(listener->updated, false);

	// Bind chatroom with a new uuid, the previous contact must be updated.
	conf.bindChatRoom(bindingUrl, "sip:127.0.0.1:6065;transport=tcp", listener);
	BC_ASSERT(asserter.iterateUpTo(10, [&listener] { return listener->updated; }));
}

TestSuite _{
    "Conference",
    {
        CLASSY_TEST(conferenceServerBindsChatroomsFromDBOnInit),
        CLASSY_TEST(conferenceServerClearsOldBindingsOnInit),
        CLASSY_TEST(inviteResentOnReconnect),
        CLASSY_TEST(singleExternalSubscriptionPerParticipant),
        CLASSY_TEST(oldChatroomSupport),
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
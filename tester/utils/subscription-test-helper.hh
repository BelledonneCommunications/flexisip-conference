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

#include <memory>

#include "linphone++/chat_room.hh"

#include "conference/registration-subscription.hh"

using namespace linphone;

namespace flexisip::tester {

enum class RegistrationSubscriptionState { None = 0, Started = 1, Stopped = 2 };

class RegistrationSubscriptionTester : public RegistrationSubscription {
public:
	RegistrationSubscriptionTester(const std::shared_ptr<linphone::ChatRoom>& cr,
	                               const std::shared_ptr<const linphone::Address>& participant)
	    : RegistrationSubscription(true, cr, participant) {}

	bool checkCapabilities(const std::string& specs) {
		return isContactCompatible(specs);
	}

	void start() override {
		mState = RegistrationSubscriptionState::Started;
	};

	void stop() override {
		mState = RegistrationSubscriptionState::Stopped;
	};

	RegistrationSubscriptionState mState{RegistrationSubscriptionState::None};
};

class ClientTester : public registration_event::Client {
public:
	ClientTester(const std::shared_ptr<registration_event::ClientFactory>& factory,
	             const std::shared_ptr<const linphone::Address>& to)
	    : Client(factory, to) {}

	void subscribe(const std::shared_ptr<registration_event::ClientListener>& = nullptr) {
		mState = RegistrationSubscriptionState::Started;
	};

	void unsubscribe() {
		mState = RegistrationSubscriptionState::Stopped;
	};

	RegistrationSubscriptionState mState{RegistrationSubscriptionState::None};
};

class ExternalRegistrationSubscriptionTester : public ExternalRegistrationSubscription {
public:
	ExternalRegistrationSubscriptionTester(const ConferenceServer& server,
	                                       const std::shared_ptr<linphone::ChatRoom>& cr,
	                                       const std::shared_ptr<const linphone::Address>& participant,
	                                       const std::shared_ptr<registration_event::Client>& client)
	    : ExternalRegistrationSubscription(server, cr, participant, client) {}

	void setClientTester(const std::shared_ptr<ClientTester>& clientTester) {
		mClientTester = clientTester;
	}

	void start() override {
		ExternalRegistrationSubscription::start();
		mClientTester->subscribe();
	}

	void stop() override {
		ExternalRegistrationSubscription::stop();
		mClientTester->unsubscribe();
	}

	std::shared_ptr<ClientTester> mClientTester{};
};

class ConferenceTestHelper {
public:
	void startMinimalCore();

	std::shared_ptr<linphone::Conference> createConference(ChatRoom::EncryptionBackend encryptionBackend,
	                                                       ChatRoom::EphemeralMode ephemeralMode,
	                                                       const std::string& subject);

	std::shared_ptr<linphone::Core> mCore{};
	std::shared_ptr<linphone::ConferenceParams> mConferenceParams{};
	std::shared_ptr<linphone::ChatParams> mChatParams{};
	std::shared_ptr<linphone::AccountParams> mAccountParams{};
};

class SubscriptionTestHelper {

public:
	SubscriptionTestHelper() {
		mParticipant1 = Factory::get()->createAddress("sip:" + mKey1);
		mParticipant2 = Factory::get()->createAddress("sip:" + mKey2);
		mParticipant3 = Factory::get()->createAddress("sip:" + mKey3);

		mConferenceTestHelper.startMinimalCore();
		mConference1 = mConferenceTestHelper.createConference(ChatRoom::EncryptionBackend::None,
		                                                      ChatRoom::EphemeralMode::DeviceManaged, "chat 1");
		mConference2 = mConferenceTestHelper.createConference(ChatRoom::EncryptionBackend::None,
		                                                      ChatRoom::EphemeralMode::DeviceManaged, "chat 2");

		mChatRoom1 = mConference1->getChatRoom();
		mChatRoom2 = mConference2->getChatRoom();

		const std::chrono::seconds refreshDelay{600};
		mClientFactory = std::make_shared<registration_event::ClientFactory>(mConferenceTestHelper.mCore, refreshDelay);
	}

	void subscribeAll() {
		mSubscription1In1 = std::make_shared<RegistrationSubscriptionTester>(mChatRoom1, mParticipant1);
		mSubscription1In2 = std::make_shared<RegistrationSubscriptionTester>(mChatRoom2, mParticipant1);
		mSubscription2In1 = std::make_shared<RegistrationSubscriptionTester>(mChatRoom1, mParticipant2);
		mSubscription3In2 = std::make_shared<RegistrationSubscriptionTester>(mChatRoom2, mParticipant3);
	}

	const std::string mKey1{"user1@domain1"};
	const std::string mKey2{"user2@domain2"};
	const std::string mKey3{"user3@domain3"};

	std::shared_ptr<linphone::Address> mParticipant1{};
	std::shared_ptr<linphone::Address> mParticipant2{};
	std::shared_ptr<linphone::Address> mParticipant3{};

	ConferenceTestHelper mConferenceTestHelper{};

	std::shared_ptr<linphone::Conference> mConference1{};
	std::shared_ptr<linphone::Conference> mConference2{};

	std::shared_ptr<linphone::ChatRoom> mChatRoom1{};
	std::shared_ptr<linphone::ChatRoom> mChatRoom2{};

	std::shared_ptr<RegistrationSubscriptionTester> mSubscription1In1{};
	std::shared_ptr<RegistrationSubscriptionTester> mSubscription1In2{};
	std::shared_ptr<RegistrationSubscriptionTester> mSubscription2In1{};
	std::shared_ptr<RegistrationSubscriptionTester> mSubscription3In2{};

	std::shared_ptr<registration_event::ClientFactory> mClientFactory{};
};

} // namespace flexisip::tester

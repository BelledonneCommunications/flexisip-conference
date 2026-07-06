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

#include "conference/registration-subscription-factory.hh"

#include <memory>

#include "linphone++/chat_room.hh"

#include "conference/conference-server.hh"
#include "conference/registration-subscription.hh"
#include "test-patterns/test.hh"
#include "test-suite.hh"
#include "utils/subscription-test-helper.hh"

using namespace std;
using namespace linphone;

namespace flexisip::tester {

namespace {

class ExternalRegistrationSubscriptionClientsStoreTester : public ExternalRegistrationSubscriptionClientsStore {
public:
	shared_ptr<registration_event::Client> getClient(const shared_ptr<const linphone::Address>& address) {
		const auto addressUri = address->asStringUriOnlyOrdered();
		const auto it = mClients.find(addressUri);
		if (it == mClients.end()) {
			return nullptr;
		}

		return it->second.lock();
	};

	bool clientFound(const shared_ptr<registration_event::Client>& expectedClient,
	                 const shared_ptr<const Address>& address) {
		return getClient(address) == expectedClient;
	}

	size_t getNumberOfClients() {
		return mClients.size();
	};
};

class ExternalRegistrationSubscriptionFactoryTester : public ExternalRegistrationSubscriptionFactory {
public:
	ExternalRegistrationSubscriptionFactoryTester()
	    : mClientsStoreTester(make_shared<ExternalRegistrationSubscriptionClientsStoreTester>()){};

	shared_ptr<RegistrationSubscription> create(const ConferenceServer& server,
	                                            RegistrarDb&,
	                                            const shared_ptr<linphone::ChatRoom>& chatRoom,
	                                            const shared_ptr<const linphone::Address>& address) override {
		const auto client = mClientsStoreTester->acquireClient(address, server.getRegEventClientFactory());
		return make_shared<ExternalRegistrationSubscription>(server, chatRoom, address, client);
	};

	const shared_ptr<ExternalRegistrationSubscriptionClientsStoreTester>& getClientsStore() const {
		return mClientsStoreTester;
	}

	shared_ptr<ExternalRegistrationSubscriptionClientsStoreTester> mClientsStoreTester{};
};

void createClient() {
	SubscriptionTestHelper helper{};
	ExternalRegistrationSubscriptionClientsStoreTester store{};

	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 0);

	const auto client1 = store.acquireClient(helper.mParticipant1, helper.mClientFactory);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1);
	BC_ASSERT_PTR_NOT_NULL(store.getClient(helper.mParticipant1));
	BC_ASSERT_PTR_NULL(store.getClient(helper.mParticipant2));

	const auto client2 = store.acquireClient(helper.mParticipant2, helper.mClientFactory);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 2);
	BC_ASSERT_PTR_NOT_NULL(store.getClient(helper.mParticipant1));
	BC_ASSERT_PTR_NOT_NULL(store.getClient(helper.mParticipant2));
}

void uniqueClientForSameAddress() {
	SubscriptionTestHelper helper{};
	ExternalRegistrationSubscriptionClientsStoreTester store{};

	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 0);

	auto client1 = store.acquireClient(helper.mParticipant1, helper.mClientFactory);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1);
	BC_ASSERT_PTR_NOT_NULL(store.getClient(helper.mParticipant1));

	auto client2 = store.acquireClient(helper.mParticipant1, helper.mClientFactory);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1);
	BC_ASSERT_TRUE(store.clientFound(client1, helper.mParticipant1));
	BC_ASSERT_TRUE(store.clientFound(client2, helper.mParticipant1));
	BC_ASSERT_PTR_EQUAL(client1, client2);
}

void recreateExpiredClientForSameAddress() {
	SubscriptionTestHelper helper{};
	ExternalRegistrationSubscriptionClientsStoreTester store{};

	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 0);

	// Client gets destroyed when it goes out of scope, so we can test that a new client is created.
	{
		const auto client1 = store.acquireClient(helper.mParticipant1, helper.mClientFactory);
		BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1);
		BC_ASSERT_PTR_NOT_NULL(store.getClient(helper.mParticipant1));
	}

	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1); // The entry is still in the map, but the weak_ptr is expired.
	BC_ASSERT_PTR_NULL(store.getClient(helper.mParticipant1));

	const auto client2 = store.acquireClient(helper.mParticipant1, helper.mClientFactory);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1);
	BC_ASSERT_PTR_NOT_NULL(client2);
}

void releaseSharedClientAfterLastReference() {
	SubscriptionTestHelper helper{};
	ExternalRegistrationSubscriptionClientsStoreTester store{};

	auto client1 = store.acquireClient(helper.mParticipant1, helper.mClientFactory);
	auto client2 = store.acquireClient(helper.mParticipant1, helper.mClientFactory);
	BC_ASSERT_PTR_EQUAL(client1, client2);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1);

	client1.reset();
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1);
	BC_ASSERT_PTR_NOT_NULL(store.getClient(helper.mParticipant1));

	client2.reset();
	BC_ASSERT_CPP_EQUAL(store.getNumberOfClients(), 1); // The entry is still in the map, but the weak_ptr is expired.
	BC_ASSERT_PTR_NULL(store.getClient(helper.mParticipant1));
}

TestSuite _{
    "RegistrationSubscriptionFactory",
    {
        CLASSY_TEST(createClient),
        CLASSY_TEST(uniqueClientForSameAddress),
        CLASSY_TEST(recreateExpiredClientForSameAddress),
        CLASSY_TEST(releaseSharedClientAfterLastReference),
    },
};

} // namespace

} // namespace flexisip::tester
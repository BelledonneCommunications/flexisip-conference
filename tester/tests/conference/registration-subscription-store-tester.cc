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

#include "conference/participant-registration-subscription-handler.hh"

#include <memory>
#include <string>

#include "linphone++/chat_room.hh"

#include "conference/registration-subscription.hh"
#include "test-patterns/test.hh"
#include "test-suite.hh"
#include "utils/subscription-test-helper.hh"

using namespace linphone;

namespace flexisip::tester {

namespace {

class RegistrationSubscriptionStoreTester : public RegistrationSubscriptionStore {
public:
	bool subscriptionFound(const std::shared_ptr<RegistrationSubscription>& expectedSubscription,
	                       const std::string& key,
	                       const std::shared_ptr<ChatRoom>& chatRoom) {
		const auto range = mSubscriptions.equal_range(key);
		for (auto it = range.first; it != range.second; it++) {
			if (it->second->getChatRoom() == chatRoom) return it->second == expectedSubscription;
		}

		return expectedSubscription == nullptr;
	}

	size_t getNumberOfSubscriptions() {
		return mSubscriptions.size();
	};
};

void addSubscription() {
	SubscriptionTestHelper helper{};
	helper.subscribeAll();

	RegistrationSubscriptionStoreTester store{};

	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 0);
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey3, helper.mChatRoom2));

	store.addSubscription(helper.mParticipant1, helper.mSubscription1In1);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 1);
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In1, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey3, helper.mChatRoom2));

	store.addSubscription(helper.mParticipant1, helper.mSubscription1In2);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 2);
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In1, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In2, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey3, helper.mChatRoom2));

	store.addSubscription(helper.mParticipant2, helper.mSubscription2In1);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 3);
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In1, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In2, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription2In1, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey3, helper.mChatRoom2));

	store.addSubscription(helper.mParticipant3, helper.mSubscription3In2);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 4);
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In1, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In2, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription2In1, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription3In2, helper.mKey3, helper.mChatRoom2));
}

void findSubscription() {
	SubscriptionTestHelper helper{};
	helper.subscribeAll();

	RegistrationSubscriptionStoreTester store{};

	store.addSubscription(helper.mParticipant1, helper.mSubscription1In1);
	store.addSubscription(helper.mParticipant1, helper.mSubscription1In2);
	store.addSubscription(helper.mParticipant2, helper.mSubscription2In1);
	store.addSubscription(helper.mParticipant3, helper.mSubscription3In2);

	BC_ASSERT_TRUE(store.findSubscription(helper.mChatRoom1, helper.mParticipant1));
	BC_ASSERT_TRUE(store.findSubscription(helper.mChatRoom1, helper.mParticipant2));
	BC_ASSERT_FALSE(store.findSubscription(helper.mChatRoom1, helper.mParticipant3));
	BC_ASSERT_TRUE(store.findSubscription(helper.mChatRoom2, helper.mParticipant1));
	BC_ASSERT_FALSE(store.findSubscription(helper.mChatRoom2, helper.mParticipant2));
	BC_ASSERT_TRUE(store.findSubscription(helper.mChatRoom2, helper.mParticipant3));
}

void removeSubscription() {
	SubscriptionTestHelper helper{};
	helper.subscribeAll();

	RegistrationSubscriptionStoreTester store{};

	store.addSubscription(helper.mParticipant1, helper.mSubscription1In1);
	store.addSubscription(helper.mParticipant1, helper.mSubscription1In2);
	store.addSubscription(helper.mParticipant2, helper.mSubscription2In1);
	store.addSubscription(helper.mParticipant3, helper.mSubscription3In2);

	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 4);
	BC_ASSERT_TRUE(helper.mSubscription1In1->mState == RegistrationSubscriptionState::None);
	BC_ASSERT_TRUE(helper.mSubscription1In2->mState == RegistrationSubscriptionState::None);
	BC_ASSERT_TRUE(helper.mSubscription2In1->mState == RegistrationSubscriptionState::None);
	BC_ASSERT_TRUE(helper.mSubscription3In2->mState == RegistrationSubscriptionState::None);

	store.removeSubscription(helper.mChatRoom1, helper.mParticipant1);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 3);
	BC_ASSERT_TRUE(helper.mSubscription1In1->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription1In2, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription2In1, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription3In2, helper.mKey3, helper.mChatRoom2));

	store.removeSubscription(helper.mChatRoom2, helper.mParticipant1);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 2);
	BC_ASSERT_TRUE(helper.mSubscription1In2->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription2In1, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription3In2, helper.mKey3, helper.mChatRoom2));

	store.removeSubscription(helper.mChatRoom1, helper.mParticipant2);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 1);
	BC_ASSERT_TRUE(helper.mSubscription2In1->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(helper.mSubscription3In2, helper.mKey3, helper.mChatRoom2));

	store.removeSubscription(helper.mChatRoom2, helper.mParticipant3);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 0);
	BC_ASSERT_TRUE(helper.mSubscription3In2->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey1, helper.mChatRoom2));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey2, helper.mChatRoom1));
	BC_ASSERT(store.subscriptionFound(nullptr, helper.mKey3, helper.mChatRoom2));
}

void removeAllSubscriptions() {
	SubscriptionTestHelper helper{};
	helper.subscribeAll();

	RegistrationSubscriptionStoreTester store{};

	store.addSubscription(helper.mParticipant1, helper.mSubscription1In1);
	store.addSubscription(helper.mParticipant1, helper.mSubscription1In2);
	store.addSubscription(helper.mParticipant2, helper.mSubscription2In1);
	store.addSubscription(helper.mParticipant3, helper.mSubscription3In2);

	BC_ASSERT_TRUE(helper.mSubscription1In1->mState == RegistrationSubscriptionState::None);
	BC_ASSERT_TRUE(helper.mSubscription1In2->mState == RegistrationSubscriptionState::None);
	BC_ASSERT_TRUE(helper.mSubscription2In1->mState == RegistrationSubscriptionState::None);
	BC_ASSERT_TRUE(helper.mSubscription3In2->mState == RegistrationSubscriptionState::None);

	store.removeAllSubscriptions();
	BC_ASSERT_TRUE(helper.mSubscription1In1->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT_TRUE(helper.mSubscription1In2->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT_TRUE(helper.mSubscription2In1->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT_TRUE(helper.mSubscription3In2->mState == RegistrationSubscriptionState::Stopped);
	BC_ASSERT_CPP_EQUAL(store.getNumberOfSubscriptions(), 0);
}

TestSuite _{
    "RegistrationSubscriptionStore",
    {
        CLASSY_TEST(addSubscription),
        CLASSY_TEST(removeSubscription),
        CLASSY_TEST(removeAllSubscriptions),
        CLASSY_TEST(findSubscription),
    },
};

} // namespace

} // namespace flexisip::tester
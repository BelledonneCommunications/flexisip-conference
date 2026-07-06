/*
    Flexisip, a flexible SIP proxy server with media capabilities.
    Copyright (C) 2010-2025 Belledonne Communications SARL, All rights reserved.

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

#include "participant-registration-subscription-handler.hh"

#include "conference/conference-server.hh"
#include "registration-subscription-factory.hh"

using namespace std;
using namespace linphone;

namespace flexisip {

namespace {

string getSubscriptionKey(const shared_ptr<const Address>& address) {
	return address->getUsername() + "@" + address->getDomain();
}

} // namespace

void RegistrationSubscriptionStore::addSubscription(const shared_ptr<const Address>& address,
                                                    const shared_ptr<RegistrationSubscription>& subscription) {
	mSubscriptions.insert({getSubscriptionKey(address), subscription});
};

bool RegistrationSubscriptionStore::findSubscription(const shared_ptr<ChatRoom>& chatRoom,
                                                     const shared_ptr<const Address>& address) {
	const auto range = mSubscriptions.equal_range(getSubscriptionKey(address));
	for (auto it = range.first; it != range.second; it++) {
		if (it->second->getChatRoom() == chatRoom) return true;
	}

	return false;
};

void RegistrationSubscriptionStore::removeSubscription(const shared_ptr<linphone::ChatRoom>& chatRoom,
                                                       const shared_ptr<const linphone::Address>& address) {
	const auto range = mSubscriptions.equal_range(getSubscriptionKey(address));
	for (auto it = range.first; it != range.second;) {
		if (it->second->getChatRoom() == chatRoom) {
			it->second->stop();
			it = mSubscriptions.erase(it);
		} else {
			it++;
		}
	}
};

void RegistrationSubscriptionStore::removeAllSubscriptions() {
	for (const auto& sub : mSubscriptions) {
		sub.second->stop();
	}
	mSubscriptions.clear();
}

ParticipantRegistrationSubscriptionHandler::ParticipantRegistrationSubscriptionHandler(const ConferenceServer& server,
                                                                                       RegistrarDb& registrarDb)
    : mServer(server), mRegistrarDb{registrarDb}, mOwnFactory{make_shared<OwnRegistrationSubscriptionFactory>()},
      mExternalFactory{make_shared<ExternalRegistrationSubscriptionFactory>()} {}

void ParticipantRegistrationSubscriptionHandler::startSubscription(
    const shared_ptr<RegistrationSubscriptionFactory>& factory,
    const shared_ptr<ChatRoom>& chatRoom,
    const shared_ptr<const Address>& address) {
	const auto subscription = factory->create(mServer, mRegistrarDb, chatRoom, address);
	mSubscriptionStore.addSubscription(address, subscription);
	subscription->start();
}

void ParticipantRegistrationSubscriptionHandler::subscribe(const shared_ptr<ChatRoom>& chatRoom,
                                                           const shared_ptr<const Address>& address) {
	LOGD << "Address '" << address->asString() << "' and chatroom '" << chatRoom->getSubject() << "'";

	if (!mSubscriptionStore.findSubscription(chatRoom, address)) {
		const auto& domains = mServer.getLocalDomains();

		if (find(domains.begin(), domains.end(), address->getDomain()) != domains.end()) {
			startSubscription(mOwnFactory, chatRoom, address);
		} else {
			startSubscription(mExternalFactory, chatRoom, address);
		}
	}
}

void ParticipantRegistrationSubscriptionHandler::unsubscribe(const shared_ptr<ChatRoom>& chatRoom,
                                                             const shared_ptr<const Address>& address) {
	mSubscriptionStore.removeSubscription(chatRoom, address);
}

void ParticipantRegistrationSubscriptionHandler::unsubscribeAll() {
	mSubscriptionStore.removeAllSubscriptions();
}

} // namespace flexisip
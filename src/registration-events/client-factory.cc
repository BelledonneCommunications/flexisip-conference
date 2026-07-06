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

#include "client-factory.hh"

#include <exception>
#include <memory>

#include "linphone++/linphone.hh"

#include "client.hh"
#include "flexisip/logmanager.hh"

using namespace std;
using namespace linphone;

namespace flexisip::registration_event {

void ClientFactory::onSubscriptionStateChanged(const shared_ptr<linphone::Core>&,
                                               const shared_ptr<linphone::Event>& linphoneEvent,
                                               linphone::SubscriptionState state) {
	try {
		auto& client = linphoneEvent->getData<Client>(Client::kEventKey);
		client.onSubscriptionStateChanged(state);
	} catch (const out_of_range&) {
		LOGD << "Client disconnected";
	} catch (const exception& exception) {
		LOGD << "Caught an unexpected exception on subscription state change:" << exception.what();
	}
}

void ClientFactory::onNotifyReceived(const shared_ptr<Core>&,
                                     const shared_ptr<linphone::Event>& lev,
                                     const string&,
                                     const shared_ptr<const Content>& body) {
	try {
		auto& client = lev->getData<Client>(Client::kEventKey);
		client.onNotifyReceived(body);
	} catch (const out_of_range&) {
		LOGD << "Client disconnected";
	} catch (const exception& exception) {
		LOGD << "Caught an unexpected exception on NOTIFY request receipt:" << exception.what();
	}
}

void ClientFactory::registerClient(Client&) {
	if (mUseCount == 0) {
		mCore->addListener(shared_from_this());
	}
	mUseCount++;
}

void ClientFactory::unregisterClient(Client&) {
	mUseCount--;
	if (mUseCount == 0) {
		mCore->removeListener(shared_from_this());
	}
}

ClientFactory::ClientFactory(const shared_ptr<linphone::Core>& core, const chrono::seconds& subscriptionRefreshDelay)
    : mCore(core), mSubscriptionRefreshDelay(subscriptionRefreshDelay) {}

shared_ptr<Client> ClientFactory::create(const shared_ptr<const linphone::Address>& to) {
	return make_shared<Client>(shared_from_this(), to);
}

chrono::seconds ClientFactory::getSubscriptionRefreshDelay() const {
	return mSubscriptionRefreshDelay;
}

} // namespace flexisip::registration_event
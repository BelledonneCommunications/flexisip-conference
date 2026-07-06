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

#include "registration-subscription-factory.hh"

#include "conference-server.hh"

using namespace std;
using namespace linphone;

namespace flexisip {

std::shared_ptr<RegistrationSubscription>
OwnRegistrationSubscriptionFactory::create(const ConferenceServer& server,
                                           RegistrarDb& registrarDb,
                                           const std::shared_ptr<linphone::ChatRoom>& chatRoom,
                                           const std::shared_ptr<const linphone::Address>& address) {
	return make_shared<OwnRegistrationSubscription>(server, chatRoom, address, registrarDb);
}

std::shared_ptr<RegistrationSubscription>
ExternalRegistrationSubscriptionFactory::create(const ConferenceServer& server,
                                                RegistrarDb&,
                                                const std::shared_ptr<linphone::ChatRoom>& chatRoom,
                                                const std::shared_ptr<const linphone::Address>& address) {
	const auto client = mClientsStore->acquireClient(address, server.getRegEventClientFactory());
	return make_shared<ExternalRegistrationSubscription>(server, chatRoom, address, client);
}

std::shared_ptr<registration_event::Client> ExternalRegistrationSubscriptionClientsStore::acquireClient(
    const std::shared_ptr<const Address>& address,
    const std::shared_ptr<registration_event::ClientFactory>& clientFactory) {

	const auto uri = address->asStringUriOnlyOrdered();
	const auto it = mClients.find(uri);
	if (it != mClients.end()) {
		if (auto existingClient = it->second.lock()) {
			return existingClient;
		}
		mClients.erase(it);
	}

	const auto newClient = make_shared<registration_event::Client>(clientFactory, address);
	mClients[uri] = newClient;
	return newClient;
}

} // namespace flexisip
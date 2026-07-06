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

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "linphone++/linphone.hh"

#include "registration-events/client.hh"
#include "registration-subscription.hh"

namespace flexisip {

class RegistrationSubscriptionFactory {
public:
	virtual std::shared_ptr<RegistrationSubscription>
	create(const ConferenceServer& server,
	       RegistrarDb& registrarDb,
	       const std::shared_ptr<linphone::ChatRoom>& chatRoom,
	       const std::shared_ptr<const linphone::Address>& address) = 0;

	virtual ~RegistrationSubscriptionFactory(){};
};

class OwnRegistrationSubscriptionFactory : public RegistrationSubscriptionFactory {
public:
	std::shared_ptr<RegistrationSubscription> create(const ConferenceServer& server,
	                                                 RegistrarDb& registrarDb,
	                                                 const std::shared_ptr<linphone::ChatRoom>& chatRoom,
	                                                 const std::shared_ptr<const linphone::Address>& address) override;
};

class ExternalRegistrationSubscriptionClientsStore {
public:
	/**
	 * @returns a client for the given address, creating it if it does not exist yet.
	 */
	std::shared_ptr<registration_event::Client>
	acquireClient(const std::shared_ptr<const linphone::Address>& address,
	              const std::shared_ptr<registration_event::ClientFactory>& clientFactory);

protected:
	/**
	 * We made a choice: this map is not cleaned up at all for now.
	 * Of course, it may grow indefinitely, but we assume that the number of external domains is limited and that the
	 * number of participants per domain is also limited.
	 */
	std::unordered_map<std::string, std::weak_ptr<registration_event::Client>> mClients{};
};

class ExternalRegistrationSubscriptionFactory : public RegistrationSubscriptionFactory {
public:
	std::shared_ptr<RegistrationSubscription> create(const ConferenceServer& server,
	                                                 RegistrarDb& registrarDb,
	                                                 const std::shared_ptr<linphone::ChatRoom>& chatRoom,
	                                                 const std::shared_ptr<const linphone::Address>& address) override;

private:
	std::shared_ptr<ExternalRegistrationSubscriptionClientsStore> mClientsStore{
	    std::make_shared<ExternalRegistrationSubscriptionClientsStore>()};
};

} // namespace flexisip
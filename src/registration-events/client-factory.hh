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

#include <chrono>
#include <memory>

#include "linphone++/linphone.hh"

namespace flexisip::registration_event {

class Client;

/*
 * Helper class to create client 'reg' subscriptions.
 * It must be alive as long as there are Client instantiated, otherwise the clients won't receive any notify anymore.
 * And it must be held by a shared_ptr.
 * Its main purpose is to centralize the linphone::Event callbacks; that are attached to the Core, and dispatch them to
 * the Clients.
 */
class ClientFactory : public std::enable_shared_from_this<ClientFactory>, public linphone::CoreListener {
	friend class Client;

public:
	ClientFactory(const std::shared_ptr<linphone::Core>& core, const std::chrono::seconds& subscriptionRefreshDelay);
	std::shared_ptr<Client> create(const std::shared_ptr<const linphone::Address>& to);

	std::chrono::seconds getSubscriptionRefreshDelay() const;

private:
	static constexpr std::string_view mLogPrefix{"ClientFactory"};

	void onNotifyReceived(const std::shared_ptr<linphone::Core>& lc,
	                      const std::shared_ptr<linphone::Event>& lev,
	                      const std::string& notifiedEvent,
	                      const std::shared_ptr<const linphone::Content>& body) override;

	void onSubscriptionStateChanged(const std::shared_ptr<linphone::Core>& core,
	                                const std::shared_ptr<linphone::Event>& linphoneEvent,
	                                linphone::SubscriptionState state) override;

	void registerClient(Client& client);

	void unregisterClient(Client& client);

	std::shared_ptr<linphone::Core> getCore() const {
		return mCore;
	}

	std::shared_ptr<linphone::Core> mCore{};
	std::chrono::seconds mSubscriptionRefreshDelay{};
	int mUseCount{};
};

} // namespace flexisip::registration_event
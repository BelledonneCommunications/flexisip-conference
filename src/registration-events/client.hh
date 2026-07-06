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

#include "linphone++/linphone.hh"

#include "client-factory.hh"
#include "client-listener.hh"
#include "utils/observable.hh"

namespace flexisip::registration_event {

/**
 * Base class for a 'reg' event package client (RFC 3680).
 *
 * @note Only manages subscription to one AOR at a time. If you need to subscribe to multiple AORs, create multiple
 * instances of this class.
 */
class Client : public flexisip::Observable<ClientListener> {
	friend class ClientFactory;

public:
	Client(const std::shared_ptr<ClientFactory>& factory, const std::shared_ptr<const linphone::Address>& to);
	~Client();

	/**
	 * Subscribe to registration events.
	 * @note If already subscribed, the listener will be notified of the current state immediately.
	 */
	void subscribe(const std::shared_ptr<ClientListener>& listener);
	void unsubscribe();

private:
	static constexpr auto* kEventKey{"Regevent::Client"};

	void onNotifyReceived(const std::shared_ptr<const linphone::Content>& body);
	void onSubscriptionStateChanged(linphone::SubscriptionState state);

	std::shared_ptr<linphone::Event> mSubscribeEvent{};
	std::shared_ptr<ClientFactory> mFactory{};
	std::shared_ptr<linphone::Address> mTo{};
	std::string mLogPrefix{};
	std::list<std::shared_ptr<linphone::ParticipantDeviceIdentity>> mParticipantDevices{};
};

} // namespace flexisip::registration_event
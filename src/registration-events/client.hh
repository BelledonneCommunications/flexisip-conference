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

namespace flexisip::registration_event {

/**
 * Base class for a 'reg' event client.
 * It has to be inherited to get notified of the results of the subscription (the incoming NOTIFY request content).
 */
class Client {
public:
	~Client();

	void subscribe();
	void unsubscribe();
	void setListener(ClientListener* listener);

protected:
	Client(const std::shared_ptr<ClientFactory>& factory, const std::shared_ptr<const linphone::Address>& to);

private:
	friend class ClientFactory;

	static constexpr auto* kEventKey{"Regevent::Client"};

	void onNotifyReceived(const std::shared_ptr<const linphone::Content>& body);
	void onSubscriptionStateChanged(linphone::SubscriptionState state);

	std::shared_ptr<linphone::Event> mSubscribeEvent;
	std::shared_ptr<ClientFactory> mFactory;
	std::shared_ptr<linphone::Address> mTo;
	ClientListener* mListener;
	std::string mLogPrefix;
};

} // namespace flexisip::registration_event
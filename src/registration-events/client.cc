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

#include "client.hh"

#include <sstream>
#include <stdexcept>

#include "linphone++/linphone.hh"

#include "flexisip/logmanager.hh"
#include "utils/string-utils.hh"
#include "xml/reginfo.hh"

using namespace std;
using namespace linphone;
using namespace reginfo;

namespace flexisip::registration_event {

Client::Client(const shared_ptr<ClientFactory>& factory, const shared_ptr<const Address>& to)
    : mFactory(factory), mTo(to->clone()),
      mLogPrefix(LogManager::makeLogPrefixForInstance(this, "registration_event::Client")) {
	mFactory->registerClient(*this);
}

void Client::subscribe(const shared_ptr<ClientListener>& listener) {
	LOGD << "Subscribing to registration events for '" << mTo->asStringUriOnly() << "'";
	if (mSubscribeEvent) {
		LOGD << "Already subscribed to registration events: notifying listener of the current state";
		// For performance reasons, only notify the new listener, not all listeners.
		listener->onNotifyReceived(mParticipantDevices);
		return;
	}

	mSubscribeEvent = mFactory->getCore()->createSubscribe(mTo, "reg", mFactory->getSubscriptionRefreshDelay().count());
	mSubscribeEvent->addCustomHeader("Accept", "application/reginfo+xml");
	mSubscribeEvent->setData(kEventKey, *this);
	mSubscribeEvent->sendSubscribe(nullptr);
}

void Client::unsubscribe() {
	LOGD << "Unsubscribing from registration events for '" << mTo->asStringUriOnly() << "'";
	if (!mSubscribeEvent) {
		LOGD << "No existing subscription to registration events";
		return;
	}

	mSubscribeEvent->unsetData(kEventKey);
	mSubscribeEvent->terminate();
	mSubscribeEvent = nullptr;
}

Client::~Client() {
	LOGD << "Destroying client for '" << mTo->asStringUriOnly() << "'";
	mFactory->unregisterClient(*this);
	unsubscribe();
}

void Client::onNotifyReceived(const shared_ptr<const linphone::Content>& body) {
	LOGD << "Received NOTIFY for '" << mTo->asStringUriOnly() << "'";
	if (!body) throw runtime_error("Empty notify Content.");

	istringstream data(body->getUtf8Text());
	unique_ptr<Reginfo> ri(parseReginfo(data, Xsd::XmlSchema::Flags::dont_validate));

	// Iterate through all registrations in the reginfo document, and find the one that matches the AOR we are
	// subscribed to. As per RFC 3680 (§5.1), there may be multiple registrations in the document, but this client only
	// manages one AOR at a time.
	for (const auto& registration : ri->getRegistration()) {
		if (registration.getAor() != mTo->asStringUriOnly()) continue;

		if (registration.getState() == Registration::StateType::terminated) {
			// Notifying that 0 devices are registered.
			notify([](ClientListener& listener) { listener.onNotifyReceived({}); });
			mParticipantDevices.clear();
			break;
		}

		size_t refreshed{};
		list<shared_ptr<ParticipantDeviceIdentity>> participantDevices{};
		for (const auto& contact : registration.getContact()) {
			const auto partDeviceAddr = Factory::get()->createAddress(contact.getUri());
			const auto& unknownParams = contact.getUnknownParam();
			const auto displayName = contact.getDisplayName() ? contact.getDisplayName()->c_str() : string("");

			for (const auto& param : unknownParams) {
				if (param.getName() != "+org.linphone.specs") continue;

				const auto identity = Factory::get()->createParticipantDeviceIdentity(partDeviceAddr, displayName);
				identity->setCapabilityDescriptor(list<string>{StringUtils::unquote(param)});

				if (contact.getEvent() == reginfo::Event::refreshed) {
					notify([&identity](ClientListener& listener) { listener.onRefreshed(identity); });
					refreshed++;
				}

				participantDevices.push_back(identity);
				break;
			}
		}

		if (refreshed < participantDevices.size()) {
			notify([&participantDevices](ClientListener& listener) { listener.onNotifyReceived(participantDevices); });
		} // else: Everything is refreshed, notifying a reception would be redundant.

		mParticipantDevices = participantDevices;
	}
}

void Client::onSubscriptionStateChanged(linphone::SubscriptionState state) {
	LOGD << "Subscription state changed to '" << static_cast<int>(state) << "' for '" << mTo->asStringUriOnly() << "'";

	switch (state) {
		case SubscriptionState::None:
		case SubscriptionState::OutgoingProgress:
		case SubscriptionState::IncomingReceived:
		case SubscriptionState::Pending:
		case SubscriptionState::Active:
		case SubscriptionState::Expiring:
			break;
		case SubscriptionState::Terminated:
		case SubscriptionState::Error:
			mSubscribeEvent->unsetData(kEventKey);
			mSubscribeEvent->terminate();
			mSubscribeEvent = nullptr;
			mParticipantDevices.clear();
			/* TODO: retry later*/
			break;
	}
}

} // namespace flexisip::registration_event
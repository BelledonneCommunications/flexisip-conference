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

namespace flexisip::registration_event {

class ClientListener {
public:
	/**
	 * This is where the parsing result of the incoming NOTIFY are notified.
	 * The ParticipantDeviceIdentity object is convenient to represent the device information returned by the reg event
	 * package.
	 */
	virtual void
	onNotifyReceived(const std::list<std::shared_ptr<linphone::ParticipantDeviceIdentity>>& participantDevices) = 0;

	virtual void onRefreshed(const std::shared_ptr<linphone::ParticipantDeviceIdentity>& participantDevice) = 0;
};

} // namespace flexisip::registration_event
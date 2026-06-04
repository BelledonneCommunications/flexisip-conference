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

#include "conference-builder.hh"

#include "call-assert.hh"
#include "client-core.hh"

using namespace linphone;

namespace flexisip::tester {

ConferenceBuilder::ConferenceBuilder(const CoreClient& client)
    : mClient(client), mConferenceInfo(Factory::get()->createConferenceInfo()) {
	mConferenceScheduler = mClient.getCore()->createConferenceScheduler(mClient.getAccount());

	mConferenceInfo->setOrganizer(mClient.getMe());
	mConferenceInfo->addParticipant(mClient.getMe());
	mConferenceInfo->setDateTime(getCurrentTime());
}

ConferenceBuilder& ConferenceBuilder::setOrganizer(const std::shared_ptr<const Address>& organizer) {
	mConferenceInfo->setOrganizer(organizer);
	return *this;
}

ConferenceBuilder& ConferenceBuilder::setDateTime(time_t dateTime) {
	mConferenceInfo->setDateTime(dateTime);
	return *this;
}

ConferenceBuilder& ConferenceBuilder::setDuration(unsigned int duration) {
	mConferenceInfo->setDuration(duration);
	return *this;
}

ConferenceBuilder& ConferenceBuilder::setSubject(const std::string& subject) {
	mConferenceInfo->setSubject(subject);
	return *this;
}

ConferenceBuilder& ConferenceBuilder::setDescription(const std::string& description) {
	mConferenceInfo->setDescription(description);
	return *this;
}

ConferenceBuilder& ConferenceBuilder::setSecurityLevel(Conference::SecurityLevel securityLevel) {
	mConferenceInfo->setSecurityLevel(securityLevel);
	return *this;
}

ConferenceBuilder& ConferenceBuilder::setCapability(StreamType streamType, bool enable) {
	mConferenceInfo->setCapability(streamType, enable);
	return *this;
}

void ConferenceBuilder::ConferenceBuilderListener::onStateChanged(const std::shared_ptr<ConferenceScheduler>&,
                                                                  ConferenceScheduler::State state) {
	mConferenceBuilder.mState = state;
}

} // namespace flexisip::tester
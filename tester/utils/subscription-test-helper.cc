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

#include "subscription-test-helper.hh"

#include "client-core.hh"

using namespace linphone;

namespace flexisip::tester {

void ConferenceTestHelper::startMinimalCore() {
	mCore = minimalCore();
	mCore->setLabel("Flexisip/Conference Tests");
	mCore->enableConferenceServer(true);
	mCore->enableDatabase(false);

	const auto factory = Factory::get();
	const auto localhostAddress = factory->createAddress("sip:localhost");
	mAccountParams = mCore->createAccountParams();
	mAccountParams->setIdentityAddress(factory->createAddress("sip:flexisip-test@localhost"));
	mAccountParams->enableRegister(false);
	mAccountParams->setServerAddress(localhostAddress);

	const auto account = mCore->createAccount(mAccountParams);
	account->setContactAddress(localhostAddress);

	mCore->addAccount(account);
	mCore->setDefaultAccount(account);
	mCore->start();
}

std::shared_ptr<linphone::Conference> ConferenceTestHelper::createConference(
    ChatRoom::EncryptionBackend encryptionBackend, ChatRoom::EphemeralMode ephemeralMode, const std::string& subject) {
	mConferenceParams = mCore->createConferenceParams(nullptr);
	mConferenceParams->setHidden(true);
	mConferenceParams->enableVideo(false);
	mConferenceParams->enableChat(true);
	mConferenceParams->enableLocalParticipant(false);
	mConferenceParams->enableOneParticipantConference(true);
	mConferenceParams->setConferenceFactoryAddress(nullptr);
	mConferenceParams->setSubject(subject);

	mChatParams = mConferenceParams->getChatParams();
	mChatParams->setEncryptionBackend(encryptionBackend);
	mChatParams->setEphemeralMode(ephemeralMode);

	return mCore->createConferenceWithParams(mConferenceParams);
}

} // namespace flexisip::tester
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

#include <initializer_list>
#include <memory>

#include "linphone++/conference_info.hh"

#include "client-builder.hh"
#include "core-assert.hh"

namespace flexisip::tester {

class CoreClient;

class ConferenceBuilder {
public:
	explicit ConferenceBuilder(const CoreClient& client);

	ConferenceBuilder& setOrganizer(const std::shared_ptr<const linphone::Address>& organizer);
	ConferenceBuilder& setDateTime(time_t dateTime);
	ConferenceBuilder& setDuration(unsigned int duration);
	ConferenceBuilder& setSubject(const std::string& subject);
	ConferenceBuilder& setDescription(const std::string& description);
	ConferenceBuilder& setSecurityLevel(linphone::Conference::SecurityLevel securityLevel);
	ConferenceBuilder& setCapability(linphone::StreamType streamType, bool enable);

	std::shared_ptr<const linphone::Address>
	build(CoreAssert<>& asserter,
	      const std::initializer_list<std::shared_ptr<const linphone::Address>>& participants = {}) {
		mListener = std::make_shared<ConferenceBuilderListener>(*this);
		mConferenceScheduler->addListener(mListener);

		for (const auto& participant : participants) {
			mConferenceInfo->addParticipant(participant);
		}

		mConferenceScheduler->setInfo(mConferenceInfo);

		asserter
		    .wait([this]() {
			    FAIL_IF(mState != linphone::ConferenceScheduler::State::Ready);
			    return ASSERTION_PASSED();
		    })
		    .hard_assert_passed();

		return mConferenceScheduler->getInfo()->getUri();
	}

private:
	class ConferenceBuilderListener : public linphone::ConferenceSchedulerListener {
	public:
		explicit ConferenceBuilderListener(ConferenceBuilder& conferenceBuilder)
		    : mConferenceBuilder(conferenceBuilder) {}

		void onStateChanged(const std::shared_ptr<linphone::ConferenceScheduler>&,
		                    linphone::ConferenceScheduler::State state) override;

		ConferenceBuilder& mConferenceBuilder;
	};

	const CoreClient& mClient;

	const std::shared_ptr<linphone::ConferenceInfo> mConferenceInfo;
	std::shared_ptr<linphone::ConferenceScheduler> mConferenceScheduler;
	std::shared_ptr<ConferenceBuilderListener> mListener;

	linphone::ConferenceScheduler::State mState{};
};

} // namespace flexisip::tester

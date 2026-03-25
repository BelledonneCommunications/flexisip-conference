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

#include <csignal> // For macOS
#include <fstream>
#include <sys/wait.h>

#include "bctoolbox/tester.h"
#include "lib/nlohmann-json-3-11-2/json.hpp"

#include "bc-utils.hh"
#include "cli-helper.hh"
#include "flexisip/logmanager.hh"
#include "main/flexisip-conference.hh"
#include "test-patterns/test.hh"
#include "test-suite.hh"
#include "tmp-dir.hh"

using namespace std;

namespace flexisip::tester {

constexpr auto step = 100ms;

class ProcessusHandler {
public:
	explicit ProcessusHandler(pid_t pid) : mPid(pid){};

	~ProcessusHandler() {
		kill();
	};

	void kill() {
		if (!mIsTerminated) {
			::kill(mPid, SIGKILL);
			mIsTerminated = true;
		}
	}

	void exitGracefully() {
		int status = 0;
		pid_t pid;
		// Ensure clean exit
		for (auto _ = 0ms; _ < 2s; _ += step) {
			pid = ::waitpid(mPid, &status, WNOHANG);
			if (pid > 0 && WIFEXITED(status)) {
				break;
			}
			this_thread::sleep_for(step);
		}
		BC_ASSERT_GREATER_STRICT(pid, 0, int, "%d");
		if (pid > 0) {
			BC_ASSERT_TRUE(WIFEXITED(status));
			BC_ASSERT_CPP_EQUAL(WEXITSTATUS(status), 0);
			mIsTerminated = true;
		} else {
			// Force to exit to avoid any leftover process
			kill();
		}
	}

private:
	pid_t mPid;
	bool mIsTerminated{};
};

nlohmann::json deserialize(const string& json) {
	try {
		return nlohmann::json::parse(json);
	} catch (const std::exception& exception) {
		bc_assert(__FILE__, __LINE__, false, json.c_str());
		BC_HARD_FAIL(exception.what());
		return {};
	}
}

/**
 * Test the main function of flexisip-conference server
 *
 * Start a flexisip conference server with all service types activated then check that all service are properly initialized.
 * Stop the server and check that the program exit cleanly.
 *
 */
void callAndStopMain() {
	auto dir = TmpDir(__func__);
	auto confFilePath = dir.path() / "flexisip-conference.conf";
	ofstream(confFilePath) << "[global]" << '\n'
	                       << "enable-snmp=true" << '\n'

	                       << "[conference-server]" << '\n'
	                       << "database-backend=sqlite3" << '\n'
	                       << "database-connection-string=\":memory:\"" << '\n';

	vector<const char*> args{
	    "flexisip-conference",
	    "-c",
	    confFilePath.c_str(),
	};

	auto childPid = ::fork();
	auto childHandler = ProcessusHandler(childPid);

	// Child process: execute main and exit.
	if (childPid == 0) {

		// Protect with try/catch to ensure the process is ended with "_exit"
		// if not, it could destruct objects of the parent process.
		int returnValue = EXIT_FAILURE;
		try {
			returnValue = flexisip_conference::main(args.size(), args.data());
		} catch (const exception& e) {
			SLOGE << "Unexpected exception while running main: " << e.what();
		}
		::_exit(returnValue);
	}

	// Short wait to ensure that main loop starts.
	this_thread::sleep_for(1s);

	// Stop flexisip execution
	BC_HARD_ASSERT_CPP_EQUAL(::kill(childPid, SIGINT), 0);
	childHandler.exitGracefully();
}

namespace {

TestSuite _{
    "mainTester",
    {
        CLASSY_TEST(callAndStopMain),
    },
};

} // namespace
} // namespace flexisip::tester
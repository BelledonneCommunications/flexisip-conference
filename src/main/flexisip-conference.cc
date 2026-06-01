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

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <regex>

#ifdef ENABLE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <openssl/crypto.h>
#include <openssl/opensslconf.h>
#if defined(OPENSSL_THREADS)
// thread support enabled
#else
// no thread support
#error "No thread support in openssl"
#endif

#include <ortp/ortp.h>

#include <sofia-sip/msg.h>
#include <sofia-sip/sofia_features.h>
#include <sofia-sip/su_log.h>

#include <tclap/CmdLine.h>

#include <flexisip-conference-config.h>
#include <flexisip-conference/flexisip-conference-version.h>
#include <flexisip/logmanager.hh>
#include <flexisip/module.hh>
#include <flexisip/sofia-wrapper/su-root.hh>

#ifndef CONFIG_DIR
#define CONFIG_DIR
#endif
#ifndef FLEXISIP_CONFERENCE_GIT_VERSION
#define FLEXISIP_CONFERENCE_GIT_VERSION "undefined"
#endif

#include "agent.hh"
#include "auth/db/authdb.hh"
#include "configdumper.hh"
#include "etchosts.hh"
#include "exceptions/bad-configuration.hh"
#include "exceptions/exit.hh"
#include "registrar/registrar-db.hh"
#include "stun.hh"

#include "conference/conference-server.hh"

#if ENABLE_FLEXIAPI
#include "flexiapi/config.hh"
#endif

#ifdef ENABLE_SNMP
#include "snmp/snmp-agent.hh"
#endif

#include "flexisip-conference.hh"

#include "flexisip/configmanager.hh"
#include "utils/pipe.hh"
#include "utils/process-monitoring/memory-watcher.hh"
#include "utils/transport/http/http2client.hh"

using namespace std;
using namespace flexisip_conference;
using namespace flexisip;

static int run = 1;
static shared_ptr<sofiasip::SuRoot> root{};
static constexpr string_view kLogPrefix{"Flexisip-conference"};

/*
 * Get the identifier of the current thread.
 */
unsigned long threadid_cb() {
	return (unsigned long)pthread_self();
}

void locking_function(int mode, int n, const char*, int) {
	static mutex* mutextab = NULL;
	if (mutextab == NULL) mutextab = new mutex[CRYPTO_num_locks()];
	if (mode & CRYPTO_LOCK) mutextab[n].lock();
	else mutextab[n].unlock();
}

static void setOpenSSLThreadSafe() {
	CRYPTO_set_id_callback(&threadid_cb);
	CRYPTO_set_locking_callback(&locking_function);
}

static void flexisip_stop(int) {
	if (run != 0) {
		run = 0;
		if (root) root->quit();
	}
}

static void flexisip_reopen_log_files(int) {
	LogManager::get().reopenFiles();
}

static void sofiaLogHandler(void*, const char* fmt, va_list ap) {
	// remove final \n from sofia
	if (fmt) {
		char* copy = strdup(fmt);
		copy[strlen(copy) - 1] = '\0';
		LOGDV(copy, ap);
		free(copy);
	}
}

static map<msg_t*, string> msg_map;

static void dump_remaining_msgs() {
	LOGD_CTX(kLogPrefix) << "### Remaining messages: " << msg_map.size();
	for (auto it = msg_map.begin(); it != msg_map.end(); ++it) {
		LOGD_CTX(kLogPrefix) << "### \t- " << it->first << "\n";
	}
}

static rlim_t getSystemFdLimit() {
#ifdef __linux
	static rlim_t maxSysFd = 0;
	constexpr auto systemLimitGetError = "unable to get system limit for number of open file descriptors";

	if (maxSysFd == 0) {
		auto f = fstream{};
		f.exceptions(ios::badbit | ios::failbit | ios::eofbit);

		constexpr auto fileMaxFilePath = "/proc/sys/fs/file-max";
		try {
			f.open(fileMaxFilePath, ios::in);
			f >> maxSysFd;
			LOGI_CTX(kLogPrefix) << "System wide maximum number of file descriptors is " << maxSysFd;
			f.close();
		} catch (const exception& e) {
			LOGE_CTX(kLogPrefix) << "Cannot read value from " << fileMaxFilePath << ": " << e.what();
			throw runtime_error{systemLimitGetError};
		}

		constexpr auto nrOpenFilePath = "/proc/sys/fs/nr_open";
		try {
			f.open(nrOpenFilePath, ios::in);
			decltype(maxSysFd) value = 0;
			f >> value;
			LOGI_CTX(kLogPrefix) << "System wide maximum number open files is " << value;
			maxSysFd = min(value, maxSysFd);
			f.close();
		} catch (const exception& e) {
			LOGE_CTX(kLogPrefix) << "Cannot read value from " << nrOpenFilePath << ": " << e.what();
		}
	}

	if (maxSysFd == 0) {
		throw runtime_error{systemLimitGetError};
	}

	return maxSysFd;
#else
	LOGW_CTX(kLogPrefix) << "Guessing of system wide fd limit is not implemented.";
	return 2048;
#endif
}

static void increaseFDLimit() noexcept {
	struct rlimit lm{};
	if (getrlimit(RLIMIT_NOFILE, &lm) == -1) {
		LOGE_CTX(kLogPrefix) << "getrlimit(RLIMIT_NOFILE) failed: " << strerror(errno);
		return;
	}

	auto newLimitAsStr = ""s;
	try {
		newLimitAsStr = to_string(getSystemFdLimit());
	} catch (const exception&) {
		newLimitAsStr = "<unknown>";
	}

	LOGI_CTX(kLogPrefix) << "Maximum number of open file descriptors is " << lm.rlim_cur << ", limit=" << lm.rlim_max
	                     << ", system wide limit=" << newLimitAsStr;

	try {
		const auto systemLimit = getSystemFdLimit();
		if (lm.rlim_cur < systemLimit) {
			const auto oldLimit = lm.rlim_cur;
			lm.rlim_cur = lm.rlim_max = systemLimit;
			if (setrlimit(RLIMIT_NOFILE, &lm) == -1) {
				LOGW_CTX(kLogPrefix) << "setrlimit(RLIMIT_NOFILE) failed: " << strerror(errno)
				                     << ", limit of number of file descriptors is low (" << oldLimit << ")";
				LOGW_CTX(kLogPrefix) << "Flexisip will not be able to process a big number of calls";
			}
			if (getrlimit(RLIMIT_NOFILE, &lm) == 0) {
				LOGI_CTX(kLogPrefix) << "Maximum number of file descriptor set to " << lm.rlim_cur;
			}
		}
	} catch (const exception& e) {
		LOGE_CTX(kLogPrefix) << "Error while setting file descriptors limit: " << e.what();
	}
}

static void makePidFile(const string& pidfile) {
	if (!pidfile.empty()) {
		FILE* f = fopen(pidfile.c_str(), "w");
		if (f) {
			fprintf(f, "%i", getpid());
			fclose(f);
		} else {
			LOGE_CTX(kLogPrefix) << "Could not write pid file [" << pidfile << "]";
		}
	}
}

static void depthFirstSearch(string& path, const GenericEntry* config, list<string>& allCompletions) {
	const auto* gStruct = dynamic_cast<const GenericStruct*>(config);
	if (gStruct) {
		string newpath;
		if (!path.empty()) newpath += path + "/";
		if (config->getName() != "flexisip") newpath += config->getName();
		for (auto it = gStruct->getChildren().cbegin(); it != gStruct->getChildren().cend(); ++it) {
			depthFirstSearch(newpath, it->get(), allCompletions);
		}
		return;
	}

	const auto* cValue = dynamic_cast<const ConfigValue*>(config);
	if (cValue) {
		string completion;
		if (!path.empty()) completion += path + "/";
		completion += cValue->getName();
		allCompletions.push_back(completion);
	}
}

static void dump_config(ConfigManager& cfg,
                        const string& dump_cfg_part,
                        bool with_experimental,
                        bool dumpDefault,
                        bool removeDeprecated,
                        const string& format) {
	cfg.applyOverrides(true);

	auto* rootStruct = cfg.getEditableRoot();
	if (dump_cfg_part != "all") {
		smatch m;
		rootStruct = dynamic_cast<GenericStruct*>(rootStruct->find(dump_cfg_part));
		if (rootStruct == nullptr) {
			throw ExitFailure{"couldn't find node " + dump_cfg_part};
		}
		if (regex_match(dump_cfg_part, m, regex("^module::(.*)$"))) {
			const auto& moduleName = m[1];
			auto moduleInfoChain = ModuleInfoManager::get()->buildModuleChain();
			auto moduleIt =
			    find_if(moduleInfoChain.cbegin(), moduleInfoChain.cend(),
			            [&moduleName](const auto& module) { return module->getModuleName() == moduleName; });
			if (moduleIt != moduleInfoChain.cend() && (*moduleIt)->getClass() == ModuleClass::Experimental &&
			    !with_experimental) {
				throw ExitFailure{
				    "module "s + moduleName.str() +
				        " is experimental, not returning anything. To override, specify '--show-experimental'.",
				};
			}
		}
	}

	unique_ptr<ConfigDumper> dumper{};
	if (format == "tex") {
		dumper = make_unique<TexFileConfigDumper>(rootStruct);
	} else if (format == "doku") {
		dumper = make_unique<DokuwikiConfigDumper>(rootStruct);
	} else if (format == "file") {
		auto fileDumper = make_unique<FileConfigDumper>(rootStruct);
		fileDumper->setMode(dumpDefault ? FileConfigDumper::Mode::DefaultValue
		                                : FileConfigDumper::Mode::DefaultIfUnset);
		dumper = std::move(fileDumper);
	} else if (format == "media") {
		dumper = make_unique<MediaWikiConfigDumper>(rootStruct);
	} else if (format == "xwiki") {
		dumper = make_unique<XWikiConfigDumper>(rootStruct);
	} else {
		throw ExitFailure{"invalid output format '" + format + "'"};
	}
	dumper->setDumpExperimentalEnabled(with_experimental);
	dumper->setRemoveDeprecated(removeDeprecated);
	dumper->dump(cout);
}

static void list_sections(ConfigManager& cfg, bool moduleOnly = false) {
	const string modulePrefix{"module::"};
	for (const auto& child : cfg.getRoot()->getChildren()) {
		if (!moduleOnly || child->getName().compare(0, modulePrefix.size(), modulePrefix) == 0) {
			cout << child->getName() << endl;
		}
	}
}

static string version() {
	stringstream version{};
	vector<string_view> options{};
	version << FLEXISIP_CONFERENCE_GIT_VERSION << " (sofia-sip: " << SOFIA_SIP_VERSION << ")";

#if ENABLE_SNMP
	options.emplace_back("SNMP");
#endif
	options.emplace_back("Redis");
	options.emplace_back("Soci");
	options.emplace_back("Conference");

	if (!options.empty()) version << " compiled with " << string_utils::join(options, 0, " - ");
	return version.str();
}

static string getPkcsPassphrase(TCLAP::ValueArg<string>& pkcsFile) {
	string passphrase;
	if (!pkcsFile.getValue().empty()) {
		ifstream dacb(pkcsFile.getValue());
		if (!dacb.is_open()) {
			LOGE_CTX(kLogPrefix) << "Cannot open pkcs passphrase file: " << pkcsFile.getValue();
		} else {
			while (!dacb.eof()) {
				dacb >> passphrase;
			}
		}
	}
	return passphrase;
}

int flexisip_conference::main(int argc, const char* argv[]) {
	int errcode = EXIT_SUCCESS;

	TCLAP::CmdLine cmd("", ' ', version());
	// TCLAP executes exit() when processing ExitException, so deactivate exceptions management.
	cmd.setExceptionHandling(false);

#define DEFAULT_CONFIG_FILE CONFIG_DIR "/flexisip-conference.conf"

	// clang-format off

	TCLAP::ValueArg<string>     configFile("c", "config", 			   "Location of the configuration file."
                                                                       "Default is: " DEFAULT_CONFIG_FILE,
                                                                       TCLAP::ValueArgOptional, DEFAULT_CONFIG_FILE, "file", cmd);
	TCLAP::SwitchArg            disableStdout("",  "disable-stdout", 			   "Do not display logs in the standard output. They are still printed in log files.",
                                                                       cmd);
	TCLAP::SwitchArg              useDebug("d", "debug", 			   "Print logs in debug level to the terminal "
                                                                       "(does not affect the logging level of log files).",
                                                                       cmd);
	TCLAP::ValueArg<string>        pidFile("p", "pidfile", 			   "PID file location (when running in daemon mode).",
                                                                       TCLAP::ValueArgOptional, "", "file", cmd);
	TCLAP::SwitchArg             useSyslog("",  "syslog", 			   "Enable system logs (syslog).",
                                                                       cmd);
	TCLAP::ValueArg<string>  transportsArg("t", "transports", 		   "List of transports to handle (overrides those "
                                                                       "defined in the configuration file).",
                                                                       TCLAP::ValueArgOptional, "", "sips:* sip:*", cmd);
	TCLAP::ValueArg<string>    dumpDefault("",  "dump-default",		   "Dump default configuration in the standard "
                                                                       "output. Use 'all' to dump the configuration of "
                                                                       "all modules, or '<module_name>' to dump the "
                                                                       "configuration of a specific module.",
                                                                       TCLAP::ValueArgOptional, "", "all", cmd);
	TCLAP::SwitchArg               dumpAll("",  "dump-all-default",    "Dump all default configurations in the standard "
                                                                       "output (equivalent to '--dump-default all'). ",
                                                                       cmd);
	TCLAP::ValueArg<string>     dumpFormat("",  "dump-format",		   "Output format of configuration dump "
                                                                       "(default: 'file'). Possible values: 'file', "
                                                                       "'tex', 'doku', 'media', 'xwiki'.",
                                                                       TCLAP::ValueArgOptional, "file", "file", cmd);
	TCLAP::SwitchArg           listModules("",  "list-modules", 	   "Dump the list of available modules in the "
                                                                       "standard output. It can be useful to combine "
                                                                       "with '--dump-default' in order to have specific "
                                                                       "documentation for a module.",
                                                                       cmd);
	TCLAP::SwitchArg           listSections("",  "list-sections", 	   "Dump the list of available sections in the "
                                                                       "standard output. It can be useful to combine "
                                                                       "with '--dump-default' in order to have specific "
                                                                       "documentation for a section.",
                                                                       cmd);
	TCLAP::SwitchArg           rewriteConf("",  "rewrite-config",      "Load the configuration file and dump a new one "
                                                                       "in the standard output adding the new settings "
                                                                       "and updating documentations. All the existing "
                                                                       "settings are preserved even if they are equal "
                                                                       "to the default value and the default value has "
                                                                       "changed.",
                                                                       cmd);
	TCLAP::SwitchArg         removeDeprecated("", "remove-deprecated", "When dumping the configuration, remove all "
                                                                       "deprecated parameters even if they had a value "
                                                                       "explicitly set.",
                                                                       cmd);
	TCLAP::SwitchArg              dumpMibs("",  "dump-mibs", 		   "Dump the MIB files for Flexisip performance "
                                                                       "counters and other related SNMP items in the "
                                                                       "standard output.",
                                                                       cmd);
	TCLAP::ValueArg<string>     pkcsFile("",    "p12-passphrase-file", "Location of the pkcs12 passphrase file.",
                                                                       TCLAP::ValueArgOptional,"", "file", cmd);
	TCLAP::SwitchArg   displayExperimental("",  "show-experimental",   "Dump the configuration of experimental modules in the "
		                                                               "standard output. It MUST be used with"
                                                                       "'--dump-default', '--dump-all-default' or"
                                                                       "'--rewrite-config' options",
                                                                       cmd);
	TCLAP::ValueArg<string>  listOverrides("",  "list-overrides",	   "Dump the list of configuration values that you "
                                                                       "can override. Use 'all' to get all possible "
                                                                       "overrides or '<module_name>' to get all "
                                                                       "possible overrides for a specific module. It "
                                                                       "can be useful to use with '--set'.",
                                                                       TCLAP::ValueArgOptional, "", "module", cmd);
	TCLAP::MultiArg<string> overrideConfig("s", "set", 				   "Allows to override a setting in the "
                                                                       "configuration file. Use --list-overrides to get "
                                                                       "the list of all values that you can override.",
                                                                       TCLAP::ValueArgOptional, "global/debug=true", cmd);
	TCLAP::MultiArg<string>  hostsOverride("",  "hosts",			   "Overrides a host address. You can use this flag "
                                                                       "multiple times. Also, you can remove an "
                                                                       "association by providing an empty "
                                                                       "value: '--hosts myhost='.",
										                               TCLAP::ValueArgOptional, "host=ip", cmd);
	TCLAP::SwitchArg           trackAllocs("",  "track-allocations",   "Track allocations of SIP messages (use with "
                                                                       "caution).",
                                                                       cmd);
	// clang-format on

	// Instantiate the LogManager.
	auto& logger = LogManager::get();

	// Try parsing command line inputs.
	try {
		cmd.parse(argc, argv);
	} catch (TCLAP::ArgException& exception) {
		cmd.getOutput()->failure(cmd, exception);
	}

	// First configuration of the logger using command line arguments.
	logger.configure({
	    .enableStandardOutput = !(disableStdout || rewriteConf),
	    .level = useDebug ? BCTBX_LOG_DEBUG : BCTBX_LOG_WARNING,
	    .enableSyslog = disableStdout,
	    .syslogLevel = BCTBX_LOG_ERROR,
	    .enableUserErrors = useDebug,
	});

	map<string, string> oset;
	for (const string& kv : overrideConfig.getValue()) {
		auto equal = find(kv.cbegin(), kv.cend(), '=');
		if (equal != kv.cend()) {
			oset[string(kv.cbegin(), equal)] = string(equal + 1, kv.cend());
		}
	}

	// Instantiate the sofiasip main loop and set signal callbacks.
	root = make_shared<sofiasip::SuRoot>();
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, flexisip_stop);
	signal(SIGINT, flexisip_stop);
	signal(SIGHUP, flexisip_reopen_log_files);

	// Instantiate the configuration manager.
	auto cfg = make_shared<ConfigManager>();
	cfg->setOverrideMap(oset);

	// Deprecate modules and sections from Flexisip that are not needed by the conference server
	const vector<string> sectionsToDeprecate{
	    "cluster",
	    "event-logs",
	    "mdns-register",
	    "regevent-server",
	    "stun-server",
	    "global::flexiapi",
	    "inter-domain-connections",
	};
	const vector<string> modulesToKeep{"module::Registrar"};

	const string modulePrefix{"module::"};
	for (const auto& section : cfg->getRoot()->getChildren()) {
		if (ranges::find(sectionsToDeprecate, section->getName()) != sectionsToDeprecate.end()) {
			section->setDeprecated("2026-06-01", "1.0.0",
			                       "This section is not needed for the conference server since it was separated from "
			                       "the other Flexisip servers.");
			continue;
		}

		const auto& sectionName = section->getName();
		if (!sectionName.rfind(modulePrefix, 0) && ranges::find(modulesToKeep, sectionName) == modulesToKeep.end()) {
			section->setDeprecated(
			    "2026-06-01", "1.0.0",
			    "Section not needed by the conference server since it was separated from the other Flexisip servers.");
		}
	}

	if (const auto module = dumpAll ? "all" : dumpDefault.getValue(); !module.empty()) {
		dump_config(*cfg, module, displayExperimental, true, removeDeprecated, dumpFormat.getValue());
		return EXIT_SUCCESS;
	}
	auto* rootCfg = cfg->getEditableRoot();
	if (dumpMibs) {
		cout << MibDumper(rootCfg);
		return EXIT_SUCCESS;
	}
	if (listModules) {
		list_sections(*cfg, true);
		return EXIT_SUCCESS;
	}
	if (listSections) {
		list_sections(*cfg);
		return EXIT_SUCCESS;
	}
	// List the overridable values.
	if (!listOverrides.getValue().empty()) {
		list<string> allCompletions;
		allCompletions.emplace_back("nosnmp");

		string empty{};
		string& filter = listOverrides.getValue();

		depthFirstSearch(empty, rootCfg, allCompletions);

		for (auto it = allCompletions.cbegin(); it != allCompletions.cend(); ++it) {
			if (filter == "all") {
				cout << *it << "\n";
			} else if (0 == it->compare(0, filter.length(), filter)) {
				cout << *it << "\n";
			}
		}
		return EXIT_SUCCESS;
	}

	// Try parsing the configuration file.
	if (cfg->load(configFile.getValue(),
	              rewriteConf ? ConfigManager::OnInvalidItem::Continue : ConfigManager::OnInvalidItem::Throw) == -1) {
		throw BadConfiguration{
		    "No configuration file found at '" + configFile.getValue() +
		        "'. A default 'flexisip-conference.conf' file should be installed in '" + CONFIG_DIR +
		        "'. Please edit it and restart flexisip-conference when ready. Alternatively, a default configuration "
		        "file can be generated using '--dump-default all'.",
		};
	}

	if (rewriteConf) {
		dump_config(*cfg, "all", displayExperimental, false, removeDeprecated, "file");
		return EXIT_SUCCESS;
	}

	if (displayExperimental) {
		LOGE_CTX(kLogPrefix) << "The '--show-experimental' option MUST be used with '--dump-default', "
		                        "'--dump-all-default' or '--rewrite-config' options";
		return EXIT_FAILURE;
	}

	const auto* globalCfg = cfg->getGlobal();
	bool enableCoreDumps = globalCfg->get<ConfigBoolean>("dump-corefiles")->read();

	ortp_init();
	su_init();
	// Tell the parser to support extra headers.
	sip_update_default_mclass(sip_extend_mclass(nullptr));

	if (enableCoreDumps) {
		rlimit lm{};
		lm.rlim_cur = RLIM_INFINITY;
		lm.rlim_max = RLIM_INFINITY;
		if (setrlimit(RLIMIT_CORE, &lm) == -1) {
			LOGW_CTX(kLogPrefix) << "Cannot enable core dump, setrlimit() failed: " << strerror(errno);
		}
	}

	if (!hostsOverride.getValue().empty()) {
		auto hosts = hostsOverride.getValue();
		auto etcResolver = EtcHostsResolver::get();

		for (auto it = hosts.begin(); it != hosts.end(); ++it) {
			size_t pos = it->find("=");
			if (pos != it->npos) {
				etcResolver->setHost(it->substr(0, pos), it->substr(pos + 1));
			}
		}
	}

	string logLevel = globalCfg->get<ConfigString>("log-level")->read();

	su_log_redirect(nullptr, sofiaLogHandler, nullptr);
	if (useDebug || logLevel == "debug") {
		const auto* sofiaLevelParameter = globalCfg->get<ConfigInt>("sofia-level");
		auto sofiaLevel = sofiaLevelParameter->read();
		if (sofiaLevel < 1 || sofiaLevel > 9) {
			throw BadConfigurationValue{sofiaLevelParameter};
		}
		su_log_set_level(nullptr, sofiaLevel);
	}

	// Read the pkcs passphrase if any from the FIFO, and keep it in memory.
	auto passphrase = getPkcsPassphrase(pkcsFile);

	string fName = "conference";

	makePidFile(pidFile.getValue());

	// Second configuration of the logger using command line arguments and configuration file parameters.
	const auto& logFilename = globalCfg->get<ConfigString>("log-filename")->read();
	logger.configure({
	    .enableStandardOutput = !disableStdout,
	    .level = rewriteConf ? BCTBX_LOG_ERROR
	             : useDebug  ? BCTBX_LOG_DEBUG
	                         : LogManager::logLevelFromName(logLevel),
	    .enableSyslog = useSyslog,
	    .syslogLevel = LogManager::logLevelFromName(globalCfg->get<ConfigString>("syslog-level")->read()),
	    .enableUserErrors = useDebug ? true : globalCfg->get<ConfigBoolean>("user-errors-logs")->read(),
	    .logFilename = regex_replace(logFilename, regex{"\\{server\\}"}, fName),
	    .logDirectory = globalCfg->get<ConfigString>("log-directory")->read(),
	    .root = root,
	});

	logger.message(kLogPrefix, __func__,
	               "Starting Flexisip-" + fName + " server [version: " FLEXISIP_CONFERENCE_GIT_VERSION "]");

	logger.setContextualFilter(globalCfg->get<ConfigString>("contextual-log-filter")->read());
	logger.setContextualLevel(
	    LogManager::logLevelFromName(globalCfg->get<ConfigString>("contextual-log-level")->read()));

	const auto showBodyForParameter = globalCfg->get<ConfigString>("show-body-for");
	try {
		MsgSip::setShowBodyFor(showBodyForParameter->read());
	} catch (const invalid_argument& e) {
		throw BadConfigurationValue{
		    showBodyForParameter,
		    "it must only contain SIP method names, whitespace separated ("s + e.what() + ")",
		};
	}

	increaseFDLimit();

#ifndef __APPLE__
	auto memoryCheckInterval = chrono::duration_cast<chrono::seconds>(
	    globalCfg->get<ConfigDuration<chrono::seconds>>("memory-usage-log-interval")->read());
	unique_ptr<process_monitoring::MemoryWatcher> memoryWatcher{};
	if (memoryCheckInterval != 0s)
		memoryWatcher = make_unique<process_monitoring::MemoryWatcher>(root, memoryCheckInterval);
#endif

	// Create an Agent in all cases because it will declare configuration items that are necessary.
	const auto authDb = std::make_shared<AuthDb>(cfg);
	const auto registrarDb = std::make_shared<RegistrarDb>(root, cfg);
	auto agent = make_shared<Agent>(root, cfg, authDb, registrarDb);
	setOpenSSLThreadSafe();

#ifdef ENABLE_SNMP
	shared_ptr<SnmpAgent> snmpAgent{};
#endif
#ifdef ENABLE_SNMP
	if (globalCfg->get<ConfigBoolean>("enable-snmp")->read()) {
		snmpAgent = make_shared<SnmpAgent>(cfg, oset);
		snmpAgent->sendNotification("Flexisip " + fName + "-server starting");
		agent->setNotifier(snmpAgent);
	}
#endif
	auto conferenceServer = make_shared<ConferenceServer>(agent->getPreferredRoute(), root, cfg, registrarDb);
	conferenceServer->init();

#ifdef ENABLE_SYSTEMD
	// Notify systemd that Flexisip has started successfully.
	int rc = sd_notify(0, "READY=1");
	if (rc < 0) {
		LOGE_CTX(kLogPrefix) << "Flexisip-conference startup: systemd sd_notify failed (" << rc << ")";
		throw ExitFailure{"Could not notify systemd that Flexisip-conference has started successfully"};
	}
	// Start the watchdog timer for the main loop keep-alive ping.
	auto watchdogNotifyInterval =
	    globalCfg->get<ConfigDuration<chrono::milliseconds>>("watchdog-notify-interval")->read();
	sofiasip::Timer watchdogTimer(root, watchdogNotifyInterval);
	watchdogTimer.setForEver([]() { sd_notify(0, "WATCHDOG=1"); });
#endif
	if (run) root->run();

	agent->unloadConfig();
	agent.reset();

	auto cleanupTasks = std::vector<std::unique_ptr<AsyncCleanup>>();
	if (conferenceServer) {
		if (auto cleanup = conferenceServer->stop()) cleanupTasks.emplace_back(std::move(cleanup));
	}

	constexpr auto timeout = 5s;
	const auto deadline = std::chrono::system_clock::now() + timeout;
	while (true) {
		auto allDone = true;
		for (auto& task : cleanupTasks) {
			allDone &= task->finished();
		}
		if (allDone) break;
		if (deadline < std::chrono::system_clock::now()) {
			LOGE_CTX(kLogPrefix) << "Async cleanup timed out after " << timeout.count()
			                     << "s, force quitting the server";
			errcode = EXIT_FAILURE;
			break;
		}
	}

	logger.message(kLogPrefix, __func__, "Exiting Flexisip-" + fName + " server normally");

	if (trackAllocs) dump_remaining_msgs();
#ifdef ENABLE_SNMP
	if (globalCfg->get<ConfigBoolean>("enable-snmp")->read() && snmpAgent) {
		snmpAgent->sendNotification("Flexisip " + fName + "-server exiting normally");
	}
#endif

	bctbx_uninit_logger();

	return errcode;
}
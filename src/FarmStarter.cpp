/*
 * FarmStarter.cpp
 *
 *  Created on: Sep 12, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "FarmStarter.h"

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <monitoring/IPCHandler.h>
#include <functional>

#include "exceptions/NA62Error.h"
#include "options/MyOptions.h"

namespace na62 {
namespace dim {

FarmStarter::FarmStarter(MessageQueueConnector_ptr myConnector) :
		availableSourceIDs_("RunControl/EnabledDetectors", -1, this), activeCREAMS_(
				"RunControl/CREAMCrates", -1, this), additionalOptions_(
				"RunControl/PCFarmOptions", -1, this), farmPID_(-1), myConnector_(
				myConnector) {

	dimListener.registerNextBurstNumberListener([this](uint nextBurst) {
		myConnector_->sendCommand(
				"UpdateNextBurstID:"
				+ std::to_string(nextBurst));});

	dimListener.registerRunNumberListener([this](uint runNumber) {
		myConnector_->sendCommand(
				"UpdateRunNumber:"
				+ std::to_string(runNumber));});

	dimListener.registerBurstNumberListener([this](uint burstID) {
		myConnector_->sendCommand(
				"UpdateBurstID:"
				+ std::to_string(burstID));});

	dimListener.registerSobListener([this](uint sob) {
		myConnector_->sendCommand(
				"SOB_Timestamp:"
				+ std::to_string(sob));});

	dimListener.registerEobListener([this](uint eob) {
		myConnector_->sendCommand(
				"EOB_Timestamp:"
				+ std::to_string(eob));});

	dimListener.registerRunningMergerListener([this](std::string mergers) {
		if(mergers.empty()) {
			myConnector_->sendCommand(
					"RunningMergers:"+mergers);
		}
	});
}

FarmStarter::~FarmStarter() {
	// TODO Auto-generated destructor stub
}

void FarmStarter::test() {
	myConnector_->sendCommand(
			"RunningMergers:" + dimListener.getRunningMergers());
}

std::vector<std::string> FarmStarter::generateStartParameters() {
	std::vector<std::string> argv;
	if (Options::GetBool(OPTION_IS_MERGER)) {
		/*
		 * Merger
		 */
		int runNumber = dimListener.getRunNumber(); // This should always be 0 unless the PC starts during a run!

		argv.push_back("--currentRunNumber=" + std::to_string(runNumber));
		return argv;
	} else {
		/*
		 * PC Farm
		 */
		argv.push_back(
				"--firstBurstID="
						+ std::to_string(dimListener.getNextBurstNumber()));

		std::string mergerList;
		while ((mergerList = dimListener.getRunningMergers()).size() == 0) {
			LOG_ERROR << "Received empty MergerList! Waiting..." << ENDL;
			usleep(500000);
		}
		boost::replace_all(mergerList, ";", ",");
		argv.push_back("--mergerHostNames=" + mergerList);

		argv.push_back("--incrementBurstAtEOB=0"); // Use the nextBurstNumber service to change the burstID instead of just incrementing at EOB

		std::string enabledDetectorIDs = "";
		if (availableSourceIDs_.getSize() <= 0) {
			LOG_ERROR
					<< "Unable to connect to EnabledDetectors service. Unable to start!"
					<< ENDL;
		} else {
			if (availableSourceIDs_.getString()[0] == (char) 0xFFFFFFFF
					&& availableSourceIDs_.getSize() == 4) {
				LOG_ERROR
						<< "EnabledDetectors is empty. Starting the pc-farm will fail!"
						<< ENDL;
			} else {
				char* str = availableSourceIDs_.getString();
				enabledDetectorIDs = std::string(str,
						availableSourceIDs_.getSize());
			}
		}

		std::string creamCrates = "";
		if (activeCREAMS_.getSize() <= 0) {
			LOG_ERROR
					<< "Unable to connect to EnabledDetectors service. Unable to start!"
					<< ENDL;
		} else {
			if (activeCREAMS_.getString()[0] == (char) 0xFFFFFFFF
					&& activeCREAMS_.getSize() == 4) {
				creamCrates = "0:0";
			} else {
				char* str = activeCREAMS_.getString();
				creamCrates = std::string(str, activeCREAMS_.getSize());
			}
		}

		argv.push_back("--L0DataSourceIDs=" + enabledDetectorIDs);
		argv.push_back("--CREAMCrates=" + creamCrates);

		std::string additionalOptions = "";
		if (additionalOptions_.getSize() <= 0) {
			LOG_ERROR
					<< "Unable to connect to AdditionalOptions service. Unable to start!"
					<< ENDL;
		} else {
			if (additionalOptions_.getString()[0] == (char) 0xFFFFFFFF
					&& additionalOptions_.getSize() == 4) {
				LOG_ERROR << "Additional options is empty." << ENDL;
			} else {
				char* str = additionalOptions_.getString();
				additionalOptions = std::string(str,
						additionalOptions_.getSize());
				boost::algorithm::trim(additionalOptions);

				std::vector<std::string> strs;
				boost::split(strs, additionalOptions, boost::is_any_of(" "));

				for (auto option : strs) {
					argv.push_back(option);
				}
			}
		}

		return argv;
	}
}

void FarmStarter::infoHandler() {
}

void FarmStarter::startFarm() {
	try {
		startFarm(generateStartParameters());
	} catch (NA62Error const& e) {
		LOG_ERROR << e.what() << ENDL;
	}
}

void FarmStarter::restartFarm() {
	killFarm();
	try {
		killFarm();
		startFarm(generateStartParameters());
	} catch (NA62Error const& e) {
		LOG_ERROR << e.what() << ENDL;
	}
}

void FarmStarter::startFarm(std::vector<std::string> params) {
	LOG_INFO << "Starting farm with following parameters: ";
	for (std::string param : params) {
		LOG_INFO << param << " ";
	}
	LOG_INFO << ENDL;

	if (Options::GetBool(OPTION_IS_MERGER)) {
		sleep(1);
	}

	if (farmPID_ > 0) {
		killFarm();
		sleep(3);
	}

	farmPID_ = fork();
	if (farmPID_ == 0) {
		boost::filesystem::path execPath(
				Options::GetString(OPTION_FARM_EXEC_PATH));

		LOG_INFO << "Starting farm program " << execPath.string() << ENDL;

		char* argv[params.size() + 2];
		argv[0] = (char*) execPath.filename().string().data();

		for (unsigned int i = 0; i < params.size(); i++) {
			argv[i + 1] = (char*) params[i].data();
		}
		argv[params.size() + 1] = NULL;

		execv(execPath.string().data(), argv);
		LOG_ERROR << "Main farm program stopped!" << ENDL;
		farmPID_ = -1;

		exit(0);
	} else if (farmPID_ == -1) {
		LOG_ERROR << "Forking failed! Unable to start the farm program!"
				<< ENDL;
	}
//myConnector_->sendState(OFF);
}

void FarmStarter::killFarm() {
	boost::filesystem::path execPath(Options::GetString(OPTION_FARM_EXEC_PATH));
	LOG_ERROR << "Killing " << execPath.filename() << ENDL;

	signal(SIGCHLD, SIG_IGN);
	if (farmPID_ > 0) {
		kill(farmPID_, SIGTERM);
//		wait((int*) NULL);
//		waitpid(farmPID_, 0,WNOHANG);
	}
	system(std::string("killall -9 " + execPath.filename().string()).data());
	farmPID_ = 0;
}
} /* namespace dim */
} /* namespace na62 */

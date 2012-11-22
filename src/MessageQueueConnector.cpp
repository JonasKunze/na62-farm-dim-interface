/*
 * MessageQueueConnector.cpp
 *
 *  Created on: Jul 11, 2012
 *      Author: root
 */
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include "MonitorDimServer.h"
#include "States.h"
#include "options/Options.h"

#include "MessageQueueConnector.h"

namespace na62 {
namespace dim {

using namespace boost::interprocess;

MessageQueueConnector::MessageQueueConnector() {
	// TODO Auto-generated constructor stub

}

MessageQueueConnector::~MessageQueueConnector() {
	// TODO Auto-generated destructor stub
}

void MessageQueueConnector::run() {
	while (true) {
		try {
			message_queue::remove("state");
			message_queue stateQueue(create_only, "state" //name
					, 1 // max message number
					, sizeof(int) // max message size
					);

			message_queue::remove("statistics");
			message_queue statisticsQueue(create_only, "statistics" //name
					, 100 // max message number
					, 1024 * 64 // max message size
							);
			unsigned int priority;
			message_queue::size_type recvd_size;

			STATE state = OFF;

			std::string statisticsMessage;
			while (true) {
				boost::posix_time::ptime t = microsec_clock::universal_time()
						+ boost::posix_time::milliseconds(
								Options::HEARTBEAT_TIMEOUT_MILLIS);

				if (stateQueue.timed_receive(&state, sizeof(int), recvd_size,
						priority, t)) {
					dimServer_->updateState(state);

					while (statisticsQueue.get_num_msg() > 0) {
						statisticsMessage.resize(1024 * 64);

						if (Options::VERBOSE) {
							std::cout << "Received: " << statisticsMessage
									<< std::endl;
						}

						if (statisticsQueue.try_receive(&(statisticsMessage[0]),
								statisticsMessage.size(), recvd_size,
								priority)) {
							statisticsMessage.resize(recvd_size);

							std::string statisticsName =
									statisticsMessage.substr(0,
											statisticsMessage.find(':'));
							std::string statistics = statisticsMessage.substr(
									statisticsMessage.find(':') + 1);

							try {
								if (statistics.find(";") != std::string::npos) {
									dimServer_->updateStatistics(statisticsName,
											statistics);
								} else {
									dimServer_->updateStatistics(statisticsName,
											boost::lexical_cast<longlong>(
													statistics));
								}
							} catch (boost::bad_lexical_cast const& e) {
								std::cout
										<< "Bad format of message for service "
										<< statisticsName << ": "
										<< statisticsMessage << std::endl;
							}
						}
					}
				} else {
					mycerr << "Timeout" << std::endl;
					dimServer_->updateState(OFF);
				}
			}

			dimServer_->updateState(OFF);
			message_queue::remove("state");
			mycout << "done" << std::endl;

		} catch (interprocess_exception &ex) {
			message_queue::remove("state");
			mycerr << "Unable to connect to message queue: " << ex.what()
					<< std::endl;
			boost::system::error_code noError;
		}
	}
}

void MessageQueueConnector::sendCommand(std::string command) {
//	if (!commandQueue_) {
	try {
		commandQueue_.reset(new message_queue(open_only // only create
				, "command" // name
				));
	} catch (interprocess_exception &ex) {
		commandQueue_.reset();
		mycerr << "Unable to connect to command message queue: " << ex.what()
				<< std::endl;
		return;
	}
//	}

	try {
		if (!commandQueue_->try_send(&(command[0]), command.size(), 0)) {
			mycout << "Unable to send command to program via IPC! "
					<< std::endl;
			commandQueue_.reset();
		}
	} catch (interprocess_exception &ex) {
		mycout << "Unable to send command to program via IPC! " << std::endl;
		commandQueue_.reset();
	}
}
} /* namespace dim */
} /* namespace na62 */

/*
 * MyCommandHandler.cpp
 *
 *  Created on: Jun 21, 2012
 *      Author: kunzejo
 */

#include "options/MyOptions.h"
#include "MyCommandHandler.h"

namespace na62 {
namespace dim {

void MyCommandHandler::commandHandler() {
	DimCommand *currCmnd = getCommand();

	std::string message;
	message.resize(currCmnd->getSize());
	message = std::string(currCmnd->getString());
	if (Options::GetInt(OPTION_VERBOSITY) != 0) {
		LOG_INFO << "Received message: " << message << ENDL;
	}

	std::transform(message.begin(), message.end(), message.begin(), ::tolower);
	if (message == "start") {
		LOG_INFO << "Start received" << ENDL;
		farmStarter_.startFarm();
	} else if (message == "restart") {
		LOG_INFO << "Restart received" << ENDL;
		farmStarter_.restartFarm();
	} else if (message == "stop") {
		LOG_INFO << "Stop received" << ENDL;
		farmStarter_.killFarm();
	} else if (message == "test") {
		LOG_INFO << "Updatemergers received" << ENDL;
		farmStarter_.test();
	} else {
		LOG_INFO << "Sending command:" << message << ENDL;
		messageQueueConnector_->sendCommand(message);
	}
}
} /* namespace dim */
} /* namespace na62 */

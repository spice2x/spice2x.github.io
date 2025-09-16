#include "io.h"

std::vector<Button>& games::xif::get_buttons() {
	static std::vector<Button> buttons;

	if (buttons.empty())
	{
		buttons = GameAPI::Buttons::getButtons("Polaris Chord");

		GameAPI::Buttons::sortButtons(
			&buttons,
			"Test",
			"Service",
			"Coin",

			"Lane Button 1",
			"Lane Button 2",
			"Lane Button 3",
			"Lane Button 4",
			"Lane Button 5",
			"Lane Button 6",
			"Lane Button 7",
			"Lane Button 8",
			"Lane Button 9",
			"Lane Button 10",
			"Lane Button 11",
			"Lane Button 12",

			"L-Fader Left",
			"L-Fader Right",
			"R-Fader Left",
			"R-Fader Right",


			"Headphones",
			"Recorder"
		);
	}

	return buttons;
}

std::vector<Analog>& games::xif::get_analogs() {
	static std::vector<Analog> analogs;

	if (analogs.empty())
	{
		analogs = GameAPI::Analogs::getAnalogs("Polaris Chord");

		GameAPI::Analogs::sortAnalogs(
			&analogs,
			"L-Fader",
			"R-Fader"
		);
	}

	return analogs;
}

std::string games::xif::get_buttons_help() {
	// keep to max 100 characters wide
	return "";
}

std::string games::xif::get_analogs_help() {
	// keep to max 100 characters wide
	return "";
}

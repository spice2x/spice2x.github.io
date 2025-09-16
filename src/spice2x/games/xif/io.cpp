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

			"Button 1",
			"Button 2",
			"Button 3",
			"Button 4",
			"Button 5",
			"Button 6",
			"Button 7",
			"Button 8",
			"Button 9",
			"Button 10",
			"Button 11",
			"Button 12",

			"Fader-L Left",
			"Fader-L Right",
			"Fader-R Left",
			"Fader-R Right",


			"Headphones"
			// "Recorder"
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
			"Fader-L",
			"Fader-R"
		);
	}

	return analogs;
}

std::string games::xif::get_buttons_help() {
	// keep to max 100 characters wide
	return "";
}

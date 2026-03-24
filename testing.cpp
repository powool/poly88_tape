#include <getopt.h>
#include <iostream>
#include <format>

#include "KansasCity.hpp"
#include "PolyPhase.hpp"
#include "Record.hpp"

void usage(int argc, char **argv)
{
	exit(1);
}

int main(int argc, char **argv)
{
	int bitRate = 4800;
	int debug = 0;
	TapeIndex tapeIndex = 0;
	bool tapeIndexInSeconds = false;
	std::string arg;
	bool invert = false;
	int hysterisis = 200;
	bool useKansasCity = false;
	int dcOffset = 0;

	int opt;
	while ((opt = getopt(argc, argv, "b:dh:i:kpO:")) != -1) {
		switch(opt) {
			case 'b':
				bitRate = std::stoi(optarg);
				break;
			case 'd':
				debug++;
				break;
			case 'h':
				hysterisis = std::stoi(optarg);
				break;
			case 'i':
				arg = optarg;
				if (arg.size()) {
					if (arg.back() == 's') {
						arg.pop_back();	// remove 's'
						tapeIndexInSeconds = true;
						tapeIndex = std::stod(arg);
					} else {
						tapeIndex = std::stoi(arg);
					}
				}
				break;
			case 'k':
				useKansasCity = true;
				hysterisis = 0;
				bitRate = 300;
				break;
			case 'p':
				invert = true;
				break;
			case 'O':
				dcOffset = std::stoi(optarg);
				break;
			default:
				usage(argc, argv);
		}
	}

	if(optind >= argc) {
		usage(argc, argv);
		exit(1);
	}

	auto audio = std::make_shared<Audio>(argv[optind]);

	if (tapeIndexInSeconds) {
		tapeIndexInSeconds = false;
		tapeIndex *= audio->SampleRate();
	}

	audio->SetInvertPhase(invert);
	audio->SetDCOffset(dcOffset);

	DataInterfacePtr decoder;
	if (useKansasCity) {
		decoder = std::make_shared<KansasCity>(audio, bitRate, hysterisis);
	} else {
		decoder = std::make_shared<PolyPhase>(audio, bitRate, hysterisis);
	}

	if (debug > 1) {
		decoder->SetDebugBit(true);
	}

	if (debug > 0) {
		decoder->SetDebugByte(true);
	}


	std::cout << std::format("Samples: {}/{}s", audio->SampleCount(), audio->TimeOffset(audio->SampleCount())) << std::endl;
	std::cout << std::format("Sample Rate: {} samples per second", audio->SampleRate()) << std::endl;
	std::cout << std::format("BitRate: {}, Starting TapeIndex: {}/{}s, Invert: {}, Hysterisis: {}",
		bitRate,
		tapeIndex,
		audio->TimeOffset(tapeIndex),
		invert,
		hysterisis) << std::endl;

#if 0
	for (auto row = 0; row < 10; row++) {
		for (auto sample = 0; sample < 40; sample++) {
			uint8_t value = audio->Value(tapeIndex + row * 40 + sample);
			std::cout << std::format("{:2x} ", value);
		}
		std::cout << std::endl;
	}
#endif

	while(tapeIndex < audio->SampleCount()) {
#if 0
		// find next carrier wave
		int observedBitRate = bitRate;
		auto carrierResult = audio->ScanForCarrier(tapeIndex, hysterisis, observedBitRate);
		std::cout << std::format("carrier found at ({}-{}) = {} samples",
				carrierResult.first,
				carrierResult.second,
				carrierResult.second - carrierResult.first) << std::endl;
		std::cout << std::format("carrier found at ({}s-{}s) = {} seconds",
				audio->TimeOffset(carrierResult.first),
				audio->TimeOffset(carrierResult.second),
				audio->TimeOffset(carrierResult.second - carrierResult.first)
				) << std::endl;

		auto leaderResult = decoder->FindEndOfNextLeader(carrierResult.second, 10);
#endif
		try {
#if 0
			KansasCity decoder(audio, bitRate, 0);
			for (auto i = tapeIndex; i < tapeIndex + 147 * 1024 ; ) {
				auto result = decoder.ReadByte(i);
				std::cout << std::format("index {}/{} ({} samples): bit: {}", i, result.first, result.first - i, result.second) << std::endl;
				i = result.first;
			}
			exit(0);
#else
			auto leaderResult = decoder->FindEndOfNextLeader(tapeIndex, 10);
			RecordPtr record = std::make_shared<Record>();
			tapeIndex = record->Read(decoder, leaderResult);
			record->Dump(true);
#endif
		} catch (const AudioEOF &e) {
			std::cout << "Reach EOF" << std::endl;
			break;
		}
	}
}

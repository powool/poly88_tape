#pragma once

#include <vector>

#include "File.hpp"

class Tape {
	std::vector<File> tapeFiles;
    public:
	std::vector<File> &GetFiles() { return tapeFiles; }
	const std::vector<File> &GetFiles() const { return tapeFiles; }
};

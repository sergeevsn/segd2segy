#pragma once

#include <stdexcept>
#include <string>

namespace segdcore {

class SegdError : public std::runtime_error {
public:
    explicit SegdError(const std::string& message) : std::runtime_error(message) {}
};

class SegdFormatError : public SegdError {
public:
    explicit SegdFormatError(const std::string& message) : SegdError(message) {}
};

class UnsupportedFormatError : public SegdError {
public:
    explicit UnsupportedFormatError(const std::string& message) : SegdError(message) {}
};

}  // namespace segdcore

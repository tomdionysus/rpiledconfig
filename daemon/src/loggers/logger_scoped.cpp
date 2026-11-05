//
// RPiLEDConfig - daemon
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the Apache License, Version 2.0 - see LICENSE
//
#include "logger_scoped.h"

#include <utility>

namespace rpiledconfig::loggers {

LoggerScoped::LoggerScoped(std::string scope, std::shared_ptr<Logger> logger) : _scope(std::move(scope)), _logger(std::move(logger)) {}

void LoggerScoped::debug(const std::string& str) { _logger->debug("(" + _scope + ") " + str); }
void LoggerScoped::info(const std::string& str) { _logger->info("(" + _scope + ") " + str); }
void LoggerScoped::warn(const std::string& str) { _logger->warn("(" + _scope + ") " + str); }
void LoggerScoped::error(const std::string& str) { _logger->error("(" + _scope + ") " + str); }
void LoggerScoped::raw(const std::string& str) { _logger->raw("(" + _scope + ") " + str); }
std::shared_ptr<Logger> LoggerScoped::base_logger() { return _logger; }

// The level belongs to whatever finally writes the line, so a scope passes it down.
void LoggerScoped::set_level(LogLevel level) { _logger->set_level(level); }

}  // namespace rpiledconfig::loggers

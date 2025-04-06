// SPDX-License-Identifier: GPL-2.0-or-later

// mirakc-arib
// Copyright (C) 2019 masnagam
//
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
// the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program; if
// not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

#pragma once

#include <iostream>

#include <rapidjson/document.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>

namespace {

class JsonlSink {
 public:
  JsonlSink() = default;
  virtual ~JsonlSink() = default;
  virtual bool HandleDocument(const rapidjson::Document&) {
    return true;
  }
};

class StdoutJsonlSink final : public JsonlSink {
 public:
  StdoutJsonlSink() = default;
  ~StdoutJsonlSink() override = default;

  bool HandleDocument(const rapidjson::Document& doc) override {
    rapidjson::OStreamWrapper stream(std::cout);
    rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
    doc.Accept(writer);
    std::cout << std::endl;
    return true;
  }
};

}  // namespace

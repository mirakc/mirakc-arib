// Copyright (c) 2019 Masayuki Nagamachi <masayuki.nagamachi@gmail.com>
//
// Licensed under either of
//
//   * Apache License, Version 2.0
//     (LICENSE-APACHE or http://www.apache.org/licenses/LICENSE-2.0)
//   * MIT License
//     (LICENSE-MIT or http://opensource.org/licenses/MIT)
//
// at your option.

#pragma once

#include "base.hh"
#include "jsonl_sink.hh"

namespace {

class JsonlSource {
 public:
  JsonlSource() = default;
  virtual ~JsonlSource() = default;

  void Connect(std::unique_ptr<JsonlSink>&& sink) {
    sink_ = std::move(sink);
  }

 protected:
  bool FeedDocument(const rapidjson::Document& doc) {
    if (!sink_) {
      return false;
    }
    return sink_->HandleDocument(doc);
  }

 private:
  std::unique_ptr<JsonlSink> sink_;

  MIRAKC_ARIB_NON_COPYABLE(JsonlSource);
};

}  // namespace

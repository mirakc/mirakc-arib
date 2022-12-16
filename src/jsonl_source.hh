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
    MIRAKC_ARIB_ASSERT(sink_ != nullptr);
    return sink_->HandleDocument(doc);
  }

 private:
  std::unique_ptr<JsonlSink> sink_;

  MIRAKC_ARIB_NON_COPYABLE(JsonlSource);
};

}  // namespace

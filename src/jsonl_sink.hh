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
  virtual bool HandleDocument(const rapidjson::Document&) { return true; }
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

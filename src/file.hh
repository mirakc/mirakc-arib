#pragma once

#include "base.hh"

namespace {

class File {
 public:
  File() = default;
  virtual ~File() = default;
  virtual ssize_t Read(uint8_t* buf, size_t len) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(File);
};

}  // namespace

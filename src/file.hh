#pragma once

#include "base.hh"

namespace {

enum class SeekMode {
  kSet,
  kCur,
  kEnd,
};

class File {
 public:
  File() = default;
  virtual ~File() = default;
  virtual const std::string& path() const = 0;
  virtual ssize_t Read(uint8_t* buf, size_t len) = 0;
  virtual ssize_t Write(uint8_t* buf, size_t len) = 0;
  virtual bool Sync() = 0;
  virtual bool Trunc(int64_t size) = 0;
  virtual int64_t Seek(int64_t offset, SeekMode mode) = 0;

 private:
  MIRAKC_ARIB_NON_COPYABLE(File);
};

}  // namespace

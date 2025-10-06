/*
 * wire-io.hpp - tiny helpers for ns-3 buffer (de)serialization
 * Mirrors LS helpers: u16-length-prefixed strings, IPv4, and count headers.
 */
#pragma once
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include <string>
#include <cstdint>

namespace wire {

inline void WriteIpv4(ns3::Buffer::Iterator& it, const ns3::Ipv4Address& a) {
  it.WriteHtonU32(a.Get());
}

inline ns3::Ipv4Address ReadIpv4(ns3::Buffer::Iterator& it) {
  return ns3::Ipv4Address(it.ReadNtohU32());
}

inline void WriteU16String(ns3::Buffer::Iterator& it, const std::string& s) {
  it.WriteU16(static_cast<uint16_t>(s.size()));
  if (!s.empty()) {
    it.Write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
  }
}

inline std::string ReadU16String(ns3::Buffer::Iterator& it) {
  uint16_t n = it.ReadU16();
  std::string out;
  out.resize(n);
  if (n) {
    it.Read(reinterpret_cast<uint8_t*>(&out[0]), n);
  }
  return out;
}

inline void WriteCountU16(ns3::Buffer::Iterator& it, uint16_t n) {
  it.WriteU16(n);
}

inline uint16_t ReadCountU16(ns3::Buffer::Iterator& it) {
  return it.ReadU16();
}

} // namespace wire

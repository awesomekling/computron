// Computron x86 PC Emulator
// Copyright (C) 2003-2018 Andreas Kling <awesomekling@gmail.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY ANDREAS KLING ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANDREAS KLING OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <limits>
#include <stdint.h>
#include <type_traits>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

enum class SegmentRegisterIndex {
    ES = 0,
    CS,
    SS,
    DS,
    FS,
    GS,
    None = 0xFF,
};

enum ValueSize {
    ByteSize = 8,
    WordSize = 16,
    DWordSize = 32,
};

class LogicalAddress;

class PhysicalAddress {
public:
    PhysicalAddress() { }
    explicit PhysicalAddress(u32 address)
        : m_address(address)
    {
    }

    u32 get() const { return m_address; }
    void set(u32 address) { m_address = address; }
    void mask(u32 m) { m_address &= m; }

    static PhysicalAddress from_real_mode(LogicalAddress);

private:
    u32 m_address { 0 };
};

class LinearAddress {
public:
    LinearAddress() { }
    explicit LinearAddress(u32 address)
        : m_address(address)
    {
    }

    LinearAddress offset(u32 o) const { return LinearAddress(m_address + o); }
    u32 get() const { return m_address; }
    void set(u32 address) { m_address = address; }
    void mask(u32 m) { m_address &= m; }

    bool operator==(const LinearAddress& other) const { return m_address == other.m_address; }

private:
    u32 m_address { 0 };
};

template<typename T>
struct TypeTrivia {
    static const unsigned bits = sizeof(T) * 8;
    static const T mask = std::numeric_limits<typename std::make_unsigned<T>::type>::max();
    static const T signBit = 1 << (bits - 1);
};

template<typename T>
struct TypeDoubler {
};
template<>
struct TypeDoubler<u8> {
    typedef u16 type;
};
template<>
struct TypeDoubler<u16> {
    typedef u32 type;
};
template<>
struct TypeDoubler<u32> {
    typedef u64 type;
};
template<>
struct TypeDoubler<i8> {
    typedef i16 type;
};
template<>
struct TypeDoubler<i16> {
    typedef i32 type;
};
template<>
struct TypeDoubler<i32> {
    typedef i64 type;
};

template<typename T>
struct TypeHalver {
};
template<>
struct TypeHalver<u16> {
    typedef u8 type;
};
template<>
struct TypeHalver<u32> {
    typedef u16 type;
};
template<>
struct TypeHalver<u64> {
    typedef u32 type;
};
template<>
struct TypeHalver<i16> {
    typedef i8 type;
};
template<>
struct TypeHalver<i32> {
    typedef i16 type;
};
template<>
struct TypeHalver<i64> {
    typedef i32 type;
};

template<typename DT>
constexpr DT weld(typename TypeHalver<DT>::type high, typename TypeHalver<DT>::type low)
{
    typedef typename std::make_unsigned<typename TypeHalver<DT>::type>::type UnsignedT;
    typedef typename std::make_unsigned<DT>::type UnsignedDT;
    return (((UnsignedDT)high) << TypeTrivia<typename TypeHalver<DT>::type>::bits) | (UnsignedT)low;
}

template<typename T, typename U>
inline constexpr T signExtendedTo(U value)
{
    if (!(value & TypeTrivia<U>::signBit))
        return value;
    return (TypeTrivia<T>::mask & ~TypeTrivia<U>::mask) | value;
}

template<typename T>
inline constexpr T least_significant(typename TypeDoubler<T>::type whole)
{
    return whole & TypeTrivia<T>::mask;
}

template<typename T>
inline constexpr T most_significant(typename TypeDoubler<T>::type whole)
{
    return (whole >> TypeTrivia<T>::bits) & TypeTrivia<T>::mask;
}

class LogicalAddress {
public:
    LogicalAddress() { }
    LogicalAddress(u16 selector, u32 offset)
        : m_selector(selector)
        , m_offset(offset)
    {
    }

    u16 selector() const { return m_selector; }
    u32 offset() const { return m_offset; }
    void set_selector(u16 selector) { m_selector = selector; }
    void set_offset(u32 offset) { m_offset = offset; }

    bool operator<(const LogicalAddress& other) const { return weld<u64>(selector(), offset()) < weld<u64>(other.selector(), other.offset()); }

private:
    u16 m_selector { 0 };
    u32 m_offset { 0 };
};

inline PhysicalAddress PhysicalAddress::from_real_mode(LogicalAddress logical)
{
    return PhysicalAddress((logical.selector() << 4) + logical.offset());
}

template<typename T>
class Badge {
    friend T;
    Badge() { }
};

// Copyright 2014 Tony Wasserka
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the owner nor the names of its contributors may
//       be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksys::util {

/*
 * Abstract bitfield class
 *
 * Allows endianness-independent access to individual bitfields within some raw
 * integer value. The assembly generated by this class is identical to the
 * usage of raw bitfields, so it's a perfectly fine replacement.
 *
 * For BitField<X,Y,Z>, X is the distance of the bitfield to the LSB of the
 * raw value, Y is the length in bits of the bitfield. Z is an integer type
 * which determines the sign of the bitfield. Z must have the same size as the
 * raw integer.
 *
 *
 * General usage:
 *
 * Create a new union with the raw integer value as a member.
 * Then for each bitfield you want to expose, add a BitField member
 * in the union. The template parameters are the bit offset and the number
 * of desired bits.
 *
 * Changes in the bitfield members will then get reflected in the raw integer
 * value and vice-versa.
 *
 *
 * Sample usage:
 *
 * union SomeRegister
 * {
 *     u32 hex;
 *
 *     BitField<0,7,u32> first_seven_bits;     // unsigned
 *     BitField<7,8,u32> next_eight_bits;      // unsigned
 *     BitField<3,15,s32> some_signed_fields;  // signed
 * };
 *
 * This is equivalent to the little-endian specific code:
 *
 * union SomeRegister
 * {
 *     u32 hex;
 *
 *     struct
 *     {
 *         u32 first_seven_bits : 7;
 *         u32 next_eight_bits : 8;
 *     };
 *     struct
 *     {
 *         u32 : 3; // padding
 *         s32 some_signed_fields : 15;
 *     };
 * };
 *
 *
 * Caveats:
 *
 * 1)
 * BitField provides automatic casting from and to the storage type where
 * appropriate. However, when using non-typesafe functions like printf, an
 * explicit cast must be performed on the BitField object to make sure it gets
 * passed correctly, e.g.:
 * printf("Value: %d", (s32)some_register.some_signed_fields);
 *
 * 2)
 * Not really a caveat, but potentially irritating: This class is used in some
 * packed structures that do not guarantee proper alignment. Therefore we have
 * to use #pragma pack here not to pack the members of the class, but instead
 * to break GCC's assumption that the members of the class are aligned on
 * sizeof(StorageType).
 */
#pragma pack(1)
template <std::size_t position, std::size_t bits, typename T,
          // StorageType is T for non-enum types and the underlying type of T if
          // T is an enumeration. Note that T is wrapped within an enable_if in the
          // former case to workaround compile errors which arise when using
          // std::underlying_type<T>::type directly.
          typename StorageType = typename std::conditional_t<
              std::is_enum<T>::value, std::underlying_type<T>, std::enable_if<true, T>>::type>
struct BitField {
    // Force default constructor to be created
    // so that we can use this within unions
    constexpr BitField() = default;

    // We declare a user-defined copy assignment operator, so the default copy constructor
    // must be defaulted explicitly to avoid a deprecation warning.
    constexpr BitField(const BitField&) = default;

    // This constructor might be considered ambiguous:
    // Would it initialize the storage or just the bitfield?
    // Hence, delete it. Use the assignment operator to set bitfield values!
    BitField(T val) = delete;

    inline constexpr void Set(T val) {
        storage =
            (storage & ~GetMask()) | ((static_cast<StorageType>(val) << position) & GetMask());
    }

    template <auto bits_ = bits, typename = std::enable_if_t<bits_ == 1>>
    inline constexpr void SetBit(bool set) {
        const auto mask = set ? ((static_cast<StorageType>(1) << position) & GetMask()) : 0;
        storage = (storage & ~GetMask()) | mask;
    }

    /// @warning This does *not* check whether the value fits within the mask,
    /// so this might overwrite unrelated fields! Using Set() is preferred.
    inline constexpr void SetUnsafe(T val) {
        storage = (storage & ~GetMask()) | (static_cast<StorageType>(val) << position);
    }

    /// @warning Same as SetUnsafe, but assumes this bitfield's bits are zero.
    /// This is intended to be called only once to efficiently initialise a bitfield,
    /// and will break very badly if called more than once. Using Set() is preferred.
    inline constexpr void Init(T val) { storage |= static_cast<StorageType>(val) << position; }

    inline constexpr BitField& operator=(const BitField& other) {
        Set(other.Value());
        return *this;
    }

    inline constexpr BitField& operator=(T val) {
        Set(val);
        return *this;
    }

#define BITFIELD_DEFINE_OP_(OP, OP_EQUAL)                                                          \
    inline constexpr BitField& operator OP_EQUAL(T val) {                                          \
        *this = Value() OP val;                                                                    \
        return *this;                                                                              \
    }

    BITFIELD_DEFINE_OP_(|, |=)
    BITFIELD_DEFINE_OP_(^, ^=)
    BITFIELD_DEFINE_OP_(&, &=)
    BITFIELD_DEFINE_OP_(+, +=)
    BITFIELD_DEFINE_OP_(-, -=)
    BITFIELD_DEFINE_OP_(*, *=)
    BITFIELD_DEFINE_OP_(/, /=)
#undef BITFIELD_DEFINE_OP_

    constexpr T Value() const {
        if constexpr (IsSigned()) {
            const size_t shift_amount = 8 * sizeof(StorageType) - bits;
            return static_cast<T>((storage << (shift_amount - position)) >> shift_amount);
        } else {
            return static_cast<T>((storage & GetMask()) >> position);
        }
    }

    constexpr operator T() const { return Value(); }  // NOLINT(google-explicit-constructor)
    static constexpr bool IsSigned() { return std::is_signed<T>(); }
    static constexpr std::size_t StartBit() { return position; }
    static constexpr std::size_t NumBits() { return bits; }
    static constexpr StorageType GetMask() {
        return (std::numeric_limits<StorageTypeU>::max() >> (8 * sizeof(StorageType) - bits))
               << position;
    }

private:
    // Unsigned version of StorageType
    using StorageTypeU = std::make_unsigned_t<StorageType>;

    StorageType storage;

    static_assert(bits + position <= 8 * sizeof(StorageType), "Bitfield out of range");
    static_assert(sizeof(T) <= sizeof(StorageType), "T must fit in StorageType");

    // And, you know, just in case people specify something stupid like bits=position=0x80000000
    static_assert(position < 8 * sizeof(StorageType), "Invalid position");
    static_assert(bits <= 8 * sizeof(T), "Invalid number of bits");
    static_assert(bits > 0, "Invalid number of bits");
};
#pragma pack()

/// Return the combined mask for all specified BitFields.
template <typename Storage, typename... BitFields>
constexpr Storage getMaskForBitFields() {
    Storage mask{};
    ((mask |= BitFields::GetMask()), ...);
    return mask;
}

/// Clear several BitFields at once.
///
/// This can sometimes produce better codegen compared to setting each BitField to zero.
/// (This function builds a mask for all the BitFields and clears those bits in one pass.)
template <typename Storage, typename... BitFields>
constexpr void clearBitFields(Storage* storage, const BitFields&... fields) {
    constexpr Storage mask = getMaskForBitFields<Storage, BitFields...>();
    *storage &= ~mask;
}

}  // namespace ksys::util

// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "Rts/Serialize/Action.hpp"
#include "Rts/Serialize/Bytes.hpp"
#include "Rts/Serialize/View.hpp"

enum class SerializerReason : uint16_t {
  Uninitialized = 0b0000'0000'0000'0000,
  Migration = 0b0000'0000'0001'0000,
  Checkpoint = 0b0000'0000'0010'0000,
  InMemoryCheckpoint = 0b0000'0000'0100'0000,
  Mask = 0b0000'0000'1111'0000
};

#ifdef RTS_MIMIC_CHARM_PUPER
namespace PUP {
class er {};
}  // namespace PUP
#endif

namespace rts {
/*!
 * \brief Serialization infrastructure. See rts::serialize::Serializer for
 * detailed documentation.
 */
namespace serialize {
/*!
 * \brief The Serializer class provides serialization and deserialization
 *        functionality for fundamental and user-defined types.
 *
 * The Serializer operates in one of several modes, specified by the
 * rts::serialize::Action enum: Sizing, Packing, Unpacking, or
 * MemoryFootprinting. It can compute the size required for serialization, pack
 * data into a buffer, unpack data from a buffer, or compute the memory
 * footprint of data. Memory footprinting provides a mechanism of computing the
 * size including data that doesn't get serialized. This is useful for monitor
 * how much memory different parts of a code is using.
 *
 * The Serializer supports serialization of fundamental types, enums, and
 * user-defined types that implement either a `pup` or `serialize` member
 * function. It also provides pipe operator (`operator|`) overloads for
 * convenient usage. New users are recommended to implement a
 * `Serializer& serialize(Serializer& s)` function as follows:
 *
 * \snippet Serializer.cpp serializer_serialize_example
 *
 * Users should use `operator|` to serialize the individual member variables
 * of a class. Only non-static members variables need to be serialized, and
 * `const` member variables can be serialized using a `const_cast`. Since
 * `Serializer& operator|()` returns a `Serializer&`, the operator can be
 * chained together. For example,
 * ```cpp
 * Serializer& serialize(Serializer& s) {
 *   return s | member_var1 | member_var2 | member_var3;
 * }
 * ```
 * It is also possible to have different behavior depending on the
 * rts::serialize::Action that the Serializer is taking. The action the
 * Serializer is taking can be checked by using the Serializer::action()
 * method, or one of Serializer::isSizing(), Serializer::isPacking(),
 * Serializer::isUnpacking(), or Serializer::isMemoryFootprinting(). As an
 * example of where this is used, consider deserializing a
 * `std::vector<double>`. Rather than serializing each element one at a time,
 * we want to serialize and deserialize all elements as a `memcpy`. This means
 * when deserializing we must first use `.resize(SIZE)` to allocate the
 * memory. This can be done as follows:
 * ```cpp
 *   size_t size = vector.size();
 *   s | size;
 *   if (s.isUnpacking()) { // or use s.action() == Action::Unpacking
 *     vector.resize(size);
 *   }
 *   // copy data
 * ```
 * Memory footprinting often also requires special action since static member
 * variables or internal buffers may not normally be serialized. We can again
 * use `std::vector<double>` as an example. Consider the case of a vector of
 * size 10 and capacity of 20. One would not want to serialize the 10
 * defaulted elements, but would want to account for them when computing the
 * memory footprint.
 *
 * ### Using the Serializer
 *
 * As an example of how to use the Serializer in Sizing mode, we will size an
 * enum:
 *
 * \snippet Serializer.cpp sizing_enum
 *
 * With the size in hand we can allocate a buffer and pack/serialize the enum
 * into the buffer:
 *
 * \snippet Serializer.cpp packing_enum
 *
 * Finally, we can unpack/deserialize the enum back into an object:
 *
 * \snippet Serializer.cpp unpacking_enum
 *
 * We can also do the same for user-defined classes. Here's an example of a
 * class that's both non-copyable and non-movable but can be serialized:
 *
 * \snippet Serializer.cpp serializer_noncopy_nonmove
 *
 * Then we can serialize and deserialize it using:
 *
 * \snippet Serializer.cpp complex_type_size_pack_unpack
 *
 * ### Serializing non-copyable and non-movable classes
 *
 * It is also possible to serialize and deserialize objects that are both
 * non-copyable and non-movable by defining a `T(Serializer& s)` constructor as
 * follows:
 *
 * \snippet Serializer.cpp serializer_noncopy_nonmove
 *
 * ### Interoperability with Charm++'s pup framework
 *
 * For easier interoperability with the Charm++ library, classes can also
 * implement a `void pup(Serializer& s)` member as follows:
 *
 * \snippet Serializer.cpp serializer_pup_example
 *
 * or, if `-D RTS_MIMIC_CHARM_PUPER=ON` is set at CMake time (the default),
 * then users may also implement a `void pup(PUP::er&)` member function as
 * follows:
 *
 * \snippet Serializer.cpp serializer_puper_example
 *
 * ### Serializing 3rd party library types
 *
 * In some cases you may need to serialize a type from a 3rd party
 * library. This is currently only possible if that type is default
 * constructible. In that case, you can write a custom `operator|` to
 * serialize the type. The recommended location to write the type is the
 * `rts::serialize` namespace. This is considered defined behavior. Keep in
 * mind that if you link against other libraries that provide conflicting
 * definitions of `operator|` for the type you may hit compiler errors, or
 * worse, violate ODR (one definition rule).
 *
 * As a concrete example, below is the definition of `operator|` for
 * `std::vector`.
 *
 * \snippet Vector.hpp serializer_definition
 *
 * Note the use of `if constexpr (is_serializer_constructible_v<T>) {` for
 * classes that can be constructed from the serializer.
 *
 * Since `std::vector<bool>` has a different API than `std::vector<T>`, we
 * provide the following overload to serialize `std::vector<bool>`:
 *
 * \snippet Vector.hpp serializer_bool_definition
 *
 * Custom stateless allocators are currently supported, and in the future the
 * Serializer class can be extended to support storing an arbitrary amount of
 * custom data to support stateful allocators.
 *
 * ### Serialization of (abstract) base classes
 *
 * Dynamic polymorphism and (abstract) base classes are a common development
 * pattern in C++. Serialization is a bit trickier in this case since, given a
 * pointer to a base class, the derived class must be serialized and the
 * receiving process must properly deserialize the byte stream. Base classes
 * that may be serialized must inherit from the SerializableBase class. As a
 * first example, consider a base class that holds no data:
 *
 * \snippet Virtual.cpp SerializableBaseNoDataInBase
 *
 * The derived class needs to inherit from SerializableDerived and continuing
 * on with our example of a base class that holds no data, the derived class
 * is implemented as
 *
 * \snippet Virtual.cpp SerializableDerivedNoDataInBase
 *
 * A few things are worth noting:
 * 1. The base class must virtually inherit from SerializableBase.
 * 2. SerializableBase takes as a template parameter the user base class (this
 * pattern is call the Curiously Recurring Template Pattern or CRTP).
 * 3. The base class must implement a (pure) virtual
 *    `virtual Serializer& serialize(Serializer& s) = 0;`
 *    function or for Charm++ PUP interoperability a (pure) virtual
 *    `virtual void pup(PUP::er& p) = 0;`
 *    function.
 * 4. The derived class must inherit from both the user base class and from
 *    SerializableDerived, which takes as template parameters the current
 *    derived class and the base class.
 * 5. The derived class must override the virtual serialize (or pup for
 *    Charm++ interoperability) functions.
 *
 * As a second example, let's look at a base class that holds data:
 *
 * \snippet Virtual.cpp SerializableBaseDataInBase
 *
 * and a derived class:
 *
 * \snippet Virtual.cpp SerializableDerivedDataInBase
 *
 * Note that:
 * 1. The inheritance is the same as before.
 * 2. The base class `serialize` (or `pup`) function is no longer pure virtual
 *    and it serializes the base class data.
 * 3. The derived class's `serialize` (and `pup`) first call the base class's
 *    `serialize` (or `pup`) method. This order is strongly recommended for
 *    consistency and because it follows C++'s base class construction order.
 *
 * The above two cases should be the most common implementations. However,
 * sometimes you have a derived class that needs to inherit from multiple base
 * classes, and any of the base classes could be serialized. In this example
 * we have two base classes, `BaseLeft` and `BaseRight`, and a derived class
 * `Derived` that inherits from both. We would like to be able to serialize
 * both `BaseLeft*` and `BaseRight*` pointers. Here are the base classes:
 *
 * \snippet Virtual.cpp MultipleBase
 *
 * Note that:
 * 1. Both inherit from `SerializableBase` with the respective base class as
 *    the template parameter.
 * 2. Both have member data, so we need to make sure all member data is
 *    serialized.
 * 3. Both define virtual `serialize` (or `pup`) function.
 * 4. They both define a constructor `Base(Serializer& s)` so that no default
 *    constructor is necessary.
 *
 * The derived class is:
 *
 * \snippet Virtual.cpp MultipleBaseDerived
 *
 * Note that:
 * 1. The class inherits from both `BaseLeft` and `BaseRight`, and inherits
 *    from the corresponding `SerializableDerived` classes.
 * 2. The `serialize` (or `pup`) functions first call into the base class
 *    serialization functions, and in the same order as they are inherited.
 * 3. A `Derived(Serializer& s)` constructor is defined that forwards to the
 *    base class constructors in the appropriate order. Note that you _cannot_
 *    call the `serialize` (or `pup`) from the constructor since this would
 *    cause a double deserialization of the data, and ultimately undefined
 *    behavior.
 *
 * While this multiple inheritance case likely somewhat rare, it is fully
 * supported.
 *
 * We briefly touched on that proper deserialization requires knowing the most
 * derived class and constructing that before unpacking the byte stream. In
 * order to know which derived class to construct, a registration system is
 * used. By default, a compiler-dependent class name is hashed for
 * registration. Since this is not portable, even if the byte stream itself
 * is, we allow users to define a custom name for derived classes that is
 * hashed for serialization. Classes that implement a
 * `static std::string rts_serializable_name();`
 * method will have that name hashed. For example,
 *
 * \snippet Virtual.cpp DerivedWithName
 *
 * It may also be the case that users have a derived class that is templated
 * on multiple types, and some form of encoding of that information is
 * necessary. Here is an example of how that can be achieved:
 *
 * \snippet Virtual.cpp DerivedTemplateWithName
 *
 * Note that the a unique name is only needed for each base class because we
 * maintain a different registration system for each base class. That is, two
 * classes can have the same registration name as long as they don't inherit
 * from the same base class.
 *
 * We use FNV-1a algorithm for computing the hash and ship our own
 * implementation. This is because `std::hash` does not guarantee portability
 * of the hashed value across implementations.
 *
 * ### Extra info for serialization
 *
 * The Serializer offers 64 bytes of "extra information" that can be set by
 * the user of the Serializer. An example usage would be to identify why
 * serialization is being done, e.g. for checking pointing to disk, migrating
 * data between address spaces (process or nodes), etc. Please see the
 * documentation of the constructors to see how to set this field.
 *
 * \warning The Serializer is not copyable, but is movable.
 *
 * \note Interoperability with Charm++'s `PUP::er` is provided by rerouting
 * calls to `p | t;` and `pup(p, t);` to this Serializer. This is enabled by
 * default but can be disabled by setting `-D RTS_MIMIC_CHARM_PUPER=OFF`.
 */
class Serializer
#ifdef RTS_MIMIC_CHARM_PUPER
    : public PUP::er
#endif
{
  using Sizing_t = std::integral_constant<Action, Action::Sizing>;
  using Packing_t = std::integral_constant<Action, Action::Packing>;
  using Unpacking_t = std::integral_constant<Action, Action::Unpacking>;
  using MemoryFootprinting_t =
      std::integral_constant<Action, Action::MemoryFootprinting>;

 public:
  /// \brief Deleted default constructor.
  Serializer() = delete;
  /// \brief Deleted copy constructor.
  Serializer(const Serializer& rhs) = delete;
  /// \brief Deleted copy assignment.
  Serializer& operator=(const Serializer& rhs) = delete;
  /// \brief Default move constructor.
  Serializer(Serializer&& rhs) = default;
  /// \brief Default move assignment.
  Serializer& operator=(Serializer&& rhs) = default;
  /// \brief Default destructor.
  ~Serializer() = default;

  /// Passed to the constructor to select Sizing mode.
  static constexpr Sizing_t Sizing{};
  /// Passed to the constructor to select Packing mode.
  static constexpr Packing_t Packing{};
  /// Passed to the constructor to select Unpacking mode.
  static constexpr Unpacking_t Unpacking{};
  /// Passed to the constructor to select MemoryFootprinting mode.
  static constexpr MemoryFootprinting_t MemoryFootprinting{};

  /*!
   * \brief Constructs a Serializer for sizing mode.
   *
   * Construct without extra info:
   *
   * \snippet Serializer.cpp serializer_construct_sizing
   *
   * Construct with extra info:
   *
   * \snippet Serializer.cpp serializer_construct_sizing_extra_info
   *
   * \param selector The Sizing_t tag.
   * \param extra_info Optional extra information.
   */
  explicit Serializer(Sizing_t selector, std::uint64_t extra_info = 0);

  /*!
   * \brief Constructs a Serializer for packing mode.
   *
   * Construct without extra info:
   *
   * \snippet Serializer.cpp serializer_construct_packing
   *
   * Construct with extra info:
   *
   * \snippet Serializer.cpp serializer_construct_packing_extra_info
   *
   * \param selector The Packing_t tag.
   * \param buffer Pointer to the buffer to pack data into.
   * \param buffer_size Size of the buffer in bytes.
   * \param extra_info Optional extra information.
   */
  Serializer(Packing_t selector, std::byte* buffer, size_t buffer_size,
             std::uint64_t extra_info = 0);

  /*!
   * \brief Constructs a Serializer for unpacking mode.
   *
   * Construct without extra info:
   *
   * \snippet Serializer.cpp serializer_construct_unpacking
   *
   * Construct with extra info:
   *
   * \snippet Serializer.cpp serializer_construct_unpacking_extra_info
   *
   * \param selector The Unpacking_t tag.
   * \param buffer Pointer to the buffer to unpack data from.
   * \param buffer_size Size of the buffer in bytes.
   * \param extra_info Optional extra information.
   */
  Serializer(Unpacking_t selector, std::byte* buffer, size_t buffer_size,
             std::uint64_t extra_info = 0);

  /*!
   * \brief Constructs a Serializer for memory footprinting mode.
   *
   * Construct using:
   * ```cpp
   * Serializer s{Serializer::MemoryFootprinting};
   * ```
   *
   * \param selector The MemoryFootprinting_t tag.
   * \param extra_info Optional extra information.
   */
  explicit Serializer(MemoryFootprinting_t selector,
                      std::uint64_t extra_info = 0);

  /// Returns the current serialization rts::serialize::Action.
  Action action() const { return action_; }

  /// Returns true if the Serializer is in Packing mode.
  bool isPacking() const { return action_ == Action::Packing; }

  /// Returns true if the Serializer is in Sizing mode.
  bool isSizing() const { return action_ == Action::Sizing; }

  /// Returns true if the Serializer is in Unpacking mode.
  bool isUnpacking() const { return action_ == Action::Unpacking; }

  /// Returns true if the Serializer is in MemoryFootprinting mode.
  bool isMemoryFootprinting() const {
    return action_ == Action::MemoryFootprinting;
  }

  /// Returns the extra information value.
  uint64_t extra_info() const { return extra_info_; }

  /// Returns the number of bytes processed by the Serializer.
  size_t number_of_bytes() const { return number_of_bytes_; }

  /// Returns the pointer to the start of the buffer.
  std::byte* start_pointer() const { return start_pointer_; }

  /*!
   * \brief Serializes or deserializes a View of objects that satisfy
   * rts::serialization::serialize_as_bytes_v.
   *
   * This essentially is used for serializing a "vector" of objects of a
   * single fundamental type. Since all classes can be decomposed into
   * collections fundamental types, this is the lowest level functionality
   * that all other serialization builds on.
   *
   * For example, here is how one could process a `std::array` (note that in
   * general using `s | array` and `operator|` is preferred with this call
   * operator being the lowest-level utility):
   *
   * \snippet Serializer.cpp serializer_call_view_array
   *
   * For example, here is how one could process a `std::vector`:
   *
   * \snippet Serializer.cpp serializer_call_view_vector
   *
   * For example, here is how one could process an object (though the other
   * call operator overload is preferred in this case!):
   *
   * \snippet Serializer.cpp serializer_call_view_object
   *
   * \tparam T The fundamental type.
   * \param view The view of items to process.
   */
  template <class T>
  Serializer& operator()(View<T> view);

  /*!
   * \brief Serializes or deserializes a single object that satisfies
   * rts::serialization::serialize_as_bytes_v.
   *
   * For example, here is how one could process an object (note that in
   * general using `s | value` and `operator|` is preferred with this call
   * operator being the lowest-level utility):
   *
   * \snippet Serializer.cpp serializer_call_object
   *
   * \tparam T The fundamental type.
   * \param data The item to process.
   * \return Reference to this Serializer.
   */
  template <class T>
  Serializer& operator()(T& data);

 private:
  /*!
   * \brief Processes a block of bytes for serialization or deserialization.
   *
   * \param data The Bytes struct describing the memory block.
   */
  void bytes(Bytes data);

  uint64_t extra_info_{};
  Action action_{};
  size_t number_of_bytes_{0};
  std::byte* start_pointer_{nullptr};
  std::byte* current_pointer_{nullptr};
  std::byte* end_pointer_{nullptr};
};

/*!
 * \brief Trait to detect if a type has a pup member function.
 *
 * Inherits from std::true_type if T has a member function `pup(Serializer&)`,
 * otherwise inherits from std::false_type.
 *
 * \tparam T The type to check.
 * \tparam U Used for SFINAE, defaults to void.
 */
template <class T, class U = void>
struct has_pup_member : std::false_type {};

/// \cond
template <class T>
struct has_pup_member<T, std::void_t<decltype(std::declval<T>().pup(
                             std::declval<Serializer&>()))>> : std::true_type {
};
/// \cond

/*!
 * \brief Bool to detect if a type has a pup member function.
 *
 * Is `true` if T has a member function `pup(Serializer&)`, otherwise `false`.
 *
 * \tparam T The type to check.
 */
template <class T>
constexpr bool has_pup_member_v = has_pup_member<T>::value;

/*!
 * \brief Trait to detect if a type has a serialize member function.
 *
 * Inherits from std::true_type if T has a member function
 * serialize(Serializer&), otherwise inherits from std::false_type.
 *
 * \tparam T The type to check.
 * \tparam U Used for SFINAE, defaults to void.
 */
template <class T, class U = void>
struct has_serialize_member : std::false_type {};

/// \cond
template <class T>
struct has_serialize_member<T, std::void_t<decltype(std::declval<T>().serialize(
                                   std::declval<Serializer&>()))>>
    : std::true_type {};
/// \endcond

/*!
 * \brief Bool to detect if a type has a serialize member function.
 *
 * Is `true` if T has a member function serialize(Serializer&), otherwise
 * `false`.
 *
 * \tparam T The type to check.
 */
template <class T>
constexpr bool has_serialize_member_v = has_serialize_member<T>::value;

/*!
 * \brief Trait to indicate if a type should be serialized as a raw block of
 * bytes.
 *
 * Specialize `as_bytes<T>` to std::true_type for types that should be
 * serialized using their raw memory representation. By default, `as_bytes<T>`
 * is std::false_type.
 *
 * You can also inherit from `as_bytes<void>` to mark a class as being able to
 * be serialized as a byte stream.
 *
 * \tparam T The type to check.
 */
template <class T>
struct as_bytes : std::false_type {};

/*!
 * \brief Evaluates to `true` if `T` can be serialized as a collection of bytes.
 *
 * This means any of the following are true:
 * - `as_bytes<T>::value`
 * - `std::is_base_of_v<as_bytes<void>, T>`
 * - `std::is_fundamental_v<T>`
 * - `std::is_same_v<T, std::byte>`
 */
template <class T>
constexpr bool serialize_as_bytes_v =
    as_bytes<T>::value or std::is_base_of_v<as_bytes<void>, T> or
    std::is_fundamental_v<T> or std::is_same_v<T, std::byte>;

/*!
 * \brief Evaluates `true` if `T` is constructible from `Serializer&`.
 */
template <class T>
constexpr bool is_serializer_constructible_v =
    std::is_constructible_v<T, Serializer&>;

/*!
 * \brief Evaluates `true` if `T` can be serialized using `operator|`.
 *
 * \warning You must write your `operator|` overloads to be SFINAE
 * friendly. This means using
 * `std::enable_if_t<is_serializable_v<T>,Serializer&>` for containers like
 * `std::vector<T>`.
 */
template <class T, class U = void>
struct is_serializable : std::false_type {};

/// \cond
template <class T>
struct is_serializable<T,
                       std::void_t<decltype(operator|(
                           std::declval<Serializer&>(), std::declval<T&>()))>>
    : std::true_type {};
/// \endcond

/*!
 * \brief Evaluates `true` if `T` can be serialized using `operator|`.
 *
 * \warning You must write your `operator|` overloads to be SFINAE
 * friendly. This means using
 * `std::enable_if_t<is_serializable_v<T>,Serializer&>` for containers like
 * `std::vector<T>`.
 */
template <class T>
constexpr bool is_serializable_v = is_serializable<T>::value;

template <class T>
Serializer& Serializer::operator()(View<T> view) {
  static_assert(serialize_as_bytes_v<T>);
  this->bytes(Bytes{reinterpret_cast<std::byte*>(view.item_), sizeof(T),
                    view.number_of_items_});
  return *this;
}

template <class T>
Serializer& Serializer::operator()(T& data) {
  static_assert(serialize_as_bytes_v<T>);
  this->bytes(Bytes{reinterpret_cast<std::byte*>(&data), sizeof(T), 1});
  return *this;
}

/*!
 * \brief Serializes or deserializes an enum type using its underlying type.
 *
 * \tparam T Enum type.
 * \param serializer The Serializer instance.
 * \param t Reference to the enum value.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<std::is_enum_v<T>, Serializer&> operator|(
    Serializer& serializer, T& t) {
  return serializer(
      *reinterpret_cast<std::underlying_type_t<T>*>(std::addressof(t)));
}

/*!
 * \brief Serializes or deserializes a fundamental type.
 *
 * \tparam T Fundamental type.
 * \param serializer The Serializer instance.
 * \param t Reference to the value.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<std::is_fundamental_v<T>, Serializer&> operator|(
    Serializer& serializer, T& t) {
  return serializer(t);
}

/*!
 * \brief Serializes or deserializes a type with a pup member function.
 *
 * This overload is selected if the type has a pup member and does not have
 * a serialize member.
 *
 * \tparam T Type with pup member.
 * \param serializer The Serializer instance.
 * \param t Reference to the value.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<has_pup_member_v<T> and not has_serialize_member_v<T>,
                 Serializer&>
operator|(Serializer& serializer, T& t) {
  t.pup(serializer);
  return serializer;
}

/*!
 * \brief Serializes or deserializes a type with a serialize member function.
 *
 * This overload is selected if the type has a serialize member.
 *
 * \tparam T Type with serialize member.
 * \param serializer The Serializer instance.
 * \param t Reference to the value.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<has_serialize_member_v<T>, Serializer&> operator|(
    Serializer& serializer, T& t) {
  return t.serialize(serializer);
}

/*!
 * \brief Serializes or deserializes a type as a raw block of bytes.
 *
 * This overload is selected if as_bytes<T>::value is true or if T is derived
 * from `as_bytes<void>`.
 *
 * \tparam T Type to be serialized as bytes.
 * \param serializer The Serializer instance.
 * \param t Reference to the value.
 * \return Reference to the Serializer.
 */
template <class T>
std::enable_if_t<as_bytes<T>::value or std::is_base_of_v<as_bytes<void>, T>,
                 Serializer&>
operator|(Serializer& serializer, T& t) {
  return serializer(View{reinterpret_cast<std::byte*>(&t), sizeof(t)});
}
}  // namespace serialize
}  // namespace rts

#ifdef RTS_MIMIC_CHARM_PUPER
namespace PUP {
template <class T>
void operator|(er& p, T& t) {
  static_cast<rts::serialize::Serializer&>(p) | t;
}
}  // namespace PUP
#endif

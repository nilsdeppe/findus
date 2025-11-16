// Copyright, Nils Deppe, 2022-
// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "Rts/Serialize/Exception.hpp"
#include "Rts/Serialize/Serializer.hpp"

namespace rts::serialize {
#if defined(__cpp_lib_constexpr_string) and \
    (__cpp_lib_constexpr_string >= 201907L)
#if not defined(RTS_CONSTEXPR_CXX20)
#define RTS_CONSTEXPR_CXX20 constexpr
#define RTS_USE_CONSTEXPR_HASH
#endif
#else  // __cpp_lib_constexpr_string
#undef RTS_CONSTEXPR_CXX20
#define RTS_CONSTEXPR_CXX20
#undef RTS_USE_CONSTEXPR_HASH
#endif

namespace detail {
/*!
 * \brief Computes a hash value for a std::string using the FNV-1a algorithm.
 *
 * This function implements the 64-bit FNV-1a hash, which is fast and provides
 * good distribution for typical string data. It is suitable for use in hash
 * tables and for generating unique identifiers for strings.
 *
 * \param string The input string to hash.
 * \return The computed hash value as a size_t.
 */
constexpr size_t hash(const std::string& string) {
  size_t hash = 14695981039346656037ull;
  const char* str = string.c_str();
  while (*str) {
    hash ^= static_cast<unsigned char>(*str++);
    hash *= 1099511628211ull;
  }
  return hash;
}

/*!
 * \brief Trait to detect if a type has a static rts_serializable_name() member
 * function.
 *
 * Inherits from std::true_type if T has a static member function
 * `rts_serializable_name()`, otherwise inherits from std::false_type.
 *
 * \tparam T The type to check.
 */
template <class T, class = void>
struct has_rts_serializable_name : std::false_type {};

/*!
 * \brief Trait specialization for types with a static rts_serializable_name()
 * member function.
 *
 * Inherits from std::true_type if T has a static member function
 * `rts_serializable_name()`.
 */
template <class T>
struct has_rts_serializable_name<
    T, std::void_t<decltype(T::rts_serializable_name())>> : std::true_type {};
}  // namespace detail

/*!
 * \brief Evaluates to true if T has a static member function
 * rts_serializable_name(), false otherwise.
 *
 * \tparam T The type to check (usually a derived class in a class hierarchy).
 */
template <class T>
constexpr bool has_rts_serializable_name_v =
    detail::has_rts_serializable_name<T>::value;

/*!
 * \brief Returns the name for a type for identification during virtual base
 * class serialization.
 *
 * If the type Derived provides a static member function
 * rts_serializable_name(), this function returns its result. Otherwise, it
 * returns a compiler-generated string representing the type (specifically,
 * `__PRETTY_FUNCTION__`).
 *
 * This function is used to obtain a unique, human-readable name for a
 * serializable type, which can be useful for registration, debugging, or
 * type identification in serialization frameworks.
 *
 * \tparam Derived The type for which to obtain the serializable name.
 * \return The serializable name as a std::string.
 */
template <class Derived>
RTS_CONSTEXPR_CXX20 std::string serializable_name() {
  if constexpr (has_rts_serializable_name_v<Derived>) {
    return Derived::rts_serializable_name();
  } else {
    return {__PRETTY_FUNCTION__};
  }
}

/*!
 * \brief Computes a hash value for the serializable name of a type.
 *
 * This function returns the hash of the serializable name for the given type
 * Derived. The resulting hash is used for type identification/registration
 * during serialization.
 *
 * The string to hash is is obtained using serializable_name<Derived>(), and the
 * hash is computed using the FNV-1a algorithm.
 *
 * \tparam Derived The type for which to compute the hash.
 * \return The hash value as a size_t.
 */
template <class Derived>
RTS_CONSTEXPR_CXX20 size_t serializable_hash() {
  return detail::hash(serializable_name<Derived>());
}

/// \cond
template <class Derived, class Base>
class SerializableDerived;
/// \endcond

/*!
 * \brief Base class for enabling virtual (polymorphic) serialization.
 *
 * This struct should be used as a virtual base for any class hierarchy that
 * requires serialization of derived types through a base class pointer.
 * It provides the necessary virtual interface for serialization and
 * deserialization of polymorphic types.
 *
 * Classes inheriting from SerializableBase must implement either a
 * `serialize(Serializer&)` or `pup(PUP::er&)` member function (if
 * `-DRTS_MIMIC_CHARM_PUPER=ON` was passed to CMake).
 *
 * \tparam Base The base class type for the polymorphic hierarchy.
 *
 * \note This struct is intended to be used with
 * rts::serialize::SerializableDerived and the serialization framework provided
 * in this library.
 *
 * Example usage:
 *
 * A base class with no data:
 *
 * \snippet Virtual.cpp SerializableBaseNoDataInBase
 *
 * A base class with data:
 *
 * \snippet Virtual.cpp SerializableBaseDataInBase
 *
 * More detailed usage is discussed with the documentation for
 * rts::serialize::Serializer
 *
 * ### Implementation note
 *
 * While naively there is no reason to template `SerializableBase` on the
 * `Base` class, it is necessary for diamond hierarchies. For example, if you
 * have a `Derived` that inherits off two serializable base classes `BaseLeft`
 * and `BaseRight`, then the only way to avoid duplicate definitions is to
 * split the hierarchy in the direction of the two base classes.
 */
template <class Base>
class SerializableBase {
 public:
  virtual ~SerializableBase() = default;

 private:
  template <class Derived, class LocalBase>
  friend class SerializableDerived;

  template <class LocalBase>
  friend Serializer& serialize_abstract_base(Serializer& serializer,
                                             LocalBase* base);

  virtual size_t rts_derived_class_serialization_id() const = 0;
};

/*!
 * \brief Class to enable registration and identification of derived
 * classes for virtual (polymorphic) serialization.
 *
 * This class should be used as a base for any derived class in a polymorphic
 * hierarchy that needs to be serialized or deserialized through a base class
 * pointer. It handles automatic registration of the derived class with the
 * serialization framework, allowing correct reconstruction of the derived
 * type during deserialization.
 *
 * Classes (say one named Derived) inheriting from SerializableDerived<Derived,
 * Base> must also inherit from Base and Base must inherit from
 * SerializableBase<Base>.
 *
 * \tparam Derived The derived class type.
 * \tparam Base The base class type for the polymorphic hierarchy.
 *
 * Example usage:
 *
 * A derived class where the base class has no data:
 *
 * \snippet Virtual.cpp SerializableDerivedNoDataInBase
 *
 * A derived class where the base class has data:
 *
 * \snippet Virtual.cpp SerializableDerivedDataInBase
 *
 * More detailed usage is discussed with the documentation for
 * rts::serialize::Serializer
 */
template <class Derived, class Base>
class SerializableDerived : public virtual SerializableBase<Base> {
 public:
  virtual ~SerializableDerived() = default;

 private:
  size_t rts_derived_class_serialization_id() const override {
    return derived_class_serialization_id_;
  }

  static size_t derived_class_serialization_id_;
};

namespace detail {
#if defined(RTS_USE_CONSTEXPR_HASH)
/*!
 * \brief Compile-time constant for the serialization ID of a derived class.
 *
 * This variable template stores the hash of the serializable name of the
 * Derived type, computed at compile time using serializable_hash<Derived>().
 * It is used to uniquely identify derived classes in serialization frameworks,
 * especially for virtual base class serialization.
 *
 * \tparam Derived The derived class type.
 * \tparam Base The base class type (not used in the computation, but included
 * for clarity).
 */
template <class Derived, class Base>
constexpr size_t constexpr_derived_class_serialization_id_v =
    serializable_hash<Derived>();
#endif
}  // namespace detail

/*!
 * \brief Holds registration information for a serializable derived class so
 * it can be reconstructed from serializing the (abstract) base class.
 *
 * The ClassEntry struct stores metadata and function pointers needed to
 * allocate and construct derived class instances during deserialization.
 * It is used in the registry for polymorphic (de)serialization.
 *
 * \tparam Base The base class type for the polymorphic hierarchy.
 */
template <typename Base>
struct ClassEntry {
  /*!
   * \brief Constructs a ClassEntry with the given parameters.
   *
   * \param derived_class_serialization_id The unique serialization ID for the
   *        derived class.
   * \param derived_class_name The human-readable name of the derived class.
   * \param default_allocate Function pointer to allocate memory for the
   *        derived class.
   * \param default_construct Function pointer to construct the derived class
   *        using a Serializer.
   */
  template <typename Allocator, typename Constructor>
  ClassEntry(size_t derived_class_serialization_id,
             std::string derived_class_name, Allocator&& default_allocate,
             Constructor&& default_construct);

  /// Serialization ID of the derived class.
  size_t derived_class_serialization_id_;
  /// Name of the derived class.
  std::string derived_class_name_;
  /// Function pointer to allocate memory.
  std::byte* (*default_allocate_)();
  /// Function pointer to construct the object and a bool to mark if it was
  /// deserialized during construction.
  std::pair<Base*, bool> (*default_construct_)(std::byte*, Serializer&);
};

template <typename Base>
template <typename Allocator, typename Constructor>
ClassEntry<Base>::ClassEntry(const size_t derived_class_serialization_id,
                             std::string derived_class_name,
                             Allocator&& default_allocate,
                             Constructor&& default_construct)
    : derived_class_serialization_id_(derived_class_serialization_id),
      derived_class_name_(std::move(derived_class_name)),
      default_allocate_(std::forward<Allocator>(default_allocate)),
      default_construct_(std::forward<Constructor>(default_construct)) {}

/*!
 * \brief Returns the registry of serializable derived classes for a given base
 * type.
 *
 * This function provides access to a static unordered_map that stores
 * ClassEntry objects for all derived classes registered under the specified
 * base class. The map is keyed by the derived class's unique serialization ID.
 *
 * The registry is used during serialization and deserialization to look up
 * and construct derived class instances from their serialization IDs.
 *
 * \tparam Base The base class type for the polymorphic hierarchy.
 * \return Reference to the registry map for the specified base class.
 */
template <class Base>
std::unordered_map<size_t, ClassEntry<Base>>& registry() {
  static std::unordered_map<size_t, ClassEntry<Base>> registry{};
  return registry;
}

/// \cond
/*!
 * \brief Registers a derived class for polymorphic serialization.
 *
 * This function inserts a ClassEntry for the derived class into the registry
 * for the specified base class. It assigns a unique serialization ID to the
 * derived class, stores its name, and sets up function pointers for allocation
 * and construction. If a class with the same serialization ID is already
 * registered, an exception is thrown.
 *
 * This function is typically called automatically when a SerializableDerived
 * is instantiated.
 *
 * \tparam Derived The derived class type to register.
 * \tparam Base The base class type for the polymorphic hierarchy.
 * \return The unique serialization ID assigned to the derived class.
 * \throws Exception if another class with the same serialization ID is
 *         already registered.
 */
template <class Derived, class Base>
size_t register_derived() {
  static_assert(std::is_base_of_v<Base, Derived>);
  std::unordered_map<size_t, ClassEntry<Base>>& reg = registry<Base>();
  RTS_CONSTEXPR_CXX20 const size_t hash = serializable_hash<Derived>();
  const auto [it, success] = reg.emplace(
      hash, ClassEntry<Base>(
                hash, serializable_name<Derived>(),
                []() -> std::byte* {
                  return reinterpret_cast<std::byte*>(
                      std::allocator<Derived>{}.allocate(1));
                },
                [](std::byte* buffer,
                   Serializer& serializer) -> std::pair<Base*, bool> {
                  if constexpr (is_serializer_constructible_v<Derived>) {
                    return {new (buffer) Derived{serializer}, true};
                  } else {
                    static_assert(std::is_default_constructible_v<Derived>,
                                  "Derived classes must have either a "
                                  "Derived(Serializer& s) or a Derived() "
                                  "(default) constructor.");
                    return {new (buffer) Derived{}, false};
                  }
                }));
  (void)it;
  if (not success) {
    throw Exception{"Unable to register derived class " +
                    serializable_name<Derived>() +
                    ". This likely is because another class has the same "
                    "serializable name."};
  }
  return hash;
}

template <class Derived, class Base>
size_t SerializableDerived<Derived, Base>::derived_class_serialization_id_ =
    register_derived<Derived, Base>();
/// \endcond

/*!
 * \brief Creates an instance of a derived class from its serialization ID.
 *
 * This function looks up the derived class in the registry using the
 * serialization ID loaded from the serializer, allocates memory for the object,
 * and constructs it using the Serializer if possible. If the ID is not found,
 * an exception is thrown.
 *
 * This is used during deserialization of polymorphic types to reconstruct
 * the correct derived type from its unique serialization identifier. The bool
 * in the returned pair is `true` if the class was deserialized during
 * construction.
 *
 * \tparam Base The base class type.
 * \param serializer The Serializer instance used for construction.
 * \return Pointer to the newly constructed derived class instance and a bool
 *         that is `true` if the class was constructed using the Serializer (and
 *         thus does not need to be deserialized again).
 * \throws Exception if the serialization ID is not found in the registry.
 */
template <class Base>
std::pair<Base*, bool> create(Serializer& serializer) {
  static_assert(std::is_base_of_v<SerializableBase<Base>, Base>);
  size_t derived_class_serialization_id = 0;
  serializer | derived_class_serialization_id;
  const auto& reg = registry<Base>();
  const auto it = reg.find(derived_class_serialization_id);
  if (it == reg.end()) {
    std::string error_msg{
        "Unable to find derived class with derived_class_serialization_id of " +
        std::to_string(derived_class_serialization_id) +
        ".\n"
        "Base class is " +
        serializable_name<Base>() + ".\nKnown derived classes are:\n"};
    for (const auto& [key, entry] : reg) {
      error_msg += std::to_string(key) + ": " + entry.derived_class_name_;
    }
    throw Exception{error_msg.c_str()};
  }
  const ClassEntry<Base>& class_entry = it->second;
  return class_entry.default_construct_(class_entry.default_allocate_(),
                                        serializer);
}

/*!
 * \brief Serializes an abstract base class pointer.
 *
 * This function serializes the unique serialization ID of the derived class
 * pointed to by the base pointer, and then serializes the object itself
 * using either the serialize or pup member function, depending on the
 * serialization mode.
 *
 * This is used for polymorphic serialization, allowing correct identification
 * and reconstruction of derived types from a base class pointer.
 *
 * Use the function rts::serialize::create() for deserialization.
 *
 * \tparam Base The base class type, which must inherit from SerializableBase.
 * \param serializer The Serializer instance.
 * \param base Pointer to the base class object to serialize or deserialize.
 * \return Reference to the Serializer.
 * \throws Exception if neither serialize nor pup member is available.
 *
 * \see rts::serialize::create()
 */
template <class Base>
Serializer& serialize_abstract_base(Serializer& serializer, Base* base) {
  static_assert(std::is_base_of_v<SerializableBase<Base>, Base>);
  size_t derived_class_serialization_id =
      base->rts_derived_class_serialization_id();
  serializer | derived_class_serialization_id;
  if constexpr (has_serialize_member_v<Base>) {
    return static_cast<Base&>(*base).serialize(serializer);
  } else {
    static_assert(
        has_pup_member_v<Base>,
        "Serializable base classes must have either a 'Serializer& "
        "serialize(Serializer&)' member function (or if Charm++ "
        "interop is enabled, a 'void pup(PUP::er&)' member function).");
    static_cast<Base&>(*base).pup(serializer);
  }
  return serializer;
}

/*!
 * \brief Deserializes a polymorphic object from a Serializer and returns a
 *        pointer to the reconstructed base class.
 *
 * This function reconstructs a derived object from a base class pointer by
 * reading the unique serialization ID from the serializer, looking up the
 * corresponding derived class in the registry, allocating memory, and
 * constructing the object. If the derived class provides a constructor that
 * takes a `Serializer&`, it is used for construction; otherwise, the object is
 * default-constructed and then deserialized.
 *
 * \tparam Base The base class type, which must inherit from
 *         SerializableBase<Base>.
 * \param serializer The Serializer instance to deserialize from.
 * \return Pointer to the newly constructed base class object (actually
 *         pointing to the correct derived type).
 * \throws Exception if the serialization ID is not found in the registry or
 *         if deserialization fails.
 *
 * \see serialize_abstract_base()
 */
template <class Base>
Base* deserialize_abstract_base(Serializer& serializer) {
  static_assert(std::is_base_of_v<SerializableBase<Base>, Base>);
  std::pair<Base*, bool> base_and_was_deserialized = create<Base>(serializer);
  if (not base_and_was_deserialized.second) {
    if constexpr (has_serialize_member_v<Base>) {
      static_cast<Base&>(*(base_and_was_deserialized.first))
          .serialize(serializer);
    } else {
      static_assert(
          has_pup_member_v<Base>,
          "Serializable base classes must have either a 'Serializer& "
          "serialize(Serializer&)' member function (or if Charm++ "
          "interop is enabled, a 'void pup(PUP::er&)' member function).");
      static_cast<Base&>(*(base_and_was_deserialized.first)).pup(serializer);
    }
  }
  return base_and_was_deserialized.first;
}
}  // namespace rts::serialize

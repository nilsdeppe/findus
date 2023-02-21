# Toy RTS - A Toy (Parallel) Runtime System(-ish)

ToyRTS is a fun little thought experiment turned implementation. The goal is to
provide something akin to a task-parallel runtime system, but keeping the
implementation as simple as possible, and forcing users to deal with MPI for
inter-node communication. Abstracting away all layers of a large parallel system
is difficult, and an active area of research and development in many companies
and universities. Instead, ToyRTS explores some of the basic features that one
might want for a hyperbolic PDE solver that runs on a tasking system.

### Intranode/Intraprocess Communication

The threading is done via a pool with threads pinned to cores via
affinity. Thread migration does not help in HPC applications since we want the
application to get all the CPU cycles. A thread pool is used (currently with
C++11 threads, but this can be generalized) since OpenMP does not give control
over the lifetime of threads, and spawning threads has non-negligible
overhead. Data sharing within node/process can therefore be done without any
network and ultimately boils down to a pointer copy. Having said that, the goal
is to give users some flexibility on how they copy data within a process.

### Distributed Objects

The central entity in ToyRTS is a distributed object. This is simply a class on
a process. However, distributed objects must get registered with the RTS so that
MPI messages can be correctly invoked by the receiver. This registration maps a
class (and member function) to unique `uint32_t` identifiers. The name of the
class is returned by a member function of the distributed object called
`const std::string& object_name()`. A similar member function
`const uint32_t object_id()` can also be used. The advantage of `object_id()` is
that the RTS does not need to do a hash to compute the lookup. That is, if
`object_name()` is used then the RTS uses an `unordered_map<std::string,
OBJECT_CONTAINER>` to store the objects. If the `object_id()` approach is used
then it is up to the user to ensure all registering distributed objects have a
unique ID. To use the `object_id()` define
```cpp
namespace rts {
static constexpr bool use_object_id = true;
```
(we intentionally avoid using macros to improve forward portability with C++
modules).

> `uint32_t` has a maximum size of `2^32=4,294,967,296`, which is a lot of
> different types of distributed objects. While practically speaking this is a
> huge amount of types of objects, from a data layout perspective this is easier
> to work with. If it turns out this overhead is too much, it is reasonable to
> decrease the object type index and the member function index to being
> `uint16_t` with maximum size `2^16=65,536`, still an absurdly large amount of
> different types of distributed objects and member functions. `uint8_t` might
> be okay, but this is only 256 unique object types, which is plausible to reach
> with a sufficiently large and complex code base.

### MessageHeader

Messages sent between MPI ranks must contain some form of metadata for the RTS
to know how to queue the message. This is encoded in the `MessageHeader` class,
which is trivial and standard-layout given by
```cpp
struct alignas(32) MessageHeader {
  std::uint32_t class_index = 0;
  std::uint32_t function_index = 0;
  std::uint32_t array_index_buffer[6] = {0, 0, 0, 0, 0, 0};
};
```
This means that the array index for an array may be any class that is 4- or
8-byte aligned, and is at most 24 bytes large. Note that `MessageHeader`
intentionally zero-constructs all members. This is to avoid incorrect unused
bits, which any array index must also do. Because `MessageHeader` is exactly 32
bytes in size and is aligned at a 32-byte word boundary means that any data
after `MessageHeader` in a message is 32-byte aligned. This should be sufficient
for most data, include 256-bit SIMD data. The reason for this design choice is
that this means message buffers can be reused, overwriting the data that was
sent and/or updating the `MessageHeader`. If 64-byte word alignment is
necessary for user data, the user must ensure proper padding in the user portion
of the message.



- What exactly does the thread pool invoke?
  So we have messages. These contain an ID for a class, an ID for a function
  (pointer), and an ID for an array index.
- Who manages message allocations?

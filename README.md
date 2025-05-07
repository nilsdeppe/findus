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
affinity (using the hwloc library). Thread migration does not help in HPC
applications since we want the application to get all the CPU cycles. A thread
pool is used (currently with C++11 threads, but this can be generalized) since
OpenMP does not give control over the lifetime of threads, and spawning threads
has non-negligible overhead. Data sharing within a node/process can therefore be
done without any network activity and ultimately boils down to a pointer
copy. Having said that, the goal is to give users some flexibility on how they
copy data within a process. Currently the messages associated with tasks are
handled completely by the runtime system, but in the future we will provide the
ability for users to handle creation and deletion of messages, allowing for
additional reuse of memory allocations and increase performance. A small-message
optimization is available to elide memory allocations for small messages.

#### NUMA nodes

ToyRTS is currently not NUMA aware, and so it is recommended that you spawn one
MPI rank per NUMA node and pin threads to each NUMA node. In the future we will
provide the NUMA awareness to the runtime system, likely through virtual
node-like behavior.

### Task driver and thread pool

ToyRTS is built up of multiple pieces that work together to provide a parallel
runtime system with dynamic load balancing. The intraprocess threads and tasks
are managed by a `ThreadPool` which has two template parameters
1. `MessageType`: the type of the message that is stored by the
   `ThreadPool`. ToyRTS uses a `struct Message_t { std::unique_ptr<char[]> };`
   where the initial `sizeof(rts::MessageHeader)` bytes are an object of type
   `rts::MessageHeader` (described below). A `MessageType` must be movable,
   default-constructible, and have an `execute` function (described below).
2. `ProcessLocalData`: this is additional data passed to the `execute`
   function. In the context of ToyRTS this is usually the task driver.
   **Note: ND thinks we can get rid of this feature completely.**
A `ThreadPool` calls the static method
```cpp
bool MessageType::execute(ThreadPool<MessageType,ProcessLocalData>& thread_pool,
                          uint32_t thread_id, MessageType& message,
                          ProcessLocalData& process_local_data);
```
**Note: ND I think there should be some way to trigger a requeue of a
   message. Currently this is achieved with the return type of the message.**

Interprocess messages are handled by the `DistributedTaskDriver` (DTD for
short). This is actually the runtime system and _all_ communication goes through
it. The `DTD` holds a `ThreadPool` object to drive the local tasking, a
`std::vector<DistributedObjectHolder>`, and various internal bookkeeping
quantities. The central entity in ToyRTS is a `DistributedObject`, discussed
next.

### Distributed Objects

The central entity in ToyRTS is a `DistributedObject`. This is an object in a
process and can be invoked remotely by a `Proxy` handle. You can have
multiple different `DistributedObject`s on a node because the
`DistributedObject` takes a `ParallelComponent` as a template parameter. A
`ParallelComponent` is a specific instance of the more
abstract `DistributedObject`. There is also a `DistributedObjectBase` that
should rarely, if ever, be needed by users. The class hierarchy is:
```cpp
class DistributedObjectBase { /*...*/ };

template <class ParallelComponent>
class DistributedObject : DistributedObjectBase { /*...*/ };


class ParallelComponent0 : DistributedObject<ParallelComponent0> { /*...*/ };

template </* ... */>
class ParallelComponent1 : DistributedObject<ParallelComponent1> { /*...*/ };
```
There are two user-defined parallel components, `ParallelComponent0` and
`ParallelComponent1`. Note that parallel components can be class templates. The
relationship between these classes is that a `ParallelComponent` *is a*
`DistributedObject` *is a* `DistributedObjectBase`.

There are two types of distributed objects:
1. `DistributedObject`: there is exactly one of these per process and they
   are indexed only with which node they are on.
2. `DistributedObjectCollection`: there are multiple objects of this type per
   process and they can be indexed by a custom index that is convertible to a
   `uint64_t`. ToyRTS will always use `uint64_t` as the index into the
   collection, and the collection can be sparse. ToyRTS will track which node
   the target parallel component with a specific index is on for communication
   so that users can only use their own index type. It is a diagnosed error to
   send a message to an element of a collection that does not exist. The
   exception type `rts::MissingElement` is raised upon send attempts and are
   propagated to user code for handling. If the exception is not handled then
   execution terminates.
   
   The class hierarchy is the same for a collection as a distributed object.

#### Sending messages to distributed objects

You can send point-to-point and broadcast messages to distributed object and
collections. Broadcasts on a `DO` will be sent once to each node, while on a
`DOC` each element of the collection will receive the message. To send a message
use
```cpp
template <class Action, class ParallelComponent, class... Args>
void rts::invoke(Args&&... args);

// or for a DistributedObjectCollection
template <class Action, class ParallelComponent, class... Args>
void rts::invoke(const uint64_t index, Args&&... args);
```
Broadcasts are done using
```cpp
template <class Action, class ParallelComponent, class... Args>
void rts::broadcast(Args&&... args);
```

However, distributed objects must get registered with the RTS so that
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

### What else is in a message?


### Serialization


### Reductions


### Threadpool

Each MPI rank has a threadpool that invokes any available actions on an
available core. The hwloc library is used to pin threads to cores.

### Locks

- What exactly does the thread pool invoke?
  So we have messages. These contain an ID for a class, an ID for a function
  (pointer), and an ID for an array index.
- Who manages message allocations?



## Known issues

1. I get `Authorization required, but no authorization protocol
   specified`. Help!
   
   Run `export HWLOC_COMPONENTS=-gl`. This is an issue with hwloc:
   https://github.com/open-mpi/ompi/issues/7701#issuecomment-932587086

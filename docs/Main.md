# findus - A Minimal Parallel Runtime System

findus is a fun little thought experiment turned implementation. The goal is to
provide something akin to a task-parallel runtime system, but keeping the
implementation as simple as possible, and forcing users to deal with MPI for
inter-node communication. Abstracting away all layers of a large parallel system
is difficult, and an active area of research and development in many companies
and universities. Instead, findus explores some of the basic features that one
might want for a hyperbolic PDE solver that runs on a tasking system.

```cpp
class DistributedObjectBase { /*...*/ };

template <class ParallelComponent>
class DistributedObject : DistributedObjectBase { /*...*/ };


class ParallelComponent0 : DistributedObject<ParallelComponent0> { /*...*/ };

template </* ... */>
class ParallelComponent1 : DistributedObject<ParallelComponent1> { /*...*/ };
```

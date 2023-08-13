<!--
    Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)

    SPDX-License-Identifier: CC-BY-SA-4.0
-->

libvirtqueue
-------------

**_This implementation is currently a work in progress_**

This directory contains a library implementation of a virtqueue inspired from
the virtio specification. This is intended to be used as a communication
mechanism between system components for bulk data transfer.
The goal of this implementation is to provide a generic interface for manipulating
and managing a 'virtqueue' connection. This library doesn't contain any code that
interfaces with seL4. It is expected that the user will provide
shared memory regions and notification/signalling handlers to this library.

To use this library in a project you can link `virtqueue` in your
target applications CMake configuration.

This library tries to follow the virtio standard. It currently offers 'used' and
'available' rings, as well as a descriptor table, and two types of virtqueues, one
for the driver side and one for the device side.

The ring implementation and the descriptor table design follow closely the
[virtio paper](https://www.ozlabs.org/~rusty/virtio-spec/virtio-paper.pdf). The
API allows the user to create a new entry in the available ring, chain (scatter)
some buffers linked to that entry, transfer it into the used ring, and pop it.

Use case
----------

The driver wants to add some buffer that will be read by the device (for
example to write something into a serial connection).
    1. The driver creates a new ring entry into the available ring and gets a
       handle back.
    2. The driver uses the handle to chain several buffers linked to that entry.
    3. The driver calls the notify callback to wake up the device.
    4. The device gets the available ring entry, which comes down to getting the
       handle.
    5. The device uses the handle to pop all the buffers linked to it and reads
       them.
    6. Finally, the device uses the handle to transfer the ring entry from the
       available ring to the used ring.
    7. The device notifies the driver that it has processed some queue data.
    8. The driver gets the handle to the used element and iterates through the
       buffers to free them.

ASCII art explanation
----------

Empty rings and descriptor table:
```
                        available ring              descriptor table
first available entry ->[ ]                         [ ]
                        [ ]                         [ ]
                        [ ]                         [ ]
                        .                           [ ]
                        .                           [ ]
                        .                           [ ]
                                                    [ ]
                                                    [ ]
                        used ring                   [ ]
first available entry ->[ ]                         [ ]
                        [ ]                         [ ]
                        [ ]                         [ ]
                        .                           .
                        .                           .
                        .                           .
```

The driver creates an available entry and links a chain of buffers to it. It
populates the buffer and notifies the device.

```
                        available ring              descriptor table
                        [x]------------------------>[x]--.
first available entry ->[ ]                         [ ]   \
                        [ ]                         [ ]   /
                        .                        .--[x]<-'
                        .                       /   [ ]
                        .                      |    [ ]
                                               |    [ ]
                        used ring               \   [ ]
first available entry ->[ ]                      '->[x]
                        [ ]                         [ ]
                        [ ]                         [ ]
                        .                           .
                        .                           .
                        .                           .
```

The device pops the available entry and receives a handle for it. It can then
iterate over the chain of buffers to get the data.

```
                        available ring              descriptor table
                        [ ]               handle--->[x]--.
first available entry ->[ ]                         [ ]   \
                        [ ]                         [ ]   /
                        .                        .--[x]<-'
                        .                       /   [ ]
                        .                      |    [ ]
                                               |    [ ]
                        used ring               \   [ ]
first available entry ->[ ]                      '->[x]
                        [ ]                         [ ]
                        [ ]                         [ ]
                        .                           .
                        .                           .
                        .                           .
```

The device pushes the handle back into the 'used' ring and notifies the driver.

```
                        available ring              descriptor table
                        [ ]              +--------->[x]--.
first available entry ->[ ]              |          [ ]   \
                        [ ]              |          [ ]   /
                        .                |       .--[x]<-'
                        .                |      /   [ ]
                        .                |     |    [ ]
                                         |     |    [ ]
                        used ring        |      \   [ ]
                        [x]--------------+       '->[x]
first available entry ->[ ]                         [ ]
                        [ ]                         [ ]
                        .                           .
                        .                           .
                        .                           .
```

The driver pops the used entry and frees the buffers. Rationale that the driver
frees the buffer is, that it has also allocated and chained them.

```
                        available ring              descriptor table
                        [ ]                         [ ]
first available entry ->[ ]                         [ ]
                        [ ]                         [ ]
                        .                           [ ]
                        .                           [ ]
                        .                           [ ]
                                                    [ ]
                        used ring                   [ ]
                        [ ]                         [ ]
first available entry ->[ ]                         [ ]
                        [ ]                         [ ]
                        .                           .
                        .                           .
                        .                           .
```

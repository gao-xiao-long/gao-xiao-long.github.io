**LevelDB is a fast key-value storage library written at Google that provides an ordered mapping from string keys to string values.**
levelDB是一个google开发的高性能的用于提供字符串类型的key到字符串类型value的有序映射的key-value库。

[![Build Status](https://travis-ci.org/google/leveldb.svg?branch=master)](https://travis-ci.org/google/leveldb)

Authors: Sanjay Ghemawat (sanjay@google.com) and Jeff Dean (jeff@google.com)

# Features
特性
  * Keys and values are arbitrary byte arrays.
  key和value类型为任意的字节数组
  * Data is stored sorted by key.
  数据对key进行有序存储
  * Callers can provide a custom comparison function to override the sort order.
  调用者可以提供一个自定义的比较器来改变排序规则
  * The basic operations are `Put(key,value)`, `Get(key)`, `Delete(key)`.
  基本的操作有: 'Put(key)' 'Get(key)' 'Delete(key)'
  * Multiple changes can be made in one atomic batch.
  可以通过一个atomic batch来进行批量更新操作
  * Users can create a transient snapshot to get a consistent view of data.
  用户可以创建一个快照来获得数据的一致性视图
  * Forward and backward iteration is supported over the data.
  支持对数据进行正向和逆向遍历
  * Data is automatically compressed using the [Snappy compression library](http://google.github.io/snappy/).
  数据会自动使用Snappy进行压缩
  * External activity (file system operations etc.) is relayed through a virtual interface so users can customize the operating system interactions.
  外部活动(比如文件系统的操作)通过一个virtual接口来传递，所以用户可以自定义与操作系统的交互

# Documentation
  [LevelDB library documentation](https://rawgit.com/google/leveldb/master/doc/index.html) is online and bundled with the source code.


# Limitations 限制
  * This is not a SQL database.  It does not have a relational data model, it does not support SQL queries, and it has no support for indexes.
  这不是一个SQL数据库，不存在关系数据模型，不支持SQL查询,并且不支持索引操作。
  * Only a single process (possibly multi-threaded) can access a particular database at a time.
  一个数据库同时只能有一个进程(内部可以多线程)可以访问。
  * There is no client-server support builtin to the library.  An application that needs such support will have to wrap their own server around the library.
  内置的库里面没有提供client-server的支持。需要client-server支持的用户可以自己进行封装。

# Contributing to the leveldb Project 为leveldb项目做贡献
The leveldb project welcomes contributions. leveldb's primary goal is to be
a reliable and fast key/value store. Changes that are in line with the
features/limitations outlined above, and meet the requirements below,
will be considered.
欢迎对leveldb项目做贡献。leveldb最主要的目标就是提供一个可靠且高性能的key/value存储。
如果Changes与上述features/limitations一致，且符合下面的要求，那么我们会考虑接受。


Contribution requirements:
贡献要求：

1. **POSIX only**. We _generally_ will only accept changes that are both
   compiled, and tested on a POSIX platform - usually Linux. Very small
   changes will sometimes be accepted, but consider that more of an
   exception than the rule.
   POSIX only. 我们通常只接受在POSIX平台(通常是Linux)上面编译和测试过的Changes。
   但是考虑到凡事都有例外,一些不符合POSIX only的非常小的changes我们也会接受(??好像翻译不通顺)

2. **Stable API**. We strive very hard to maintain a stable API. Changes that
   require changes for projects using leveldb _might_ be rejected without
   sufficient benefit to the project.
   稳定的API 我们很努力的维护一个稳定的API。需要对接口改变的Changes如果不是对leveldb非常重要我们
   通常会拒绝。

3. **Tests**: All changes must be accompanied by a new (or changed) test, or
   a sufficient explanation as to why a new (or changed) test is not required.
   测试: 所有的Changes必须有一个新的(或者修改过的)test,或者或者对为什么Changs不需要
   test做一个充分的说明。

## Submitting a Pull Request
Before any pull request will be accepted the author must first sign a
Contributor License Agreement (CLA) at https://cla.developers.google.com/.
在pull request被接受前，pull提交者必须先在(https://cla.developers.google.com/)注册一个Contributor License Agreement

In order to keep the commit timeline linear
为了让提交时间表呈线性(??啥意思，待翻译)
[squash](https://git-scm.com/book/en/v2/Git-Tools-Rewriting-History#Squashing-Commits)
your changes down to a single commit and [rebase](https://git-scm.com/docs/git-rebase)
on google/leveldb/master. This keeps the commit timeline linear and more easily sync'ed
with the internal repository at Google. More information at GitHub's
[About Git rebase](https://help.github.com/articles/about-git-rebase/) page.

# Performance
性能

Here is a performance report (with explanations) from the run of the
included db_bench program.  The results are somewhat noisy, but should
be enough to get a ballpark performance estimate.
下面是通过跑db_bench得出的性能报告。结果可能会有些偏差，但是足够用来获得一个近似的性能
预估

## Setup
开始

We use a database with a million entries.  Each entry has a 16 byte
key, and a 100 byte value.  Values used by the benchmark compress to
about half their original size.
我们使用了有100万条记录的数据库。每条记录由16个字节的key和100字节的value组成.
Value使用了基准压缩,大概比实际小了一半。

    LevelDB:    version 1.1
    Date:       Sun May  1 12:11:26 2011
    CPU:        4 x Intel(R) Core(TM)2 Quad CPU    Q6600  @ 2.40GHz
    CPUCache:   4096 KB
    Keys:       16 bytes each
    Values:     100 bytes each (50 bytes after compression)
    Entries:    1000000
    Raw Size:   110.6 MB (estimated)
    File Size:  62.9 MB (estimated)

## Write performance
写性能

The "fill" benchmarks create a brand new database, in either
sequential, or random order.  The "fillsync" benchmark flushes data
from the operating system to the disk after every operation; the other
write operations leave the data sitting in the operating system buffer
cache for a while.  The "overwrite" benchmark does random writes that
update existing keys in the database.
'fill'基准按序或者随机创建了一个全新的数据库。'fillsync'基准在每次操作之后将数据从操作系统
写到磁盘上去。其他写操作将数据写到操作系统缓冲区. 'overwite'基准执行随机写操作更新
数据库中存在的key


    fillseq      :       1.765 micros/op;   62.7 MB/s
    fillsync     :     268.409 micros/op;    0.4 MB/s (10000 ops)
    fillrandom   :       2.460 micros/op;   45.0 MB/s
    overwrite    :       2.380 micros/op;   46.5 MB/s

Each "op" above corresponds to a write of a single key/value pair.
I.e., a random write benchmark goes at approximately 400,000 writes per second.
上面的每个op指的是写单个键/值对。
一个随机写可以达到接近400000次/秒

Each "fillsync" operation costs much less (0.3 millisecond)
than a disk seek (typically 10 milliseconds).  We suspect that this is
because the hard disk itself is buffering the update in its memory and
responding before the data has been written to the platter.  This may
or may not be safe based on whether or not the hard disk has enough
power to save its memory in the event of a power failure.
每个'fillsync'操作耗时(0.3毫秒)比磁盘seed操作小的多(10毫秒).我们猜测是因为硬盘本身
将更新缓存到本身的memory中并且在数据写到盘面上之前返回响应。这样操作是否安全取决于
当发生电力故障时磁盘是否有能力保存还在内存中的数据。(不是很懂??)

## Read performance

We list the performance of reading sequentially in both the forward
and reverse direction, and also the performance of a random lookup.
Note that the database created by the benchmark is quite small.
Therefore the report characterizes the performance of leveldb when the
working set fits in memory.  The cost of reading a piece of data that
is not present in the operating system buffer cache will be dominated
by the one or two disk seeks needed to fetch the data from disk.
Write performance will be mostly unaffected by whether or not the
working set fits in memory.
我们列出了正向顺序读、逆向顺序读及随机读的性能表现。需要说明的是通过基准
创建的数据库很小，所以这个性能适用于数据集合大小可以放到内存中的情况。读取
不在操作系统缓冲区的数据时，耗时会被一次或者两次的磁盘seek操作(将数据从磁盘加载到操作系统)影响
写操作性能大部分不会受是否工作集合大小fit in memery影响。
    readrandom   :      16.677 micros/op;  (approximately 60,000 reads per second)
    readseq      :       0.476 micros/op;  232.3 MB/s
    readreverse  :       0.724 micros/op;  152.9 MB/s

LevelDB compacts its underlying storage data in the background to
improve read performance.  The results listed above were done
immediately after a lot of random writes.  The results after
compactions (which are usually triggered automatically) are better.
LevelDB通过在后台压缩底层存储的数据来提高读性能.上面的结果是在大量随机写操作后进行的。
在压缩后(通常自动进行触发)在执行上述基准结果会更好

    readrandom   :      11.602 micros/op;  (approximately 85,000 reads per second)
    readseq      :       0.423 micros/op;  261.8 MB/s
    readreverse  :       0.663 micros/op;  166.9 MB/s

Some of the high cost of reads comes from repeated decompression of blocks
read from disk.  If we supply enough cache to the leveldb so it can hold the
uncompressed blocks in memory, the read performance improves again:
很多耗时比较长的read操作是由于对从磁盘读取的数据重复解压导致的。如果我们为leveldb
分配足够的cache大小，它就可以将压缩过的数据放到内存，读性能会再次提高。

    readrandom   :       9.775 micros/op;  (approximately 100,000 reads per second before compaction)
    readrandom   :       5.215 micros/op;  (approximately 190,000 reads per second after compaction)

## Repository contents 库中内容

See doc/index.html for more explanation. See doc/impl.html for a brief overview of the implementation.
通过doc/index.html获得更多说明。通过doc/impl.html来大概了解实现原理。

The public interface is in include/*.h.  Callers should not include or
rely on the details of any other header files in this package.  Those
internal APIs may be changed without warning.

公共的接口在include/*.h中。调用者不应该include或者依赖其他文件中的头文件。那些内部apis可能
在没有经过任何警告的情况下改变。

Guide to header files:  头文件指南

* **include/db.h**: Main interface to the DB: Start here
                    DB主要的接口: 从这里开始

* **include/options.h**: Control over the behavior of an entire database,
and also control over the behavior of individual reads and writes.
                        控制整个数据库的行为,并且控制独立的读和写的行为

* **include/comparator.h**: Abstraction for user-specified comparison function.
If you want just bytewise comparison of keys, you can use the default
comparator, but clients can write their own comparator implementations if they
want custom ordering (e.g. to handle different character encodings, etc.)
                            用户自定义的比较函数的抽象。如果你想按字节序对key进行比较
                            可以使用默认的比较器,如果用户想要自己定义排序规则(比如处理不同的字符编码)
                            可以实现自己的比较器

* **include/iterator.h**: Interface for iterating over data. You can get
an iterator from a DB object.
                          遍历数据的接口，可以获得一个DB的迭代器

* **include/write_batch.h**: Interface for atomically applying multiple
updates to a database.
                            原子更新多个数据库改动的接口

* **include/slice.h**: A simple module for maintaining a pointer and a
length into some other byte array.
                       用于维护指向其他byte array的指针和及指针长度的简单模块(??翻译不通顺)

* **include/status.h**: Status is returned from many of the public interfaces
and is used to report success and various kinds of errors.
                        很多公共接口会返回Status类型，它用来指示调用成功或者多种错误。

* **include/env.h**:
Abstraction of the OS environment.  A posix implementation of this interface is
in util/env_posix.cc
OS环境的抽象接口。一个posix实现在util/evn_posix.cc中。

* **include/table.h, include/table_builder.h**: Lower-level modules that most
clients probably won't use directly
                                                大部分用户不会使用的底层模块

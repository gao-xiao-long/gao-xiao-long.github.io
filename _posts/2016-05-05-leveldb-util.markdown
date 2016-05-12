---
layout: post
title: leveldb源码剖析(1)--通用模块(util)
date: 2016-5-5
author: "gao-xiao-long"
catalog: true
tags:
    - leveldb
---

leveldb util目录中提供了通用功能的实现，比如内存管理(arena), 布隆过滤器(bloom), cache等，下面对这些通用功能的实现做下简单的分析。

# arena

leveldb自身实现了一个简单的内存管理器, 对外部暴露如下三个接口:

```C++
char* Allocate(size_t bytes); // 分配指定大小内存
char* AllocateAligned(size_t bytes); // 分配指定大小内存并保证内存对齐
size_t MemoryUsage() const; // arena所占用内存的大小

```

内部的数据结构也比较简单


```C++
std::vector<char*> blocks_; // 系统分配的内存数组
char* alloc_ptr_;           // 指向空闲块起始位置指针
size_t alloc_bytes_remaining_; // 空闲块剩余大小
port::AtomicPointer memory_usage_; // arena使用内存大小
```
结构图:
![结构图](/img/in-post/leveldb/arena.png)

分配过程:

通过Allocate接口申请内存时(AllocateAligned接口类似，不同地方是保证了内存对齐):

- 如果申请的内存大小bytes不大于alloc_bytes_remaining_, 则直接返回当前alloc_ptr_指向位置并且alloc_ptr_ += bytes; alloc_bytes_remaining_ -= bytes;

- 如果申请的内存大小bytes大于alloc_bytes_remaining_

    - 如果bytes大于 kBlockSize / 4 (kBlockSize默认为4096), 则直接通过系统调用申请bytes大小, 并将申请的内存挂接到block_中
    - 如果bytes小于 kBlockSize /4 则通过系统调用申请kBlockSize大小, 将申请的内存挂接到block_中，然后调整alloc_ptr_位置及alloc_bytes_remaining_大小

**从上述实现来看arena仅实现了Allocate接口，没有对应的Free及内存整理等功能，是为leveldb高度定制的。非常适合小块内存的申请。**


#LRU cache
1. 与2的余数计算方式 a&(length-1)
2. C++柔型数组 char key_data[1]的用法
3. 分成16个shadle防止多线程加锁
4. refs的作用， next_hash、next、prev、hash的作用
5. insert时如果key存在会怎么处理。(返回旧的handle，可以用于后面删除？)
6. resize是如何做的？？
7. 空结构体struct Handle {}; 的意义是啥
8. Cache中Prune() ? sharedhash如何hash到不同槽位？
9. 两种强制类型转换不不同点
#ENV&EVN_POSIX

#LOG&logging

#Status

#Slice

#Bloom Filter

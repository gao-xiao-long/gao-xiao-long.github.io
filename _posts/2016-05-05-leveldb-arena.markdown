---
layout: post
title: leveldb1.18源码剖析--通用模块(Arena)
date: 2016-5-5
author: "gao-xiao-long"
catalog: true
tags:
    - leveldb
    - arena
---

leveldb util目录中提供了通用功能的实现，比如内存管理(arena), 布隆过滤器(bloom), cache等，下面对这内存管理器Arena进行分析

arena是leveldb中实现的一个简单的内存管理器, 对外部暴露如下三个接口:

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


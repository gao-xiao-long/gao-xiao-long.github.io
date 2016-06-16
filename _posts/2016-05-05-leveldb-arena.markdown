---
layout: post
title: leveldb1.18源码剖析--通用模块(Arena)
date: 2016-5-5
author: "gao-xiao-long"
catalog: true
tags:
    - leveldb
    - arena
    - 内存管理
---

leveldb util目录中提供了通用功能的实现，比如内存管理(arena), 布隆过滤器(bloom), cache等，下面对这内存管理器Arena进行分析

### Arena

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

下面简单介绍下Linux下的内存管理，温故知新。

### Linux内存管理

**Linux虚拟内存**

像所有现代操作系统一样，Linux将它的物理内存虚拟化。进程并不能直接在物理内存上寻址。而是由Linux内核为每个进程维护一个特殊的虚拟地址空间(virtual adress space)。这个地址空间是线性的。从0开始，一直到某个最大值(64位系统总最大虚拟地址空间为256TB(2^48),而不需要16EB(2^64)如此巨大的地址空间)。在进程访问内存时Linux内存管理单元(MMU)负责将虚拟地内存址映射为物理内存地址。MMU管理的最小单元为页，虚拟地址空间就是由许多页组成的，系统的的体系结构以及机型决定了页的大小(页的大小是固定的)，典型的页大小包含4K(32位系统)和8K(64位系统)。程序被投入运行时，并不是把程序和数据全部装入内存。仅装入当前使用的页面。当需要执行某条指令或使用某个数据而发现他们不再主存时，产生缺页中断，然后内核会介入，把数据从磁盘切换到物理内存中(paging in)，而且对用户透明。由于虚拟内存比物理内存大得多,内核可能需要把数据从内存中切换出来，从而为后面要Page in的页腾出更多空间。因而，内核也需要把数据从物理内存切换到磁盘，这个过程称为Paging out。需要说明的是，进程看到的地址是连续的(虚拟地址)，但实际上程序在物理内存中可能会分散在不同的地址不连续的页中。

**Linux内存布局**

Linux内存地址空间是按照段进行管理的。一个Linux进程标准的内存布局如下(32位与64位布局基本相同)：
![结构图](/img/in-post/leveldb/linux_memory.png)

其中: 蓝色区域表示映射到物理内存的虚拟地址空间，白色区域表示未映射的虚拟地址空间。
Linux会给栈、mmap段、和堆的起始地址一个随机偏离,以防止远程利用漏洞进行攻击的行为。
如上图所示，进程空间的最上面是**栈**，栈里面保存了程序局部变量及函数的返回值。栈大小最高
为RLIMIT_STACK(通常是8M)，如果程序使用的栈大小超出RLIMIT_STACK，则会导致栈溢出，出现段错误(Segmentation Fault)。
栈的下面就是**mmap段**，可以通过mmap()调用将文件内容映射为内存。也可以创建与文件无关的匿名内存映射来存放程序数据(malloc大内存申请会用到)。
mmap下是**堆段**,包含了一个进程的动态内存空间。这个段是可写的，而且他的大小是可以变化的。这部分空间往往是由malloc分配的。
BBS/Data segment/Textsegment等段不再讲述

**glibc动态内存分配器**

动态内存分配器可以理解为对从堆和mmap段上内存的申请和释放进行管理的库和工具。常见的动态内存分配器有ptmalloc2、jemalloc、tcmalloc等。
其中ptmalloc2为glibc的内部使用的分配器。ptmalloc2实现的malloc函数采用"伙伴内存分配算法" + "匿名内存映射”的方式来分配内存。小空间分配
采用"伙伴内存分配算法"在"堆段"进行内存的分配，大空间的分配则使用"匿名内存映射"在"mmap段"进行分配。一般来说"大空间"和"小空间"的临街值为
128KB(可以通过系统调用进行配置)，分配小于等于128KB的空间是在"堆段"实现，而对于更大空间的分配则使用匿名内存映射在"mmap段"来实现。
使用"堆段"进行内存分配的优点是高速简单，缺点是容易产生碎片。在某些大型复杂系统中有可能会出现"疑似内存泄露"现象(free后glibc不会把内存直接归还给系统，而是维护释放的内存)。使用"mmap段"的好处是无需关注碎片,当程序不需要这块内存使用的时候，只需要撤销映射，这块内存就会立即还给系统，缺点是创建一个新的内存映射比从对中返回内存的代价要高。

需要注意的是Linux使用了一种"**投机性内存分配策略(opportunistic allocation strategy)**"。当一个进程向内核请求额外的内存，比如扩大它的"堆段"
或者创建一个新的内存映射时，申请的都是虚拟内存。内核实际上没有分配给进程任何的物理存储。仅当进程对新分配的内存区域进行写操作时，内核才分配一块物理内存(**产生缺页中断,然后分配物理内存**)。

可以参考以下资料以了解更多Linux内存管理相关知识:

1.linux系统编程第二版

2.孙钟秀操作系统教程

3.[内存空间布局](http://duartes.org/gustavo/blog/post/anatomy-of-a-program-in-memory/)

4.[A malloc Tutorial](http://www.inf.udec.cl/~leo/Malloc_tutorial.pdf)

5.[Glibc 内存管理](http://www.valleytalk.org/wp-content/uploads/2015/02/glibc%E5%86%85%E5%AD%98%E7%AE%A1%E7%90%86ptmalloc%E6%BA%90%E4%BB%A3%E7%A0%81%E5%88%86%E6%9E%901.pdf)


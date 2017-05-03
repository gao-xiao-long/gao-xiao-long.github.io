---                                                                                                    
layout: post                                                                                           
title: mmap文件映射方式导致系统平响异常情况分析                                                                   
date: 2017-03-18                                                                                       
author: "gao-xiao-long"                                                                                
catalog: true                                                                                          
tags:                                                                                                  
    - 基础技术                                                                                          
---       

内部某检索系统使用了mmap文件映射的方式加载正排索引及倒排索引。部署方式为物理机独立部署，线上运行了很长时间都很稳定，偶尔也有波动。最近将其迁移到公司内虚拟化平台时(类似Docker)却发现性能频繁波动，经常出现平响飙升的情况。对服务SLA有很大影响。经过排查发现平响飙升的原因是由于系统mmap内存映的内存空间被回收(page reclaim)到磁盘导致。排查期间对内核页面回收机制有了更进一步的的理解，特粗略记录下来。

#### 相关知识：页面回收
Linux内核的基本设计决策之一是：缓存通常不是固定长度的，可以动态增长，直到用尽所有的物理内存。向物理内存填充信息是件好事，因为未使用的内存实际上是资源的浪费，但是内核需要有一种机制，以便在有更紧急的任务需要内存时能够收缩缓存,这种机制称为“页面回收”。

**页面回收(page reclaim)：**
内核将很少被使用到的内存换出到块设备(swap区)，以提供更多的主存，这种机制称为页交换(swapping)或者换页(paging)；如果一个很少使用的页的后备存储器是一个块设备(例如mmap文件映射)，则无需换出被修改的页，直接将页中数据与块设备同步，并腾出页帧；前面两种场景连同选择很少使用页的策略，统称为页面回收。

页面回收主要是将页面交换到swap区或者同步到块设备。那么那种页可以被swap，那种页可以同步到块设备呢

**可被换出的交换区的页：**
* 类别为MAP_ANONYMOUS的页：例如，使用mmap匿名映射方式创建的页(使用malloc()等动态申请大内存时会使用这种方式)，它没有关联到任何文件(或属于/dev/zero的一个映射)
* 进程的私有映射：例如，通过mmap创建映射时指定MAP_PRIVATE标志(进程采用写时复制，对内存的任何操作不影响源文件)，这种情况下文件不能再作为后背存储器，因为此时不能再从文件中恢复它的内容
* 用于实现某种通信机制的页：例如，用于在进程之间交换数据的共享内存页。

**可被同步到块设备的页：**
* 文件读写过程中用于缓存数据的页面(read()及write()调用等)
* 用户地址空间中用于文件内存映射的页面(mmap()调用，指定MAP_SHARED标示)


**页面回收时机：**

用于页缓存的物理页面无法被页面的使用者主动释放，因为它们不知道这些页面何时应该被释，Linux 操作系统使用如下这两种机制检查系统内存的使用情况，从而确定可用的内存是否太少从而需要进行页面回收。
- 周期性的检查：这是由后台运行的守护进程kswapd完成的。该进程定期检查当前系统的内存使用情况，它通过判断watermark[min/low/high]这三个值来判断是否进行页面回收，当系统空闲内存低于watermark[low]时，会启动回收，直到内存空闲数量达到watermark[high]后停止回收。可以通过查看 /proc/zoneinfo 得到min/low/high的值，单位为"页数" (一台机器上可能有多个zone，如DMA、DMA32、Normal等，每个zone都有watermark值，这里不再不在展开，想要详细了解的可以参考文章最后面列出的参考资料),一般线上系统设置的watermark[low]在几十兆左右。
- “内存严重不足”事件的触发：在某些情况下，比如，操作系统忽然需要通过伙伴系统(malloc()等调用触发)为用户进程分配一大块内存，或者需要创建一个很大的缓冲区，而当时系统中的内存没有办法提供足够多的物理内存以满足这种内存请求，这时候，操作系统就必须尽快进行页面回收操作，以便释放出一些内存空间从而满足上述的内存请求。（这种页面回收方式也被称作“直接页面回收”，会阻塞掉操作进程。）

如果操作系统在进行了内存回收操作之后仍然无法回收到足够多的页面以满足上述内存要求，那么操作系统只有最后一个选择，那就是使用 OOM( out of memory )killer，它从系统中挑选一个最合适的进程杀死它，并释放该进程所占用的所有页面。



**页面回收细节：**

页面回收主要包括回收页面选择算法及针对单页面的回收流程。Linux 中的页面回收是基于 LRU(least recently used，即最近最少使用 ) 算法的。LRU 算法基于这样一个事实，过去一段时间内频繁使用的页面，在不久的将来很可能会被再次访问到。反过来说，已经很久没有访问过的页面在未来较短的时间内也不会被频繁访问到。因此，在物理内存不够用的情况下，这样的页面成为被换出的最佳候选者。具体涉及到内部的逻辑不再细述。重点讲下如果选定好了需要回收的页面后，大概的回收流程。

当内核通过一定算法选择了需要回收的页面后，会调用shrink_page_list函数从参数取得一组选中的回收页，并试图将各页写回到对应的后背存储器。主要的逻辑流程如下：
![图](/img/in-post/shrink_page.png)

说明：
1. 对于每一个页，内核都需要判断是否保留当前页。例如，如果页面被锁定了(比如通过mlock()调用)，则会对其保留
2. 线上机器如果关闭了swap功能，则采用匿名映射的页将不会被换出
3. 内核将脏数据回写到底层存储介质分为同步回写和异步回写两种方式，采用异步回写方式时可能不会立即释放页面，而是等到下一次处理此页时，这里不再展开。

#### 回到问题
回到我们系统遇到的问题上，系统使用了mmap将文件通过MAP_SHARED方式映射到了内存。且线上机器均默认关闭了swap。性能出问题的时间点(14:35左右)进程现场及集机器整体现场图如下：

**出问题时程序所在机器整体状态如下：**
![图](/img/in-post/machine_status.png)
说明：
* CPU_PHYSICAL_CORE: cpu物理核数
* CPU_INTERRUPT:     cpu软中断次数
* CPU_IDLE:          cpu空闲率
* CPU_SERVER_LOADAGV: cpu负载
* MEM_CACHE: 用于缓存的物理内存使用量
* MEM_FREE:物理内存空闲量(包括MEM_CACHE)
* CPU_SYS：内核态CPU时间比率
* CPU_USER: 用户态CPU时间比率

**出问题时程序相关状态如下：**
![图](/img/in-post/proc_status.png)

说明：
* costtime_avg：程序平均响应时间
* log_pv_cnt：    程序处理的访问请求数
* io_read_kb：    程序读IO大小
* resident_mem:   程序占用内存大小
* proc_cpu_usage: 程序对CPU的利用率
* proc_cpu_usage_per_core:  每个核对CPU的利用率
* proc_io_write_kb：程序写IO大小
* proc_net_fd_num: 网络句柄大小

**分析：**
1. 从上面机器整体看到，程序运行在16核物理机上，出问题的时间点CPU整体正常(IDLE大约80， 单核load avg大概在1.25), 排除是机器CPU打满导致的问题
2. 程序占用的内存在出问题的时间段明显降低，且后续内存又趋于稳定，并且读IO明显增多，明显出现了mmap被“页面回收”
3. 在整体PV稳定的情况下，出问题的时间点程序处理请求的平响明显增大，且整体处理的请求量明显变少，且网络连接数明显增多，即，程序的处理性能明显降低。结合内存曲线，及cpu软中断等曲线，印证了是程序需要重新建立mmap映射耗时较长导致。

**解决方案：**

使用mmap映射时指定MAP_LOCKED参数：指定此参数后mmap()函数会调用mlock()将内存区域锁定，防止被换出到磁盘，但是如果不是root账号只能锁定RLIMIT_MEMLOCK大小的内存(x86_64下默认为64K)，需要调大此参数或修改为ulimited


毕

#### 参考：

1. 深入Linux内核架构
2. [Linux2.6中的页面回收与反向映射](https://www.ibm.com/developerworks/cn/linux/l-cn-pagerecycle/)
3. [Deep dive into linux memory management](http://balodeamit.blogspot.com/2015/11/deep-dive-into-linux-memory-management.html)
4. [How the Linux kernel divides up your RAM](https://utcc.utoronto.ca/~cks/space/blog/linux/KernelMemoryZones)
5. [Kernel Documents/mmap](http://kernel.taobao.org/index.php?title=Kernel_Documents/mmap_18_32)
6. [Kerynal Documents/mm sysctl](http://kernel.taobao.org/index.php?title=Kernel_Documents/mm_sysctl)

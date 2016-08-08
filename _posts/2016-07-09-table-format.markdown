---
layout: post
title: leveldb1.18源码剖析--SSTable格式详解
date: 2016-7-9
author: "gao-xiao-long"
catalog: true
tags:
    - leveldb
    - sstable
---

SSTable为Sorted String Table的简称，用来存储一系列有序Kev-Value对。LevelDb中不同层级下有多个SSTable文件(.sst)，下面介绍单个SSTable文件的静态结构。

![结构图](/img/in-post/leveldb/sstable.png)

单个SSTable文件的格式如上图所示，文件由五大部分组成：Data Blocks, Meta Blocks, MetaIndexBlock, DataIndexBlock, Footer。其中：
Data Blocks: 存储一系列的按序排列的Key-Value数据
Meta Blocks: ??待补充 如果数据库打开时指定了过滤策略(FilterPolicy)则存储一系列的filter
MetaIndexBlocks:  ??待补充
DataIndexBlocks: ??待补充
Footer: 固定大小，??待补充
下面对这五大块进行详细的讲解

## Data Block

Data Block是基于block_builder.cc生成的。存储了有序的Key-Value对，内部的基本结构如下:

![结构图](/img/in-post/leveldb/data_block.png)

其中

* block data为实际的key-value值

* type目前只有0或者1。0表示block数据未压缩，1表示使用了Snappy压缩(LevelDB中的压缩以block为单位)。

* crc 表示block data和type的CRC校验值

* 除了Data Block 所有的block都有type和crc字段，后面讲述meta block或其他block时不再表示。
* 每个block大小通过options.block_size指定的，默认为4K。实际情况中某个block有可能超过4K，原因在table_builder.cc的Add逻辑中
判断逻辑为(estimated_block_size >= options.block_size) 可能会出现Add完一个key后block超出block_size的情况。

当存储一个Key时LevelDB采用了前缀压缩(prefix-compressed)，由于LevelDB中Key是按序排列的，这可以显著的减少空间占用。另外，每间隔k个keys(目前版本中k的默认值为16)，leveldb就取消使用前缀压缩，而是存储整个key(我们把存储整个key的点叫做重启点)。这样的好处是提高在block中检索key的速度。在block中随机检索一个key时，可以先对重启点进行二分查找，缩小查找范围，然后再遍历查找。如果没有重启点，在block中查找某个key的时候只能顺序遍历，因为如果要知道第i+1条记录需要知道第i条记录的key，如果要恢复第i条记录需要知道第i-1条记录的key，一直这样递归下去，才能知道key值。
另外，从数据结构上看，leveldb还使用了varint类型来进一步对数据进行压缩，varint对整数类型进行边长编码，比如可以将一个4字节的int32值最短可以编码成1个字节表示，最长编码成5个字节，对于小整数来说，压缩效果很明显。(要了解前缀压缩、Varint编码、CRC校验等基本知识可以参照phylips的"LevelDB SSTable格式详解")

## Meta block
Meta block用来存储一些元信息。目前的版本中仅存储了filter信息。如果打开数据库时指定了"FilterPolicy", 那么每个Table中都会存储一个filter block。leveldb中默认的FilterPolicy为bloom filter。在查找某个key时(DB::Get())，可以先通过FilterPolicy来判断key是否在某个SSTable中，如果不存在，则直接跳过此SSTable，避免对此SSTable进行更进一步的磁盘访问操作。
table的filter block中存储了一系列的filters。每个filter又是由一系列的key生成的，第i个filter由在sstable中文件偏移为[i*base...(i+1)*base]的所有key生成的,base默认为2KB。生成filter的操作在table_builder.cc中，由TableBuilder::Flush()函数调用。当某个block超过了options.block_size时，调用TableBuilder::Flush()将此block刷新到磁盘，并调用FilterBlock::StartBlock(block_offset)函数，StartBlock函数根据此block在文件中的offset来判断是否创建新的filter。由此可见，一个filter可能会对应多个block，一个block肯定不会跨越两个filter。访问时先从data index block中得到data block在文件中的offset，然后通过offset计算出该block在哪个filter，之后就可以直接读取该filter数据。block filter文件组织格式如下:

![结构图](/img/in-post/leveldb/filter_block.png)

(PS: lg(base)存储的是对2取对数的值，如果base=2KB，那么lg(base) = lg(2*1024) = 11)
通过调用FilterBlockReader::KeyMayMatch(block_offset, key)可以判断某个key是存在，通过block_offset找到filter位置，然查找某个key是否在filter中。
2. 为什么要每2k建立一个filter 这样的目的是啥？性能，or else？ 这样是为了配合Get操作，读取某个block前先读取此block的filter，如果没有数据就跳过此block？


## Index Block
索引优化：
 我们知道在Meta Index block和Data Index Bock这两个Block中存放指向Data Block和Meta Block的索引。对于Data Index BLock来说，他的每个KeyValue值均指向一个个Data Block,通过这个KeyValue值就可以寻址到对应的Data Block。按照常理来说，我们可以采用它指向的那个Data Block中最大或者最小的按个key值作为索引的key值，这样当给定某个key的时候可以先查看索引，来定位他应该位于哪个Data Block中。但是LevelDB对策进行了优化，不需要采用Data Block中出现的key值作为索引中的key，它只需要改key>=对应的那个DataBLock的最大的key，同时 < 下一个包窗口中最小的key即可。
 也就是说这就提供了一种选择空间，可以缩阴选择长度更小的key。距离说明，假设当前Data BLock最大的kye是"apple on the tree" 下一个DataBlock的最小key是"fly in the sky" 那么索引中就可以选择b作为该Data Block中的key，而不是非要将apple on the tree存储在索引中， "b" 显然比"apple on the tree" 占用的空间少多了。 对最后一个DataBlock来说，相对比较特殊，因为没有下一个Data BLock了，因此只要找到满足>=改Block的最大的key的key就可以了。关于如何确定满足这样条件的key，LevelDB已经将这部分逻辑做了很好的封装，用户通过自己的Compparator实现，就可以定制这些逻辑。

 实际实现是通过Comparator这两个函数
 void FindShortestSeparator(std::sting *start, const Slice& limit) const;
 void FindShortSuccesor(std::string *key) const;
 来找到满足条件的key， 其中第一个函数用于找打位于[start, limit)之间的最短字串，第二个函数则永不找到>=*key的后继者。
 SSTable 在生成索引的KeyValue值时会调用这两个函数，来决定key。用户可以选择定义自己的Comparator实现。
 在uitl/comparaotr.cc中的BytewiseComparatorImpl实现中，FindShortestSeparator会查找start和limit之间第一个不相同的字节，如果
如果在打开数据

每个这样的指针被称为一个BlockHandle，包含如下信息：
offset: varint64
size: varint64
如图所示，Footer中会有一个meta index handle用来指向Meta index block，还有一个dtaa index handle 用来指向Data Index Block， 然后这两个Index Block，实际上是一系列Data Blocks和Meta Blocks的索引，其内部的KeyValue值就包含了指向文件中的一系列Meta Block和Data Block的handle。

(1) 文件内的key/value对序列有序排列，然后划分到一系列的data blocks中。这些个blocks一个接一个的分布在文件的开头。每个data block会根据block_builder.cc里面的代码进行格式化，然后进行可选择的压缩。
(2) 在数据blocks之后存储的一些meta blocks，目前支持的meta block类型会在下面进行描述。未来也可能添加更多的meta block类型。每个meta block也会根据block_builder.cc里的代码进行格式化，然后进行可选择的压缩。
(3) A "metaindex block" 会为每个meta block保存一条记录，记录的key值就是meta block的名称。value值就是指向该meta block的一个BlockHandle。
(4) An "index" block 会为meige data block 保存一条记录， key值是>=对应的data block里最后的那个key值，同事在后面那个data block第一个key值之前的那个key值，value值就是指向该meta block的一个BlockHandle。
(5) 文件最后一个是定长的footer，包含了metaindex和index这两个block的BlockHandle，以及一个magic number
    metaindex_handle: char[p]; // block handle for metaindex
index_handle: char[q]; // block handle for index
padding: char[40-p-q]; // 0 bytes to make fixed length
magic: fixed64; // ==0xdb........
所以footer的总大小为40+8=48

5. meta index和data index是两个相对特殊的block，其内部的KeyValue具有特殊含义，如图所示，目前的LevelDB版本中并未实现Meta block
需要着重解释的是： 上图除了DataBlock外，其他类型的Block： MetaBlock、MetaIndexBlock、DataIndexBlock也都属于Block这一个相同结构，都具有BlockDatay，Type，CRC这三个部分，为了简化起见，并没有画出这一级，只是画出了存在于这些Block中的KeyValue内容。同事对于BlockData来说，也是具有内部结构的，如上图中DataBlock部分换出的那样，BlockData又由如下几部分组成：KeyValue数据，Restart数组，RestartsNum。其中KeyValue部分结构如图顶部所示，Restart数据和RestartsNum都是与前缀压缩相关的结构，Restart数组记录了重启点的偏移位置，RestartsNum则是重启点的个数，也即Restart数组的元素个数。他们实际上担当了BlockData内部数据索引的角色，具体细节见下面的关于前缀压缩机制的分析。



参考:
    SST格式详解(1)
    http://www.samecity.com/blog/Article.asp?ItemID=100(leveldb日知录之四：SSTable文件)



1. 关于PosixRandomAccessFile 使用pread方法好处，写下P48。线程共享文件表？

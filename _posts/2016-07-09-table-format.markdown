单个SSTable文件的格式如上图所示，文件由五大部分组成：Data Blocks, Meta Blocks, MetaIndexBlock, DataIndexBlock, Footer。除Footer部分外，其余都是一些block组成的结构，每个block是由多个KeyValue组成的数据块。文件包含一些内部指针。每个这样的指针被称为一个BlockHandle，包含如下信息：
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

另需要注意如下几点：
1. 图中Handle类型，也就是上面提到的BlockHandle，是由offset和size组成
2. handle中的size大小不包括Type和CRC这两个部分
3. CRC校验的内容包含了Type部分。
4. block type目前有两种： 0-不压缩，1-snappy压缩
5. meta index和data index是两个相对特殊的block，其内部的KeyValue具有特殊含义，如图所示，目前的LevelDB版本中并未实现Meta block
需要着重解释的是： 上图除了DataBlock外，其他类型的Block： MetaBlock、MetaIndexBlock、DataIndexBlock也都属于Block这一个相同结构，都具有BlockDatay，Type，CRC这三个部分，为了简化起见，并没有画出这一级，只是画出了存在于这些Block中的KeyValue内容。同事对于BlockData来说，也是具有内部结构的，如上图中DataBlock部分换出的那样，BlockData又由如下几部分组成：KeyValue数据，Restart数组，RestartsNum。其中KeyValue部分结构如图顶部所示，Restart数据和RestartsNum都是与前缀压缩相关的结构，Restart数组记录了重启点的偏移位置，RestartsNum则是重启点的个数，也即Restart数组的元素个数。他们实际上担当了BlockData内部数据索引的角色，具体细节见下面的关于前缀压缩机制的分析。

数据压缩：
SStable中的压缩是以Block为单位进行的。目前只支持一种压缩方式：Snappy, 用户也可以选择不进行压缩。该压缩算法本事的实现并不在LevelDB内，用户如果使用的话需要首先安装Snappy，这是Google开源的一个压缩裤。
当然，即使没有安装Snappy，LevelDB也是可以工作的。因为为了保证可一致性，LevelDB中对该压缩算法的调用也做了一层封装，如下：port/port_posix.h
inline bool Snappy_Compress(const char* input, size_t length, ::std::string* output) {
#ifdef SNAPPY
    output->resize(snappy::MAxCompressedLength(*leth));
    size_t outlen;
    snappy::RawCompress(iunput, length, &(*output)[0], &outlen);
    output->resize(outlen);
    return true;
}
return fa;se;


Varint编码：
基本原理：
从上面图中可以看出，很多字段都是varint类型的。varint是对整数类型进行了变长编码。比如int32原本只有4个字节，而编码后最短只需要一个字节，最长需要5个字节。在key。value长度都很小的情况下，采用varint编码方式所带来的结构信息的空间节省会非常明显。
在varint编码中，编码后的每个字节的最高位为1表明，下一个字节也是当前数组的组成部分，这样对于一个varint来说除了足有一个字节最高位0为0外，其他自己的高位都是1。
比如对于整数1， 只需要一个字节的存储： 00000001.更复杂的比如400，他的二进制格式是00000001 10010000.按照7位一组就是10000100, 0000011,因为已经没有下一个非零值，所以编码后的结果是10010000000000001。解码过程正好与指向发，去掉最高位后得出00100000000011，发转后得到最终结果000000

varint编码解码在util/coding.cc里忙。

CRC较远

CRC较远的基本思想是利用线性编码理论，在发送端根据要传送一个n比特的帧或者报文，发送器生成一个r比特的序列，成为帧检测序列，这样所形成的帧将由(n+r)比特组成。这个帧刚好坑内某个预先确定的数整除。接收器用相同的数出去外来的帧。如果无余数，认为无差错

前缀压缩：
基本原理：
 key的存储采用了前缀压缩机制，前缀压缩的概念很简单，就是对于key的相同前缀，尽量只存储一次以节省空间。但是对于SSTable来说他并不是对整个block的所有key进行一次性的前缀压缩，而是设置了很多区段，处于同一区段的key进行一次前缀压缩，每个区段的起点就是一个重启点，之所以设置很多区段就是为了更好的支持随机读取，这样就可以在block内部对重启点进行二分，然后再在单个区段内遍历即可找到对应key值的记录，随意分段压缩就承担了block内部二级索引的功能。如果整体进行前缀压缩，那么就没法在block内进行二分，只能进行遍历，因为如果要恢复第i+1条记录需要知道第i条记录的key，如果要恢复第i条记录需要知道第i-1条记录的ye，如此递归下去，意味着都要从第一条开始，才能将key恢复出来。这样block内部的查找就必须是顺序的了。o
 前缀压缩机制导致每条记录需要记住它对应的kye的共享长度和非共享长度。所谓key的共享长度，是指当前这条记录的key与上一条记录的key公共前缀的长度，非共享长度则是去掉相同部分后同部分长度。这样当前这条记录只需要存储不同的那部分key的值即可。
 同事分段压缩，又导致在block尾部需要用一个数据做记录这些重启点的offset，同事block的最后4个字节则被固定用来保存重启点的个数，同事每个重启点都是一个4字节的正数，这样知道了block的起始地址及长度后，就很容易计算出restarts_了。加上这些级之后，导师机械过程变得比较复杂，但是解读书还是很快的。灵位每条记录的key的共享长度，key的非共享长度，value长度本身继续你改了varint32边长编码，这又是一个节省空间的优化。

 设计前缀压缩的读取逻辑在table/block.cc  写入逻辑在table/block_builder.cc中。对于读取过程来说，在block::iter::ParseNextKey()中，需要理解其对RestartPoint的处理，首先需要明确restart_index_所代表的意义，他是当前的Entry所属的RestartPoint

 索引优化：
 我们知道在Meta Index block和Data Index Bock这两个Block中存放指向Data Block和Meta Block的索引。对于Data Index BLock来说，他的每个KeyValue值均指向一个个Data Block,通过这个KeyValue值就可以寻址到对应的Data Block。按照常理来说，我们可以采用它指向的那个Data Block中最大或者最小的按个key值作为索引的key值，这样当给定某个key的时候可以先查看索引，来定位他应该位于哪个Data Block中。但是LevelDB对策进行了优化，不需要采用Data Block中出现的key值作为索引中的key，它只需要改key>=对应的那个DataBLock的最大的key，同时 < 下一个包窗口中最小的key即可。
 也就是说这就提供了一种选择空间，可以缩阴选择长度更小的key。距离说明，假设当前Data BLock最大的kye是"apple on the tree" 下一个DataBlock的最小key是"fly in the sky" 那么索引中就可以选择b作为该Data Block中的key，而不是非要将apple on the tree存储在索引中， "b" 显然比"apple on the tree" 占用的空间少多了。 对最后一个DataBlock来说，相对比较特殊，因为没有下一个Data BLock了，因此只要找到满足>=改Block的最大的key的key就可以了。关于如何确定满足这样条件的key，LevelDB已经将这部分逻辑做了很好的封装，用户通过自己的Compparator实现，就可以定制这些逻辑。

 实际实现是通过Comparator这两个函数
 void FindShortestSeparator(std::sting *start, const Slice& limit) const;
 void FindShortSuccesor(std::string *key) const;
 来找到满足条件的key， 其中第一个函数用于找打位于[start, limit)之间的最短字串，第二个函数则永不找到>=*key的后继者。
 SSTable 在生成索引的KeyValue值时会调用这两个函数，来决定key。用户可以选择定义自己的Comparator实现。
 在uitl/comparaotr.cc中的BytewiseComparatorImpl实现中，FindShortestSeparator会查找start和limit之间第一个不相同的字节，如果

 几个问题：

 看SSTable的格式图可知： Data Index BLock和 Meta Index　ＢＬｏｃｋ都是ｂｌｏｃｋ，另外我们也知道对于ＳＳＴａｂｌｅ文件来说，有一个参数及block_size。如果block_size很小，会不会导致出现多个Data Index BLock 和 Meta Index Block，而Footer中只能有两个handle，那么到底改怎么处理呢。
查看下table/table_builder.cc中代码可知，对于普通的block，在Add(key,value)过程张会判断estimated_block_size>=r->options.block_sze, 古国满足改掉见，会被Flush出去。但是对于Data Index Block 和Meta Index Block这两个block，他们的写出是在TableBUilder：：Finish()中完成的。对于Meta Index Block， 目前版本中只提供了一个空的Block，对于Data Index Block，则对应着r->index-block内容，也就是说block_size这个参数试试用来限制普通block的大小的，而对于Data Index Block和Meta Index Block，无论多大都会被写成一个block。o
Q: 由于同一个KeyValue记录不会跨越两个不同的BLock，因此block大小就无法保证严格等于block_size，那么block大小同事是大于规定的block_size还是小于block_size呢。
    结果是大于，即在组装block时，判断条件是：如果当前block的大小>=block_size,就结束改block
    ，同事第一个不满足这个条件的那个KeyValue也会被写入改block，也即是采用的检查夨落是写入后检查而不是写入前。可以换个角度考虑，如果是保证实际block大小<=block_size，那么极端情况下，比如我们的单个value就很大，已经超过了block_size，那么对于这种情况，SSTable就没法进行存储了，所以通常，实际的Block大小都是要略微大于block_size的。
Q 在开启压缩的情况下，block大小是指压缩前还是压缩后呢
这首先取决于block大小的使用场景，比如是用来与block-size进行比较以限制block大小呢，还是block handle中的size字段。 block handle中的size 记录就是block 压缩后的实际大小。而在于block-size比较以限制block大小事，册用的是压缩前的大小，原因是这样的： 与block_size的比较实在每次Add一个keyvalue就需要机型的，但是如果要知道压缩后实际的大小，那就么就需要实际的进行一次呀USO才能小子，zheyagnkcpu开销就太大了额，因此直接使用前所欠的大小进行比较是更合理的选择。素银采勇压缩式，假设压缩率是1/3 那么实际存储的爆粗口大小
乳沟CRC校验失败改如何处理：
首先CRC校验代码位于table/format.cc中的ReadBLock函数，如果校验失败，该含税会返回一个状态Status::Corruption("block checksumn mismatch")同事*block=NULL。而读取时，该函数又会被table/table.cc真够的Table：：BlockReader上层函数调用，如果ReadBLock失败，可以看到它会林勇


参考:
    SST格式详解(1)
    http://www.samecity.com/blog/Article.asp?ItemID=100(leveldb日知录之四：SSTable文件)



1. 关于PosixRandomAccessFile 使用pread方法好处，写下P48。线程共享文件表？

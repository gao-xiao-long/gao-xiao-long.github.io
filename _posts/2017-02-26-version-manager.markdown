---
layout: post
title: leveldb1.2源码剖析--版本管理(Version)
date: 2017-2-26
author: "gao-xiao-long"
catalog: true
tags:
    - leveldb
---

在LevelDB中，LSM树由一系列的SST文件组成。每一次Compaction操作后，都会生成新的SST文件，合并完成之后，合并输入文件(input files)则可以丢弃。但是有可能get或者迭代器操作还需要这些input files。所以，这些文件不能立刻删除。需要等get操作执行完成或者迭代器释放之后才可以删除。为了管理这一系列的SST文件，LevelDB引入了版本(Version)的概念。下面来探究下具体的实现。

### 实现原理--引用计数

在LevelDB中，保存LSM树的SST文件列表的数据结构被称为**version**. 在每次Compaction结束或者memtable被刷新到磁盘后，都会生成一个新的version来代表更新之后的LSM树. 其中有一个被叫做**current**的version代表当前最新的LSM树结构。
新的get请求或者新的迭代器在整个生命周期内将会使用**current version**。所有正在被get或者迭代器使用的version都会被保留。不被任何get或者迭代器使用的version将会被删除。并且如果某个SST文件也没有被任何version使用，它也将会被删除。下面举例说明：
假设一开始一个version有三个文件：

```c++
v1={f1,f2,f3} (current)
files on disk: f1, f2, f3
```

此时创建了一个迭代器

```c++
v1={f1,f2,f3} (current, used by iterator1)
files on disk: f1, f2, f3
```

之后一个memtable数据flush成SST文件f4, 新版本被创建

```c++
v2={f1,f2,f3,f4} (current)
v1={f1,f2,f3} (used by iterator1)
files on disk: f1, f2, f3, f4
```

现在f2,f3,f4通过Compation合并成一个新的文件f5。新版本被创建

```c++
   v3={f1,f5} (current)
   v2={f1,f2,f3,f4}
   v1={f1,f2,f3} (used by iterator1)
   files on disk: f1, f2, f3, f4, f5
```

现在v2没有被任何操作引用，也不代表最新的version，可以将其删除，文件f4一并被删除，因为它没有被任何版本引用。v1现在还在被iterator1占用，所以不做任何处理。

```c++
   v3={f1,f5} (current)
   v1={f1,f2,f3} (used by iterator1)
   files on disk: f1, f2, f3, f5
```

假设迭代器iterator1被释放

```c++
   v3={f1, f5} (current)
   v1={f1, f2, f3}
   files on disk: f1, f2, f3, f5
```

现在版本v1不再被引用，可以将其删除。并且f2,f3文件可以一并被删除

```c++
  v3 = {f1, f5} (current)
  files on disk: f1, f5
```

上述的逻辑使用了**引用计数**方式实现，每个SST文件及version都有一个引用计数。

当创建一个新的version时，会对此version中的文件引用计数都加1。当version过时后，version中的所有文件引用计数都会减1。当一个文件
的引用计数降为0后，那么可以删除此文件。

每个version也有一个引用计数。当一个version被创建后，引用计数为1。当次version不再是最新的version后，
对应的引用计数减1。任何需要再某个version上进行的操作都会将version引用计数加1，操作结束后引用计数减1。当一个version的引用计数变为0后，则将此version删除。

逻辑讲清楚了，下面看下LevelDB中的具体代码实现。

### 实现分析

LevelDB涉及到版本控制的类或者结构主要有：Version、VersionSet、VersionEdit、VersionSet::Builder、FileMetaData、MANIFEST

LevelDB在给定时间的某个状态被称为version(Version类表示)。对version的任何修改都被视为一次version edit(VersionEdit类表示)。一个version由一系列的version edit构成。即，version + version edit + version eidt + ... = new version。其中new version的生成由VersionSet::Builder类完成。前面讲过，由于get操作或者迭代器的引用，系统同一时刻可能存在多个version。系统存在的version集合就用VersionSet来表示。FileMataData用于存储SST文件的元数据信息，比如文件名、被引用次数、最大最小key等。对SST文件的引用计数就是通过操作FileMataData实现。MANIFEST则是用于持久化当前系统状态，保证系统重启后数据一致性。下面看下主要类的定义。

#### FileMetaData

LevelDB使用FileMEtaData表示SST文件元信息。数据结构如下:

```c++
struct FileMetaData {
  int refs;                   // 引用计数
  int allowed_seeks;          // Compaction使用(暂不展开)
  uint64_t number;            // SST文件命名规则为(number + .sst),以number即可表示SST文件名称。
  uint64_t file_size;         // 文件大小
  InternalKey smallest;       // SST文件中最小的key。(SST文件中key按序排列)
  InternalKey largest;        // SST文件中最大的key。

  FileMetaData() : refs(0), allowed_seeks(1 << 30), file_size(0) { }
};
```

结构中refs变量就是上面提到的引用计数实现。每引用一个version后，version中对应的所有文件的引用计数会加1，version过时后引用计数减1。

下面是Version类析构函数代码, verison析构后会将FileMetaData引用计数减1。当引用计数为0时，直接删除元信息数据结构。文件的物理删除则是在每次Compation之后通过DBImpl::DeleteObsoleteFiles()实现。

```c++

// 析构函数，对FileMetaData引用计数减1
Version::~Version() {
  assert(refs_ == 0);

  // Remove from linked list
  prev_->next_ = next_;
  next_->prev_ = prev_;

  // Drop references to files
  for (int level = 0; level < config::kNumLevels; level++) {
    for (size_t i = 0; i < files_[level].size(); i++) {
      FileMetaData* f = files_[level][i];
      assert(f->refs > 0);
      f->refs--;
      if (f->refs <= 0) {
        delete f;
      }
    }
  }
}
```


#### Version

Version类的定义在db/version_set.h中，主要的成员变量如下:

```c++
  VersionSet* vset_;            //  Version所属于的VersionSet
  Version* next_;               //  Version在VersionSet中以双向链表组织，指向下一个Version
  Version* prev_;               //  Version在VersionSet中以双向链表组织，指向上一个Version
  int refs_;                    //  Version引用计数

  // 每个level对应的文件列表
  std::vector<FileMetaData*> files_[config::kNumLevels];

  // Compaction操作相关结构，暂时不展开
  FileMetaData* file_to_compact_;
  int file_to_compact_level_;
  double compaction_score_;
  int compaction_level_;
```

从结构中看到Version主要是通过files_[config::kNumLevels]维护了一个带有层级关系的SST文件列表，以及通过
next_和prev_维护了一个version的双向链表。

#### VersionEdit

VersionEdit的主要成员变量如下:

```c++
  // 某个level下删除的文件
  typedef std::set< std::pair<int, uint64_t> > DeletedFileSet;
  DeletedFileSet deleted_files_;

  // 某个level下添加的文件
  std::vector< std::pair<int, FileMetaData> > new_files_;
  std::string comparator_; // 比较器名称
  uint64_t log_number_;    // WAL log file number
  uint64_t prev_log_number_;
  uint64_t next_file_number_;
  SequenceNumber last_sequence_; // leveldb中最后的sequence

  // Compaction相关，暂不展开
  std::vector< std::pair<int, InternalKey> > compact_pointers_;
```
前面我们讲过Version和Version Edit的关系，即
![version](/img/in-post/leveldb/version.png) 图片引用自:[catkang.github.io](http://catkang.github.io/2017/02/03/leveldb-version.html )

后面我们会讲到，每生成一个新的version edit，都会以一条记录的形式将其序列化信息写到manifest log中, (记录格式见[log format格式](http://gao-xiao-long.github.io/2016/06/20/log-format/) )，以便系统能够再重启时保持最终数据一致性,version edit的序列化代码如下(保存基本的添加、删除文件、合并点等信息)。

```c++
void VersionEdit::EncodeTo(std::string* dst) const {
  if (has_comparator_) {
    PutVarint32(dst, kComparator);
    PutLengthPrefixedSlice(dst, comparator_);
  }
  if (has_log_number_) {
    PutVarint32(dst, kLogNumber);
    PutVarint64(dst, log_number_);
  }
  if (has_prev_log_number_) {
    PutVarint32(dst, kPrevLogNumber);
    PutVarint64(dst, prev_log_number_);
  }
  if (has_next_file_number_) {
    PutVarint32(dst, kNextFileNumber);
    PutVarint64(dst, next_file_number_);
  }
  if (has_last_sequence_) {
    PutVarint32(dst, kLastSequence);
    PutVarint64(dst, last_sequence_);
  }

  for (size_t i = 0; i < compact_pointers_.size(); i++) {
    PutVarint32(dst, kCompactPointer);
    PutVarint32(dst, compact_pointers_[i].first);  // level
    PutLengthPrefixedSlice(dst, compact_pointers_[i].second.Encode());
  }

  for (DeletedFileSet::const_iterator iter = deleted_files_.begin();
       iter != deleted_files_.end();
       ++iter) {
    PutVarint32(dst, kDeletedFile);
    PutVarint32(dst, iter->first);   // level
    PutVarint64(dst, iter->second);  // file number
  }

  for (size_t i = 0; i < new_files_.size(); i++) {
    const FileMetaData& f = new_files_[i].second;
    PutVarint32(dst, kNewFile);
    PutVarint32(dst, new_files_[i].first);  // level
    PutVarint64(dst, f.number);
    PutVarint64(dst, f.file_size);
    PutLengthPrefixedSlice(dst, f.smallest.Encode());
    PutLengthPrefixedSlice(dst, f.largest.Encode());
  }
}
```

#### VersionSet

VersionSet用于保存一系列version集合，与本主题相关的数据结构主要有两个:

```c++
  Version dummy_versions_;  //  version双向链表的头指针
  Version* current_;        //  current version(== dummy_versions_.prev_)，代表最新version
```

VersionSet是以双向链表的格式将version组织到一起，以方便对verison进行添加即删除操作。内部双向链表组织见下图:
![version](/img/in-post/leveldb/version_set.png) 图片引用自:[catkang.github.io](http://catkang.github.io/2017/02/03/leveldb-version.html)


#### VersionSet::Builder

LevelDB使用Builder类来高效的将base version及一系列的version edit合成一个新的version。避免中间结果产生

```c++
 // Helper to sort by v->files_[file_number].smallest
  struct BySmallestKey {
    const InternalKeyComparator* internal_comparator;

    bool operator()(FileMetaData* f1, FileMetaData* f2) const {
      int r = internal_comparator->Compare(f1->smallest, f2->smallest);
      if (r != 0) {
        return (r < 0);
      } else {
        // Break ties by file number
        return (f1->number < f2->number);
      }
    }
  };

  typedef std::set<FileMetaData*, BySmallestKey> FileSet;
  struct LevelState {
    std::set<uint64_t> deleted_files;
    FileSet* added_files;
  };

  VersionSet* vset_;
  Version* base_;
  LevelState levels_[config::kNumLevels];

 // 主要提供的成员函数
  void Apply(VersionEdit* edit) // 将version edit应用到当前状态
  void SaveTo(Version* v)  // 将当前状态保存成版本v
```

以上是版本管理相关核心结构的的实现介绍。知道了各种核心结构之间的关系，还有一个问题需要搞清楚，即:内存version信息如何在LevelDB重启时还能恢复到最新的一致性状态呢?

答案是:MANIFEST。

#### MANIFEST: 一致性保证
LevelDB主要是依赖MANIFEST来保持数据的一致性。先介绍几个术语:

- MANIFEST: 指的是一个以事务日志(transactional log)方式跟踪LevelDB状态变化的系统
- Manifest log： 指的是一个包含LevelDB状态(version edits)的单个日志文件
- CURRENT 指的是最新的manifest log

MANIFEST记录是LevelDB版本信息状态变化的事务日志。它包含manifest log及指向最新的manifest log的指针。Manifest logs是名为MANIFEST-(seq number)的滚动日志文件。seq number(序列号)一直增加。CURRENT是一个执行最新的manifest log的特殊文件。
在系统启动时，最新的manifest log保存了LevelDB的一致性状态。对LevelDB状态的任何后续更改(新增version edit)都会记录到manifest log中。当manifest文件超过指定大小时。一个新的manifest文件将会被创建，作为保存LevelD状态的快照。指向最新的manifest的文件指针(CURRENT file)也将更新并同步到文件系统(sync)。成功更新到CURRENT file后，冗余的manifest文件将会被清除。

- MANIFEST = { CURRENT, MANIFEST-<seq-no>* }
- CURRENT = 指向最新的manifest log
- MANIFEST-<seq no> = (version edit1)、(version edit2)....(version editn)

LevelDB重启时会调用VersionSet::Recover()来恢复最新的一致性状态。它的主要逻辑是从CURRENT文件中读取最近的MANIFEST，然后依次读取记录，将每条记录解码成VersionEdit实例。
再依次调用VersionSet::Builder::Apply(&edit)。所有Version Edit读取完成之后，调用builder.SaveTo()生成新版本，并将最新版本设置为current verison，添加到VersionSet中。
整个版本恢复流程借助于VersionSet::Builder，可以避免大量的临时结果产生,优化过程见下图：
![version](/img/in-post/leveldb/version_builder.png) [图片引用自catkang.github.io](http://catkang.github.io/2017/02/03/leveldb-version.html)


关于版本管理的介绍就到此结束，后面会分析LevelDB的Compation机制。


#### 参考

[leveldb源码](https://github.com/google/leveldb)

[rocksdb wiki](https://github.com/facebook/rocksdb/wiki/How-we-keep-track-of-live-SST-files )

[rocksdb wiki](https://github.com/facebook/rocksdb/wiki/MANIFEST)

[庖丁解LevelDB之版本控制](http://catkang.github.io/2017/02/03/leveldb-version.html)

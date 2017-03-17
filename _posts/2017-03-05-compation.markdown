---                                                                                                    
layout: post                                                                                           
title: leveldb1.2源码剖析--Compation                                                                   
date: 2017-03-05                                                                                       
author: "gao-xiao-long"                                                                                
catalog: true                                                                                          
tags:                                                                                                  
    - leveldb                                                                                          
---                                                                                                    

LevelDB是基于WAL+LSM实现的，Compaction是LSM的核心，它是将现有的SST文件进行合并生成一个新SST文件过程。 Compaction操作的主要目的有两个:通过减少文件的增长量来保证读操作的性能及通过合并文件来消除其中重复的更新或者删除操作。本文将详细剖析Compaction在LevelDB中的实现。

#### Compaction分类
LevelDB中有两种Compaction：
- 将内存中的immutable dump到磁盘，生成SST文件
- 按一定策略从现有SST文件中选取文件合并成新文件

#### 何时检查是否需要Compaction
LevelDB通过调用DBImpl::MaydbScheduleCompaction()函数来判断是否需要Compaction，如果需要，则调用Env::Schedule唤起Compaction。在了解Compaction细节之前，先来分析下LevelDB何时会“检查是否需要Compaction”。
- 数据库Open()时
- Write()时
- Get()调用时
- Iterator调用时
- Compcation执行后

LevelDB会在某些时刻调用函数为DBImpl::MaybeScheduleCompaction()来判断是否需要进行Compaction。函数的实现如下：

```c++
  mutex_.AssertHeld();
  if (bg_compaction_scheduled_) {
    // Already scheduled
  } else if (shutting_down_.Acquire_Load()) {
    // DB is being deleted; no more background compactions
  } else if (!bg_error_.ok()) {
    // Already got an error; no more changes
  } else if (imm_ == NULL &&
              manual_compaction_ == NULL &&
              !versions_->NeedsCompaction()) {
    // No work to be done
  } else {
    bg_compaction_scheduled_ = true;
    env_->Schedule(&DBImpl::BGWork, this);
  }
```
此函数会检查是否有immutable需要dump磁盘(imm_ == NULL ?)或者现有的的SST文件是否需要合并(versions_->NeedsCompaction() ?), 如果条件满足且当前没有Compaction任务，就会通过调用env_->Schedule(&DBImpl::BGWork, this)来启动后台线程进行合并操作。
那么LevelDB会何时调用MaybeScheduleCompaction()来判断是否需要合并呢？（涵盖了get、write、iterator、open？）
 ####### 调用时机1：数据库Open()
      之所以在打开数据库时判断是否需要Compaction是因为有可能
      （1）打开已有的数据库时需要从WAL中恢复数据到memtable，在恢复时如果memtable达到阈值，会直接dump成SST文件，有可能dump的sst文件达到level0的合并上限？
      （2）数据库在退出前已经NeedsCompaction(),还未来得及合并便退出。
  #######调用时机2：上一次数据Compaction完成后
             上一次数据Compaction完成后可能会在一个级别中生成了太多文件，这时候会再次启动Compaction()
  #######调用时机3：数据库Get()操作如果某个文件频繁被访问，则会对此文件进行Compaction
  ###### 调用时机4：write操作时发现level0的文件达到某个阈值（MakeRoomForWrite）会触发合并操作
  ###### 调用时机5：RecordReadSample() 读取字节样本，确认是否需要触发Compaction （执行迭代器的时候？）



（1） 打
下面详细介绍下两种合并方式。
Â
##### immutable dump

[前面的文章](http://gao-xiao-long.github.io/2016/09/24/memtable/)中讲过，当往LevelDB中写入(Put)一条记录的时，会先将此记录追加到Log文件中，之后再插入Memtable，当Memtable占用的内存达到指定阈值后，LevelDB会生成新的Log文件及Memtable文件，并将原来的Memtable转化为Immutable Memtable(只读)，待后续落地成SSTable文件。落地成SSTable文件的实现逻辑主要在DBImpl::WriteLevel0Table()中
1. 将Memtable中的键值对按照[SST格式](http://gao-xiao-long.github.io/2016/08/07/table-format/)落地成一个SST文件
2. 然调用Version::PickLevelForMemTableOutput()来确定新建的SST文件所属的level
      我们知道，在LevelDB中除了level0中的文件key可以重复，其他level中的文件key是不重复的。所以在选择level时，如果与level0中的文件不存在key的重叠且有比level0更合适的level，它会优先放到其他level，没有其他合适的level才会放到level0。需要注意的是，memtable文件最多会放到level2中，而不会放到更多的level，原因在kMaxMemCompactLevel的定义处(dbformat.h)有解释，最主要的原因还是尽量减少Compaction次数及磁盘空间的浪费。

```c++
int Version::PickLevelForMemTableOutput(
    const Slice& smallest_user_key,
    const Slice& largest_user_key) {
  int level = 0;
  // 是否与level0中的文件存在key重叠，如果是直接将新sst文件归为level0
  if (!OverlapInLevel(0, &smallest_user_key, &largest_user_key)) {
    // Push to next level if there is no overlap in next level,
    // and the #bytes overlapping in the level after that are limited.
    InternalKey start(smallest_user_key, kMaxSequenceNumber, kValueTypeForSeek);
    InternalKey limit(largest_user_key, 0, static_cast<ValueType>(0));
    std::vector<FileMetaData*> overlaps;
    while (level < config::kMaxMemCompactLevel) {
      if (OverlapInLevel(level + 1, &smallest_user_key, &largest_user_key)) {
        break;
      }
      if (level + 2 < config::kNumLevels) {
        // Check that file does not overlap too many grandparent bytes.
        GetOverlappingInputs(level + 2, &start, &limit, &overlaps);
        const int64_t sum = TotalFileSize(overlaps);
        if (sum > kMaxGrandParentOverlapBytes) {
          break;
        }
      }
      level++;
    }
  }
  return level;
}
```

3. 构建文件(level, file meta)信息，插入到[VersionEdit](http://gao-xiao-long.github.io/2017/02/26/version-manager/)中，新SST文件最终出现在current verison中体现
4. 更新CompactionStats信息：
   CompactionStats在db_impl.h中定义，主要记录每个level的读写信息：
   ```c++
   struct CompactionStats {
   int64_t micros;
   int64_t bytes_read;
   int64_t bytes_written; //

   CompactionStats() : micros(0), bytes_read(0), bytes_written(0) { }

   void Add(const CompactionStats& c) {
     this->micros += c.micros;
     this->bytes_read += c.bytes_read;
     this->bytes_written += c.bytes_written;
   }
 };
 CompactionStats stats_[config::kNumLevels]; // 每个level的compaction状态
 ```




(1) LevelDB如何选择文件进行合并                                                                        

1. 从Level0到最高Level中选取第一个score大于1的level Lb,作为compaction的base level                      
2. 确定Compaction的输出level: Lo = Lb + 1                                                              
3.                                                                                                     

1. what compaction                                                                                     
1. why compaction                                                                                      
2. how compaction                                                                                      
    (1) 确定level                                                                                      
    (2) 确定文件                                                                                       
    (3) 文件超过限速控制(dbformat.h)                                                                   

DBImpl::BackgroundCall:  ? 整个compaction流程是什么？                                                  
  // Previous compaction may have produced too many files in a level,                                  
  // so reschedule another compaction if needed.                                                       

3种调用： immutable合并、手动、自动                                                                    

画出整个调用触发的大致流程图（不仅仅一个线程？后台线程怎么启动的？）                                   

将imuutable compation的具体流程(WriteLevel0Table): 注意： 不是简单的将其immutable放到level0 上面(前人谬已。再画具体的整体结构图时说明这一点)。而是会Version::PickLevelForMemT
ableOutput() 详细讲下这个过程。


cache的那个博客在详细讲下。builder.cc::BuildTable的逻辑流程。每次BuildTable产生一个新Table时，会将此Table加入到Cache索引中。并将Table的Index Block索引到内存中。(又涉及到TwoL
evelIterator)当Cache索引删除这个Table时，会注册一个删除函数讲此Table结构注销。

static double MaxBytesForLevel(int level) {
  // Note: the result for level zero is not really used since we set
  // the level-0 compaction threshold based on number of files.
  double result = 10 * 1048576.0;  // Result for both level-0 and level-1
  while (level > 1) {
    result *= 10;
    level--;
  }
  return result;
}

void VersionSet::Finalize(Version* v) {
  // Precomputed best level for next compaction
  int best_level = -1;
  double best_score = -1;

  for (int level = 0; level < config::kNumLevels-1; level++) {
    double score;
    if (level == 0) {
      // We treat level-0 specially by bounding the number of files
      // instead of number of bytes for two reasons:
      //
      // (1) With larger write-buffer sizes, it is nice not to do too
      // many level-0 compactions.
      //
      // (2) The files in level-0 are merged on every read and
      // therefore we wish to avoid too many files when the individual
      // file size is small (perhaps because of a small write-buffer
      // setting, or very high compression ratios, or lots of
      // overwrites/deletions).
      score = v->files_[level].size() /
          static_cast<double>(config::kL0_CompactionTrigger);
    } else {
      // Compute the ratio of current size to size limit.
      const uint64_t level_bytes = TotalFileSize(v->files_[level]);
      score = static_cast<double>(level_bytes) / MaxBytesForLevel(level);
    }

    if (score > best_score) {
      best_level = level;
      best_score = score;
    }
  }

  v->compaction_level_ = best_level;
  v->compaction_score_ = best_score;
}



通用压缩：
通用压缩方式目标在于减少写放大(write amplification) 并且折中读取放大(read amplification)及空间放大(space amplification)
使用这种方式时，所有的SST文件都是按照key的顺序排序。

在通用压缩方式中，有时候需要(full compaction)。在这种情况下，输出文件的大小和输入文件的大小相似，在压缩期间，输入文件(input files)和输出文件(output file)都需要保留，所以DB会临时使用双倍的磁盘空间，来保证有足够的空间进行
full compaction.

在使用通用压缩时，如果num_levels=1，DB中的所有的数据有时会压缩成一个SST文件。SST文件有大小限制，在RocksDB中，一个block不能够达到4GB（uint32的最大值）。如果单个SST文件太大，index block可能超限。index block的大小取决于数据的大小。在我们的使用案例额中，当数据库增长到大于250GB时，使用4k数据库大小，我们观察到DB会到达限制。

如果用户将num_levels设置大于1，那么这个问题就可以得到缓解。在这种情况下，较大的“文件”将会放到较大的“级别”，并且文件会分为较小的文件。


下面是一个典型的文件布局：
Level 0： File0_0, File0_1, File0_2
Level 1: (empty)
Level 2: (empty)
Level 3: (empty)
Level 4: File4_0, File4_1, File4_2, File4_3
Level 5: File5_0, File5_1, File5_2, File5_3, File5_4, File5_5, File5_6, File5_7

Level大的文件数据比Level小的文件旧。在这个例子中，有5个 sorted runs： 3个文件在level0， leve4、level5. Level5是最旧的结果集。level4相对较新，level0是最新的。

使用上面的列子，我们有以下的sorted runs:
File0_0
File0_1
File0_2
Level 4: File4_0, File4_1, File4_2, File4_3
Level 5: File5_0, File5_1, File5_2, File5_3, File5_4, File5_5, File5_6, File5_7
如果我们将所有的数据压缩，sorted run的输出将会放到level5中，变成：
level5: File5_0, File5_1, File5-_2。
如果我们压缩File0_0, File0_1, File0_2,输出将会放到level3
如果我们压缩File0_0 File0_1 输出将仍然放到level 0.

如果options.num_levels = 1 我们仍然使用相同的侧脸，这表示所有的数据将会被放到level0 并且所有的都是sorted run.

压缩选择算法：
假设我们有
R1， R2， R3,..,Rn

前提条件   n >= options.level0_file_num_compaction_trigger  否则不会触发compaction

1. Space Amplification触发
如果 amplification ratio大于max_size_amplification_percent/100,所有的文件都会都被压缩成一个文件。
size amplicatgion ratio  = (size(r1) + size(r2) + size(rn-1)) size(rn)

http://weakyon.com/2015/04/08/Log-Structured-Merge-Trees.html

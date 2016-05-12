其他可参考的译文(http://duanple.blog.163.com/blog/static/70971767201171705113636/)
The implementation of leveldb is similar in spirit to the representation of a single Bigtable tablet (section 5.3). However the organization of the files that make up the representation is somewhat different and is explained below.
leveldb的实现跟Bigtable中单个tablet(section 5.3)比较像。只是文件的组织形式有些不同。下面是leveldb文件组织的说明:
Each database is represented by a set of files stored in a directory. There are several different types of files as documented below:
每个数据库都由一个目录中一系列文件组成。组成数据库的不同类型的文件如下:

Log files Log文件

A log file (*.log) stores a sequence of recent updates. Each update is appended to the current log file. When the log file reaches a pre-determined size (approximately 4MB by default), it is converted to a sorted table (see below) and a new log file is created for future updates.
一个log文件(*.log)保存了一系列的近期的更新。每个更新都添加到当前log文件中。当log文件达到了预定义的大小(默认是4MB左右)，log文件将会被传化成Sorted table(见下面介绍)并且会产生一个新的log文件来记录后续的更新
A copy of the current log file is kept in an in-memory structure (the memtable). This copy is consulted on every read so that read operations reflect all logged updates.
当前log文件的copy保存在内存数据结构中(memtable)。每次读操作都会查询这个copy。这样可以保证读操作能够得到最新数据(??翻译不是很通畅)

Sorted tables 有序表

A sorted table (*.sst) stores a sequence of entries sorted by key. Each entry is either a value for the key, or a deletion marker for the key. (Deletion markers are kept around to hide obsolete values present in older sorted tables).
一个sorted table(*.sst)保存了一些按key排序的实体。每个实体是key对应的一个value值，或者是一个删除标识。(删除标识用于隐藏在旧的sorted table中过时的数据)
The set of sorted tables are organized into a sequence of levels. The sorted table generated from a log file is placed in a special young level (also called level-0). When the number of young files exceeds a certain threshold (currently four), all of the young files are merged together with all of the overlapping level-1 files to produce a sequence of new level-1 files (we create a new level-1 file for every 2MB of data.)
sorted table集合按照level组织在一起。由log文件生成的sorted table被房子在一个特殊的young level中(也被称作level-0)。当young文件个数达到指定阈值(目前阈值是4)。所有的young文件会跟level-1文件合并到一块，来产生一系列新的level-1文件(我们为每2MB数据创建一个新的level-1文件)

Files in the young level may contain overlapping keys. However files in other levels have distinct non-overlapping key ranges. Consider level number L where L >= 1. When the combined size of files in level-L exceeds (10^L) MB (i.e., 10MB for level-1, 100MB for level-2, ...), one file in level-L, and all of the overlapping files in level-(L+1) are merged to form a set of new files for level-(L+1). These merges have the effect of gradually migrating new updates from the young level to the largest level using only bulk reads and writes (i.e., minimizing expensive seeks).
young level中的文件可能会存在重叠的key数据，但是处于其他level中的文件不会有重叠的key。假定level L (L >=1 ) 当level-L的文件中数据达到(10^L)兆(比如level-1达到10M。level-2达到100M ...)。一个Level-L文件和level-(L+1)中重叠的文件会被合并到一块(??看看具体怎么实现,翻译的不是很通顺)。(??后面再翻译)

Manifest

A MANIFEST file lists the set of sorted tables that make up each level, the corresponding key ranges, and other important metadata. A new MANIFEST file (with a new number embedded in the file name) is created whenever the database is reopened. The MANIFEST file is formatted as a log, and changes made to the serving state (as files are added or removed) are appended to this log.
Manifest文件一系列的sorted tables以及对应的level，key的范围，以及其他重要的元数据。一个当数据库被重新打开时会创建一个新的MANIFEST文件(文件名中嵌入一个数字)。MANIFEST文件可以看做是一个log文件，服务状态的改变(比如文件添加或者移除)会被追加到这个log中。

Current

CURRENT is a simple text file that contains the name of the latest MANIFEST file.
CURRENT 是一个简单的文本文件，里面记录了最新的MANIFEST文件的名字。

Info logs

Informational messages are printed to files named LOG and LOG.old.
(??不懂) 信息化消息会被打印到以LOG和LOG.old命名的文件中

Others

Other files used for miscellaneous purposes may also be present (LOCK, *.dbtmp).
还有其他的杂项文件(LOCK, *。dbtmp)

Level 0

When the log file grows above a certain size (1MB by default):
当log文件大小达到某一阈值(默认是1MB)
Create a brand new memtable and log file and direct future updates here
创建一个全新的memtable和log文件,后续的更新都会写到这里。
In the background:
后台:
Write the contents of the previous memtable to an sstable
将之前的memtable中的内容写到sstable
Discard the memtable
丢弃memtable
Delete the old log file and the old memtable
删除旧的log文件和旧的memtable
Add the new sstable to the young (level-0) level.
在level-0级别下添加新的sstable
Compactions
压缩

When the size of level L exceeds its limit, we compact it in a background thread. The compaction picks a file from level L and all overlapping files from the next level L+1. Note that if a level-L file overlaps only part of a level-(L+1) file, the entire file at level-(L+1) is used as an input to the compaction and will be discarded after the compaction. Aside: because level-0 is special (files in it may overlap each other), we treat compactions from level-0 to level-1 specially: a level-0 compaction may pick more than one level-0 file in case some of these files overlap each other.
当level L的大小达到阈值,我们再后台线程中将其压缩。压缩程序选在一个level L中的文件以及(??后面看到细节后再翻译)

A compaction merges the contents of the picked files to produce a sequence of level-(L+1) files. We switch to producing a new level-(L+1) file after the current output file has reached the target file size (2MB). We also switch to a new output file when the key range of the current output file has grown enough to overlap more than ten level-(L+2) files. This last rule ensures that a later compaction of a level-(L+1) file will not pick up too much data from level-(L+2).

The old files are discarded and the new files are added to the serving state.

Compactions for a particular level rotate through the key space. In more detail, for each level L, we remember the ending key of the last compaction at level L. The next compaction for level L will pick the first file that starts after this key (wrapping around to the beginning of the key space if there is no such file).

Compactions drop overwritten values. They also drop deletion markers if there are no higher numbered levels that contain a file whose range overlaps the current key.

Timing 定时器

Level-0 compactions will read up to four 1MB files from level-0, and at worst all the level-1 files (10MB). I.e., we will read 14MB and write 14MB.
Other than the special level-0 compactions, we will pick one 2MB file from level L. In the worst case, this will overlap ~ 12 files from level L+1 (10 because level-(L+1) is ten times the size of level-L, and another two at the boundaries since the file ranges at level-L will usually not be aligned with the file ranges at level-L+1). The compaction will therefore read 26MB and write 26MB. Assuming a disk IO rate of 100MB/s (ballpark range for modern drives), the worst compaction cost will be approximately 0.5 second.

If we throttle the background writing to something small, say 10% of the full 100MB/s speed, a compaction may take up to 5 seconds. If the user is writing at 10MB/s, we might build up lots of level-0 files (~50 to hold the 5*10MB). This may significantly increase the cost of reads due to the overhead of merging more files together on every read.

Solution 1: To reduce this problem, we might want to increase the log switching threshold when the number of level-0 files is large. Though the downside is that the larger this threshold, the more memory we will need to hold the corresponding memtable.

Solution 2: We might want to decrease write rate artificially when the number of level-0 files goes up.

Solution 3: We work on reducing the cost of very wide merges. Perhaps most of the level-0 files will have their blocks sitting uncompressed in the cache and we will only need to worry about the O(N) complexity in the merging iterator.

Number of files

Instead of always making 2MB files, we could make larger files for larger levels to reduce the total file count, though at the expense of more bursty compactions. Alternatively, we could shard the set of files into multiple directories.
An experiment on an ext3 filesystem on Feb 04, 2011 shows the following timings to do 100K file opens in directories with varying number of files:

Files in directory  Microseconds to open a file
1000    9
10000   10
100000  16
So maybe even the sharding is not necessary on modern filesystems?
Recovery

Read CURRENT to find name of the latest committed MANIFEST
Read the named MANIFEST file
Clean up stale files
We could open all sstables here, but it is probably better to be lazy...
Convert log chunk to a new level-0 sstable
Start directing new writes to a new log file with recovered sequence#
Garbage collection of files

DeleteObsoleteFiles() is called at the end of every compaction and at the end of recovery. It finds the names of all files in the database. It deletes all log files that are not the current log file. It deletes all table files that are not referenced from some level and are not the output of an active compaction.

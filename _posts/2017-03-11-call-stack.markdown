---
layout: post
title: gdb core 段错误调用栈被破坏时排查方法
date: 2017-3-11
author: "gao-xiao-long"
catalog: true
tags:
    - 基础技术
---

本文讨论在x86_64下gdb core文件调用栈被破坏时两个tips。

####基础知识
栈是一个动态内存区域，程序可以将数据压入栈中(入栈，push)，也可以将已经压入栈中的数据弹出(出栈，pop)。它遵循FIFO规则(First In Last Out)，即先入栈的数据后出栈。Linux操作系统中，栈是向下增长的，栈顶由称为rsp的寄存器进行定位(i386下寄存器名称为esp)。栈保存了一个函数调用所需要维护的信息，这些信息被称为堆栈帧(Stack Frame)或活动记录(Activate Record)。堆栈帧一般包括如下几方面内容：
- 函数的返回地址和参数
- 临时变量：包括函数的非静态局部变量及编译器自动生成的其他临时变量
- 保存的上下文：包括在函数调用前后需要保持不变的寄存器。

在x86_64中，一个函数的堆栈帧用rbp和rsp两个寄存器划定。rsp寄存器始终指向栈的顶部，同时也就指向了当前函数堆栈帧的顶部。rbp始终指向了一个活动记录的固定位置，它被称为帧指针(Frame Pointer)寄存器。它指向的数据是调用该函数前的rbp的值，这样函数返回的时候，rbp可以通过读取这个值恢复到调用前的值。
一个常见的活动记录如图所示:
![结构图](/img/in-post/leveldb/stack.png)

####core文件完整--调用堆栈被破坏
在GDB中调用backtrace(bt)命令时，就是通过rsp和rbp寄存器一层层解析出函数调用栈。如果程序执行过程中将函数的返回地址(指向函数调用者的下一条指令)破坏，那么GDB分析函数返回地址(地址查找符号表)就会失败，失败后它不会进行容错处理，直接停止对调用栈进行分析。所以我们在调用bt命令时有可能只会看到部分调用栈。一般情况下调用堆栈只是部分被破坏了，rsp堆栈寄存器中保存的值一般是正确的，所以可以通过手动方法查看调用堆栈。如下:
![图](/img/in-post/leveldb/gdb.png)
说明：
- set logging on: 将gdb命令的输出结果保存到名为gdb.txt的文件。也可以通过set logging file file 指定自定义输出文件。
- x /2000a:  x(全称: examine)用来以各种格式输出存储器的值，具体用法参见[gdb x命令](http://visualgdb.com/gdbreference/commands/x)

在上面的例子中'_Z4func2v+34'看似是一个有效的函数(编译器会对函数名字进行修饰，使其有唯一标示，所以通过编译器看到的名字会有些别扭，但通过名字应该很容易猜出来实际的函数名。),通过x 命令中显示的函数名称可以大概的分析出函数调用堆栈。


####core文件不存在或不完整
如果段错误时没有输出core文件或者core文件不完整(与ulimit设置有关)，则无法通过gdb获取函数调用信息，这时候可以通过dmesg+addr2line命令获取程序执行的最后一行代码：

![图](/img/in-post/leveldb/dmesg.png)

 dmesg用于输出内核信息，addr2line用于将地址转化为文件名称及行号

 对dmesg的输出说明:
> a.out[41728]: segfault at 7fffcbc2f000 ip 000000000040052c sp 00007fffcbc2df70 error 6 in a.out[400000+1000]

-  ip: 指令寄存器, 存放前从主存储器读出的正在执行的一条指令。
-  sp: 栈寄存器指。


参考:
[the coredump file stack destruction](http://intercontineo.com/article/804589737/)
程序员的自我修养--链接、装载与库



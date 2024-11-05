# arcofs
## 简介
一个简单的文件系统驱动arcofs.ko，以及创建该文件系统的工具mkarcofs

## 说明
### 分支
linux-4.15 在4.15版本内核上编译、运行通过

linux-6.6 如果gcc的版本低于9, 需要在Make menuconfig时关闭STACKPROTECTOR

### version 0.0
挂载： 支持

创建文件夹： 不支持 & 没打算做<br>
(所以arcofs文件系统挂载之后是平坦——的, 只有一层路径, 哈哈

创建文件： 支持

创建链接： 不支持 (没实现symlink

删除文件： 支持

写文件： 当前仅支持1次性写入1kb以内数据, 再次写入同一文件会提示 No space left on device<br>
(目标是支持单个文件正常的存储最多8kb数据<br>
实现的过时的write方法, write_iter还没看明白

读文件： 当前仅支持读取1024kb以内的内容<br>
实现的过时的read方法, read_iter还没看明白

## 实现细节
**block size:** 1024byte

**super block:<br>**
魔数、inode总数、空闲inode数、块总数、空闲块总数

**arcofs inode<br>**
i_mode、i_size、i_block[8]、char filename[12]<br>
8个i_block都是直接块，没搞间接块，所以文件大小最多支持8kb<br>
没搞dentry结构，文件名直接放在inode里，所以限定12字节<br>
arcofs inode设定为64byte, 还有12字节的padding

**文件系统的系统块划分:**<br>
第0个block, 不使用<br>
第1个block, 用作super block<br>
第2个block, 用作block bytemap<br>
第3个block, 用作inode bytemap<br>
第4个block, 用作inode table<br>
(为了方便编程, 我直接使用一个unsigned char类型来标注一个块是否被占用, 所以是 byte map

**数据块管理**<br>
简化了ext2文件系统中间接、双重间接、三重间接的管理方式，arcofs的每个inode仅管理8个直接块

**dentry结构**<br>
没有<br>
arcofs没有设立专门的dentry结构，也没打算管理目录；文件名以最长11个字节的形式保存在inode中

## mkarcofs 说明
原谅我<br>
没有什么真正的物理块设备给我用(给我我也不会)<br>
也不咋会用虚拟机<br>
生怕搞坏设备的我, 就用文件的方式来做文件系统镜像了<br>

凑合用吧, 至少比用内存强, 哈哈

## 现存bug
啊——！<br>
来自用户态的cat、ls命令我还没明白，为什么他们要连续读取两次? 然后会导致无限读取挂死<br>
我使用了一个全局标志位来使只在第奇数次读取时返回, 但这样引入了一些问题...
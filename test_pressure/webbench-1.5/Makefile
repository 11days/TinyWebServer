CFLAGS?=	-Wall -Wno-unused-parameter -ggdb -W -O		# 设置编译参数
CC?=		gcc		# 编译器选择为gcc
LIBS?=		# 库文件
LDFLAGS?=		# 链接参数
PREFIX?=	/usr/local		# 安装目录前缀
VERSION=1.5		# 版本号
TMPDIR=/tmp/webbench-$(VERSION)		# 临时目录

all:   webbench tags		# 默认构建webbench和tags

tags:  *.c
	-ctags *.c		# 生成tags文件

install: webbench
	install -s webbench $(DESTDIR)$(PREFIX)/bin		# 安装可执行文件到指定路径
	install -m 644 webbench.1 $(DESTDIR)$(PREFIX)/man/man1		# 安装man页到指定路径
	install -d $(DESTDIR)$(PREFIX)/share/doc/webbench		# 安装文档目录
	install -m 644 debian/copyright $(DESTDIR)$(PREFIX)/share/doc/webbench		# 安装版权信息
	install -m 644 debian/changelog $(DESTDIR)$(PREFIX)/share/doc/webbench		# 安装changelog

webbench: webbench.o Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o webbench webbench.o $(LIBS) 		# 编译链接生成webbench可执行文件

clean:
	-rm -f *.o webbench *~ core *.core tags		# 清理中间文件和tags

tar:   clean
	-debian/rules clean		# 清理debian相关文件
	rm -rf $(TMPDIR)		# 移除临时目录
	install -d $(TMPDIR)		# 创建临时目录
	cp -p Makefile webbench.c socket.c webbench.1 $(TMPDIR)		# 拷贝文件到临时目录
	install -d $(TMPDIR)/debian		# 创建debian目录
	-cp -p debian/* $(TMPDIR)/debian		# 拷贝debian文件
	ln -sf debian/copyright $(TMPDIR)/COPYRIGHT		# 创建软链接
	ln -sf debian/changelog $(TMPDIR)/ChangeLog		# 创建软链接
	-cd $(TMPDIR) && cd .. && tar cozf webbench-$(VERSION).tar.gz webbench-$(VERSION)		# 打包

webbench.o:	webbench.c socket.c Makefile		# webbench.o依赖于这些文件

.PHONY: clean install all tar		# 声明clean、install、all、tar为伪目标

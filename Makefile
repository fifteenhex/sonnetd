MAKEFLAGS += --no-builtin-rules

# Make sure we know where to get nolibc
ifndef NOLIBCDIR
$(error Please pass NOLIBCDIR with the path to your copy of nolibc (tools/include/nolibc/ in the linux source))
endif

COPTS=-ggdb \
	-nostdlib \
	-std=c99 \
	-Os \
	-include $(NOLIBCDIR)/nolibc.h \
	-Wl,--hash-style=gnu

sonnetd: sonnetd.c
	$(CC) $(COPTS) -o $@ $<

.PHONY: clean
clean:
	rm -rf sonnetd

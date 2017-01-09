CONFIGFILE = config.mk
include $(CONFIGFILE)

BIN =\
	vu-colour-srgb\
	vu-concat\
	vu-dissolve\
	vu-flip\
	vu-flop\
	vu-frame-to-image\
	vu-image-to-frame\
	vu-invert-luma\
	vu-next-frame\
	vu-read-head\
	vu-repeat\
	vu-set-alpha\
	vu-set-luma\
	vu-single-colour\
	vu-stack\
	vu-write-head

all: $(BIN)

%: %.o util.o stream.o
	$(CC) $(LDFLAGS) -o $@ $^

%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	-rm $(BIN) $(BIN:=.o) util.o stream.o

.PHONY: all clean

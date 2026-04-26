# BitRipple Library Makefile
# Builds libnanorq (static + shared) and libbitripple (static + shared)
# nanorq is cloned with --recursive-submodules and built in-tree.

CC      = gcc
AR      = ar
CFLAGS  = -Wall -Wextra -O3 -g -std=c99 -fPIC
CPPFLAGS = -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64

# ---------------------------------------------------------------------
# nanorq paths
# ---------------------------------------------------------------------
NANORQ_DIR      = nanorq
NANORQ_INC      = $(NANORQ_DIR)/include
OBLAS_DIR       = $(NANORQ_DIR)/deps/oblas
OBLAS_INC       = $(OBLAS_DIR)

# nanorq core objects (exclude example programs at repo root)
NANORQ_SRCS     = $(NANORQ_DIR)/lib/bitmask.c \
                  $(NANORQ_DIR)/lib/io.c \
                  $(NANORQ_DIR)/lib/nanorq.c \
                  $(NANORQ_DIR)/lib/params.c \
                  $(NANORQ_DIR)/lib/precode.c \
                  $(NANORQ_DIR)/lib/rand.c \
                  $(NANORQ_DIR)/lib/sched.c \
                  $(NANORQ_DIR)/lib/spmat.c \
                  $(NANORQ_DIR)/lib/tuple.c \
                  $(NANORQ_DIR)/lib/wrkmat.c

NANORQ_OBJS     = $(NANORQ_SRCS:.c=.o)
OBLAS_LIB       = $(OBLAS_DIR)/liboblas.a

# ---------------------------------------------------------------------
# bitripple sources
# ---------------------------------------------------------------------
BITRIPPLE_SRCS  = src/bitripple.c \
                  src/bitripple_profile.c \
                  src/bitripple_spray.c \
                  src/bitripple_codec.c \
                  src/bitripple_net.c \
                  src/bitripple_client.c \
                  src/bitripple_server.c \
                  src/bitripple_feedback.c

BITRIPPLE_OBJS  = $(BITRIPPLE_SRCS:.c=.o)
BITRIPPLE_INCS  = -I./include -I$(NANORQ_INC) -I$(OBLAS_INC)

.PHONY: all clean test clone-nanorq

all: libnanorq.a libnanorq.so libbitripple.a libbitripple.so

# ---------------------------------------------------------------------
# Clone nanorq with recursive submodules if missing
# ---------------------------------------------------------------------
clone-nanorq:
	@if [ ! -f "$(NANORQ_DIR)/Makefile" ]; then \
		echo "Cloning nanorq with submodules..."; \
		git clone --recurse-submodules https://github.com/sleepybishop/nanorq.git $(NANORQ_DIR); \
	fi

# ---------------------------------------------------------------------
# Build oblas dependency (submodule inside nanorq)
# ---------------------------------------------------------------------
$(OBLAS_LIB): | clone-nanorq
	$(MAKE) -C $(OBLAS_DIR) CPPFLAGS+="-DOBLAS_AVX -DOCTMAT_ALIGN=32"

# ---------------------------------------------------------------------
# nanorq object compilation
# ---------------------------------------------------------------------
$(NANORQ_DIR)/lib/%.o: $(NANORQ_DIR)/lib/%.c | clone-nanorq $(OBLAS_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(NANORQ_DIR) -I$(NANORQ_INC) -I$(OBLAS_INC) \
		-c $< -o $@

# ---------------------------------------------------------------------
# libnanorq.a (static) — bundles oblas objects, matching upstream style
# ---------------------------------------------------------------------
libnanorq.a: $(NANORQ_OBJS) $(OBLAS_LIB)
	$(AR) rcs $@ $(NANORQ_OBJS) $(OBLAS_DIR)/*.o

# ---------------------------------------------------------------------
# libnanorq.so (shared) — compiled from sources + oblas objects
# ---------------------------------------------------------------------
libnanorq.so: $(NANORQ_SRCS) $(OBLAS_LIB) | clone-nanorq
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC \
		-I$(NANORQ_DIR) -I$(NANORQ_INC) -I$(OBLAS_INC) \
		$(NANORQ_SRCS) $(OBLAS_DIR)/*.o -o $@

# ---------------------------------------------------------------------
# libbitripple
# ---------------------------------------------------------------------
$(BITRIPPLE_OBJS): %.o: %.c include/bitripple.h include/bitripple_types.h | clone-nanorq
	$(CC) $(CFLAGS) $(BITRIPPLE_INCS) -c $< -o $@

libbitripple.a: $(BITRIPPLE_OBJS)
	$(AR) rcs $@ $^

libbitripple.so: $(BITRIPPLE_OBJS) libnanorq.a
	$(CC) -shared -o $@ $(BITRIPPLE_OBJS) -L. -lnanorq -lm

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------
test: all
	LD_LIBRARY_PATH=. python3 tests/test_library.py
	LD_LIBRARY_PATH=. python3 tests/test_library_net.py

clean:
	rm -f src/*.o $(NANORQ_DIR)/lib/*.o lib*.a lib*.so
	if [ -f "$(OBLAS_DIR)/Makefile" ]; then \
		$(MAKE) -C $(OBLAS_DIR) clean; \
	fi
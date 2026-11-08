# Aura C 核心构建（MinGW 或开发板 gcc）

CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Icsrc
LDLIBS ?= -lm
SRC = csrc/aura_kernels.c \
      csrc/aura_bin_scorer.c \
      csrc/aura_spatial.c \
      csrc/aura_complex_fusion.c \
      csrc/aura_hr.c \
      csrc/aura_event.c \
      csrc/aura_rate.c \
      csrc/aura_hrv.c \
      csrc/aura_fast_motion.c \
      csrc/aura_pipeline.c
HDR = csrc/aura_kernels.h \
      csrc/aura_bin_scorer.h \
      csrc/aura_spatial.h \
      csrc/aura_complex_fusion.h \
      csrc/aura_hr.h \
      csrc/aura_event.h \
      csrc/aura_rate.h \
      csrc/aura_hrv.h \
      csrc/aura_fast_motion.h \
      csrc/aura_pipeline.h \
      csrc/aura_sos_constants.h

all: libaura.a aura_demo aura_probe

libaura.a: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -c $(SRC)
	$(AR) rcs $@ *.o
	rm -f *.o

aura_demo: csrc/aura_demo.c libaura.a
	$(CC) $(CFLAGS) -o $@ csrc/aura_demo.c libaura.a $(LDLIBS)

aura_probe: aura_probe.c libaura.a
	$(CC) $(CFLAGS) -o $@ aura_probe.c libaura.a $(LDLIBS)

clean:
	rm -f libaura.a aura_demo aura_probe *.o

.PHONY: all clean

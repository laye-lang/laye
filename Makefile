# %OS% is defined on Windows
ifdef OS
	ODIR = win32/o
	RM = del /Q
	RMRF = rmdir /Q /S
	CP = copy /Y
	MkDir = if not exist $1 ( mkdir $1 )
	ExePath = $1.exe
   	FixPath = $(subst /,\,$1)
	CMAKE_VARS = set CC=clang && set CXX=clang++ &&
else
	ifeq ($(shell uname), Linux)
		ODIR = linux/o
		RM = rm -f
		RMRF = rm -rf
		CP = cp -f
		MkDir = mkdir -p $1
		ExePath = $1
		FixPath = $1
		CMAKE_VARS = CC=clang CXX=clang++
	endif
endif

CC = clang
LD = clang

CFLAGS = -std=c2x -pedantic -pedantic-errors -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-gnu-zero-variadic-macro-arguments -Wno-missing-field-initializers -Wno-deprecated-declarations -fdata-sections -ffunction-sections -Werror=return-type -D__USE_POSIX -D_XOPEN_SOURCE=600 -fms-compatibility -fsanitize=address -ggdb
LDFLAGS = -Wl,--gc-sections -Wl,--as-needed -fsanitize=address

LYIR_INCDIR = -I. -Ilca/include -Ilyir/include
LYIR_INC = $(wildcard ./lca/include/*.h) $(wildcard ./lyir/include/*.h) $(wildcard ./lyir/lib/*.h)
LYIR_LIB = $(wildcard ./lyir/lib/*.c)
LYIR_OBJ = $(patsubst ./lyir/lib/%.c, ./out/$(ODIR)/lyir_lib_$(subst /,_,%).o, $(LYIR_LIB))

CCLY_INCDIR = -I. -Ilca/include -Ilyir/include -Iccly/include
CCLY_INC = $(wildcard ./lca/include/*.h) $(wildcard ./lyir/include/*.h) $(wildcard ./lyir/lib/*.h) $(wildcard ./ccly/include/*.h) $(wildcard ./ccly/lib/*.h)
CCLY_LIB = $(wildcard ./ccly/lib/*.c)
CCLY_OBJ = $(patsubst ./ccly/lib/%.c, ./out/$(ODIR)/ccly_lib_%.o, $(CCLY_LIB))

LAYE_INCDIR = -I. -Ilca/include -Ilyir/include -Iccly/include -Ilaye/include
LAYE_INC = $(wildcard ./lca/include/*.h) $(wildcard ./lyir/include/*.h) $(wildcard ./lyir/lib/*.h) $(wildcard ./ccly/include/*.h) $(wildcard ./ccly/lib/*.h) $(wildcard ./laye/include/*.h) $(wildcard ./laye/lib/*.h) $(wildcard ./laye/src/*.h)
LAYE_LIB = $(wildcard ./laye/lib/*.c)
LAYE_OBJ = $(patsubst ./laye/lib/%.c, ./out/$(ODIR)/laye_lib_%.o, $(LAYE_LIB))

LAYEC0_SRC = ./laye/src/compiler.c
LAYEC0_OBJ = ./out/$(ODIR)/laye_src_compiler.o
LAYEC0_EXE = $(call ExePath,$(call FixPath,./out/layec0))

LAYE1_SRC = ./laye/src/laye.c
LAYE1_OBJ = ./out/$(ODIR)/laye_src_laye.o
LAYE1_EXE = $(call ExePath,$(call FixPath,./out/laye1))

LAYE2_EXE = $(call ExePath,$(call FixPath,./out/laye2))

LAYE_EXE = $(call ExePath,$(call FixPath,./out/laye))

EXEC_TEST_RUNNER_OBJ = ./out/$(ODIR)/exec_test_runner.o
EXEC_TEST_RUNNER_EXE = $(call ExePath,$(call FixPath,./out/exec_test_runner))

default: $(LAYEC0_EXE)

bootstrap: $(LAYEC0_EXE) $(LAYE_EXE)

clean:
	$(RMRF) out
	$(RMRF) test-out

test: run_exec_test run_ctest

run_exec_test: $(LAYEC0_EXE) $(EXEC_TEST_RUNNER_EXE)
	$(call ExePath,$(call FixPath,./out/exec_test_runner))

run_ctest: $(LAYEC0_EXE)
	$(CMAKE_VARS) cmake -S . -B $(call FixPath,./test-out/$(ODIR)) -DBUILD_TESTING=ON
	cmake --build $(call FixPath,./test-out/$(ODIR))
	ctest --test-dir $(call FixPath,./test-out/$(ODIR)) -j`nproc` --progress

$(LAYEC0_EXE): $(LYIR_OBJ) $(CCLY_OBJ) $(LAYE_OBJ) $(LAYEC0_OBJ)
	$(LD) -o $@ $^ $(LDFLAGS)

$(LAYE1_EXE): $(LYIR_OBJ) $(CCLY_OBJ) $(LAYE_OBJ) $(LAYE1_OBJ)
	$(LD) -o $@ $^ $(LDFLAGS)

# TODO(local): actually implement bootstrapping for everything other than laye1, when the compilers can actually do that
$(LAYE2_EXE): $(LAYE1_EXE)
	$(CP) $< $@

$(LAYE_EXE): $(LAYE2_EXE)
	$(CP) $< $@

$(EXEC_TEST_RUNNER_EXE): $(EXEC_TEST_RUNNER_OBJ)
	$(LD) -o $@ $< $(LDFLAGS)

./out/$(ODIR)/lyir_lib_%.o: ./lyir/lib/%.c $(LYIR_INC)
	$(call MkDir,$(call FixPath,./out/$(ODIR)))
	$(CC) -o $@ -c $< $(CFLAGS) $(LYIR_INCDIR)

./out/$(ODIR)/ccly_lib_%.o: ./ccly/lib/%.c $(CCLY_INC)
	$(call MkDir,$(call FixPath,./out/$(ODIR)))
	$(CC) -o $@ -c $< $(CFLAGS) $(CCLY_INCDIR)

./out/$(ODIR)/laye_lib_%.o: ./laye/lib/%.c $(LAYE_INC)
	$(call MkDir,$(call FixPath,./out/$(ODIR)))
	$(CC) -o $@ -c $< $(CFLAGS) $(LAYE_INCDIR)

./out/$(ODIR)/laye_src_%.o: ./laye/src/%.c $(LAYE_INC)
	$(call MkDir,$(call FixPath,./out/$(ODIR)))
	$(CC) -o $@ -c $< $(CFLAGS) $(LAYE_INCDIR)

$(EXEC_TEST_RUNNER_OBJ): ./laye/src/exec_test_runner.c
	$(call MkDir,$(call FixPath,./out/$(ODIR)))
	$(CC) -o $@ -c $< $(CFLAGS) -I. -Ilca/include

.PHONY: default bootstrap clean test run_exec_test run_ctest

CC      = cc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -Isrc
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LDLIBS = -framework Accelerate
  LIBOMP_PREFIX ?= /opt/homebrew/opt/libomp
  OMP_CFLAGS = -Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include -DAFQMC_USE_OPENMP
  OMP_LDLIBS = -L$(LIBOMP_PREFIX)/lib -lomp
else
  LDLIBS = -llapack -lblas -lm
  OMP_CFLAGS = -fopenmp -DAFQMC_USE_OPENMP
  OMP_LDLIBS = -fopenmp
endif

MPICC ?= mpicc
MPIRUN ?= mpirun
MPI_CFLAGS = -DAFQMC_USE_MPI

SRC  = $(wildcard src/*.c)
OBJ  = $(SRC:.c=.o)
OMP_OBJ = $(SRC:.c=.omp.o)
MPI_OBJ = $(SRC:.c=.mpi.o)
HYBRID_OBJ = $(SRC:.c=.hybrid.o)
HDRS = $(wildcard src/*.h)
ALL_TESTS = $(wildcard tests/test_*.c)
TEST_HDRS = $(wildcard tests/*.h)
# Keep hook-enabled objects and executables separate from every production build.
HOOK_DIR = build/test-hooks
HOOK_OBJ = $(patsubst src/%.c,$(HOOK_DIR)/%.o,$(SRC))
HOOK_OMP_OBJ = $(patsubst src/%.c,$(HOOK_DIR)/%.omp.o,$(SRC))
HOOK_MPI_OBJ = $(patsubst src/%.c,$(HOOK_DIR)/%.mpi.o,$(SRC))
HOOK_HYBRID_OBJ = $(patsubst src/%.c,$(HOOK_DIR)/%.hybrid.o,$(SRC))
# *_slow.c are long end-to-end regressions; excluded from the default `test`
# target and run via `make test_slow`.
SLOW_TESTS = $(wildcard tests/test_*_slow.c)
TESTS = $(filter-out $(SLOW_TESTS),$(ALL_TESTS))
TESTBIN = $(TESTS:.c=)
SLOW_TESTBIN = $(SLOW_TESTS:.c=)
TESTBIN_OMP = $(patsubst tests/test_%.c,tests/test_%_omp,$(TESTS))
TESTBIN_MPI = $(patsubst tests/test_%.c,tests/test_%_mpi,$(TESTS))
TESTBIN_HYBRID = $(patsubst tests/test_%.c,tests/test_%_hybrid,$(TESTS))

dqmc: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

src/%.omp.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -c -o $@ $<

dqmc_omp: $(OMP_OBJ)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -o $@ $(OMP_OBJ) $(LDLIBS) $(OMP_LDLIBS)

src/%.mpi.o: src/%.c $(HDRS)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -c -o $@ $<

dqmc_mpi: $(MPI_OBJ)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -o $@ $(MPI_OBJ) $(LDLIBS)

src/%.hybrid.o: src/%.c $(HDRS)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) $(OMP_CFLAGS) -c -o $@ $<

dqmc_hybrid: $(HYBRID_OBJ)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) $(OMP_CFLAGS) -o $@ $(HYBRID_OBJ) $(LDLIBS) $(OMP_LDLIBS)

LIBSRC = $(filter-out src/main.c,$(SRC))
tests/test_%: tests/test_%.c $(LIBSRC) $(HDRS) $(TEST_HDRS)
	$(CC) $(CFLAGS) -o $@ $< $(LIBSRC) $(LDLIBS)

LIBSRC_OMP = $(filter-out src/main.omp.o,$(OMP_OBJ))
tests/test_%_omp: tests/test_%.c $(LIBSRC_OMP) $(HDRS) $(TEST_HDRS)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -o $@ $< $(LIBSRC_OMP) $(LDLIBS) $(OMP_LDLIBS)

LIBSRC_MPI = $(filter-out src/main.mpi.o,$(MPI_OBJ))
tests/test_%_mpi: tests/test_%.c $(LIBSRC_MPI) $(HDRS) $(TEST_HDRS)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -o $@ $< $(LIBSRC_MPI) $(LDLIBS)

LIBSRC_HYBRID = $(filter-out src/main.hybrid.o,$(HYBRID_OBJ))
tests/test_%_hybrid: tests/test_%.c $(LIBSRC_HYBRID) $(HDRS) $(TEST_HDRS)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) $(OMP_CFLAGS) -o $@ $< $(LIBSRC_HYBRID) $(LDLIBS) $(OMP_LDLIBS)

test_szz: dqmc
	@printf "%-28s " tests/test_szz_output.sh; sh tests/test_szz_output.sh

test_sperp: dqmc
	@printf "%-28s " tests/test_sperp_output.sh; sh tests/test_sperp_output.sh

test_scalar: dqmc
	@printf "%-28s " tests/test_scalar_output.sh; sh tests/test_scalar_output.sh

test_dat: dqmc
	@printf "%-28s " tests/test_dat_output.sh; sh tests/test_dat_output.sh

test_dat_parallel: dqmc dqmc_omp dqmc_mpi dqmc_hybrid
	@set -e; for mode in omp mpi hybrid; do \
	  MPIRUN="$(MPIRUN)" sh tests/test_dat_output.sh $$mode; done

test_scalar_parallel: dqmc dqmc_omp dqmc_mpi dqmc_hybrid
	@set -e; for mode in omp mpi hybrid; do \
	  MPIRUN="$(MPIRUN)" sh tests/test_scalar_output.sh $$mode; done

test_szz_parallel: dqmc dqmc_omp dqmc_mpi dqmc_hybrid
	@printf "%-35s " tests/test_szz_parallel.sh; sh tests/test_szz_parallel.sh

test_sperp_parallel: dqmc dqmc_omp dqmc_mpi dqmc_hybrid
	@printf "%-35s " tests/test_sperp_parallel.sh; sh tests/test_sperp_parallel.sh

test: $(TESTBIN) test_szz test_sperp test_scalar test_dat test_global_disabled test_global_output test_global_hook_isolation
	@fail=0; for t in $(TESTBIN); do printf "%-28s " $$t; ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi; echo "ALL TESTS PASSED"

test_slow: $(SLOW_TESTBIN)
	@fail=0; for t in $(SLOW_TESTBIN); do printf "%-40s " $$t; ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME SLOW TESTS FAILED"; exit 1; fi; echo "ALL SLOW TESTS PASSED"

test_omp: $(TESTBIN_OMP) test_global_output_omp
	@fail=0; for t in $(TESTBIN_OMP); do printf "%-28s " $$t; ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME OMP TESTS FAILED"; exit 1; fi; echo "ALL OMP TESTS PASSED"

test_mpi: $(TESTBIN_MPI)
	@fail=0; for t in $(TESTBIN_MPI); do printf "%-32s " $$t; $(MPIRUN) -np 1 ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME MPI TESTS FAILED"; exit 1; fi; echo "ALL MPI TESTS PASSED"

test_hybrid: $(TESTBIN_HYBRID) test_szz_parallel test_sperp_parallel test_scalar_parallel test_dat_parallel test_global_parallel
	@fail=0; for t in $(TESTBIN_HYBRID); do printf "%-35s " $$t; $(MPIRUN) -np 1 ./$$t || fail=1; done; \
	  if [ $$fail -ne 0 ]; then echo "SOME HYBRID TESTS FAILED"; exit 1; fi; echo "ALL HYBRID TESTS PASSED"

clean:
	rm -f src/*.o src/*.omp.o src/*.mpi.o src/*.hybrid.o dqmc dqmc_omp dqmc_mpi dqmc_hybrid $(TESTBIN) $(SLOW_TESTBIN) $(TESTBIN_OMP) $(TESTBIN_MPI) $(TESTBIN_HYBRID) $(HOOK_OBJ) $(HOOK_OMP_OBJ) $(HOOK_MPI_OBJ) $(HOOK_HYBRID_OBJ) $(HOOK_DIR)/dqmc $(HOOK_DIR)/dqmc_omp $(HOOK_DIR)/dqmc_mpi $(HOOK_DIR)/dqmc_hybrid

.PHONY: test test_szz test_sperp test_scalar test_dat test_dat_parallel test_scalar_parallel test_szz_parallel test_sperp_parallel test_slow test_omp test_mpi test_hybrid clean

GLOBAL_DEFAULT_VARIANTS ?= omitted none binonly profile
test_global_default: dqmc
	@printf "%-28s " tests/test_global_default_unchanged.sh; sh tests/test_global_default_unchanged.sh $(GLOBAL_DEFAULT_VARIANTS)

.PHONY: test_global_default

test_global_output: dqmc $(HOOK_DIR)/dqmc
	@printf "%-28s " tests/test_global_output.sh; sh tests/test_global_output.sh

test_global_parallel: dqmc dqmc_omp dqmc_mpi dqmc_hybrid $(HOOK_DIR)/dqmc_mpi $(HOOK_DIR)/dqmc_hybrid
	@printf "%-35s " tests/test_global_parallel.sh; sh tests/test_global_parallel.sh

.PHONY: test_global_output test_global_parallel

$(HOOK_DIR)/%.o: src/%.c $(HDRS) Makefile
	@mkdir -p $(HOOK_DIR)
	$(CC) $(CFLAGS) -DAFQMC_TEST_HOOKS -c -o $@ $<

$(HOOK_DIR)/%.omp.o: src/%.c $(HDRS) Makefile
	@mkdir -p $(HOOK_DIR)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -DAFQMC_TEST_HOOKS -c -o $@ $<

$(HOOK_DIR)/%.mpi.o: src/%.c $(HDRS) Makefile
	@mkdir -p $(HOOK_DIR)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -DAFQMC_TEST_HOOKS -c -o $@ $<

$(HOOK_DIR)/%.hybrid.o: src/%.c $(HDRS) Makefile
	@mkdir -p $(HOOK_DIR)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) $(OMP_CFLAGS) -DAFQMC_TEST_HOOKS -c -o $@ $<

$(HOOK_DIR)/dqmc: $(HOOK_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(HOOK_DIR)/dqmc_omp: $(HOOK_OMP_OBJ)
	$(CC) $(CFLAGS) $(OMP_CFLAGS) -o $@ $^ $(LDLIBS) $(OMP_LDLIBS)

$(HOOK_DIR)/dqmc_mpi: $(HOOK_MPI_OBJ)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) -o $@ $^ $(LDLIBS)

$(HOOK_DIR)/dqmc_hybrid: $(HOOK_HYBRID_OBJ)
	$(MPICC) $(CFLAGS) $(MPI_CFLAGS) $(OMP_CFLAGS) -o $@ $^ $(LDLIBS) $(OMP_LDLIBS)

test_global_disabled: dqmc
	@sh tests/test_global_disabled.sh

test_global_hook_isolation: dqmc
	@sh tests/test_global_hook_isolation.sh

.PHONY: test_global_disabled test_global_hook_isolation

test_global_output_omp: dqmc_omp $(HOOK_DIR)/dqmc_omp
	@OMP_NUM_THREADS=2 AFQMC_TEST_MODE=omp sh tests/test_global_output.sh

.PHONY: test_global_output_omp

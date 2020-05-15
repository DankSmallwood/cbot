BUILDDIR=build
PROFILE=release

DEPDIR:=$(BUILDDIR)/$(PROFILE)/deps
DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.d -fsyntax-only

TARGET:=$(BUILDDIR)/$(PROFILE)/cbot
.DEFAULT_GOAL=$(TARGET)
TEST_TARGET:=$(BUILDDIR)/$(PROFILE)/test

CC=gcc

LIBS=-lpcre2-8

CFLAGS_debug=-g
CFLAGS_release=-O3
CFLAGS=-Wall -std=c11 -Isrc -I$(BUILDDIR)/$(PROFILE) $(CFLAGS_$(PROFILE))

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

SRC=$(subst src/,,$(call rwildcard,src,*.c))
OBJ=$(patsubst %.c,$(BUILDDIR)/$(PROFILE)/%.o,$(SRC))
OBJ_NOMAIN=$(filter-out build/release/main.o,$(OBJ))

GEN_Y=$(subst src/,,$(call rwildcard,src,*.y))
GEN_Z=$(patsubst %.y,$(BUILDDIR)/$(PROFILE)/%.z,$(GEN_Y))

DEPFILES:=$(SRC:%.c=$(DEPDIR)/%.d)
$(DEPFILES):
-include $(wildcard $(DEPFILES))

$(TARGET): $(GEN_Z) $(OBJ)
	@echo $@
	@$(CC) $(CFLAGS) $(OBJ) $(LIBS) -o $@

$(TEST_TARGET): $(OBJ_NOMAIN) test/test.c $(wildcard test/*.x)
	@echo $@
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -Isrc test/test.c $(OBJ_NOMAIN) $(LIBS) -o $@

.PHONY:
test: $(TEST_TARGET)
	@$(TEST_TARGET)

$(DEPDIR)/%.d: src/%.c
	@echo $@
	@mkdir -p $(dir $@)
	@-$(CC) $(CFLAGS) $(DEPFLAGS) $< 2>/dev/null

$(BUILDDIR)/$(PROFILE)/%.o: src/%.c $(DEPDIR)/%.d
	@echo $@
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -I$(dir $@) -c $< -o $@

.SECONDEXPANSION:
$(BUILDDIR)/$(PROFILE)/%.z: src/%.y
	@echo $@
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -I$(dir $@) -P -E -x c $< >$@2
	@mv $@2 $@
	@clang-format -i $@

.phony:
clean:
	@rm -rf $(BUILDDIR)

APP_NAME := check-link-consistency
PREFIX=/usr
DESTDIR=

JOBS = $(shell nproc)
MAKEFLAGS += " -j $(JOBS) -l $(JOBS)"
# Don't print commands being executed, nor "Nothing to be done for ..." message.
MAKEFLAGS += " -s"

DISTRO := Arch
CC := c++ -march=x86-64 -O2 -flto=auto -std=c++20 -fno-rtti -Wall -Wextra -Wpedantic -Werror=format -Werror=return-type -Wl,-z,relro -Wl,-z,now -DDISTRO=$(DISTRO)


MAIN_CPPs := $(shell find src/main/ -type f -name '*.cpp')
MAIN_Ds := $(MAIN_CPPs:src/%.cpp=target/build/main/%.d)
MAIN_Os := $(MAIN_Ds:.d=.o)

TEST_PATH := target/build/test/run
TEST_CPPs := $(shell find src/test/ -type f -name '*.cpp') \
		src/main/util/alloc/alloc.cpp \
		src/main/util/alloc/Arena.cpp \
		src/main/util/alloc/String.cpp \
		src/main/util/Error.cpp \
		src/main/util/Log.cpp \
		src/main/util/StdCapture.cpp \
		src/main/util/util.cpp
TEST_Ds := $(TEST_CPPs:src/%.cpp=target/build/test/%.d)
TEST_Os := $(TEST_Ds:.d=.o)


# ----------------------------------------------------------------------------------------------------------------------------------------


.PHONY: all
all: target/$(APP_NAME) target/$(APP_NAME).conf.sample test


# Delete built-in rules.
%.o: %.cpp

target/build/main/%.o: src/%.cpp target/build/main/%.d
	@echo 'CC $@'
	@mkdir -p $(@D)
	@$(CC) -c -MMD -MP -fPIC -o $@ $<

target/build/test/%.o: src/%.cpp target/build/test/%.d
	@echo 'CC $@'
	@mkdir -p $(@D)
	@$(CC) -c -MMD -MP -fPIC -o $@ $<


# Don't delete .d files, phony if missing.
$(MAIN_Ds):
$(TEST_Ds):
-include $(MAIN_Ds)
-include $(TEST_Ds)


target/$(APP_NAME): $(MAIN_Os)
	@echo 'LL $@'
	@$(CC) -s -lstdc++ -pthread -lelf -larchive -o $@ $^

$(TEST_PATH): $(TEST_Os)
	@echo 'LL $@'
	@$(CC) -s -lstdc++ -pthread -o $@ $^


target/$(APP_NAME).conf.sample: src/etc/$(APP_NAME).conf.sample
	cp $< $@


# ----------------------------------------------------------------------------------------------------------------------------------------


.PHONY: test
test: $(TEST_PATH).timestamp

$(TEST_PATH).timestamp: $(TEST_PATH)
	@echo '>> $(TEST_PATH)'
	@$(TEST_PATH)
	@touch $(TEST_PATH).timestamp


# ----------------------------------------------------------------------------------------------------------------------------------------


.PHONY: install
install:
	install -Dm755 target/$(APP_NAME) -t $(DESTDIR)$(PREFIX)/bin/
	install -Dm644 target/$(APP_NAME).conf.sample -t $(DESTDIR)$(PREFIX)/share/$(APP_NAME)


# ----------------------------------------------------------------------------------------------------------------------------------------


.PHONY: clean
clean:
	@if [ -e target ]; then echo "rm -rf target"; rm -rf target; fi

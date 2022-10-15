APP_NAME := check-link-consistency
DISTRO := Arch

PREFIX=/usr
DESTDIR=

CC := c++ -march=x86-64 -O2 -flto=$(if $(NPROC),$(NPROC),1) -std=c++20 -fno-rtti -Wall -Wextra -Wpedantic -Werror=format -Werror=return-type -Wl,-z,relro -Wl,-z,now -DDISTRO=$(DISTRO)

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
all: target/$(APP_NAME) target/$(APP_NAME).conf.sample test dummy


# Getting rid of "Nothing to be done for '...'."
dummy:
	@true


target/build/make/transform-d: src/make/transform-d.cpp
	@echo 'CC $@'
	@mkdir -p $(@D)
	@$(CC) -o $@ $<


target/build/main/%.d: src/%.cpp target/build/make/transform-d
	@echo 'CC $@'
	@mkdir -p $(@D)
	@$(CC) -E -MMD -MP -MF $@ -MT "$(@:.d=.o)" -o /dev/null $<
	@target/build/make/transform-d $@

target/build/test/%.d: src/%.cpp target/build/make/transform-d
	@echo 'CC $@'
	@mkdir -p $(@D)
	@$(CC) -E -MMD -MP -MF $@ -MT "$(@:.d=.o)" -o /dev/null $<
	@target/build/make/transform-d $@


# Don't delete .d files.
$(MAIN_Ds):
$(TEST_Ds):

# Delete built-in rules.
%.o: %.cpp

-include $(MAIN_Ds)
-include $(TEST_Ds)


target/build/main/%.o: src/%.cpp target/build/main/%.d
	@echo 'CC $@'
	@mkdir -p $(@D)
	@$(CC) -c -fPIC -o $@ $<

target/build/test/%.o: src/%.cpp target/build/test/%.d
	@echo 'CC $@'
	@mkdir -p $(@D)
	@$(CC) -c -fPIC -o $@ $<


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
test: $(TEST_PATH).timestamp dummy

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


# Does not work: make tries to make .d files first.
# Use ./clean.sh instead.

#.PHONY: clean
#clean:
#	@if [ -e target ]; then echo "rm -rf target"; rm -rf target; fi

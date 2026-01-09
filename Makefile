SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := .
TEST_SRC_DIR := test/src
TEST_EXE_DIR := test/bin

EXE := $(BIN_DIR)/gb

SRC := $(filter-out $(SRC_DIR)/main.cpp, $(wildcard $(SRC_DIR)/*.cpp $(SRC_DIR)/*.c))
OBJ := $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

TEST_SRC := $(wildcard $(TEST_SRC_DIR)/*.cpp)
TEST_EXES := $(TEST_SRC:$(TEST_SRC_DIR)/%.cpp=$(TEST_EXE_DIR)/%)

DEFS := -DPTHREADS -D_REENTRANT_ -D_CHECK_FORMAT_STRING_
CPPFLAGS := -g -Wall -Wextra -Wpedantic -pipe -fno-stack-protector $(DEFS) -O3 -std=c++23
LDLIBS := -lm -lpthread -lssl -lcrypto -lz
TEST_CPPFLAGS := -I$(SRC_DIR) -Iextern/catch2
LDFLAGS := -rdynamic -export-dynamic

all: $(EXE)

$(OBJ_DIR)/catch2/catch_amalgamated.o: extern/catch2/catch_amalgamated.cpp | $(OBJ_DIR)/catch2
	$(CXX) $(CPPFLAGS) $(CFLAGS) -DCATCH_AMALGAMATED_CUSTOM_MAIN -c $< -o $@

$(TEST_EXE_DIR)/%: $(TEST_SRC_DIR)/%.cpp $(OBJ) $(OBJ_DIR)/catch2/catch_amalgamated.o | $(TEST_EXE_DIR)
	$(CXX) $(LDFLAGS) $(TEST_CPPFLAGS) $^ $(LDLIBS) -o $@

$(EXE): $(OBJ) $(OBJ_DIR)/main.o | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR) $(TEST_EXE_DIR) $(OBJ_DIR)/catch2:
	mkdir -p $@

test: $(TEST_EXES)
	for t in $(TEST_EXES) ; do $$t ; done

Make.depend:
	$(CXX) -MM $(DEFS) $(SRC) > $@

install: $(EXE)
	mkdir -p $(DESTDIR)/var/gigablast/data0/
	mkdir -p $(DESTDIR)/usr/bin/
	mkdir -p $(DESTDIR)/etc/init.d/
	mkdir -p $(DESTDIR)/etc/init/
	mkdir -p $(DESTDIR)/etc/rc3.d/
	mkdir -p $(DESTDIR)/lib/init/
	$(EXE) copyfiles $(DESTDIR)/var/gigablast/data0/
	$(RM) $(DESTDIR)/usr/bin/gb
	ln -s /var/gigablast/data0/gb $(DESTDIR)/usr/bin/gb
	cp S99gb $(DESTDIR)/etc/init.d/gb
	ln -s /etc/init.d/gb $(DESTDIR)/etc/rc3.d/S99gb

clean:
	rm -rf $(OBJ_DIR) $(EXE) $(TEST_EXE_DIR)

depend: Make.depend

include Make.depend

.PHONY: all clean install depend test

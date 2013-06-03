CXX = clang++
TARGET = fox
SRC = src
OBJ = obj
SRCS = $(wildcard $(SRC)/*.cpp)
TOOL_DIR = tools
TOOLS = $(basename $(wildcard $(TOOL_DIR)/*.cpp))
OBJECTS = $(addprefix $(OBJ)/, $(notdir $(addsuffix .o, $(basename $(SRCS)))))

KODO_PATH = ../kodo
INCLUDES = -I $(KODO_PATH)/src/ \
	   -I $(KODO_PATH)/bundle_dependencies/fifi-master/src/ \
	   -I $(KODO_PATH)/bundle_dependencies/sak-master/src/ \
	   -I /usr/include/libnl3
TOOLS_INCLUDES = -I src
LDFLAGS = -lpthread -lrt -lnl-3 -lnl-genl-3 -lglog -lgflags -rdynamic
TOOLS_LIBS = -lrt -lpthread
CXXFLAGS := $(CXXFLAGS) -std=c++11 -pthread -g

ifneq ($(ASAN),)
    CXXFLAGS := $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer -O1
endif

ifneq ($(TSAN),)
    CXXFLAGS := $(CXXFLAGS) -fsanitize=thread -fPIE -O1
    LDFLAGS := $(LDFLAGS) -fsanitize=thread -pie -O1
endif

ifneq ($(MPROF),)
    LDFLAGS := $(LDFLAGS) -ltcmalloc
endif

ifneq ($(CPUPROF),)
    LDFLAGS := $(LDFLAGS) -lprofiler
endif

all: $(TARGET) tools

.PHONY: clean

depend: .depend

.depend: $(SRCS)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -MM $^ | sed -E "s/^.+\.o/$(OBJ)\/&/" > ./.depend;

-include .depend

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) -o $(TARGET)

$(OBJ)/%.o: $(SRC)/%.cpp $(SRC)/%.hpp | $(OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $<

$(OBJ):
	mkdir -p $(OBJ)

$(TOOL_DIR)/%: $(TOOL_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TOOLS_INCLUDES) $(TOOLS_LIBS) -o $@ $<

tools: $(TOOLS)

clean:
	rm -rf $(TARGET) $(OBJECTS) $(TOOLS) .depend doc/* obj

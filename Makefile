DIR_SRC         := src
DIR_HEADER      := inc
DIR_OUT         := out

CC = gcc

ADD_DIRS_SRC    := $(foreach dir, $(shell find $(DIR_SRC)/ -type d), $(dir:$(DIR_SRC)/%=%))
ADD_DIRS_HEADER := $(foreach dir, $(shell find $(DIR_HEADER)/ -type d), $(dir:$(DIR_HEADER)/%=%))

ADD_DIRS_OUT    := $(ADD_DIRS_SRC)

DIRS_OUT        := $(DIR_OUT) $(shell echo "$(foreach dir, $(ADD_DIRS_OUT), $(DIR_OUT)/$(dir))" | tr ' ' '\n' | sort -u)

SRCS := $(wildcard $(DIR_SRC)/*.c) \
	    $(foreach dir, $(ADD_DIRS_SRC), $(wildcard $(DIR_SRC)/$(dir)/*.c))
OBJS := $(SRCS:$(DIR_SRC)/%.c=$(DIR_OUT)/%.o)
DEPS := $(wildcard $(DIR_OUT)/*.d) \
	    $(foreach dir, $(ADD_DIRS_OUT), $(wildcard $(DIR_OUT)/$(dir)/*.d))

FLAGS    = -std=c99 -Wall -Werror -Wpedantic -Wextra -I$(DIR_HEADER) \
		   $(foreach dir, $(ADD_DIRS_HEADER), -I$(DIR_HEADER)/$(dir))
LFLAGS   = -lpthread
ADDFLAGS =

.PHONY: build debug clean run test default

default: drun

run: build
run:
	./run.sh

test:
	@echo FLAGS: $(FLAGS)

drun: debug
drun:
	./run.sh

$(DIR_OUT)/.build%: | $(DIR_OUT)
	rm -rf $(DIR_OUT)/*
	rm -f $(DIR_OUT)/.build*
	rm -f *.out
	touch $@

build: $(DIR_OUT)/.buildrelease app.out

debug: FLAGS += -g3
debug: $(DIR_OUT)/.builddebug app.out

$(DIRS_OUT):
	mkdir -p $@

$(DIR_OUT)/%.o: $(DIR_SRC)/%.c | $(DIRS_OUT)
	@$(CC) $(FLAGS) $(ADDFLAGS) -MM -MF $(@:%.o=%.d) -MT $@ -c -o /dev/null \
		 $(@:$(DIR_OUT)/%.o=$(DIR_SRC)/%.c)
	$(CC) $(FLAGS) $(ADDFLAGS) -c -o $@ $(@:$(DIR_OUT)/%.o=$(DIR_SRC)/%.c)

app.out: $(OBJS) | $(DIRS_OUT)
	$(CC) -o app.out $(OBJS) $(LFLAGS)

clean:
	rm -f $(DIR_OUT)/.build*
	rm -rf $(DIR_OUT)/*

include $(DEPS)


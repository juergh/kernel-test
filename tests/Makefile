SUBDIRS = $(shell find -maxdepth 1 -type d -not -name '.*' -printf '%P\n')

.PHONY: $(SUBDIRS)

all: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $(@)

install: $(SUBDIRS:%=install-%)
$(SUBDIRS:%=install-%):
	$(MAKE) -C $(@:install-%=%) install

clean: $(SUBDIRS:%=clean-%)
$(SUBDIRS:%=clean-%):
	$(MAKE) -C $(@:clean-%=%) clean

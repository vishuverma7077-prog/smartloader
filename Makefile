.PHONY: all clean loader launcher test

# Build in dependency order: shared loader, launcher, then ELF test programs.
all:
	$(MAKE) -C loader
	$(MAKE) -C launcher
	$(MAKE) -C test

loader:
	$(MAKE) -C loader

launcher: loader
	$(MAKE) -C launcher

test: launcher
	$(MAKE) -C test

# Clean the same subdirectories in the specified loader -> launcher -> test order.
clean:
	$(MAKE) -C loader clean
	$(MAKE) -C launcher clean
	$(MAKE) -C test clean

BUILD_DIR = build
CMAKE = cmake
CTEST = ctest

.PHONY: all debug release clean test-debug test-release debug-test release-test

all: debug

clean:
	@rm -rf $(BUILD_DIR)

debug:
	@$(CMAKE) -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Debug
	@$(CMAKE) --build $(BUILD_DIR)

release:
	@$(CMAKE) -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Release
	@$(CMAKE) --build $(BUILD_DIR)

debug-test: debug
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

release-test: release
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

ifeq ($(firstword $(MAKECMDGOALS)),debug)
  ifeq ($(word 2,$(MAKECMDGOALS)),test)
    $(eval test:;@:)
    override MAKECMDGOALS = debug-test
  endif
endif

ifeq ($(firstword $(MAKECMDGOALS)),release)
  ifeq ($(word 2,$(MAKECMDGOALS)),test)
    $(eval test:;@:)
    override MAKECMDGOALS = release-test
  endif
endif

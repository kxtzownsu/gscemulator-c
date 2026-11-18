fuzz: $(FUZZ_BIN)
	@echo "  FUZZ      $(FUZZ_BIN)"
	$(Q)$(MKDIR) -p $(FUZZ_CORPUS)
	$(Q)$(FUZZ_BIN) -max_total_time=$(FUZZ_TIME) -print_final_stats=1 -dict=$(FUZZ_DICT) $(FUZZ_CORPUS) $(PROJECT_DIR)/tests/corpus $(FUZZ_ARGS)

$(FUZZ_BIN): $(FUZZ_SRC) $(FUZZ_DICT) $(SRC_DIR)/libargs.c $(INCLUDE_DIR)/libargs.h | $(BUILD_DIR)
	@echo "  FUZZCC    $@"
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)$(FUZZ_CC) $(FUZZ_CFLAGS) -I$(INCLUDE_DIR) $(FUZZ_SRC) $(SRC_DIR)/libargs.c -o $@

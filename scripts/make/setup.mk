$(BUILD_DIR):
	@echo "  MKDIR     $(BUILD_DIR)"
	$(Q)$(MKDIR) -p $@

$(UNICORN_BUILD_DIR):
	@echo "  MKDIR     $(UNICORN_BUILD_DIR)"
	$(Q)$(MKDIR) -p $@

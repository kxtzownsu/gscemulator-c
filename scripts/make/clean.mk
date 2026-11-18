clean:
	@echo "  RM        $(TARGET)"
	$(Q)$(RM) -f $(TARGET)
	@echo "  RM        $(OBJ_DIR)"
	$(Q)$(RM) -rf $(OBJ_DIR)

fullclean:
	@echo "  RM        $(BUILD_DIR)"
	$(Q)$(RM) -rf $(BUILD_DIR)

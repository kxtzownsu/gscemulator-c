clean:
	@echo "  RM        $(TARGET)"
	$(Q)$(RM) -f $(TARGET)
	@echo "  RM        $(OBJ_DIR)"
	$(Q)$(RM) -rf $(OBJ_DIR)
	@echo "  RM        $(VERSION_HEADER)"
	$(Q)$(RM) -f $(VERSION_HEADER)

fullclean:
	@echo "  RM        $(BUILD_DIR)"
	$(Q)$(RM) -rf $(BUILD_DIR)
	@echo "  RM        $(VERSION_HEADER)"
	$(Q)$(RM) -f $(VERSION_HEADER)

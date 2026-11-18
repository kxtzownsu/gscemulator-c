ifeq ($(BUNDLED_FIRMWARE),1)
all: $(BUNDLED_FIRMWARE_HEADER) $(TARGET)
else
all: $(TARGET)
endif

ifeq ($(LOCAL_UNICORN),1)
all: $(UNICORN_STATIC_LIB)
endif

$(TARGET): $(OBJS) | $(BUILD_DIR)
	@echo "  LD        $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

ifeq ($(STATIC),1)
$(TARGET): $(UNICORN_STATIC_LIB)
endif

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "  CC        $<"
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	@echo "  RUN       $(TARGET)"
	$(Q)$(TARGET)

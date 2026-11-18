UNICORN_REPO_URL := https://github.com/iuqw/better-unicorn -b super-ultra-pro-max

$(UNICORN_STATIC_LIB): | $(UNICORN_BUILD_DIR)
	@echo "  GIT       unicorn"
	-$(Q)$(GIT) clone $(UNICORN_REPO_URL) $(UNICORN_DIR)
	$(Q)cd $(UNICORN_DIR) && $(GIT) pull
	@echo "  CMAKE     unicorn"
	$(Q)cd $(UNICORN_BUILD_DIR) && $(CMAKE) $(UNICORN_DIR) -DCMAKE_TYPE_RELEASE=Release -DBUILD_SHARED_LIBS=false
	@echo "  MAKE      unicorn"
	$(Q)$(MAKE) -C $(UNICORN_BUILD_DIR)

ifeq ($(STATIC),1)
$(OBJS): | $(UNICORN_STATIC_LIB)
endif

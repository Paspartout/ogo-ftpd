#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := odroid-ftp

include $(IDF_PATH)/make/project.mk

APP_TOOL := $(PROJECT_PATH)/tools/app-tool.py
MKFW := mkfw
APP_JSON := $(PROJECT_PATH)/app.json
APP_ICON_DIR := $(PROJECT_PATH)/media/icons
APP_APP:=$(APP_BIN:.bin=.app)
APP_FW:=$(APP_BIN:.bin=.fw)

dist: $(APP_APP) $(APP_FW)

$(APP_APP): $(APP_BIN)
	@echo MKAPP $@
	@$(APP_TOOL) $@ $(APP_JSON) $^ $(APP_ICON_DIR)

$(APP_FW): $(APP_BIN)
	@echo MKFW $@
	@$(MKFW) "$(PROJECT_NAME)" media/tile.raw 0 16 1048576 "$(PROJECT_NAME)" $^
	@mv firmware.fw $@

#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := odroid-ftp

MAKECMDGOALS := launcher_app
launcher_app: all_binaries

include $(IDF_PATH)/make/project.mk

APP_TOOL := $(PROJECT_PATH)/tools/app-tool.py
APP_JSON := $(PROJECT_PATH)/app.json
APP_ICON_DIR := $(PROJECT_PATH)/media/icons

launcher_app: all_binaries
	$(APP_TOOL) $(APP_BIN:.bin=.app) $(APP_JSON) $(APP_BIN) $(APP_ICON_DIR)

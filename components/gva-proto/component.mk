#
# Component Makefile
#

ifeq ("$(VOICE_ASSISTANT)","gva")
COMPONENT_ADD_INCLUDEDIRS += proto-c
COMPONENT_SRCDIRS += proto-c
endif

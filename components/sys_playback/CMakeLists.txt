set(COMPONENT_ADD_INCLUDEDIRS .)

# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES audio_utils codecs audio_hal esp-downmix)

set(COMPONENT_SRCS ./sys_playback.c)

register_component()

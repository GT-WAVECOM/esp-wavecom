# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += /home/robert/esp/esp-adf/components/audio_hal/include /home/robert/esp/esp-adf/components/audio_hal/driver/es8388 /home/robert/esp/esp-adf/components/audio_hal/driver/es8374 /home/robert/esp/esp-adf/components/audio_hal/driver/es8311 /home/robert/esp/esp-adf/components/audio_hal/driver/es7243 /home/robert/esp/esp-adf/components/audio_hal/driver/zl38063 /home/robert/esp/esp-adf/components/audio_hal/driver/zl38063/api_lib /home/robert/esp/esp-adf/components/audio_hal/driver/zl38063/example_apps /home/robert/esp/esp-adf/components/audio_hal/driver/zl38063/firmware
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/audio_hal -laudio_hal -L/home/robert/esp/esp-adf/components/audio_hal/driver/zl38063/firmware -lfirmware
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += audio_hal
component-audio_hal-build: 
